"""Check a render-pipeline JSON before the engine sees it.

The compiler reports the first thing it cannot parse and stops, with no idea
which pass or file it came from:

    RuntimeError: Unknown ResourceUsage: 'color_blnd'

This finds everything wrong in one pass and says where:

    import shoonyakasha as sk
    for problem in sk.pipeline.validate("showcase_pipeline.json"):
        print(problem)

Vocabulary here mirrors the C++ string tables in
`src/Vulkan/FrameGraph/FrameGraphJson.cpp` and `include/FrameGraph/BufferFieldTypes.h`.
When the engine extension is installed, validation also loads the pipeline with
the engine's own loader and reports what it rejects; `tests/python/test_utilities.py`
checks that the two agree.
"""

import difflib
import json
import os
import re
from pathlib import Path

__all__ = [
    "DEFAULT",
    "Problem",
    "RESOURCE_USAGES",
    "BUFFER_USAGES",
    "PASS_TYPES",
    "RESOURCE_KINDS",
    "FIELD_TYPES",
    "PACKING_RULES",
    "validate",
    "validate_json",
    "check",
    "load_document",
    "files",
    "fetch",
    "PipelineFileError",
]

#: The default pipeline shipped with the package: deferred PBR lighting with
#: cascaded sun shadows. `sk.Engine()` loads it when no pipeline is named.
#: Its settings are `scene.custom.default.*` values; see its README.md.
DEFAULT = Path(__file__).parent / "pipelines" / "default" / "pipeline.json"

RESOURCE_USAGES = frozenset({
    "color_write", "color_attachment_write",
    "color_blend", "color_attachment_blend",
    "depth_write", "depth_stencil_write", "depth_read",
    "shader_read", "shader_read_write", "storage_image_write",
    "input_attachment", "transfer_src", "transfer_dst", "present",
})

PASS_TYPES = frozenset({"graphics", "compute", "transfer", "ray_tracing"})

#: What a pass may "require"; a device without it leaves the pass out.
CAPABILITIES = frozenset({"rayQuery", "drawIndirectCount", "geometryShader", "tessellationShader",
                          "meshShader", "taskShader", "rayTracingPipeline",
                          "bindlessTextures"})

#: What a buffer layout with "source": {"type": "scene"} holds ("data").
SCENE_DATA = frozenset({"lights", "rayInstances", "components"})

#: What a descriptor binding's "source" may name, and the binding type each takes.
BINDING_SOURCES = {"sceneTextures": "combined_image_sampler", "material": "uniform_buffer"}
RESOURCE_KINDS = frozenset({"image", "buffer"})

FIELD_TYPES = frozenset({
    "float", "double", "int", "uint", "bool",
    "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
    "uvec2", "uvec3", "uvec4", "mat2", "mat3", "mat4",
})

PACKING_RULES = frozenset({"std140", "std430", "scalar", "push_constant"})

#: How a buffer layout is bound. Descriptor types the JSON accepts for a layout.
BUFFER_USAGES = frozenset({"uniform_buffer", "storage_buffer", "push_constant", "descriptor_set"})

#: Usages that write a colour attachment, for the present check.
_COLOUR_WRITES = {"color_write", "color_attachment_write",
                  "color_blend", "color_attachment_blend", "present"}


class Problem:
    """One issue, with enough context to find it in the file."""

    def __init__(self, where, message, severity="error", hint=None):
        self.where = where
        self.message = message
        self.severity = severity
        self.hint = hint

    @property
    def is_error(self):
        return self.severity == "error"

    def __str__(self):
        text = "%s: %s\n    %s" % (self.severity, self.where, self.message)
        if self.hint:
            text += "\n    %s" % self.hint
        return text

    __repr__ = __str__


def _suggest(value, options):
    """'did you mean' for a mistyped enum, or None."""
    close = difflib.get_close_matches(str(value), sorted(options), n=1, cutoff=0.6)
    return ("did you mean '%s'?" % close[0]) if close else None


def _check_enum(value, options, where, what, problems):
    if value in options:
        return True
    problems.append(Problem(where, "%s '%s' is not known" % (what, value),
                            hint=_suggest(value, options)))
    return False


_PLACEHOLDER = re.compile(r"\{([A-Za-z0-9_]+?)([+-][0-9]+)?\}")


