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
Two copies of one list is a drift risk, so `tests/python/test_pipeline.py` parses
those sources and asserts they still agree.
"""

import difflib
import json
from pathlib import Path

__all__ = [
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
]

RESOURCE_USAGES = frozenset({
    "color_write", "color_attachment_write",
    "color_blend", "color_attachment_blend",
    "depth_write", "depth_stencil_write", "depth_read",
    "shader_read", "shader_read_write", "storage_image_write",
    "input_attachment", "transfer_src", "transfer_dst", "present",
})

PASS_TYPES = frozenset({"graphics", "compute", "transfer"})
RESOURCE_KINDS = frozenset({"image", "buffer"})

FIELD_TYPES = frozenset({
    "float", "double", "int", "uint", "bool",
    "vec2", "vec3", "vec4", "ivec2", "ivec3", "ivec4",
    "uvec2", "uvec3", "uvec4", "mat2", "mat3", "mat4",
})

PACKING_RULES = frozenset({"std140", "std430", "scalar", "push_constant"})

#: How a buffer layout is bound. Descriptor types the JSON accepts for a layout.
BUFFER_USAGES = frozenset({"uniform_buffer", "storage_buffer", "push_constant"})

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


def validate_json(document, base_dir=None, source="<json>"):
    """Validate an already-parsed pipeline. Returns a list of Problem."""
    problems = []
    base_dir = Path(base_dir) if base_dir else None

    if not isinstance(document, dict):
        return [Problem(source, "top level must be a JSON object")]

    # ── resources ──────────────────────────────────────────────
    declared = {}
    resources = document.get("resources", [])
    if not isinstance(resources, list):
        problems.append(Problem(source, "'resources' must be an array"))
        resources = []

    for index, resource in enumerate(resources):
        where = "%s resources[%d]" % (source, index)
        if not isinstance(resource, dict):
            problems.append(Problem(where, "must be an object"))
            continue
        name = resource.get("name")
        if not name:
            problems.append(Problem(where, "missing 'name'"))
            continue
        where = "%s resources[%d] '%s'" % (source, index, name)
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
    if isinstance(layouts, list):
        # Tolerated: a list of objects each carrying its own "name".
        layouts = {entry.get("name", "<unnamed>"): entry
                   for entry in layouts if isinstance(entry, dict)}
    if not isinstance(layouts, dict):
        problems.append(Problem(source, "'bufferLayouts' must be an object"))
        layouts = {}

    for name, layout in layouts.items():
        where = "%s bufferLayouts '%s'" % (source, name)
        if not isinstance(layout, dict):
            problems.append(Problem(where, "must be an object"))
            continue

        packing = layout.get("packing")
        if packing is not None:
            _check_enum(packing, PACKING_RULES, where, "packing", problems)

        usage = layout.get("usage")
        if usage is not None:
            _check_enum(usage, BUFFER_USAGES, where, "usage", problems)

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
            field_type = field.get("type")
            if field_type is None:
                problems.append(Problem(field_where, "missing 'type'"))
            else:
                # "vec4[16]" -> "vec4"; the array suffix is checked separately.
                base_type = str(field_type).split("[", 1)[0]
                _check_enum(base_type, FIELD_TYPES, field_where, "type", problems)

    # ── passes ─────────────────────────────────────────────────
    written, read, presents = set(), set(), []

    passes = document.get("passes", [])
    if not isinstance(passes, list):
        problems.append(Problem(source, "'passes' must be an array"))
        passes = []

    for index, pass_decl in enumerate(passes):
        where = "%s passes[%d]" % (source, index)
        if not isinstance(pass_decl, dict):
            problems.append(Problem(where, "must be an object"))
            continue
        name = pass_decl.get("name", "<unnamed>")
        where = "%s passes[%d] '%s'" % (source, index, name)

        if "name" not in pass_decl:
            problems.append(Problem(where, "missing 'name'"))

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
                if direction == "outputs" and (access.get("present")
                                               or usage == "present"):
                    presents.append((name, resource))

                if "present" in access and not isinstance(access["present"], bool):
                    problems.append(Problem(access_where, "'present' must be true or false"))

        # ── shader files ───────────────────────────────────────
        pipeline_state = pass_decl.get("pipeline", {})
        if not isinstance(pipeline_state, dict):
            problems.append(Problem("%s pipeline" % where, "must be an object"))
            pipeline_state = {}

        for key in ("vertexShader", "fragmentShader", "computeShader",
                    "geometryShader"):
            reference = pipeline_state.get(key)
            if not reference or base_dir is None:
                continue
            if not (base_dir / reference).exists():
                hint = None
                glsl = base_dir / reference[:-4] if reference.endswith(".spv") else None
                if glsl is not None and glsl.exists():
                    hint = ("the GLSL source exists — compile it with "
                            "shoonyakasha.shaders.compile_dir()")
                problems.append(Problem("%s pipeline.%s" % (where, key),
                                        "'%s' does not exist" % reference, hint=hint))

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

    return problems


def validate(path):
    """Validate a pipeline JSON file. Returns a list of Problem."""
    path = Path(path)
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        return [Problem(str(path), "file not found")]
    except json.JSONDecodeError as exc:
        return [Problem("%s:%d:%d" % (path, exc.lineno, exc.colno),
                        "invalid JSON: %s" % exc.msg)]

    return validate_json(document, base_dir=path.parent, source=path.name)


def check(path, warnings_are_errors=False):
    """Validate and raise on failure. Returns the problem list when it passes."""
    problems = validate(path)
    fatal = [p for p in problems
             if p.is_error or (warnings_are_errors and not p.is_error)]
    if fatal:
        raise ValueError("%s has %d problem(s):\n\n%s"
                         % (path, len(fatal), "\n".join(str(p) for p in fatal)))
    return problems
