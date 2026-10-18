"""Regression checks for links when repository Markdown becomes a website."""

import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from docs_site import on_page_markdown, rewrite_markdown, rewrite_target
from check_docs_site import check_site

REPO = "https://github.com/Maximilian-Winter/Shoonyakasha"


class DocumentationSiteLinks(unittest.TestCase):
    def test_built_native_links_sources_and_project_base_are_checked(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "api/native"
            native.mkdir(parents=True)
            (root / "index.html").write_text(
                '<link rel="canonical" href="https://example.com/engine/">'
                '<a href="api/native/class.html#run">Run</a>', encoding="utf-8",
            )
            symbol = native / "class.html"
            symbol.write_text('<a href="../../">Guides</a><h2 id="run">run()</h2>'
                              '<a href="/engine/api/native/class.html#run">Run</a>'
                              '<a href="https://example.invalid/remote">Remote</a>', encoding="utf-8")
            self.assertEqual([], check_site(root))
            symbol.write_text('<a href="class.html#absent">Missing member</a>'
                              '<img src="missing.png">', encoding="utf-8")
            errors = check_site(root)
            self.assertEqual(3, len(errors))  # Including the entry page's now-missing #run.
            self.assertTrue(any("missing.png" in error for error in errors))

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

    def test_published_native_links_work_in_local_preview(self):
        site_url = "https://maximilian-winter.github.io/Shoonyakasha/"
        self.assertEqual("native/annotated.html#letter_E", rewrite_target(
            site_url + "api/native/annotated.html#letter_E", "docs/api/index.md", "api/index.md",
            {"api/native/annotated.html": "api/native/annotated.html"}, REPO, "master", site_url,
        ))
        # A different site's URL is not an internal link, even with the same path.
        other = "https://example.com/Shoonyakasha/api/native/annotated.html"
        self.assertEqual(other, rewrite_target(
            other, "docs/api/index.md", "api/index.md",
            {"api/native/annotated.html": "api/native/annotated.html"}, REPO, "master", site_url,
        ))

    def test_preview_uses_published_origin_when_mkdocs_changes_site_url(self):
        published = "https://maximilian-winter.github.io/Shoonyakasha/"
        config = SimpleNamespace(
            repo_url=REPO, site_url="http://127.0.0.1:8000/Shoonyakasha/",
            extra={"source_branch": "master", "published_site_url": published},
        )
        page = SimpleNamespace(file=SimpleNamespace(src_uri="api/index.md", dest_uri="api/index.html"))
        files = [SimpleNamespace(src_uri="api/native/annotated.html", dest_uri="api/native/annotated.html")]
        self.assertEqual("[Classes](native/annotated.html)", on_page_markdown(
            "[Classes](" + published + "api/native/annotated.html)", page, config, files,
        ))

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
