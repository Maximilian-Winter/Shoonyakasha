# Museum

The [showroom](../showroom/README.md)'s cars as a small digital museum. They stand on the turntable one at a time, oldest first, each beside a placard:

- the year, large, and the maker and name, in Playfair Display;
- what kind of car it is and its body;
- its figures: engine, power, top speed, weight, how many were built and who designed it;
- a few lines about it;
- a timeline of the collection along the bottom, with this car marked.

```
python tools/fetch_assets.py showroom          # once, from the repository root; see the showroom's README
python museum.py
```

It renders with the showroom's pipeline, studio and models, so everything in the showroom's README applies: the keys (**←/→** walk through the collection), stills and recordings for posting, depth of field, supersampling, `--vertical`. Captures go to `museum_captures/`.

```
python museum.py --screenshots shots/                       # every car from every still pose, beside its placard
python museum.py --record museum.mp4 --dof 2.8 --grain 0.02 # the collection in order, with the placard
python museum.py --record reel.mp4 --vertical               # phone-shaped: the car above, the placard below
```

**Layout:** the placard takes the left third of the frame, and the camera shifts so the car stands in the rest. In `--vertical` the placard takes the lower half and the car stands above it. **H** hides the placard and centres the car again; the model's credit stays on screen, as its licence asks.

## The placards

[exhibits.py](exhibits.py) holds them, keyed by the showroom catalogue's names. Each has a maker, name, year (which also sets the order), kind, body, up to six figures and a paragraph, which the placard wraps. A car in the catalogue without an entry shows its catalogue title and year only.

The figures are the commonly published ones, rounded: manufacturers' claims for power and top speed, and production counts as usually given. Check them against a source you trust before showing them to an audience.

## Fonts

Playfair Display (`assets/fonts/PlayfairDisplay.ttf`, SIL Open Font License) sets the year, name and timeline; Roboto the rest. Every label has a soft shadow, so it reads over the bright softboxes as well as the dark cove.

## How it is put together

[museum.py](museum.py) is a copy of `showroom.py` with three changes:

- **The collection:** the catalogue's cars, sorted by their exhibit's year.
- **The placard** in place of the showroom's caption bar: `Overlay`, with the `Text` helper for shadowed labels.
- **Framing:** `Showroom.beside_placard` moves every shot's camera sideways (or down) by a fraction of the view, and back a little, so the car sits beside the placard.