def _substitute(value, index, number, where, problems):
    """Replace {index}, {index+N}, {index-N} as the engine's loader does: a
    string that is only a placeholder becomes an int."""
    if isinstance(value, str):
        whole = _PLACEHOLDER.fullmatch(value)
        def replace(match):
            if match.group(1) != index:
                return match.group(0)
            result = number + int(match.group(2) or 0)
            if result < 0:
                problems.append(Problem(where, "'%s' is negative for %s = %d"
                                        % (match.group(0), index, number)))
            return str(result)
        if whole and whole.group(1) == index:
            return int(replace(whole))
        return _PLACEHOLDER.sub(replace, value)
    if isinstance(value, list):
        return [_substitute(v, index, number, where, problems) for v in value]
    if isinstance(value, dict):
        return {k: _substitute(v, index, number, where, problems) for k, v in value.items()}
    return value


def _expand_repeats(passes, source, problems):
    """Expand passes that declare "repeat", mirroring the engine's loader."""
    expanded = []
    for index, pass_decl in enumerate(passes):
        if not isinstance(pass_decl, dict) or "repeat" not in pass_decl:
            expanded.append(pass_decl)
            continue
        name = pass_decl.get("name", "<unnamed>")
        where = "%s passes[%d] '%s' repeat" % (pass_decl.get(_FILE, source), index, name)
        spec = pass_decl["repeat"]
        count = spec.get("count") if isinstance(spec, dict) else None
        if not isinstance(count, int) or isinstance(count, bool) or count < 1:
            problems.append(Problem(where, "needs a 'count' of at least 1"))
            continue
        placeholder = spec.get("index", "i")
        first = spec.get("first", 0)
        step = spec.get("step", 1)
        if not isinstance(step, int) or isinstance(step, bool) or step == 0:
            problems.append(Problem(where, "'step' must be a non-zero integer"))
            continue
        if first + step * (count - 1) < 0:
            problems.append(Problem(where, "counts below 0: every value must be at least 0"))
            continue
        base = {k: v for k, v in pass_decl.items() if k != "repeat"}
        for k in range(count):
            value = first + step * k
            instance = _substitute(base, placeholder, value, where, problems)
            if instance.get("name") == name:
                instance["name"] = "%s[%d]" % (name, value)
            expanded.append(instance)
    return expanded


