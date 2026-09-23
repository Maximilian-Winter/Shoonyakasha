"""Compile GLSL to SPIR-V from Python.

The C++ examples get their shaders compiled by CMake (`target_compile_shaders`).
The Python side had no equivalent: `examples/python/getting_started/demo/shaders/` holds GLSL
sources with committed `.spv` beside them and nothing that regenerates either,
and `examples/python/games_2d/full_showcase/showcase_demo.py` documented the `glslc` invocation
in a docstring for the reader to run by hand. A build step living in a comment
drifts, and one of these `.spv` files had already drifted once.

    import shoonyakasha as sk

    sk.shaders.compile_dir("shaders")          # only what changed
    sk.shaders.compile("shaders/basic.vert")   # -> shaders/basic.vert.spv

Every compile can `#include` the engine's shared GLSL library, which ships in
this package under `glsl/` (see `include_dir()`):

    #include "sk/pbr.glsl"

Staleness is decided by modification time. glslc writes a dependency file
beside each output (`basic.vert.spv.d`) listing every file the shader
included, and an output is stale when its source or any of those files is
newer than it.
"""

import os
import shutil
import subprocess
import sys
from pathlib import Path

__all__ = [
    "GlslcNotFound",
    "ShaderCompileError",
    "SHADER_EXTENSIONS",
    "find_glslc",
    "include_dir",
    "is_stale",
    "compile",
    "compile_dir",
]

#: Stages glslc infers from the file extension.
SHADER_EXTENSIONS = (
    ".vert", ".frag", ".comp", ".geom", ".tesc", ".tese", ".mesh", ".task",
    ".rgen", ".rint", ".rahit", ".rchit", ".rmiss", ".rcall",
)


class GlslcNotFound(RuntimeError):
    """glslc could not be located."""


class ShaderCompileError(RuntimeError):
    """glslc rejected a shader. `stderr` holds its diagnostics verbatim."""

    def __init__(self, source, stderr, returncode):
        self.source = str(source)
        self.stderr = stderr
        self.returncode = returncode
        message = stderr.strip() or ("glslc exited with %d" % returncode)
        super().__init__("%s\n%s" % (self.source, message))


def _candidate_glslc_paths():
    """Where glslc plausibly lives, most authoritative first."""
    exe = "glslc.exe" if os.name == "nt" else "glslc"

    sdk = os.environ.get("VULKAN_SDK")
    if sdk:
        yield Path(sdk) / "Bin" / exe
        yield Path(sdk) / "bin" / exe

    found = shutil.which("glslc")
    if found:
        yield Path(found)

    # Default SDK install roots, newest version first. Someone who installed the
    # SDK without letting it set VULKAN_SDK still gets a working compiler.
    if os.name == "nt":
        roots = [Path("C:/VulkanSDK")]
    elif sys.platform == "darwin":
        roots = [Path.home() / "VulkanSDK", Path("/usr/local/lib/vulkansdk")]
    else:
        roots = [Path.home() / "VulkanSDK", Path("/usr/lib/vulkansdk")]

    for root in roots:
        if not root.is_dir():
            continue
        for version in sorted(root.iterdir(), reverse=True):
            for sub in ("Bin", "bin", "macOS/bin", "x86_64/bin"):
                yield version / sub / exe


def find_glslc(hint=None):
    """Absolute path to glslc.

    `hint` is tried first, so a caller with its own copy is never overridden.
    Raises GlslcNotFound with the places that were searched, because "not found"
    without a list of where you looked is not actionable.
    """
    tried = []
    candidates = [Path(hint)] if hint else []
    candidates += list(_candidate_glslc_paths())

    for candidate in candidates:
        tried.append(str(candidate))
        if candidate.is_file():
            return str(candidate)

    raise GlslcNotFound(
        "glslc not found. Install the Vulkan SDK, set VULKAN_SDK, or put glslc "
        "on PATH.\nLooked in:\n  " + "\n  ".join(tried or ["(nowhere)"]))


def output_path_for(source):
    """`shaders/basic.vert` -> `shaders/basic.vert.spv`, the repo's convention."""
    return Path(str(source) + ".spv")


