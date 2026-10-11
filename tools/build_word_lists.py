#!/usr/bin/env python3
"""Build the word lists the canvas UI breaks Thai, Lao, Khmer and Myanmar with.

These scripts are written without spaces between words, so lines can only
break where a dictionary says a word ends. The lists come from ICU's
dictionary sources (icu4c/source/data/brkitr/dictionaries in
github.com/unicode-org/icu); their licences are in ICU's LICENSE file, which
ships beside them as assets/text/LICENSE-ICU.txt.

Each list is written as UTF-8, one word per line, without ICU's comments,
byte-order mark or frequency columns, and compressed with zlib. The engine
reads it with the zlib decoder of the vendored stb_image
(UI/WordDictionary.cpp).

    python tools/build_word_lists.py <directory with thaidict.txt, laodict.txt,
                                      khmerdict.txt, burmesedict.txt> [ICU LICENSE]

Writes assets/text/{thai,lao,khmer,myanmar}.words.zlib, and copies the
licence file when it is given.
"""

import pathlib
import shutil
import sys
import zlib

LISTS = {
    "thaidict.txt": "thai",
    "laodict.txt": "lao",
    "khmerdict.txt": "khmer",
    "burmesedict.txt": "myanmar",
}

OUT = pathlib.Path(__file__).resolve().parent.parent / "assets" / "text"


def words_of(path):
    """The words of an ICU dictionary source, in its order, without repeats."""
    words = []
    seen = set()
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        word = line.split("\t")[0].strip()   # some lists carry a frequency column
        if word and word not in seen:
            seen.add(word)
            words.append(word)
    return words


def main(argv):
    if len(argv) < 2:
        print(__doc__)
        return 2
    source = pathlib.Path(argv[1])
    OUT.mkdir(parents=True, exist_ok=True)
    for name, script in LISTS.items():
        words = words_of(source / name)
        data = zlib.compress("\n".join(words).encode("utf-8"), 9)
        target = OUT / ("%s.words.zlib" % script)
        target.write_bytes(data)
        print("%-8s %6d words, %8d bytes" % (script, len(words), len(data)))
    if len(argv) > 2:
        shutil.copyfile(argv[2], OUT / "LICENSE-ICU.txt")
        print("licence  copied")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
