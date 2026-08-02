"""Generate a runnable starter project.

    python -m shoonyakasha.init my_game
    cd my_game && python main.py

The templates are a reduction of `examples/cpp/api/instancing_test` — a pipeline that
demonstrably renders, stripped to a single forward pass — rather than something
written from the schema and hoped for.
"""

import argparse
import shutil
import sys
from pathlib import Path

__all__ = ["TEMPLATE_DIR", "create", "main"]

TEMPLATE_DIR = Path(__file__).parent / "templates"

#: Files whose contents get `{project}` substituted. Everything else is copied
#: byte for byte — shaders and JSON must not be touched by string formatting.
_SUBSTITUTED = {"main.py", "README.md"}


def create(destination, project_name=None, force=False):
    """Write a starter project into `destination`. Returns the created Path."""
    destination = Path(destination)
    project_name = project_name or destination.name

    if destination.exists() and any(destination.iterdir()) and not force:
        raise FileExistsError(
            "%s already exists and is not empty (pass force=True to overwrite)"
            % destination)

    if not TEMPLATE_DIR.is_dir():
        raise FileNotFoundError(
            "templates are missing from the installed package: %s" % TEMPLATE_DIR)

    for source in sorted(TEMPLATE_DIR.rglob("*")):
        if source.is_dir():
            continue
        target = destination / source.relative_to(TEMPLATE_DIR)
        target.parent.mkdir(parents=True, exist_ok=True)

        if source.name in _SUBSTITUTED:
            text = source.read_text(encoding="utf-8")
            target.write_text(text.replace("{project}", project_name),
                              encoding="utf-8")
        else:
            shutil.copyfile(source, target)

    return destination


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="python -m shoonyakasha.init", description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("destination", help="directory to create")
    parser.add_argument("--name", help="project name (default: directory name)")
    parser.add_argument("--force", action="store_true",
                        help="write into a non-empty directory")
    args = parser.parse_args(argv)

    try:
        created = create(args.destination, args.name, args.force)
    except (FileExistsError, FileNotFoundError) as exc:
        print("error: %s" % exc, file=sys.stderr)
        return 1

    print("Created %s" % created)
    for path in sorted(created.rglob("*")):
        if path.is_file():
            print("  %s" % path.relative_to(created).as_posix())
    print("\n  cd %s && python main.py" % created)
    return 0


if __name__ == "__main__":
    sys.exit(main())