def validate_json(document, base_dir=None, source="<json>", origins=None):
    """Validate an already-parsed pipeline. Returns a list of Problem.

    `origins` maps (section, name) to the file that defined a named entry,
    as load_document fills it; problems are reported against that file."""
    problems = []
    base_dir = Path(base_dir) if base_dir else None
    origins = origins or {}

    def file_of(section, name):
        return origins.get((section, name), source)

    if not isinstance(document, dict):
        return [Problem(source, "top level must be a JSON object")]

    # ── resources ──────────────────────────────────────────────
    declared = {}
    resources = document.get("resources", [])
    if not isinstance(resources, list):
        problems.append(Problem(source, "'resources' must be an array"))
        resources = []

    for index, resource in enumerate(resources):
        where = "%s resources[%d]" % (resource.get(_FILE, source) if isinstance(resource, dict) else source, index)
        if not isinstance(resource, dict):
            problems.append(Problem(where, "must be an object"))
            continue
        name = resource.get("name")
        if not name:
            problems.append(Problem(where, "missing 'name'"))
            continue
        where = "%s resources[%d] '%s'" % (resource.get(_FILE, source), index, name)
        if name in declared:
            problems.append(Problem(where, "declared more than once"))
        declared[name] = resource

        kind = resource.get("kind")
        if kind is None:
            problems.append(Problem(where, "missing 'kind'",
                                    hint="one of: %s" % ", ".join(sorted(RESOURCE_KINDS))))
        else:
            _check_enum(kind, RESOURCE_KINDS, where, "kind", problems)

    if not document.get("passes"):
        problems.append(Problem(source, "no passes declared"))

    # ── buffer layouts ─────────────────────────────────────────
    # A JSON object keyed by layout name, so iterate items rather than indices.
    layouts = document.get("bufferLayouts", {})
    if not isinstance(layouts, dict):
        problems.append(Problem(source, "'bufferLayouts' must be an object keyed by layout name"))
        layouts = {}

    for name, layout in layouts.items():
        where = "%s bufferLayouts '%s'" % (file_of("bufferLayouts", name), name)
        if not isinstance(layout, dict):
            problems.append(Problem(where, "must be an object"))
            continue

        packing = layout.get("packing")
        if packing is not None:
            _check_enum(packing, PACKING_RULES, where, "packing", problems)

        usage = layout.get("usage")
        if usage is not None:
            _check_enum(usage, BUFFER_USAGES, where, "usage", problems)

        layout_source = layout.get("source")
        if isinstance(layout_source, dict) and layout_source.get("type") == "scene":
            _check_enum(str(layout_source.get("data")), SCENE_DATA, where, "source data", problems)
            if layout_source.get("data") == "components" and not layout_source.get("component"):
                problems.append(Problem(where, "'data': 'components' needs the 'component' whose rows it holds"))

        fields = layout.get("fields", [])
        if not isinstance(fields, list):
            problems.append(Problem(where, "'fields' must be an array"))
            continue

        for field_index, field in enumerate(fields):
            if not isinstance(field, dict):
                problems.append(Problem("%s field %d" % (where, field_index),
                                        "must be an object"))
                continue
            field_where = "%s field '%s'" % (where, field.get("name", field_index))
            if "name" not in field:
                problems.append(Problem(field_where, "missing 'name'"))
            field_type = str(field.get("type", "float"))
            if "[" in field_type:
                problems.append(Problem(field_where, "type '%s' has array syntax" % field_type,
                                        hint="use \"type\": \"%s\" with \"arrayCount\""
                                             % field_type.split("[", 1)[0]))
            else:
                _check_enum(field_type, FIELD_TYPES, field_where, "type", problems)

    # ── descriptor set layouts ─────────────────────────────────
    set_layouts = document.get("descriptorSetLayouts", {})
    for layout_name, set_layout in (set_layouts.items() if isinstance(set_layouts, dict) else []):
        bindings = set_layout.get("bindings", []) if isinstance(set_layout, dict) else []
        for binding in bindings if isinstance(bindings, list) else []:
            if not isinstance(binding, dict) or "source" not in binding:
                continue
            where = "%s descriptorSetLayouts '%s' binding %s" % (file_of("descriptorSetLayouts", layout_name), layout_name, binding.get("binding"))
            binding_source = str(binding["source"])
            if not _check_enum(binding_source, frozenset(BINDING_SOURCES), where, "source", problems):
                continue
            if binding.get("type") != BINDING_SOURCES[binding_source]:
                problems.append(Problem(where, "a '%s' binding is a %s"
                                        % (binding_source, BINDING_SOURCES[binding_source])))
            elif binding_source == "material":
                buffer_layouts = document.get("bufferLayouts", {})
                if not isinstance(buffer_layouts, dict) or binding.get("name") not in buffer_layouts:
                    problems.append(Problem(where, "a 'material' binding is filled from the buffer layout "
                                                   "named by its \"name\"; '%s' is not one" % binding.get("name")))
                else:
                    for field in buffer_layouts[binding["name"]].get("fields", []):
                        field_source = str(field.get("source", "")) if isinstance(field, dict) else ""
                        if field_source and not field_source.startswith(("entity.material.", "const.")):
                            problems.append(Problem(where, "material buffer field '%s' reads '%s'; a material "
                                                           "buffer reads only entity.material.* and const.*"
                                                    % (field.get("name"), field_source)))
                if "autoBindBuffer" in binding or binding.get("count", 1) != 1:
                    problems.append(Problem(where, "a 'material' binding is one buffer, without "
                                                   "\"autoBindBuffer\""))

    # ── passes ─────────────────────────────────────────────────
    written, read, presents = set(), set(), []

    passes = document.get("passes", [])
    if not isinstance(passes, list):
        problems.append(Problem(source, "'passes' must be an array"))
        passes = []
    passes = _expand_repeats(passes, source, problems)

    for index, pass_decl in enumerate(passes):
        where = "%s passes[%d]" % (pass_decl.get(_FILE, source) if isinstance(pass_decl, dict) else source, index)
        if not isinstance(pass_decl, dict):
            problems.append(Problem(where, "must be an object"))
            continue
        name = pass_decl.get("name", "<unnamed>")
        where = "%s passes[%d] '%s'" % (pass_decl.get(_FILE, source), index, name)

        if "name" not in pass_decl:
            problems.append(Problem(where, "missing 'name'"))

        requires = pass_decl.get("requires", [])
        if not isinstance(requires, list):
            problems.append(Problem(where, "'requires' must be an array, e.g. [\"rayQuery\"]"))
            requires = []
        for capability in requires:
            if capability not in CAPABILITIES:
                problems.append(Problem(where, "requires '%s', which is not a capability" % capability,
                                        hint=_suggest(str(capability), CAPABILITIES)))

        pass_type = pass_decl.get("type")
        if pass_type is None:
            problems.append(Problem(where, "missing 'type'",
                                    hint="one of: %s" % ", ".join(sorted(PASS_TYPES))))
        else:
            _check_enum(pass_type, PASS_TYPES, where, "type", problems)

        for direction in ("inputs", "outputs"):
            accesses = pass_decl.get(direction, [])
            if not isinstance(accesses, list):
                problems.append(Problem("%s %s" % (where, direction),
                                        "must be an array"))
                continue

            for access_index, access in enumerate(accesses):
                access_where = "%s %s[%d]" % (where, direction, access_index)
                if not isinstance(access, dict):
                    problems.append(Problem(access_where, "must be an object"))
                    continue

                resource = access.get("resource")
                if resource is None:
                    problems.append(Problem(access_where, "missing 'resource'"))
                elif resource not in declared:
                    problems.append(Problem(
                        access_where, "'%s' is not declared in resources" % resource,
                        hint=_suggest(resource, declared)))

                usage = access.get("usage")
                if usage is None:
                    problems.append(Problem(access_where, "missing 'usage'"))
                else:
                    _check_enum(usage, RESOURCE_USAGES, access_where, "usage", problems)

                if resource:
                    (written if direction == "outputs" else read).add(resource)
                # A multisampled attachment's "resolve" target is written too.
                resolve = access.get("resolve")
                if resolve is not None:
                    if direction != "outputs":
                        problems.append(Problem(access_where, "'resolve' applies to outputs"))
                    elif resolve not in declared:
                        problems.append(Problem(
                            access_where, "resolve target '%s' is not declared in resources" % resolve,
                            hint=_suggest(resolve, declared)))
                    else:
                        written.add(resolve)
                if direction == "outputs" and (access.get("present")
                                               or usage == "present"):
                    presents.append((name, resource))

                if "present" in access and not isinstance(access["present"], bool):
                    problems.append(Problem(access_where, "'present' must be true or false"))

        # ── occlusion image ────────────────────────────────────
        execution = pass_decl.get("execution", {})
        occlusion = execution.get("occlusion") if isinstance(execution, dict) else None
        if occlusion is not None:
            image = declared.get(occlusion)
            image_desc = image.get("image", {}) if isinstance(image, dict) else {}
            if not (isinstance(image, dict) and image.get("kind") == "image" and
                    isinstance(image_desc, dict) and image_desc.get("format") == "R32_SFLOAT" and
                    isinstance(image.get("readback"), dict)):
                problems.append(Problem("%s execution" % where,
                                        "'occlusion' names '%s', which is not an R32_SFLOAT image "
                                        "resource with a readback" % occlusion))

        # ── shader files ───────────────────────────────────────
        pipeline_state = pass_decl.get("pipeline", {})
        if not isinstance(pipeline_state, dict):
            problems.append(Problem("%s pipeline" % where, "must be an object"))
            pipeline_state = {}

        references = [(key, pipeline_state.get(key)) for key in (
            "vertexShader", "fragmentShader", "computeShader", "geometryShader",
            "tessControlShader", "tessEvaluationShader", "taskShader", "meshShader",
            "rayGenShader")]
        misses = pipeline_state.get("missShaders")
        if isinstance(misses, list):
            references += [("missShaders[%d]" % i, m) for i, m in enumerate(misses)]
        groups = pipeline_state.get("hitGroups")
        if isinstance(groups, list):
            for i, group in enumerate(groups):
                if isinstance(group, dict):
                    references += [("hitGroups[%d].%s" % (i, k), group.get(k))
                                   for k in ("closestHit", "anyHit", "intersection")]

        for key, reference in references:
            if not isinstance(reference, str) or not reference or base_dir is None:
                continue
            if not (base_dir / reference).exists():
                hint = None
                glsl = base_dir / reference[:-4] if reference.endswith(".spv") else None
                if glsl is not None and glsl.exists():
                    hint = ("the GLSL source exists — compile it with "
                            "shoonyakasha.shaders.compile_dir()")
                problems.append(Problem("%s pipeline.%s" % (where, key),
                                        "'%s' does not exist" % reference, hint=hint))

    # ── presets ────────────────────────────────────────────────
    presets = document.get("presets", {})
    if not isinstance(presets, dict):
        problems.append(Problem(source, "'presets' must be an object of named presets"))
        presets = {}
    pass_names = {p.get("name") for p in passes if isinstance(p, dict)}
    for original in document.get("passes", []) if isinstance(document.get("passes"), list) else []:
        if isinstance(original, dict) and "repeat" in original:
            pass_names.add(original.get("name"))   # a repeated pass's declared name
    for preset_name, preset in presets.items():
        where = "%s presets '%s'" % (file_of("presets", preset_name), preset_name)
        if not isinstance(preset, dict):
            problems.append(Problem(where, "must be an object"))
            continue
        switches = preset.get("passes", {})
        if not isinstance(switches, dict):
            problems.append(Problem(where, "'passes' must map pass names to true or false"))
            switches = {}
        for pass_name, enabled in switches.items():
            if not isinstance(enabled, bool):
                problems.append(Problem(where, "pass '%s' must be true or false" % pass_name))
            if pass_name not in pass_names:
                problems.append(Problem(where, "switches '%s', which is not a pass" % pass_name,
                                        hint=_suggest(pass_name, pass_names)))
        values = preset.get("values", {})
        if not isinstance(values, dict):
            problems.append(Problem(where, "'values' must map scene.custom keys to values"))
            values = {}
        for key, value in values.items():
            number = lambda v: isinstance(v, (int, float)) and not isinstance(v, bool)
            if not (value is None or number(value)
                    or (isinstance(value, list) and value and all(number(v) for v in value))):
                problems.append(Problem(where, "value '%s' must be a number, an array of numbers or null"
                                        % key))

    # ── ui ─────────────────────────────────────────────────────
    ui = document.get("ui")
    if ui is not None:
        where = "%s ui" % source
        if (not isinstance(ui, dict) or not isinstance(ui.get("depth"), str)
                or not isinstance(ui.get("afterPass"), str)):
            problems.append(Problem(where, "must be an object with 'depth', an image, and 'afterPass', "
                                           "the pass after which it is read"))
        else:
            if ui["depth"] not in declared:
                problems.append(Problem(where, "'depth' names '%s', which is not a resource" % ui["depth"],
                                        hint=_suggest(ui["depth"], declared)))
            after = next((p for p in passes if isinstance(p, dict) and p.get("name") == ui["afterPass"]), None)
            if after is None:
                problems.append(Problem(where, "'afterPass' names '%s', which is not a pass" % ui["afterPass"],
                                        hint=_suggest(ui["afterPass"], pass_names)))
            elif not any(isinstance(o, dict) and o.get("resource") == ui["depth"]
                         for o in (after.get("outputs") or [])):
                problems.append(Problem(where, "pass '%s' does not write '%s'" % (ui["afterPass"], ui["depth"])))

    # ── whole-graph checks ─────────────────────────────────────
    for resource in sorted(read - written):
        if not declared.get(resource, {}).get("imported"):
            problems.append(Problem(
                "%s resources '%s'" % (source, resource),
                "read by a pass but never written and not imported",
                severity="warning"))

    if not presents:
        colour_targets = any(
            isinstance(access, dict) and access.get("usage") in _COLOUR_WRITES
            for pass_decl in passes
            if isinstance(pass_decl, dict)
            for access in (pass_decl.get("outputs") or [])
            if isinstance(pass_decl.get("outputs"), list))
        if colour_targets:
            problems.append(Problem(
                source, "no output declares 'present'", severity="warning",
                hint="the compiler will assume the last pass writing a "
                     "presentable image meant to, and warn at runtime"))

    error = _engine_loader_error(document)
    if error:
        problems.append(Problem(source, "the engine's loader rejects it: %s" % error))
    return problems


