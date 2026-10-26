# Planet explorer

The planet and its moon of [`shoonyakasha.planet`](../../../../docs/api/python/planet.md), looked at like a digital globe or a planetarium of the system: procedural terrain from a few metres above the ground to the whole system, an atmosphere, clouds, an ocean, sun shadows and ground textures, with the terrain's settings in a menu. Needs numpy and a GPU with 64-bit float shaders (`shaderFloat64`).

```
python -m shoonyakasha.planet.ground_textures   # download the ground's textures (once)
python explorer.py
python explorer.py --capture DIR [--shots NAME...]   # screenshots of the views, then exit
```

It starts in the globe view, 400 km from a point near the middle of the +Z face, tilted 30 degrees, north up, with the clock running at 1x. **Tab** switches between the two views; a bar along the bottom shows the controls of the one on screen, and **F2** shows them all.

| Globe view (`GlobeCamera`) | |
|---|---|
| Left mouse, drag | Move the globe: the ground grabbed stays under the pointer; let go while moving and it coasts |
| Right mouse, drag | Tilt (up, down) and turn (sideways) about the point looked at |
| Mouse wheel | Zoom by 20 % a step towards the point under the pointer |
| Double-click | Fly to the point, 70 % closer |
| **W/A/S/D** | Move over the ground |
| **Q / E** | Turn |
| **N** | North up |
| **U** | Look straight down |

| System view (`FocusCamera`) | |
|---|---|
| Left mouse, drag | Circle the body; let go while moving and it coasts |
| Mouse wheel | Zoom by 15 % a step |
| Click | Focus the body clicked: the view moves to it over 1.5 seconds, 3 of its radii from its centre |
| **Q / E**, **R / F** | Circle the body, up / down |

| Anywhere | |
|---|---|
| **Tab** | Globe view / system view: the system view focused on the globe view's body, from where the camera is; the globe view over the point below the camera, looking straight down |
| **1 / 2** | The planet / the moon: in the globe view over the point of it facing the camera, two of its radii away, looking straight down; in the system view, focused |
| **Home** | Back to the globe view where it started |
| **Space** | Pause / run the clock |
| **, / .** | Time warp down / up: 1x, 10x, 100x, 1,000x, 10,000x |
| **F1** | Settings menu |
| **F2** | The controls |
| **I** | Debug card |
| **P** | Screenshot into `explorer_captures/` |

The mouse goes to the menu and the controls sheet while the pointer is over them.

- **The world** is `PlanetWorld` (`pad=False`: no flat pad at the site). Each frame the clock advances by the frame's time times the warp unless paused, which moves the moon on its orbit and the clouds with the wind; then the camera is updated and the world is drawn for it.
- **The card** at the top left: in the globe view the body, the point's latitude and longitude (degrees from the equator and about the +Y axis from +Z), the ground's height there, the range, the camera's height above sea level (on the moon, above its radius), the heading, the tilt and the time warp; in the system view the focused body, the camera's distance from its centre and height above it, the time warp and the clock in hours. The debug card (**I**) has the frame time, the chunks drawn of those selected, the pool, the missing chunks and the deepest level.
- **The moon's orbit** is drawn as a line in the system view (the menu's **Moon's orbit**).
- **The settings menu**: the time warp and the pause; the sky, the clouds, the ground, the view and the planet's terrain from `shoonyakasha.planet.settings`; the hint bar, the moon's orbit and the debug card.
- **Captures** (`--capture`): the start; mountains 60 km from the site from 6 km, tilted 70 degrees; the ground at the site from 1.5 km, tilted 80 degrees; the whole planet from 2,000 km; the moon's sunlit side from 4 km; the system view of the planet from 3 radii and from 9 radii (with the moon's orbit); the moon from 3 radii; the menu; and the controls. The clock is paused for them.

`tests/python/test_planet_cameras.py` checks the cameras with a scripted mouse on a world that is not set up: the pointer's ray, a basis that is orthonormal and looks at the target at any tilt, the grabbed ground staying under the pointer through a drag, coasting after letting go and slowing down, zooming by the step with the point under the pointer staying within half a degree, the right button's turn and tilt and the tilt's limit, a double-click flying to the point, clicking a body to focus it (and a drag not counting as a click), the focus ending 3 radii away, and the zoom's limits.
