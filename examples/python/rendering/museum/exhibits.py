"""
The museum's placards: what each car is, its figures and a few lines about it.

Keyed by the showroom catalogue's names (../showroom/catalogue.py). Figures
are the commonly published ones, rounded: manufacturer's claims for power and
top speed, and production counts as usually given. Check them against a source
you trust before putting them in front of an audience.
"""


class Exhibit:
    def __init__(self, maker, name, year, kind, body, stats, text):
        self.maker = maker          # "Alfa Romeo"
        self.name = name            # "Montreal"
        self.year = year            # the year shown large; also the museum's order
        self.kind = kind            # "Grand tourer"
        self.body = body            # "2+2 coupe, front engine"
        self.stats = stats          # (label, value) rows
        self.text = text            # a short paragraph; the placard wraps it


EXHIBITS = {
    "alfa_33_stradale_1968": Exhibit(
        "Alfa Romeo", "33 Stradale", 1968,
        "Mid-engine sports car", "Two-seat coupe, butterfly doors",
        [("Engine", "2.0 L V8, behind the driver"),
         ("Power", "230 PS at 8,800 rpm"),
         ("Top speed", "260 km/h"),
         ("Weight", "700 kg"),
         ("Built", "1967 - 1969, 18 made"),
         ("Design", "Franco Scaglione")],
        "The Tipo 33 racing car made fit for the road. Scaglione shaped the "
        "body over a racing chassis, with the V8 from the track behind the "
        "seats. Few cars of its day were faster, and only a handful were "
        "built, by hand. It is often called one of the most beautiful cars "
        "ever made."),

    "alfa_montreal": Exhibit(
        "Alfa Romeo", "Montreal", 1970,
        "Grand tourer", "2+2 coupe, front engine",
        [("Engine", "2.6 L V8"),
         ("Power", "200 PS"),
         ("Top speed", "220 km/h"),
         ("Weight", "1,270 kg"),
         ("Built", "1970 - 1977, 3,925 made"),
         ("Design", "Marcello Gandini, Bertone")],
        "First shown as a concept at Expo 67 in Montreal, which gave it its "
        "name. The production car took a detuned V8 from the 33 racers. "
        "Gandini gave it slatted covers over the headlights and the slats "
        "behind the doors."),

    "alfa_gtv6": Exhibit(
        "Alfa Romeo", "GTV-6", 1986,
        "Sports coupe", "2+2 hatchback, transaxle",
        [("Engine", "2.5 L V6"),
         ("Power", "160 PS"),
         ("Top speed", "205 km/h"),
         ("Weight", "1,210 kg"),
         ("Built", "1980 - 1987, about 22,000 made"),
         ("Design", "Giorgetto Giugiaro")],
        "Giugiaro's Alfetta GT body with Giuseppe Busso's V6, whose fuel "
        "injection needed the bulge in the bonnet. The gearbox sits at the "
        "rear axle, which balances the weight between the ends. It won in "
        "European touring car racing through the early eighties."),

    "alfa_33_stradale_2024": Exhibit(
        "Alfa Romeo", "33 Stradale", 2024,
        "Mid-engine supercar", "Two-seat coupe, butterfly doors",
        [("Engine", "3.0 L twin-turbo V6"),
         ("Power", "620 PS"),
         ("Top speed", "333 km/h"),
         ("0 - 100 km/h", "under 3 seconds"),
         ("Built", "33 made"),
         ("Design", "Alfa Romeo Centro Stile")],
        "A tribute to the 1967 car, built in a series of 33. Under the "
        "carbon-fibre body is the chassis of the Maserati MC20. Buyers chose "
        "the twin-turbo V6 shown here or an electric drive of 750 PS."),
}


def wrap(text, width):
    """Lines of at most about `width` characters, broken at spaces."""
    lines, line = [], ""
    for word in text.split():
        if line and len(line) + 1 + len(word) > width:
            lines.append(line)
            line = word
        else:
            line = word if not line else line + " " + word
    if line:
        lines.append(line)
    return lines
