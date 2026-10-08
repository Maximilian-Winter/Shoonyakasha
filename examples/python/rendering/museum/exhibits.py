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

    "gaz13_chaika": Exhibit(
        "GAZ", "Chaika", 1959,
        "Limousine", "Seven-seat saloon, front engine",
        [("Engine", "5.5 L V8"),
         ("Power", "195 PS"),
         ("Top speed", "160 km/h"),
         ("Weight", "2,100 kg"),
         ("Built", "1959 - 1981, about 3,200 made"),
         ("Design", "Lev Yeremeev")],
        "The Soviet official's car, never sold to private buyers. Its fins "
        "and chrome follow the American cars of the late fifties, which were "
        "studied closely at Gorky. It had a push-button automatic gearbox, "
        "and was built largely by hand for more than twenty years with "
        "hardly a change."),

    "shelby_cobra": Exhibit(
        "Shelby", "Cobra 289", 1963,
        "Sports car", "Two-seat roadster, front engine",
        [("Engine", "4.7 L Ford V8"),
         ("Power", "275 PS"),
         ("Top speed", "220 km/h"),
         ("Weight", "1,050 kg"),
         ("Built", "1962 - 1967, about 1,000 Cobras"),
         ("Design", "AC Ace, John Tojeiro")],
        "Carroll Shelby's idea: the light British AC Ace with a small-block "
        "Ford V8 in place of its six. The bodies were built in England and "
        "shipped to California for their engines. On the track it beat the "
        "Corvettes of its day, and its Daytona coupe won the world GT "
        "championship for Shelby in 1965."),

    "chevrolet_camaro": Exhibit(
        "Chevrolet", "Camaro Z/28", 1969,
        "Muscle car", "2+2 coupe, front engine",
        [("Engine", "4.9 L V8 (302 cu in)"),
         ("Power", "294 PS"),
         ("Top speed", "about 210 km/h"),
         ("Weight", "1,500 kg"),
         ("Built", "1969 Z/28s: about 20,000"),
         ("Design", "Henry Haga's studio, GM")],
        "Chevrolet's answer to the Mustang. The Z/28 was a package made to "
        "qualify the car for the Trans-Am series, whose rules capped engines "
        "at five litres: hence the high-revving 302. The 1969 car, with its "
        "sharper creases and deep-set grille, is the one most remembered."),

    "porsche_911_turbo": Exhibit(
        "Porsche", "911 Turbo", 1975,
        "Sports car", "2+2 coupe, rear engine",
        [("Engine", "3.0 L turbo flat-six, air-cooled"),
         ("Power", "260 PS"),
         ("Top speed", "250 km/h"),
         ("Weight", "1,140 kg"),
         ("Built", "1975 - 1989, about 21,000 made"),
         ("Design", "F. A. Porsche (the 911)")],
        "Type 930, the first turbocharged production 911, built at first so "
        "Porsche could race a turbo in Group 4. Wide arches and the whale "
        "tail, which also cooled the engine, set it apart. Its turbo lag and "
        "short wheelbase earned it the name widowmaker."),

    "corvette_c8": Exhibit(
        "Chevrolet", "Corvette C8", 2020,
        "Mid-engine sports car", "Two-seat convertible, folding hardtop",
        [("Engine", "6.2 L V8, behind the driver"),
         ("Power", "502 PS (Z51)"),
         ("Top speed", "312 km/h"),
         ("0 - 100 km/h", "under 3 seconds"),
         ("Built", "from 2020"),
         ("Design", "Kirk Bennion, GM Design")],
        "Zora Arkus-Duntov wanted the engine behind the seats in the sixties; "
        "it took until the eighth generation. The Stingray kept the "
        "pushrod V8 and a price far below the European cars it chased. The "
        "convertible folds its hardtop away in sixteen seconds, the first "
        "Corvette with a hardtop that does."),

    "lamborghini_revuelto": Exhibit(
        "Lamborghini", "Revuelto", 2023,
        "Hybrid supercar", "Two-seat coupe, scissor doors",
        [("Engine", "6.5 L V12, three electric motors"),
         ("Power", "1,015 PS combined"),
         ("Top speed", "over 350 km/h"),
         ("0 - 100 km/h", "2.5 seconds"),
         ("Built", "from 2023"),
         ("Design", "Mitja Borkert, Centro Stile")],
        "The successor to the Aventador and Lamborghini's first plug-in "
        "hybrid. A new V12 sits behind the cabin; two electric motors drive "
        "the front wheels and a third helps the gearbox, which sits across "
        "the car behind the engine. The battery runs down the tunnel where "
        "a gearbox used to be."),
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