def include_dir():
    """The engine's shared GLSL library, passed to glslc as `-I` on every compile.

    The same directory is the include path of the C++ build (cmake/CompileShaders.cmake).
    """
    return Path(__file__).resolve().parent / "glsl"


def depfile_path_for(output):
    """`shaders/basic.vert.spv` -> `shaders/basic.vert.spv.d`."""
    return Path(str(output) + ".d")


def _depfile_dependencies(depfile):
    """Paths listed in a make-style dependency file, or [] if it cannot be read.

    The file reads `target: dep dep ...`. glslc does not escape spaces inside
    paths, so whitespace-separated tokens are joined until they name an
    existing file. Tokens that never do are returned as one path that does not
    exist, which is_stale() treats as stale.
    """
    try:
        text = Path(depfile).read_text(encoding="utf-8", errors="replace")
    except OSError:
        return []
    text = text.replace("\\\r\n", " ").replace("\\\n", " ")
    _, sep, deps = text.partition(": ")
    if not sep:
        return []

    placeholder = "\0"   # a `\ ` escape, from tools that do write one
    paths, pending = [], ""
    for token in deps.replace("\\ ", placeholder).split():
        token = token.replace(placeholder, " ")
        pending = token if not pending else pending + " " + token
        if Path(pending).is_file():
            paths.append(Path(pending))
            pending = ""
    if pending:
        paths.append(Path(pending))
    return paths


def is_stale(source, output=None):
    """Does `output` need rebuilding from `source` or anything it includes?"""
    source = Path(source)
    output = Path(output) if output else output_path_for(source)
    if not output.exists():
        return True
    built = output.stat().st_mtime
    if source.stat().st_mtime > built:
        return True
    for dependency in _depfile_dependencies(depfile_path_for(output)):
        try:
            if dependency.stat().st_mtime > built:
                return True
        except OSError:
            # A dependency that no longer exists: rebuild so glslc reports
            # the broken #include instead of keeping the old output.
            return True
    return False


def compile(source, output=None, *, glslc=None, args=(), force=False, quiet=True):
    """Compile one shader. Returns the output path.

    Skips the work when the output is newer than the source unless `force`.
    Raises ShaderCompileError carrying glslc's own diagnostics on failure.
    """
    source = Path(source)
    if not source.is_file():
        raise FileNotFoundError("no such shader: %s" % source)

    output = Path(output) if output else output_path_for(source)
    if not force and not is_stale(source, output):
        return output

    output.parent.mkdir(parents=True, exist_ok=True)
    # The source goes in as an absolute path, so the dependency file does not
    # depend on the working directory the next staleness check runs from.
    command = [glslc or find_glslc(), str(source.resolve()), "-o", str(output),
               "-I", str(include_dir()),
               "-MD", "-MF", str(depfile_path_for(output)), *args]

    proc = subprocess.run(command, capture_output=True, text=True)
    if proc.returncode != 0:
        # Do not leave a partial or stale .spv behind claiming to be current.
        if output.exists():
            try:
                output.unlink()
            except OSError:
                pass
        raise ShaderCompileError(source, proc.stderr, proc.returncode)

    if not quiet:
        print("  compiled %s" % source)
    return output


def compile_dir(directory, *, recursive=True, force=False, glslc=None, args=(),
                extensions=SHADER_EXTENSIONS, quiet=True):
    """Compile every shader under `directory`. Returns the outputs that were built.

    Only paths that were actually recompiled are returned, so an up-to-date tree
    gives back an empty list and a caller can report "nothing to do" honestly.
    """
    directory = Path(directory)
    if not directory.is_dir():
        raise FileNotFoundError("no such directory: %s" % directory)

    # Resolved once rather than per file: the search walks the SDK directory.
    compiler = glslc or find_glslc()

    pattern = "**/*" if recursive else "*"
    sources = sorted(p for p in directory.glob(pattern)
                     if p.suffix.lower() in extensions and p.is_file())

    built = []
    for source in sources:
        output = output_path_for(source)
        if force or is_stale(source, output):
            compile(source, output, glslc=compiler, args=args, force=True, quiet=quiet)
            built.append(output)

    if not quiet:
        print("%d shader(s) compiled, %d already current"
              % (len(built), len(sources) - len(built)))
    return built
