"""Tests for the pure-Python utility modules.

Stdlib unittest rather than pytest, so this runs anywhere the package does
without adding a test dependency:

    python -m unittest discover -s tests/python

The load-bearing tests are the two anti-drift ones at the bottom. `pipeline.py`
and `assets.py` restate vocabulary and rules that are defined in C++; those
tests parse the C++ sources and fail when the copies disagree.
"""

import math
import os
import re
import shutil
import sys
import tempfile
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO / "python"))

from shoonyakasha import assets, keys, mathutil, pipeline, shaders  # noqa: E402


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

    def test_a_shader_can_name_its_own_arguments(self):
        # Ray queries need SPIR-V 1.4, which some glslc versions only target
        # when asked; the shader asks, and compile() passes it on.
        body = ("#extension GL_EXT_ray_query : require\n"
                "layout(set = 0, binding = 0) uniform accelerationStructureEXT scene;\n"
                "layout(location = 0) out vec4 color;\n"
                "void main() { rayQueryEXT q; rayQueryInitializeEXT(q, scene, 0u, 0xFFu, vec3(0), 0.0, vec3(1), 1.0);"
                " color = vec4(1.0); }\n")
        source = self.write("traced.frag", "#version 460\n// glslc: --target-env=vulkan1.2\n" + body)
        self.assertEqual(["--target-env=vulkan1.2"], shaders.source_args(source))
        self.assertGreater(shaders.compile(source).stat().st_size, 0)

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

    def test_every_shared_library_header_compiles(self):
        # Each header on its own, in a fragment shader because shapes2d uses
        # fwidth(). Catches a header that only compiled alongside another.
        headers = sorted((shaders.include_dir() / "sk").glob("*.glsl"))
        self.assertTrue(headers, "no headers under %s" % shaders.include_dir())
        for header in headers:
            with self.subTest(header=header.name):
                source = self.write(header.stem + ".frag",
                                    '#version 450\n#include "sk/%s"\n' % header.name
                                    + "layout(location = 0) out vec4 c;\n"
                                    + "void main() { c = vec4(1.0); }\n")
                self.assertTrue(shaders.compile(source, force=True).exists())

    def _shader_with_local_include(self, directory):
        (directory / "common.glsl").write_text("float half_of(float x) { return x * 0.5; }\n",
                                               encoding="utf-8")
        source = directory / "uses.frag"
        source.write_text('#version 450\n#include "common.glsl"\n'
                          "layout(location = 0) out vec4 c;\n"
                          "void main() { c = vec4(half_of(1.0)); }\n", encoding="utf-8")
        return source

    def test_a_newer_include_makes_the_output_stale(self):
        source = self._shader_with_local_include(self.tmp)
        output = shaders.compile(source)
        self.assertFalse(shaders.is_stale(source))

        header = self.tmp / "common.glsl"
        os.utime(header, (output.stat().st_atime + 10, output.stat().st_mtime + 10))
        self.assertTrue(shaders.is_stale(source))
        self.assertEqual([output], shaders.compile_dir(self.tmp))

    def test_a_deleted_include_makes_the_output_stale(self):
        source = self._shader_with_local_include(self.tmp)
        shaders.compile(source)
        (self.tmp / "common.glsl").unlink()
        self.assertTrue(shaders.is_stale(source))

    def test_dependencies_are_found_in_paths_with_spaces(self):
        # glslc writes these paths unescaped; they still have to be recognised.
        spaced = self.tmp / "has a space"
        spaced.mkdir()
        source = self._shader_with_local_include(spaced)
        shaders.compile(source)
        self.assertFalse(shaders.is_stale(source))


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

    def test_repeated_passes_are_checked_per_instance(self):
        document = dict(self.MINIMAL)
        document["resources"] = self.MINIMAL["resources"] + [
            {"name": "shadow0", "kind": "image"}, {"name": "shadow1", "kind": "image"}]
        document["passes"] = [{
            "name": "Shadow", "type": "graphics",
            "repeat": {"count": 3, "index": "c"},
            "outputs": [{"resource": "shadow{c}", "usage": "depth_write"}],
        }] + self.MINIMAL["passes"]
        found = self.problems(document)
        # shadow0 and shadow1 exist; the third instance names shadow2
        undeclared = [p for p in found if "not declared" in p.message]
        self.assertEqual(1, len(undeclared))
        self.assertIn("Shadow[2]", undeclared[0].where)
        self.assertIn("shadow2", undeclared[0].message)

    def test_repeat_substitutes_numbers_like_the_engine(self):
        problems = []
        instance = pipeline._substitute(
            {"layer": "{c}", "mip": "{c-1}", "path": "x{c+1}.spv", "other": "{d}"},
            "c", 2, "here", problems)
        self.assertEqual({"layer": 2, "mip": 1, "path": "x3.spv", "other": "{d}"}, instance)
        self.assertEqual([], problems)

    def test_repeat_step_counts_down_like_the_engine(self):
        import copy
        document = copy.deepcopy(self.MINIMAL)
        document["resources"].append({"name": "chain", "kind": "image"})
        document["passes"].insert(0, {
            "name": "Up{m}", "type": "graphics",
            "repeat": {"count": 3, "index": "m", "first": 2, "step": -1},
            "outputs": [{"resource": "chain", "usage": "color_blend", "mip": "{m}"}]})
        expanded = pipeline._expand_repeats(document["passes"], "<json>", [])
        self.assertEqual(["Up2", "Up1", "Up0"], [p["name"] for p in expanded[:3]])
        self.assertEqual([2, 1, 0], [p["outputs"][0]["mip"] for p in expanded[:3]])

        document["passes"][0]["repeat"]["first"] = 1
        self.assertTrue(any("at least 0" in p.message for p in self.problems(document)))

    def test_pass_requirements_must_be_known_capabilities(self):
        import copy
        document = copy.deepcopy(self.MINIMAL)
        document["passes"][0]["requires"] = ["rayQuery"]
        self.assertEqual([], self.problems(document))
        document["passes"][0]["requires"] = ["rayQuerry"]
        messages = [p.message for p in self.problems(document)]
        self.assertTrue(any("'rayQuerry', which is not a capability" in m for m in messages), messages)

    def test_presets_are_checked_against_the_passes(self):
        import copy
        document = copy.deepcopy(self.MINIMAL)
        document["presets"] = {"low": {"passes": {"Only": False}, "values": {"a.b": 1, "a.c": None}}}
        self.assertEqual([], self.problems(document))
        document["presets"]["low"]["passes"]["Onyl"] = True
        document["presets"]["low"]["values"]["a.d"] = "bright"
        messages = [p.message for p in self.problems(document)]
        self.assertTrue(any("'Onyl', which is not a pass" in m for m in messages), messages)
        self.assertTrue(any("'a.d' must be" in m for m in messages), messages)

    def test_repeat_without_a_count_is_reported(self):
        document = dict(self.MINIMAL)
        document["passes"] = [dict(self.MINIMAL["passes"][0], repeat={"index": "i"})]
        found = self.problems(document)
        self.assertTrue(any("count" in p.message for p in found))

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

    def test_the_default_pipeline_ships_complete(self):
        # Every shader it names is compiled and in the package: a wheel has no
        # compiler to build them, so a missing .spv is an error here too.
        self.assertTrue(pipeline.DEFAULT.is_file())
        self.assertEqual([], pipeline.validate(pipeline.DEFAULT))
        ibl = pipeline.DEFAULT.parent / "shaders" / "ibl"
        for name in ("equirect_to_cubemap", "irradiance_convolution", "prefilter_convolution"):
            self.assertTrue((ibl / (name + ".comp.spv")).is_file(), name)

    def test_the_default_pipeline_spirv_is_current(self):
        # A shader edited without recompiling would ship the old program.
        stale = [str(p) for p in sorted((pipeline.DEFAULT.parent / "shaders").rglob("*"))
                 if p.suffix in shaders.SHADER_EXTENSIONS and shaders.is_stale(p)]
        self.assertEqual([], stale)


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