def _engine_loader_error(document):
    """The engine's loader error for `document`, or None when it loads or the
    extension is not installed."""
    try:
        from ._shoonyakasha import _check_pipeline_json
    except ImportError:
        return None
    return _check_pipeline_json(json.dumps(_without_file_tags(document))) or None


# ── Pipelines split across files ───────────────────────────────

#: Key load_document(tag=True) adds to each pass and resource: the file it came from.
_FILE = "__file__"

#: Sections keyed by name, combined across files; and lists, appended.
_NAMED_SECTIONS = ("samplers", "vertexFormats", "uniformBuffers", "entityDataBindings",
                   "bufferLayouts", "descriptorSetLayouts", "presets")
_LIST_SECTIONS = ("resources", "passes")
_SHADER_KEYS = ("vertexShader", "fragmentShader", "computeShader", "geometryShader",
                "tessControlShader", "tessEvaluationShader", "taskShader", "meshShader")


class PipelineFileError(ValueError):
    """A pipeline's files cannot be read or merged; `where` names the file."""

    def __init__(self, where, message):
        super().__init__("%s: %s" % (where, message))
        self.where = where
        self.message = message


def _resolve_beside(value, directory):
    """A relative path naming a file in `directory` becomes the path to that file."""
    if not isinstance(value, str) or not value or Path(value).is_absolute():
        return value
    beside = directory / value
    return os.path.normpath(str(beside)).replace("\\", "/") if beside.exists() else value


