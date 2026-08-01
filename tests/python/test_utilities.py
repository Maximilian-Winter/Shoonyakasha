"""Tests for the pure-Python utility modules.

Stdlib unittest rather than pytest, so this runs anywhere the package does
without adding a test dependency:

    python -m unittest discover -s tests/python

The load-bearing tests are the two anti-drift ones at the bottom. `pipeline.py`
and `assets.py` restate vocabulary and rules that are defined in C++; those
tests parse the C++ sources and fail when the copies disagree.
"""

import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))

from shoonyakasha import assets, pipeline, shaders  # noqa: E402


class ShaderCompilation(unittest.TestCase):
    """Needs glslc; skipped where the Vulkan SDK is absent."""

    @classmethod
    def setUpClass(cls):
        try:
            cls.glslc = shaders.find_glslc()
        except shaders.GlslcNotFound as exc:
            raise unittest.SkipTest(str(exc).splitlines()[0])

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.tmp, ignore_errors=True)

    def write(self, name, text):
        path = self.tmp / name
        path.write_text(text, encoding="utf-8")
        return path

    VALID = "#version 450\nvoid main() { gl_Position = vec4(0.0); }\n"

    def test_compiles_to_spv_beside_the_source(self):
        source = self.write("a.vert", self.VALID)
        output = shaders.compile(source)
        self.assertEqual(self.tmp / "a.vert.spv", output)
        self.assertGreater(output.stat().st_size, 0)

    def test_second_call_is_a_no_op(self):
        source = self.write("a.vert", self.VALID)
        first = shaders.compile(source)
        stamp = first.stat().st_mtime_ns
        shaders.compile(source)
        self.assertEqual(stamp, first.stat().st_mtime_ns)

    def test_force_recompiles_anyway(self):
        source = self.write("a.vert", self.VALID)
        output = shaders.compile(source)
        self.assertFalse(shaders.is_stale(source))
        shaders.compile(source, force=True)
        self.assertTrue(output.exists())

    def test_a_newer_source_is_stale(self):
        source = self.write("a.vert", self.VALID)
        output = shaders.compile(source)
        os.utime(source, (output.stat().st_atime + 10, output.stat().st_mtime + 10))
        self.assertTrue(shaders.is_stale(source))

    def test_failure_carries_glslc_diagnostics(self):
        source = self.write("bad.frag",
                            "#version 450\nvoid main(){ no_such_function(); }\n")
        with self.assertRaises(shaders.ShaderCompileError) as caught:
            shaders.compile(source)
        self.assertIn("no_such_function", caught.exception.stderr)

    VALID_FRAGMENT = ("#version 450\n"
                      "layout(location = 0) out vec4 c;\n"
                      "void main() { c = vec4(1.0); }\n")

    def test_failure_leaves_no_spv_claiming_to_be_current(self):
        # A leftover .spv from an earlier good build would otherwise look newer
        # than the source and be skipped on the next run — reporting success for
        # a shader that no longer compiles.
        source = self.write("shader.frag", self.VALID_FRAGMENT)
        self.assertTrue(shaders.compile(source).exists())

        source.write_text("#version 450\nvoid main(){ nope(); }\n", encoding="utf-8")
        with self.assertRaises(shaders.ShaderCompileError):
            shaders.compile(source, force=True)

        self.assertFalse((self.tmp / "shader.frag.spv").exists())

    def test_compile_dir_returns_only_what_it_built(self):
        (self.tmp / "nested").mkdir()
        self.write("a.vert", self.VALID)
        (self.tmp / "nested" / "b.vert").write_text(self.VALID, encoding="utf-8")

        self.assertEqual(2, len(shaders.compile_dir(self.tmp)))
        self.assertEqual(0, len(shaders.compile_dir(self.tmp)))

    def test_compile_dir_ignores_unrelated_files(self):
        self.write("notes.txt", "not a shader")
        self.write("a.vert", self.VALID)
        self.assertEqual(1, len(shaders.compile_dir(self.tmp)))


class AssetLookup(unittest.TestCase):

    def test_finds_the_repository_asset_root(self):
        self.assertEqual((REPO / "assets").resolve(), assets.root(refresh=True).resolve())

    def test_locate_resolves_against_the_root(self):
        found = assets.locate("models/Box.gltf")
        self.assertTrue(found.exists())

    def test_locate_returns_the_original_when_missing(self):
        self.assertEqual(Path("env/nope.hdr"), assets.locate("env/nope.hdr"))

    def test_exists_reports_absent_assets(self):
        self.assertFalse(assets.exists("models/definitely_not_here.gltf"))


