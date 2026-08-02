"""Key codes for Input.is_key_down() and the key-event callbacks.

The engine passes key codes through to GLFW unchanged, so these are GLFW's
values.

    from shoonyakasha import keys

    if engine.input.is_key_down(keys.W):
        ...

Printable keys use their ASCII value, so ord("W") also works. The named
constants cover the keys that have no character: arrows, function keys and
modifiers.
"""

# ── Printable keys (ASCII) ────────────────────────────────────
SPACE = 32
APOSTROPHE = 39
COMMA = 44
MINUS = 45
PERIOD = 46
SLASH = 47

NUM_0, NUM_1, NUM_2, NUM_3, NUM_4 = 48, 49, 50, 51, 52
NUM_5, NUM_6, NUM_7, NUM_8, NUM_9 = 53, 54, 55, 56, 57

SEMICOLON = 59
EQUAL = 61

A, B, C, D, E, F, G, H, I, J, K, L, M = (65, 66, 67, 68, 69, 70, 71,
                                         72, 73, 74, 75, 76, 77)
N, O, P, Q, R, S, T, U, V, W, X, Y, Z = (78, 79, 80, 81, 82, 83, 84,
                                         85, 86, 87, 88, 89, 90)

LEFT_BRACKET = 91
BACKSLASH = 92
RIGHT_BRACKET = 93
GRAVE_ACCENT = 96

# ── Named keys ────────────────────────────────────────────────
ESCAPE = 256
ENTER = 257
TAB = 258
BACKSPACE = 259
INSERT = 260
DELETE = 261

RIGHT = 262
LEFT = 263
DOWN = 264
UP = 265

PAGE_UP = 266
PAGE_DOWN = 267
HOME = 268
END = 269

CAPS_LOCK = 280
SCROLL_LOCK = 281
NUM_LOCK = 282
PRINT_SCREEN = 283
PAUSE = 284

F1, F2, F3, F4, F5, F6 = 290, 291, 292, 293, 294, 295
F7, F8, F9, F10, F11, F12 = 296, 297, 298, 299, 300, 301

KP_0, KP_1, KP_2, KP_3, KP_4 = 320, 321, 322, 323, 324
KP_5, KP_6, KP_7, KP_8, KP_9 = 325, 326, 327, 328, 329
KP_DECIMAL = 330
KP_DIVIDE = 331
KP_MULTIPLY = 332
KP_SUBTRACT = 333
KP_ADD = 334
KP_ENTER = 335
KP_EQUAL = 336

LEFT_SHIFT = 340
LEFT_CONTROL = 341
LEFT_ALT = 342
LEFT_SUPER = 343
RIGHT_SHIFT = 344
RIGHT_CONTROL = 345
RIGHT_ALT = 346
RIGHT_SUPER = 347
MENU = 348

# ── Mouse buttons, for is_mouse_button_down() ─────────────────
MOUSE_LEFT = 0
MOUSE_RIGHT = 1
MOUSE_MIDDLE = 2


def name(code):
    """Return the name of a key code, or its number as a string if unnamed.

    Key-event callbacks receive a raw code.
    """
    for key, value in globals().items():
        if key.isupper() and value == code and not key.startswith("_"):
            return key
    return str(code)