def _resolve_paths(document, directory):
    """Shader paths and file-source paths beside `directory`, as the engine resolves them."""
    for pass_decl in document.get("passes", []) if isinstance(document.get("passes"), list) else []:
        pipe = pass_decl.get("pipeline") if isinstance(pass_decl, dict) else None
        if not isinstance(pipe, dict):
            continue
        for key in _SHADER_KEYS + ("rayGenShader",):
            if key in pipe:
                pipe[key] = _resolve_beside(pipe[key], directory)
        if isinstance(pipe.get("missShaders"), list):
            pipe["missShaders"] = [_resolve_beside(m, directory) for m in pipe["missShaders"]]
        for group in pipe.get("hitGroups", []) if isinstance(pipe.get("hitGroups"), list) else []:
            if isinstance(group, dict):
                for key in ("closestHit", "anyHit", "intersection"):
                    if key in group:
                        group[key] = _resolve_beside(group[key], directory)
    layouts = document.get("bufferLayouts")
    for layout in layouts.values() if isinstance(layouts, dict) else []:
        source = layout.get("source") if isinstance(layout, dict) else None
        if isinstance(source, dict) and source.get("type") == "file" and "path" in source:
            source["path"] = _resolve_beside(source["path"], directory)


def _pipeline_file(path):
    path = Path(path)
    return path / "pipeline.json" if path.is_dir() else path