class KeyCodes(unittest.TestCase):
    """The codes are GLFW's, so they have to keep matching GLFW's."""

    def test_printable_keys_are_their_ascii_value(self):
        for letter in "ABCDEFGHIJKLMNOPQRSTUVWXYZ":
            self.assertEqual(ord(letter), getattr(keys, letter))
        for digit in range(10):
            self.assertEqual(ord(str(digit)), getattr(keys, "NUM_%d" % digit))
        self.assertEqual(ord(" "), keys.SPACE)

    def test_named_keys_match_glfw(self):
        # Spot values straight from glfw3.h. If these drift, every game built
        # on the module reads the wrong key.
        self.assertEqual(256, keys.ESCAPE)
        self.assertEqual(257, keys.ENTER)
        self.assertEqual((262, 263, 264, 265),
                         (keys.RIGHT, keys.LEFT, keys.DOWN, keys.UP))
        self.assertEqual(290, keys.F1)
        self.assertEqual(301, keys.F12)
        self.assertEqual(340, keys.LEFT_SHIFT)

    def test_name_round_trips(self):
        self.assertEqual("ESCAPE", keys.name(keys.ESCAPE))
        self.assertEqual("999", keys.name(999))


class FakeInput:
    def __init__(self):
        self.down = set()

    def is_key_down(self, code):
        return code in self.down


class KeyEdgeDetection(unittest.TestCase):
    def setUp(self):
        self.input = FakeInput()
        self.edges = keys.KeyEdges(self.input)

    def test_a_held_key_is_reported_once(self):
        self.input.down.add(keys.SPACE)
        self.assertEqual([True, False, False],
                         [self.edges.pressed(keys.SPACE) for _ in range(3)])

    def test_release_and_press_again_is_reported_again(self):
        self.input.down.add(keys.P)
        self.assertTrue(self.edges.pressed(keys.P))
        self.input.down.discard(keys.P)
        self.assertFalse(self.edges.pressed(keys.P))
        self.input.down.add(keys.P)
        self.assertTrue(self.edges.pressed(keys.P))

    def test_keys_are_tracked_separately(self):
        self.input.down.update((keys.LEFT, keys.RIGHT))
        self.assertTrue(self.edges.pressed(keys.LEFT))
        self.assertTrue(self.edges.pressed(keys.RIGHT))
        self.assertFalse(self.edges.pressed(keys.LEFT))


