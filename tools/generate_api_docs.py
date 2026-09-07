"""Refresh source-derived API inventories inside the Markdown references.

Run from any directory; --check reports drift without writing files.
This is a deliberately small extractor for this repository's facade headers
and Cython wrapper, not a general C++/Cython parser. Prose outside the markers
is maintained by hand and describes runtime contracts the declarations omit.
"""

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
START = "<!-- BEGIN SOURCE API -->"
END = "<!-- END SOURCE API -->"
APIS = {"Engine": "engine", "Scene": "scene", "Input": "input",
        "Physics": "physics", "Ecs": "ecs"}


def python_inventory(source, name, native_source=None):
    match = re.search(r"^cdef class " + name + r":\n(.*?)(?=^cdef class |\Z)", source, re.M | re.S)
    if not match:
        raise ValueError("Missing Python class " + name)
    block = match.group(1)
    entries = []
    native_returns = {}
    if native_source:
        for declaration in cpp_inventory(native_source, name).splitlines():
            declaration_match = re.match(r"^(.+?) (\w+)\(", declaration)
            if declaration_match:
                native_returns[declaration_match.group(2)] = declaration_match.group(1)
    for method in re.finditer(r"^    def (\w+)\((.*?)\):\n", block, re.M | re.S):
        method_name, args = method.groups()
        if method_name.startswith("_") and not (name == "Engine" and method_name == "__init__"):
            continue
        before = block[:method.start()].rstrip().splitlines()
        decorator = before[-1].strip() if before else ""
        if decorator.endswith(".setter"):
            continue
        args = re.sub(r"\b(?:str|int|float|bint|dict|uint32_t)\s+", "", args)
        args = re.sub(r"\s+", " ", args).strip()
        args = re.sub(r"^self(?:,\s*)?", "", args)
        if decorator == "@property":
            signature = method_name + (" (read/write property)" if "@" + method_name + ".setter" in block else " (read-only property)")
        else:
            signature = (name if method_name == "__init__" else method_name) + "(" + args + ")"
        tail = block[method.end():]
        body = re.split(r"^    (?:def |@)", tail, maxsplit=1, flags=re.M)[0]
        native_call = re.search(r"self\._ptr\.(\w+)\(", body)
        native_type = native_returns.get(native_call.group(1), "") if native_call else ""
        result_types = {"bool": "bool", "void": "None", "float": "float",
                        "int": "int", "uint32_t": "int", "uint8_t": "int",
                        "uint64_t": "int", "size_t": "int", "EntityHandle": "int",
                        "std::string": "str", "glm::vec2": "2-tuple",
                        "glm::vec3": "3-tuple", "glm::vec4": "4-tuple",
                        "glm::mat4": "4×4 tuple (column-major)",
                        "CameraType": "int", "LightType": "int", "UIAnchor": "int",
                        "std::vector<EntityHandle>": "list[int]",
                        "std::vector<std::string>": "list[str]",
                        "std::shared_ptr<void>": "object or None"}
        result_type = result_types.get(native_type, "See contract")
        if not re.search(r"\breturn\b", body):
            result_type = name if method_name == "__init__" else "None"
        if name == "Engine":
            result_type = {"scene": "Scene", "input": "Input", "physics": "Physics",
                           "ecs": "Ecs", "load_gltf_scene": "GltfResult"}.get(method_name, result_type)
        if name == "Ecs" and method_name == "get_component":
            result_type = "object or None"
        if native_source and result_type == "See contract":
            raise ValueError("Document the Python return conversion for %s.%s" % (name, method_name))
        doc = re.match(r'\s*"""(.*?)"""', tail, re.S)
        description = re.sub(r"\s+", " ", doc.group(1).split("\n\n")[0]).strip() if doc else (
            "Calls native `" + native_call.group(1) + "`; see the class contract above." if native_call else "See the class contract above.")
        entries.append("| `" + signature + "` | " + result_type + " | " + description.replace("|", "\\|") + " |")
    if not entries:
        raise ValueError("Empty Python API inventory: " + name)
    return "## Members\n\nSignatures and short descriptions below are extracted from the Cython wrapper; return shapes follow its conversions and the native declarations.\n\n| Member | Returns / property value | Description |\n|---|---|---|\n" + "\n".join(entries) + "\n"


def cpp_inventory(source, name):
    source = re.sub(r"#ifdef SHOONYAKASHA_TESTING.*?#endif", "", source, flags=re.S)
    block = source.split("class " + name + "API {", 1)[1].split("private:", 1)[0]
    # Strip comments before accumulating multiline declarations.
    block = re.sub(r"//[^\n]*", "", block).split("public:", 1)[1]
    declarations = []
    for statement in block.split(";"):
        statement = re.sub(r"\s+", " ", statement).strip()
        if not statement or "= delete" in statement or "wireSprite2DManager" in statement or statement == "struct Impl":
            continue
        declarations.append(statement + ";")
    if not declarations:
        raise ValueError("Empty C++ API inventory: " + name)
    return "## Members\n\nDeclarations below are extracted from the public facade header; test constructors and internal wiring are omitted.\n\n```cpp\n" + "\n".join(declarations) + "\n```\n"


def generated_pages():
    pyx = (ROOT / "python/shoonyakasha/_shoonyakasha.pyx").read_text(encoding="utf-8")
    for name, slug in APIS.items():
        header = (ROOT / ("include/Facade/" + name + "API.h")).read_text(encoding="utf-8")
        yield ROOT / ("docs/api/python/" + slug + ".md"), python_inventory(pyx, name, header)
        cpp_slug = slug + "-api"
        yield ROOT / ("docs/api/cpp/" + cpp_slug + ".md"), cpp_inventory(header, name)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    changed = []
    for path, inventory in generated_pages():
        current = path.read_text(encoding="utf-8")
        replacement = START + "\n\n" + inventory + "\n" + END
        if START in current:
            if END not in current:
                raise ValueError("Missing end marker in " + str(path))
            updated = current[:current.index(START)] + replacement + current[current.index(END) + len(END):]
        else:
            updated = current.rstrip() + "\n\n" + replacement + "\n"
        if updated != current:
            changed.append(str(path.relative_to(ROOT)))
            if not args.check:
                path.write_text(updated, encoding="utf-8")
    if args.check and changed:
        print("API inventory drift; run python tools/generate_api_docs.py:\n" + "\n".join(changed))
        return 1
    print("API inventories current" if args.check else "Updated %d API inventories" % len(changed))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