def _expand_include(directory, include):
    """The files `include` names, relative to `directory`: itself, or every
    match of a * and ? pattern in its last part, in name order."""
    full = directory / include
    if any(c in str(full.parent) for c in "*?"):
        raise ValueError("only the last part of an include may be a pattern")
    if not any(c in full.name for c in "*?"):
        return [full]
    pattern = re.compile(".*".join(re.escape(p).replace("\\?", ".") for p in full.name.split("*")) + r"\Z")
    return sorted(p for p in full.parent.iterdir() if p.is_file() and pattern.match(p.name)) \
        if full.parent.is_dir() else []


def load_document(path, origins=None, tag=False):
    """The pipeline at `path` with the files it includes merged, as the
    engine reads it: a pipeline JSON file, or a directory holding
    pipeline.json.

    A file's "include" lists more files, relative to it, in order; a name
    may end in a pattern with * and ?, which includes every file it matches
    in name order. Included files are merged in the order listed, then the
    including file's own keys: "passes" and "resources" are appended, the
    named sections (samplers, vertexFormats, uniformBuffers,
    entityDataBindings, bufferLayouts, descriptorSetLayouts, presets) are
    combined, and any other key may appear in only one file or with the
    same value. Relative shader and buffer-file paths resolve beside the
    file that names them. Raises PipelineFileError for a file that cannot
    be read, a name defined in two files, an include that names no file,
    and a file that includes itself.

    `origins`, when given, is filled with (section, name) -> file for named
    entries, and ("file", n) -> the nth file read. With `tag`, each pass and
    resource holds the file it came from under "__file__"."""
    root = _pipeline_file(path)
    base = root.parent
    origins = {} if origins is None else origins
    document = {}
    stack = []
    read = []

    def label(file):
        try:
            return file.relative_to(base).as_posix()
        except ValueError:
            return str(file)

    def twice(what, first, second):
        raise PipelineFileError(second, "%s is also defined in %s" % (what, first))

    def merge(file):
        file = Path(os.path.normpath(str(file)))
        name = label(file)
        if file in stack:
            raise PipelineFileError(label(stack[-1]), "includes %s, which includes it" % name)
        origins[("file", len(read))] = name
        read.append(name)
        try:
            data = json.loads(file.read_text(encoding="utf-8"))
        except FileNotFoundError:
            raise PipelineFileError(name, "file not found") from None
        except json.JSONDecodeError as exc:
            raise PipelineFileError("%s:%d:%d" % (name, exc.lineno, exc.colno), "invalid JSON: %s" % exc.msg) from None
        if not isinstance(data, dict):
            raise PipelineFileError(name, "does not hold a JSON object")
        _resolve_paths(data, file.parent.absolute())

        stack.append(file)
        includes = data.get("include", [])
        if not isinstance(includes, list) or not all(isinstance(i, str) for i in includes):
            raise PipelineFileError(name, "'include' must be a list of paths")
        for include in includes:
            try:
                found = _expand_include(file.parent, include)
            except ValueError as exc:
                raise PipelineFileError(name, "include '%s': %s" % (include, exc)) from None
            if not found or (len(found) == 1 and not found[0].is_file()):
                raise PipelineFileError(name, "include '%s' names no file" % include)
            for included in found:
                merge(included)
        stack.pop()

        for key, value in data.items():
            if key == "include":
                continue
            if key in _LIST_SECTIONS:
                if not isinstance(value, list):
                    raise PipelineFileError(name, "'%s' must be a list" % key)
                for item in value:
                    if isinstance(item, dict) and isinstance(item.get("name"), str):
                        entry = (key, item["name"])
                        if entry in origins:
                            twice("%s '%s'" % entry, origins[entry], name)
                        origins[entry] = name
                        if tag:
                            item[_FILE] = name
                    document.setdefault(key, []).append(item)
            elif key in _NAMED_SECTIONS:
                if not isinstance(value, dict):
                    raise PipelineFileError(name, "'%s' must be an object keyed by name" % key)
                section = document.setdefault(key, {})
                for entry_name, body in value.items():
                    entry = (key, entry_name)
                    if entry in origins:
                        twice("%s '%s'" % entry, origins[entry], name)
                    origins[entry] = name
                    section[entry_name] = body
            elif key in document and document[key] != value:
                twice("'%s'" % key, origins[(key, None)], name)
            else:
                origins.setdefault((key, None), name)
                document[key] = value

    merge(root)
    return document


