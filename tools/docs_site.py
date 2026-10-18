"""Publish repository Markdown without duplicating its canonical sources.

MkDocs hooks include the root build guide and catalogs as virtual pages and
translate repository-relative links for the hosted site. The checked-in
Markdown keeps working when read directly on GitHub.
"""

import posixpath
import re
from pathlib import Path
from urllib.parse import quote, unquote, urlsplit, urlunsplit

ROOT = Path(__file__).resolve().parents[1]
REPO_PAGES = {
    "BUILDING.md": "getting-started/building.md",
    "examples/README.md": "examples/index.md",
    "assets/README.md": "reference/assets.md",
    "logo.png": "images/logo.png",
}
HISTORY = {"old", "plans", "reviews"}


def rewrite_target(target, source, site_uri, site_files, repo_url, branch):
    """Resolve a link against its repository source, then its published page."""
    url = urlsplit(target)
    if url.scheme or url.netloc or not url.path:
        return target
    path = posixpath.normpath(
        unquote(url.path).lstrip("/") if url.path.startswith("/")
        else posixpath.join(posixpath.dirname(source), unquote(url.path))
    )
    destination = REPO_PAGES.get(path)
    if destination is None and path.startswith("docs/"):
        destination = path[len("docs/"):]
    if destination in site_files:
        relative = posixpath.relpath(site_files[destination], posixpath.dirname(site_uri) or ".")
        return urlunsplit(("", "", quote(relative, safe="/"), url.query, url.fragment))
    # Only redirect real repository paths. Leave typos for MkDocs to reject.
    resolved = (ROOT / path).resolve()
    if not resolved.is_relative_to(ROOT) or not resolved.exists():
        return target
    kind = "tree" if resolved.is_dir() else "blob"
    return urlunsplit((
        "https", urlsplit(repo_url).netloc,
        urlsplit(repo_url).path.rstrip("/") + "/" + kind + "/"
        + quote(branch, safe="") + "/" + quote(path, safe="/"),
        url.query, url.fragment,
    ))


# Code spans are matched first so link-shaped code remains literal. The other
# branches cover inline Markdown, reference definitions, and HTML images/links.
LINKS = re.compile(
    r"(?P<code>`+[^`\n]*`+)"
    r"|(?P<prefix>\]\(\s*|^\s*\[[^]\n]+\]:\s*|(?:href|src)=[\"'])"
    r"(?P<target><[^>\n]+>|[^\s)\"'<>]+)",
    re.MULTILINE,
)


def rewrite_markdown(markdown, rewrite):
    """Rewrite prose links, preserving fenced blocks and inline code."""
    def replace(match):
        if match.group("code") is not None:
            return match.group(0)
        target = match.group("target")
        # Raw HTML bypasses MkDocs' Markdown link processing, so its links
        # must already be relative to the generated HTML file.
        is_html = match.group("prefix").startswith(("href=", "src="))
        rewritten = rewrite(target.strip("<>"), is_html)
        if target.startswith("<"):
            rewritten = "<" + rewritten + ">"
        return match.group("prefix") + rewritten

    output, prose = [], []
    fence = None
    for line in markdown.splitlines(keepends=True):
        marker = re.match(r"^ {0,3}(`{3,}|~{3,})(.*)$", line.rstrip("\r\n"))
        if fence is None and marker:
            output.append(LINKS.sub(replace, "".join(prose)))
            prose = []
            fence = marker.group(1)
            output.append(line)
        elif fence is not None:
            output.append(line)
            if (marker and marker.group(1)[0] == fence[0]
                    and len(marker.group(1)) >= len(fence)
                    and not marker.group(2).strip()):
                fence = None
        else:
            prose.append(line)
    output.append(LINKS.sub(replace, "".join(prose)))
    return "".join(output)


def on_files(files, config):
    from mkdocs.structure.files import File

    for file in files:
        file.edit_uri = "docs/" + file.src_uri
    for source, destination in REPO_PAGES.items():
        file = File.generated(config, destination, content=(ROOT / source).read_bytes())
        file.edit_uri = source
        files.append(file)
    return files


def on_page_markdown(markdown, page, config, files):
    originals = {destination: source for source, destination in REPO_PAGES.items()}
    source = originals.get(page.file.src_uri, "docs/" + page.file.src_uri)
    # Excluded history stays on GitHub even if MkDocs includes it internally.
    active = [
        file for file in files
        if file.src_uri.split("/", 1)[0] not in HISTORY
        and file.src_uri != "images/examples/README.md"
    ]
    markdown_paths = {file.src_uri: file.src_uri for file in active}
    html_paths = {file.src_uri: file.dest_uri for file in active}
    return rewrite_markdown(markdown, lambda target, is_html: rewrite_target(
        target, source, page.file.dest_uri if is_html else page.file.src_uri,
        html_paths if is_html else markdown_paths,
        config.repo_url, config.extra["source_branch"],
    ))