def engine_forward(rotation):
    """TransformComponent::getForward() in include/ECS/Core.h."""
    pitch, yaw = rotation[0], rotation[1]
    return (-math.sin(yaw) * math.cos(pitch), math.sin(pitch),
            -math.cos(yaw) * math.cos(pitch))


class CameraMath(unittest.TestCase):
    def assertVecAlmostEqual(self, a, b):
        for x, y in zip(a, b):
            self.assertAlmostEqual(x, y, places=9)

    def test_rotation_facing_points_the_engine_forward_along_the_direction(self):
        for direction in [(0, 0, -1), (1, 0, 0), (-1, 0, 0), (0, 0, 1),
                          (0.3, 0.8, -0.2), (-2.0, -1.0, 4.0), (0.0, 0.999, 0.01)]:
            with self.subTest(direction=direction):
                rotation = mathutil.rotation_facing(direction)
                self.assertEqual(0.0, rotation[2])
                self.assertVecAlmostEqual(mathutil.normalize(direction),
                                          engine_forward(rotation))

    def test_rotation_facing_minus_z_is_identity(self):
        self.assertVecAlmostEqual((0.0, 0.0, 0.0), mathutil.rotation_facing((0, 0, -5)))

    def test_rotation_facing_survives_degenerate_directions(self):
        for direction in [(0.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, -3.0, 0.0)]:
            with self.subTest(direction=direction):
                rotation = mathutil.rotation_facing(direction)
                self.assertTrue(all(math.isfinite(c) for c in rotation))
        self.assertAlmostEqual(math.pi / 2, mathutil.rotation_facing((0, 2, 0))[0])

    def test_look_rotation_faces_the_target(self):
        eye, target = (1.0, 2.0, 3.0), (-4.0, 0.5, 7.0)
        direction = tuple(t - e for t, e in zip(target, eye))
        self.assertVecAlmostEqual(mathutil.normalize(direction),
                                  engine_forward(mathutil.look_rotation(eye, target)))

    def test_orbit_places_the_eye_at_distance_and_angles(self):
        target = (1.0, 2.0, 3.0)
        self.assertVecAlmostEqual((1.0, 2.0, 8.0), mathutil.orbit(target, 5.0, 0.0, 0.0))
        self.assertVecAlmostEqual((6.0, 2.0, 3.0), mathutil.orbit(target, 5.0, 90.0, 0.0))
        self.assertVecAlmostEqual((1.0, 7.0, 3.0), mathutil.orbit(target, 5.0, 0.0, 90.0))

    def test_yaw_point_turns_plus_z_towards_plus_x(self):
        self.assertVecAlmostEqual((1.0, 0.5, 0.0), mathutil.yaw_point((0.0, 0.5, 1.0), 90.0))
        # Same direction as the engine's yaw: forward -Z turns towards -X.
        self.assertVecAlmostEqual(engine_forward((0.0, math.radians(30.0), 0.0)),
                                  mathutil.yaw_point((0.0, 0.0, -1.0), 30.0))

    def test_smoothstep_clamps_and_eases(self):
        self.assertEqual(0.0, mathutil.smoothstep(-1.0))
        self.assertEqual(1.0, mathutil.smoothstep(2.0))
        self.assertEqual(0.5, mathutil.smoothstep(0.5))
        self.assertAlmostEqual(0.15625, mathutil.smoothstep(0.25))

    def test_lerp_and_vector_helpers(self):
        self.assertEqual(2.5, mathutil.lerp(2.0, 4.0, 0.25))
        self.assertEqual((1.0, 3.0), mathutil.lerp3((0.0, 2.0), (2.0, 4.0), 0.5))
        self.assertEqual((4, 6), mathutil.add((1, 2), (3, 4)))
        self.assertEqual((2, 4), mathutil.scale((1, 2), 2))
        self.assertEqual((0.0, 0.0), mathutil.normalize((0.0, 0.0)))

    def test_conventions_match_the_cpp_transform(self):
        source = (REPO / "include/ECS/Core.h").read_text(encoding="utf-8")
        self.assertIn("return glm::vec3(-sy * cx, sx, -cy * cx);", source,
                      "TransformComponent::getForward changed; update engine_forward and mathutil")
        self.assertIn("asinf(glm::clamp(d.y, -1.0f, 1.0f)), atan2f(-d.x, -d.z)", source,
                      "TransformComponent::rotationFacing changed; update mathutil.rotation_facing")


if __name__ == "__main__":
    unittest.main()
