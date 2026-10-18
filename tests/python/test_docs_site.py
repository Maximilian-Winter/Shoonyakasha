"""Regression checks for links when repository Markdown becomes a website."""

import sys
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from docs_site import rewrite_markdown, rewrite_target

REPO = "https://github.com/Maximilian-Winter/Shoonyakasha"


class DocumentationSiteLinks(unittest.TestCase):
    def rewrite(self, target, source="docs/getting-started/cpp-quickstart.md",
                page="getting-started/cpp-quickstart.md", paths=None):
        return rewrite_target(target, source, page, paths or {
            "getting-started/building.md": "getting-started/building.md",
            "guides/default-pipeline.md": "guides/default-pipeline.md",
        }, REPO, "master")

    def test_shared_build_guide_becomes_a_site_page(self):
        self.assertEqual("building.md#troubleshooting",
                         self.rewrite("../../BUILDING.md#troubleshooting"))

    def test_virtual_catalog_resolves_against_original_source(self):
        self.assertEqual("../guides/default-pipeline.md",
                         self.rewrite("../docs/guides/default-pipeline.md",
                                      source="examples/README.md", page="examples/index.md"))

    def test_source_and_history_stay_on_github(self):
        self.assertEqual(REPO + "/blob/master/include/Facade/EngineAPI.h",
                         self.rewrite("../../include/Facade/EngineAPI.h"))
        self.assertEqual(REPO + "/tree/master/examples/cpp/api/facade_test",
                         self.rewrite("../../examples/cpp/api/facade_test"))
        self.assertEqual(REPO + "/blob/master/docs/old/CameraSystemIntegration.md",
                         self.rewrite("../old/CameraSystemIntegration.md"))

    def test_fragment_external_and_missing_links_remain_visible(self):
        for link in ("#lifecycle", "https://example.com/guide", "missing.md"):
            with self.subTest(link=link):
                self.assertEqual(link, self.rewrite(link))

    def test_html_uses_output_paths_below_nested_pages(self):
        self.assertEqual("../../images/examples/cpp/facade_test.png", self.rewrite(
            "../images/examples/cpp/facade_test.png",
            source="docs/examples/cpp-examples.md", page="examples/cpp-examples/index.html",
            paths={"images/examples/cpp/facade_test.png": "images/examples/cpp/facade_test.png"},
        ))

    def test_links_rewrite_without_changing_code(self):
        text = ('[guide](guide.md)\n[ref]: guide.md\n'
                '<img src="image.png">\n<a href="guide.md">Guide</a>\n'
                '`[literal](guide.md)`\n```md\n[example](guide.md)\n```\n'
                '~~~~text\n~~~\n[still code](guide.md)\n~~~~\n')
        result = rewrite_markdown(text, lambda target, html: ("html/" if html else "md/") + target)
        self.assertIn('[guide](md/guide.md)', result)
        self.assertIn('[ref]: md/guide.md', result)
        self.assertIn('<img src="html/image.png">', result)
        self.assertIn('<a href="html/guide.md">', result)
        self.assertIn('`[literal](guide.md)`', result)
        self.assertIn('```md\n[example](guide.md)\n```', result)
        self.assertIn('~~~\n[still code](guide.md)\n~~~~', result)


if __name__ == "__main__":
    unittest.main()
