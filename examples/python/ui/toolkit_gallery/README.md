# Toolkit gallery

Run with the installed `shoonyakasha` package, like the other Python examples.
From this directory:

```sh
python toolkit_gallery.py
```

Or from the repository root:

```sh
python examples/python/ui/toolkit_gallery/toolkit_gallery.py
python examples/python/ui/toolkit_gallery/toolkit_gallery.py --smoke --screenshot gallery.png
```

The managed screen demonstrates parameters, tabs, modal focus, theme replacement,
a clipped list, drawings and bounded plots. A second context on a world canvas
explicitly forwards updates and has a button clickable in 3D. Escape releases or
captures the pointer, following engine defaults.

`--smoke` exercises parameters, tabs and modal transitions, then closes after 65
frames. `--width` and `--height` change window size; reference layout is 1920×1080.
See the [toolkit guide](../../../../docs/guides/python-toolkit.md).
