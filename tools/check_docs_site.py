"""Check generated HTML links and images, including raw HTML in Markdown.

Run after `python -m mkdocs build --strict`. Uses only the standard library;
external URLs are not fetched.
"""

import argparse
from functools import cache
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit


class PageLinks(HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []
        self.anchors = set()
        self.canonical = None

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if tag == "link" and attributes.get("rel") == "canonical":
            self.canonical = attributes.get("href")
        for key, value in attrs:
            if value and key in {"href", "src"}:
                self.links.append(value)
            if value and (key == "id" or (tag == "a" and key == "name")):
                self.anchors.add(value)


def check_site(root):
    root = Path(root).resolve()
    pages = {}
    for path in root.rglob("*.html"):
        parser = PageLinks()
        parser.feed(path.read_text(encoding="utf-8"))
        pages[path] = parser
    if root / "index.html" not in pages:
        return ["No built index.html found. Run python -m mkdocs build --strict first."]
    base_path = urlsplit(pages[root / "index.html"].canonical or "/").path

    # Generated API pages share thousands of links. Resolve and stat each
    # distinct destination once, rather than once per referring member.
    @cache
    def resolve_path(path):
        return path.resolve()

    @cache
    def html_destination(path):
        target = path / "index.html" if path.is_dir() else path
        return target, target.exists()

    errors = []
    for path, parser in pages.items():
        for link in parser.links:
            url = urlsplit(link)
            if url.scheme or url.netloc:
                continue
            # The generated 404 page uses the canonical site's base path
            # because it can be served from any missing URL on GitHub Pages.
            if url.path.startswith("/"):
                if not url.path.startswith(base_path):
                    errors.append(f"{path.relative_to(root)}: URL outside site base {link}")
                    continue
                target = resolve_path(root / unquote(url.path[len(base_path):]))
            else:
                target = resolve_path(path.parent / unquote(url.path)) if url.path else path
            if not target.is_relative_to(root):
                errors.append(f"{path.relative_to(root)}: non-portable local URL {link}")
                continue
            target, exists = html_destination(target)
            if not exists:
                errors.append(f"{path.relative_to(root)}: missing target {link}")
            elif url.fragment and target in pages and unquote(url.fragment) not in pages[target].anchors:
                errors.append(f"{path.relative_to(root)}: missing anchor {link}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", default="site")
    args = parser.parse_args()
    errors = check_site(args.directory)
    for error in errors:
        print(error)
    print(f"Generated documentation: {len(errors)} error(s)")
    return bool(errors)


if __name__ == "__main__":
    raise SystemExit(main())