def files(path):
    """Every file of the pipeline at `path`: its pipeline JSON first, then
    each file it includes, in the order load_document reads them. Watch
    their modification times to reload a pipeline when any of them changes."""
    origins = {}
    load_document(path, origins)
    base = _pipeline_file(path).parent
    return [base / origins[("file", n)] for n in range(sum(1 for k in origins if k[0] == "file"))]


def _is_url(path):
    return isinstance(path, str) and path.startswith(("http://", "https://"))


def _download(url, timeout):
    import urllib.request
    request = urllib.request.Request(url, headers={"User-Agent": "shoonyakasha"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def _referenced_files(document):
    """The relative shader and buffer-file paths a pipeline JSON names."""
    names = []
    for pass_decl in document.get("passes", []) if isinstance(document.get("passes"), list) else []:
        pipe = pass_decl.get("pipeline") if isinstance(pass_decl, dict) else None
        if not isinstance(pipe, dict):
            continue
        names += [pipe.get(k) for k in _SHADER_KEYS + ("rayGenShader",)]
        names += pipe.get("missShaders", []) if isinstance(pipe.get("missShaders"), list) else []
        for group in pipe.get("hitGroups", []) if isinstance(pipe.get("hitGroups"), list) else []:
            if isinstance(group, dict):
                names += [group.get(k) for k in ("closestHit", "anyHit", "intersection")]
    layouts = document.get("bufferLayouts")
    for layout in layouts.values() if isinstance(layouts, dict) else []:
        source = layout.get("source") if isinstance(layout, dict) else None
        if isinstance(source, dict) and source.get("type") == "file":
            names.append(source.get("path"))
    return [n for n in names if isinstance(n, str) and n and "://" not in n and not n.startswith("/")]


def fetch(url, cache=None, timeout=60):
    """Download the pipeline at `url` into a local directory and return its
    path, to give the engine (sk.Engine does this itself for a URL).

    `url` names a pipeline JSON, or ends in "/" for the pipeline.json there.
    The files it includes, and the shader and buffer files ("source":
    {"type": "file"}) they name by relative paths, are downloaded beside it,
    keeping their paths relative to the pipeline JSON. Includes must name
    their files: patterns cannot be listed over HTTP. Every file must lie
    under the pipeline JSON's directory URL.

    `cache` is the directory to download into; by default one per URL under
    ~/.cache/shoonyakasha/pipelines. Files already there are replaced.
    Raises PipelineFileError naming the URL that failed."""
    import hashlib
    import posixpath
    import urllib.error
    import urllib.parse

    if url.endswith("/"):
        url += "pipeline.json"
    base = url.rsplit("/", 1)[0] + "/"
    if cache is None:
        digest = hashlib.sha256(url.encode("utf-8")).hexdigest()[:16]
        cache = Path.home() / ".cache" / "shoonyakasha" / "pipelines" / digest
    cache = Path(cache)

    def local_name(file_url):
        """`file_url` relative to `base`, or PipelineFileError when it lies outside."""
        if not file_url.startswith(base):
            raise PipelineFileError(file_url, "lies outside the pipeline's URL %s" % base)
        relative = urllib.parse.unquote(urllib.parse.urlsplit(file_url[len(base):]).path)
        if not relative or relative.startswith("/") or ".." in relative.split("/"):
            raise PipelineFileError(file_url, "lies outside the pipeline's URL %s" % base)
        return relative

    def get(file_url):
        relative = local_name(file_url)
        try:
            data = _download(file_url, timeout)
        except (urllib.error.URLError, OSError) as exc:
            raise PipelineFileError(file_url, "download failed: %s" % exc) from None
        target = cache / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + ".partial")
        partial.write_bytes(data)
        os.replace(partial, target)
        return data

    seen = set()

    def fetch_json(file_url, chain):
        if file_url in chain:
            raise PipelineFileError(chain[-1], "includes %s, which includes it" % file_url)
        if file_url in seen:
            return
        seen.add(file_url)
        data = get(file_url)
        try:
            document = json.loads(data.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise PipelineFileError(file_url, "invalid JSON: %s" % exc) from None
        if not isinstance(document, dict):
            raise PipelineFileError(file_url, "does not hold a JSON object")
        directory = file_url.rsplit("/", 1)[0] + "/"
        includes = document.get("include", [])
        if not isinstance(includes, list) or not all(isinstance(i, str) for i in includes):
            raise PipelineFileError(file_url, "'include' must be a list of paths")
        for include in includes:
            if any(c in include for c in "*?"):
                raise PipelineFileError(file_url, "include '%s' is a pattern, which cannot be listed over HTTP; "
                                                  "name each file" % include)
            fetch_json(_join_url(directory, include, posixpath), chain + [file_url])
        for name in _referenced_files(document):
            file_url_ = _join_url(directory, name, posixpath)
            if file_url_ not in seen:
                seen.add(file_url_)
                get(file_url_)

    fetch_json(url, [])
    return cache / local_name(url)


def _join_url(directory, relative, posixpath):
    """`relative` resolved against the directory URL `directory`, with . and .. removed."""
    import urllib.parse
    parts = urllib.parse.urlsplit(directory)
    path = posixpath.normpath(posixpath.join(parts.path, relative))
    return urllib.parse.urlunsplit((parts.scheme, parts.netloc, path, "", ""))


def _without_file_tags(document):
    """`document` with the "__file__" tags load_document(tag=True) adds removed."""
    if not isinstance(document, dict) or not any(
            isinstance(item, dict) and _FILE in item
            for key in _LIST_SECTIONS if isinstance(document.get(key), list) for item in document[key]):
        return document
    clean = dict(document)
    for key in _LIST_SECTIONS:
        if isinstance(document.get(key), list):
            clean[key] = [{k: v for k, v in item.items() if k != _FILE} if isinstance(item, dict) else item
                          for item in document[key]]
    return clean


def validate(path):
    """Validate a pipeline: a pipeline JSON file, or a directory holding
    pipeline.json, with the files it includes (load_document). Problems name
    the file they are in. Returns a list of Problem."""
    path = Path(path)
    origins = {}
    try:
        document = load_document(path, origins, tag=True)
    except PipelineFileError as exc:
        return [Problem(exc.where, exc.message)]
    root = _pipeline_file(path)
    return validate_json(document, base_dir=root.parent, source=root.name, origins=origins)


def check(path, warnings_are_errors=False):
    """Validate and raise on failure. Returns the problem list when it passes."""
    problems = validate(path)
    fatal = [p for p in problems
             if p.is_error or (warnings_are_errors and not p.is_error)]
    if fatal:
        raise ValueError("%s has %d problem(s):\n\n%s"
                         % (path, len(fatal), "\n".join(str(p) for p in fatal)))
    return problems