class PipelineValidation(unittest.TestCase):

    MINIMAL = {
        "version": 1,
        "resources": [{"name": "swapchain", "kind": "image", "imported": True}],
        "passes": [{
            "name": "Only", "type": "graphics",
            "outputs": [{"resource": "swapchain", "usage": "color_write",
                         "present": True}],
        }],
    }

    def problems(self, document):
        return pipeline.validate_json(document, source="test.json")

    def test_a_valid_pipeline_has_nothing_to_report(self):
        self.assertEqual([], self.problems(self.MINIMAL))

    def test_unknown_usage_is_reported_with_a_suggestion(self):
        document = dict(self.MINIMAL)
        document["passes"] = [{
            "name": "Only", "type": "graphics",
            "outputs": [{"resource": "swapchain", "usage": "color_blnd"}],
        }]
        found = self.problems(document)
        self.assertTrue(any("color_blnd" in p.message for p in found))
        self.assertTrue(any(p.hint and "color_blend" in p.hint for p in found))

    def test_undeclared_resource_is_reported(self):
        document = dict(self.MINIMAL)
        document["passes"] = [{
            "name": "Only", "type": "graphics",
            "outputs": [{"resource": "typo", "usage": "color_write",
                         "present": True}],
        }]
        self.assertTrue(any("not declared" in p.message for p in self.problems(document)))

    def test_missing_present_is_a_warning_not_an_error(self):
        document = dict(self.MINIMAL)
        document["passes"] = [{
            "name": "Only", "type": "graphics",
            "outputs": [{"resource": "swapchain", "usage": "color_write"}],
        }]
        found = self.problems(document)
        self.assertTrue(found)
        self.assertTrue(all(not p.is_error for p in found))

    def test_malformed_input_reports_rather_than_raises(self):
        # A validator that throws on bad input is useless — bad input is its job.
        for document in ({"passes": "not a list"},
                         {"resources": 5, "passes": []},
                         {"passes": [None]},
                         {"passes": [{"name": "x", "type": "graphics",
                                      "outputs": "nope"}]},
                         {"bufferLayouts": 7, "passes": []}):
            with self.subTest(document=document):
                self.assertIsInstance(self.problems(document), list)

    def test_every_shipped_pipeline_validates(self):
        checked = 0
        for path in sorted((REPO / "examples").rglob("*.json")):
            if "cmake-build" in str(path) or path.name.endswith(".gltf"):
                continue
            document = path.read_text(encoding="utf-8")
            if '"passes"' not in document:
                continue
            errors = [p for p in pipeline.validate(path) if p.is_error]
            self.assertEqual([], errors, "%s: %s" % (path.name, errors))
            checked += 1
        self.assertGreater(checked, 5, "expected to find shipped pipelines")


class VocabularyMatchesTheEngine(unittest.TestCase):
    """The reason these modules are allowed to restate C++ tables at all."""

    def test_resource_usages_match_the_cpp_table(self):
        source = (REPO / "src/Vulkan/FrameGraph/FrameGraphJson.cpp").read_text(
            encoding="utf-8")
        block = source.split("stringToResourceUsage", 1)[1].split("};", 1)[0]
        in_cpp = set(re.findall(r'\{"([a-z_]+)",\s*ResourceUsage::', block))

        self.assertEqual(in_cpp, set(pipeline.RESOURCE_USAGES),
                         "pipeline.RESOURCE_USAGES has drifted from FrameGraphJson.cpp")

    def test_pass_types_match_the_cpp_table(self):
        source = (REPO / "src/Vulkan/FrameGraph/FrameGraphJson.cpp").read_text(
            encoding="utf-8")
        block = source.split("stringToPassType", 1)[1].split("};", 1)[0]
        in_cpp = set(re.findall(r'\{"([a-z_]+)",\s*PassType::', block))

        self.assertEqual(in_cpp, set(pipeline.PASS_TYPES))

    def test_field_types_and_packing_rules_match_the_cpp_header(self):
        source = (REPO / "include/FrameGraph/BufferFieldTypes.h").read_text(
            encoding="utf-8")

        types = source.split("parseFieldType", 1)[1].split("inline", 1)[0]
        in_cpp = set(re.findall(r'== "([a-z0-9]+)"', types))
        self.assertEqual(in_cpp, set(pipeline.FIELD_TYPES),
                         "pipeline.FIELD_TYPES has drifted from BufferFieldTypes.h")

        rules = source.split("parsePackingRule", 1)[1].split("inline", 1)[0]
        in_cpp = set(re.findall(r'== "([a-z0-9_]+)"', rules))
        self.assertEqual(in_cpp, set(pipeline.PACKING_RULES))

    def test_asset_marker_and_env_var_match_the_cpp_resolver(self):
        source = (REPO / "src/Core/AssetPaths.cpp").read_text(encoding="utf-8")

        marker = re.search(r'kMarkerFile\s*=\s*"([^"]+)"', source).group(1)
        env_var = re.search(r'kEnvVar\s*=\s*"([^"]+)"', source).group(1)

        self.assertEqual(marker, assets.MARKER_FILE,
                         "assets.MARKER_FILE has drifted from AssetPaths.cpp")
        self.assertEqual(env_var, assets.ENV_VAR)


if __name__ == "__main__":
    unittest.main()
