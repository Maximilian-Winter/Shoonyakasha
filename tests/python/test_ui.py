"""Tests for the canvas UI's Python surface that need no compiled extension.

    python -m unittest discover -s tests/python

The binding is spread over four files that restate one another: the facade
header, the Cython declarations, the wrapper class and the package exports.
These tests fail when they disagree, and check the default pipeline's UI pass.
"""

import json
import re
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))

from shoonyakasha import pipeline  # noqa: E402

HEADER = (REPO / "include/Facade/UIAPI.h").read_text(encoding="utf-8")
PXD = (REPO / "python/shoonyakasha/_engine_api.pxd").read_text(encoding="utf-8")
PYX = (REPO / "python/shoonyakasha/_shoonyakasha.pyx").read_text(encoding="utf-8")
INIT = (REPO / "python/shoonyakasha/__init__.py").read_text(encoding="utf-8")
DEFAULT = REPO / "python/shoonyakasha/pipelines/default"


def header_methods():
    public = HEADER.split("class UIAPI {", 1)[1].split("private:", 1)[0]
    public = re.sub(r"#ifdef SHOONYAKASHA_TESTING.*?#endif", "", public, flags=re.S)
    public = re.sub(r"//[^\n]*", "", public)
    return set(re.findall(r"\b(\w+)\s*\([^;{]*\)\s*(?:const\s*)?;", public)) - {"UIAPI"}


def pxd_methods():
    block = PXD.split('cdef cppclass CppUIAPI "Shoonyakasha::Facade::UIAPI":', 1)[1]
    block = block.split("\n# ", 1)[0]
    return set(re.findall(r"^\s+\w[\w&<>]*\s+(\w+)\(", block, re.M))


def wrapper_class():
    return re.search(r"^cdef class UI:\n(.*?)(?=^cdef class |\Z)", PYX, re.M | re.S).group(1)


class BindingInventory(unittest.TestCase):
    def test_every_facade_method_is_declared_for_cython(self):
        self.assertEqual(header_methods(), pxd_methods())

    def test_every_facade_method_is_called_by_the_wrapper(self):
        called = set(re.findall(r"self\._ptr\.(\w+)\(", wrapper_class()))
        self.assertEqual(header_methods(), called)

    def test_the_engine_hands_out_the_wrapper(self):
        self.assertIn("CppUIAPI& getUI() except +", PXD)
        self.assertRegex(PYX, r"def ui\(self\):[\s\S]*?&self\._ptr\.getUI\(\)")

    def test_the_package_exports_the_class_and_constants(self):
        names = ["UI", "TEXT_ALIGN_TOP", "TEXT_ALIGN_MIDDLE", "TEXT_ALIGN_BOTTOM",
                 "CANVAS_CONSTANT_PIXEL", "CANVAS_SCALE_WITH_SCREEN"]
        exported = INIT.split("from ._shoonyakasha import (", 1)[1].split("\n    )", 1)[0]
        lazy = INIT.split("_ENGINE_SYMBOLS = frozenset({", 1)[1].split("})", 1)[0]
        listed = INIT.split("__all__ = [", 1)[1].split("]", 1)[0]
        for name in names:
            with self.subTest(name=name):
                self.assertRegex(exported, r"\b%s,\n" % name)
                self.assertIn('"%s"' % name, lazy)
                self.assertIn('"%s"' % name, listed)
                self.assertRegex(PYX, r"(?m)^%s = <int>" % name if name != "UI" else r"(?m)^cdef class UI:")

    def test_constants_follow_the_facade_enums(self):
        types = (REPO / "include/Facade/FacadeTypes.h").read_text(encoding="utf-8")
        for enum, prefix, members in (("TextVAlign", "TEXT_ALIGN_", ("Top", "Middle", "Bottom")),
                                      ("CanvasScaleMode", "CANVAS_", ("ConstantPixel", "ScaleWithScreen"))):
            body = types.split("enum class %s" % enum, 1)[1].split("};", 1)[0]
            for member in members:
                with self.subTest(member=member):
                    self.assertRegex(body, r"\b%s\s*=" % member)
                    self.assertIn("%s_%s" % (enum, member), PYX)


class DefaultPipelineUI(unittest.TestCase):
    def test_the_last_pass_draws_the_ui_and_presents(self):
        passes = json.loads((DEFAULT / "pipeline.json").read_text(encoding="utf-8"))["passes"]
        last = passes[-1]
        self.assertEqual(last["execution"]["type"], "ui_canvas")
        self.assertEqual(last["outputs"], [{"resource": "swapchain", "usage": "color_blend", "present": True}])
        for earlier in passes[:-1]:
            for output in earlier.get("outputs", []):
                self.assertFalse(output.get("present", False), earlier["name"])

    def test_it_validates(self):
        pipeline.check(DEFAULT / "pipeline.json")

    def test_it_ships_the_ui_shaders(self):
        for name in ("ui.vert", "ui.frag"):
            with self.subTest(name=name):
                source = DEFAULT / "shaders" / "ui" / name
                compiled = source.with_name(name + ".spv")
                self.assertTrue(source.exists())
                self.assertTrue(compiled.exists())
                self.assertGreaterEqual(compiled.stat().st_mtime, source.stat().st_mtime - 2,
                                        "%s is older than its source; recompile it" % compiled.name)


if __name__ == "__main__":
    unittest.main()
