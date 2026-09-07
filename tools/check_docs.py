"""Check active documentation's local links, anchors, and snippet syntax.

Uses only the standard library. Does not fetch remote links or execute code.
Historical docs remain indexed but their content is intentionally not checked.
"""

import ast
import json
import re
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
HISTORICAL = {"old", "plans", "reviews"}


def active_documents(root):
    paths = list(root.glob("*.md"))
    for directory in ("docs", "examples", "assets", "python/shoonyakasha/templates"):
        paths.extend((root / directory).rglob("*.md"))
    return sorted(p for p in set(paths)
                  if not (p.relative_to(root).parts[0] == "docs"
                          and len(p.relative_to(root).parts) > 1
                          and p.relative_to(root).parts[1] in HISTORICAL))


def split_fences(text):
    """Return prose with fence lines blanked and (language, code, line) blocks."""
    prose, blocks, body = [], [], []
    opening = None
    language, first_line = "", 0
    for number, line in enumerate(text.splitlines(), 1):
        match = re.match(r"^\s*(`{3,}|~{3,})(.*)$", line)
        if opening is None and match:
            opening = match.group(1)
            language = match.group(2).strip().split(" ", 1)[0]
            first_line = number + 1
            body = []
            prose.append("")
        elif opening and match and match.group(1)[0] == opening[0] and len(match.group(1)) >= len(opening) and not match.group(2).strip():
            blocks.append((language, "\n".join(body), first_line))
            opening = None
            prose.append("")
        elif opening:
            body.append(line)
            prose.append("")
        else:
            prose.append(line)
    if opening:
        raise ValueError("Unclosed code fence at line %d" % (first_line - 1))
    return "\n".join(prose), blocks


def heading_anchors(prose):
    anchors, counts = set(), {}
    for heading in re.findall(r"^ {0,3}#{1,6}\s+(.+?)\s*#*\s*$", prose, re.M):
        heading = re.sub(r"<[^>]*>", "", heading)
        heading = re.sub(r"\[([^]]+)\]\([^)]*\)", r"\1", heading)
        slug = "".join(c for c in heading.lower() if c.isalnum() or c in "_ -").replace(" ", "-")
        count = counts.get(slug, 0)
        candidate = slug + ("-%d" % count if count else "")
        while candidate in anchors:
            count += 1
            candidate = slug + "-%d" % count
        counts[slug] = count + 1
        anchors.add(candidate)
    anchors.update(re.findall(r'(?:id|name)=[\'\"]([^\'\"]+)[\'\"]', prose))
    return anchors


def link_targets(prose):
    # The repository uses inline links and explicit reference links. Remove
    # code spans so command examples do not accidentally become links.
    prose = re.sub(r"`+[^`\n]*`+", "", prose)
    for match in re.finditer(r"\[[^\]\n]*\]\(\s*(<[^>]+>|[^\s)]+)(?:\s+[^)]*)?\)", prose):
        yield match.group(1).strip("<>"), prose.count("\n", 0, match.start()) + 1
    references = {}
    for match in re.finditer(r"^\s*\[([^]]+)\]:\s*(<[^>]+>|\S+)", prose, re.M):
        references[match.group(1).casefold()] = match.group(2).strip("<>")
        yield match.group(2).strip("<>"), prose.count("\n", 0, match.start()) + 1
    for match in re.finditer(r"\[([^]\n]+)\]\[([^]\n]*)\]", prose):
        key = (match.group(2) or match.group(1)).casefold()
        if key not in references:
            yield "missing-reference:" + key, prose.count("\n", 0, match.start()) + 1
    for match in re.finditer(r'(?:href|src)=[\'\"]([^\'\"]+)[\'\"]', prose):
        yield match.group(1), prose.count("\n", 0, match.start()) + 1


def check_document(path, root):
    errors = []
    relative = path.relative_to(root)
    try:
        prose, blocks = split_fences(path.read_text(encoding="utf-8"))
    except ValueError as exc:
        return ["%s: %s" % (relative, exc)]
    for target, line in link_targets(prose):
        if target.startswith("missing-reference:"):
            errors.append("%s:%d: %s" % (relative, line, target))
            continue
        url = urlsplit(target)
        if url.scheme or url.netloc:
            continue
        destination = (root / unquote(url.path).lstrip("/") if url.path.startswith("/")
                       else path.parent / unquote(url.path)) if url.path else path
        if not destination.exists():
            errors.append("%s:%d: missing path %s" % (relative, line, target))
        elif url.fragment and destination.suffix.lower() == ".md":
            other, _ = split_fences(destination.read_text(encoding="utf-8"))
            if unquote(url.fragment) not in heading_anchors(other):
                errors.append("%s:%d: missing anchor %s" % (relative, line, target))
    for language, code, line in blocks:
        try:
            if language in {"python", "py"}:
                ast.parse(code)
            elif language == "json":
                json.loads(code)
        except (SyntaxError, ValueError) as exc:
            errors.append("%s:%d: invalid %s snippet: %s" % (relative, line, language, exc))
    return errors


def main():
    paths = active_documents(ROOT)
    errors = [error for path in paths for error in check_document(path, ROOT)]
    for error in errors:
        print(error)
    print("Checked %d active Markdown files: %d error(s)" % (len(paths), len(errors)))
    return 1 if errors else 0


if __name__ == "__main__":
    raise SystemExit(main())
