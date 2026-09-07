"""Regression tests for documentation checking and API inventory extraction."""

import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from check_docs import check_document, heading_anchors, split_fences
from generate_api_docs import cpp_inventory, python_inventory


class DocumentationChecks(unittest.TestCase):
    def test_links_anchors_and_syntax(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "other page.md").write_text("# C++ setup\n\n## Repeat\n## Repeat\n", encoding="utf-8")
            doc = root / "README.md"
            doc.write_text('[ok](<other page.md#c-setup>)\n[repeat](other%20page.md#repeat-1)\n'
                           '[remote](https://example.invalid/no-network)\n'
                           '```python\nx = 1\n```\n```json\n{"ok": true}\n```\n', encoding="utf-8")
            self.assertEqual([], check_document(doc, root))
            doc.write_text('[bad](missing.md)\n[bad](other%20page.md#absent)\n'
                           '```python\nif\n```\n```json\n{bad}\n```\n', encoding="utf-8")
            self.assertEqual(4, len(check_document(doc, root)))

    def test_code_is_not_prose(self):
        prose, blocks = split_fences('# Real\n```text\n# Fake\n[bad](missing.md)\n```\n')
        self.assertEqual({'real'}, heading_anchors(prose))
        self.assertEqual(1, len(blocks))
        with self.assertRaises(ValueError):
            split_fences('```python\nx=1\n')

    def test_reference_and_html_links(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            doc = root / "README.md"
            doc.write_text('[label][missing]\n<img src="absent.png">\n', encoding="utf-8")
            self.assertEqual(2, len(check_document(doc, root)))

    def test_python_signatures_and_properties(self):
        source = '''cdef class Engine:
    def __init__(self, str title="a", int width=10):
        pass
    @property
    def delta(self):
        """Time in seconds."""
        return 0
    @delta.setter
    def delta(self, float value):
        pass
    def create(self, pos, float speed=8.0):
        """Create an object."""
        pass
'''
        inventory = python_inventory(source, "Engine")
        self.assertIn('Engine(title="a", width=10)', inventory)
        self.assertIn('delta (read/write property)', inventory)
        self.assertNotIn('delta(value)', inventory)
        self.assertIn('create(pos, speed=8.0)', inventory)

    def test_cpp_multiline_and_internal_exclusions(self):
        source = '''class SceneAPI {
public:
    void setPosition(EntityHandle entity,
                     const glm::vec3& pos);
#ifdef SHOONYAKASHA_TESTING
    SceneAPI(entt::registry& registry);
#endif
    SceneAPI(const SceneAPI&) = delete;
    void wireSprite2DManager(Sprite2DManager* manager);
private:
    void secret();
};'''
        inventory = cpp_inventory(source, "Scene")
        self.assertIn('void setPosition(EntityHandle entity, const glm::vec3& pos);', inventory)
        for excluded in ('registry', '= delete', 'wireSprite', 'secret'):
            self.assertNotIn(excluded, inventory)

    def test_native_return_conversion_is_documented(self):
        native = '''class SceneAPI {
public:
    glm::mat4 getWorldMatrix(EntityHandle entity) const;
private:
};'''
        wrapper = '''cdef class Scene:
    def get_world_matrix(self, uint32_t entity):
        return _mat4_to_tuple(self._ptr.getWorldMatrix(entity))
'''
        inventory = python_inventory(wrapper, "Scene", native)
        self.assertIn('4×4 tuple (column-major)', inventory)
        with self.assertRaises(ValueError):
            python_inventory(wrapper, "Scene", native.replace('glm::mat4', 'NewResultType'))


if __name__ == '__main__':
    unittest.main()
