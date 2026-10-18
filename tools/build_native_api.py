"""Build the native C++ symbol reference without compiling the engine."""

import shutil
import re
import subprocess
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "build/docs-api"


def build_native_api():
    doxygen = shutil.which("doxygen")
    if not doxygen:
        raise RuntimeError(
            "The C++ reference requires Doxygen >= 1.9.8 on PATH. "
            "See docs/maintenance.md for installation instructions."
        )
    version = subprocess.run([doxygen, "--version"], check=True, capture_output=True, text=True).stdout
    numbers = re.match(r"(\d+)\.(\d+)\.(\d+)", version)
    if not numbers or tuple(map(int, numbers.groups())) < (1, 9, 8):
        raise RuntimeError("The C++ reference requires Doxygen >= 1.9.8; found " + version.strip())
    OUTPUT.mkdir(parents=True, exist_ok=True)
    # Doxygen can retain files for deleted symbols between builds. Remove only
    # our verified generated output so stale classes cannot be published.
    for name in ("html", "xml"):
        destination = (OUTPUT / name).resolve()
        if not destination.is_relative_to((ROOT / "build").resolve()):
            raise RuntimeError("Native API output must stay inside the build directory")
        if destination.exists():
            shutil.rmtree(destination)

    # Start from this Doxygen version's default layout, retaining every class
    # and member section, and add a route back to the MkDocs documentation.
    layout = OUTPUT / "layout.xml"
    subprocess.run([doxygen, "-l", str(layout)], cwd=ROOT, check=True, capture_output=True)
    tree = ET.parse(layout)
    tree.getroot().find("navindex").insert(0, ET.Element("tab", {
        "type": "user", "visible": "yes", "title": "Documentation", "url": "../../",
    }))
    tree.write(layout, encoding="utf-8", xml_declaration=True)
    result = subprocess.run([doxygen, "Doxyfile"], cwd=ROOT)
    warnings = OUTPUT / "warnings.log"
    if warnings.exists() and warnings.stat().st_size:
        print(warnings.read_text(encoding="utf-8"), end="")
    if result.returncode:
        raise RuntimeError("Doxygen failed; see build/docs-api/warnings.log")
    if not (OUTPUT / "html/index.html").is_file():
        raise RuntimeError("Doxygen did not generate its HTML entry point")
    return OUTPUT / "html"


if __name__ == "__main__":
    print("Generated C++ API:", build_native_api())
