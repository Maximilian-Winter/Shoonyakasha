"""Locate and fetch the shared example assets.

The engine already resolves asset paths itself (`Core/AssetPaths.h`), so a demo
that passes `"env/sky.hdr"` to the engine does not need this module. It exists
for the things that happen *before* the engine starts: checking whether a large
optional asset is present so a demo can fall back gracefully, and downloading
one when it is not.

    import shoonyakasha as sk

    if not sk.assets.exists("models/NewSponza_Main_glTF_003.gltf"):
        model = "models/Box.gltf"

The search below mirrors `AssetPaths::search()` in `src/Core/AssetPaths.cpp`.
Two implementations of one rule is a drift risk, so `tests/python/` asserts they
agree on the marker filename and the environment variable.
"""

import os
import subprocess
import sys
from pathlib import Path

__all__ = [
    "MARKER_FILE",
    "ENV_VAR",
    "root",
    "locate",
    "exists",
    "describe",
    "fetch",
]

#: Identifies a directory as ours, so an unrelated `assets/` higher up the tree
#: is not mistaken for it. Must match kMarkerFile in src/Core/AssetPaths.cpp.
MARKER_FILE = ".shoonyakasha-assets"

#: Overrides the search entirely. Must match kEnvVar in src/Core/AssetPaths.cpp.
ENV_VAR = "SHOONYAKASHA_ASSET_DIR"

_root = None
_origin = "not searched yet"


def _search_upward(start):
    """Walk from `start` to the filesystem root looking for an asset directory."""
    current = Path(start).resolve()
    for directory in [current, *current.parents]:
        candidate = directory / "assets"
        if (candidate / MARKER_FILE).is_file():
            return candidate
    return None


def _search():
    global _origin

    from_env = os.environ.get(ENV_VAR)
    if from_env:
        candidate = Path(from_env)
        if candidate.is_dir():
            _origin = "%s=%s" % (ENV_VAR, candidate)
            return candidate
        _origin = ("%s is set to '%s' but that is not a directory; "
                   "fell back to searching" % (ENV_VAR, from_env))

    found = _search_upward(Path.cwd())
    if found:
        _origin = "found above the working directory: %s" % found
        return found

    # Beside the package, for an installed wheel or a checkout whose working
    # directory is somewhere else entirely.
    found = _search_upward(Path(__file__).parent)
    if found:
        _origin = "found above the package: %s" % found
        return found

    _origin = ("no assets/ directory with a %s marker was found above the "
               "working directory or the package" % MARKER_FILE)
    return None


def root(refresh=False):
    """The shared asset directory as a Path, or None if it was not found."""
    global _root
    if _root is None or refresh:
        _root = _search()
    return _root


def describe():
    """Where the root came from — for startup messages and failure reports."""
    root()
    return _origin


def locate(relative):
    """Best guess at where `relative` actually is.

    Matches `AssetPaths::locate`: a path that already resolves is returned
    untouched, then the asset-root resolution, and otherwise the input unchanged
    so failures name the path the caller actually wrote.
    """
    if not relative:
        return None

    as_given = Path(relative)
    if as_given.exists():
        return as_given

    base = root()
    if base is not None:
        resolved = base / relative
        if resolved.exists():
            return resolved

    return as_given


def exists(relative):
    """Is this asset present, either as given or under the asset root?"""
    if not relative:
        return False
    found = locate(relative)
    return found is not None and found.exists()


def fetch(*what):
    """Download large assets by running tools/fetch_assets.py.

    Names match that script: "env", "all", or an individual asset. Returns True
    if it succeeded. Raises FileNotFoundError when running from an installed
    wheel, where the tools directory is not shipped.
    """
    base = root()
    if base is None:
        raise FileNotFoundError(
            "cannot fetch: no asset root found (%s)" % describe())

    script = base.parent / "tools" / "fetch_assets.py"
    if not script.is_file():
        raise FileNotFoundError(
            "cannot fetch: %s not found. tools/ ships with the repository, not "
            "with the wheel — download assets from a checkout." % script)

    result = subprocess.run([sys.executable, str(script), *(what or ("env",))])
    return result.returncode == 0
