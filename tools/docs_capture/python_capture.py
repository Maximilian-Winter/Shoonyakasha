"""Run an unchanged Python example and capture a still or a clip from it.

    python python_capture.py SCRIPT [EXAMPLE ARGS...]

The example sees only its own path and EXAMPLE ARGS on sys.argv. The
environment says what to capture:

    DOCS_CAPTURE_PATH            still to write (.png)
    DOCS_CAPTURE_RECORD          clip to write (.mp4) instead of a still
    DOCS_CAPTURE_SIZE            window size, WxH (default 1280x720)
    DOCS_CAPTURE_SECONDS         wait this long after the first frame (default 3)
    DOCS_CAPTURE_FRAMES          or wait this many frames
    DOCS_CAPTURE_CLIP_SECONDS    clip length (default 10)
    DOCS_CAPTURE_FPS             clip frame rate (default 30)
    DOCS_CAPTURE_PASSIVE         only size the window; the example captures itself
    DOCS_CAPTURE_KEYS            keys to press, KEY@FRAME separated by commas:
                                 F2@3,N@10 presses F2 on frame 3 and N on frame 10

Clips record with a fixed step: every frame advances the example's clock by
1/fps, so the clip plays at the example's own speed however slowly it renders.
A window larger than the screen still gets the full size. Prints DOCS_CAPTURE_RESULT and exits when done.
"""
import os
import runpy
import sys
import time

import shoonyakasha as sk

Original = sk.Engine
SIZE = tuple(int(v) for v in os.environ.get("DOCS_CAPTURE_SIZE", "1280x720").split("x"))
RECORD = os.environ.get("DOCS_CAPTURE_RECORD", "")
STILL = os.environ.get("DOCS_CAPTURE_PATH", "")
FRAMES = int(os.environ.get("DOCS_CAPTURE_FRAMES", "0"))
SECONDS = float(os.environ.get("DOCS_CAPTURE_SECONDS", "3"))
FPS = int(os.environ.get("DOCS_CAPTURE_FPS", "30"))
CLIP_FRAMES = round(float(os.environ.get("DOCS_CAPTURE_CLIP_SECONDS", "10")) * FPS)
PASSIVE = bool(os.environ.get("DOCS_CAPTURE_PASSIVE"))
KEYS = [(int(frame), key) for key, frame in
        (item.rsplit("@", 1) for item in os.environ.get("DOCS_CAPTURE_KEYS", "").split(",") if item)]
VIRTUAL_KEYS = {"SPACE": 0x20, "ESCAPE": 0x1B, "LEFT": 0x25, "RIGHT": 0x27,
                **{"F%d" % n: 0x6F + n for n in range(1, 13)}}


def post_key(hwnd, key, down):
    """Post a key press or release to the window, as the keyboard would.
    GLFW reads the key from the scan code, so that goes in lParam too."""
    import ctypes
    user32 = ctypes.windll.user32
    vk = VIRTUAL_KEYS.get(key) or ord(key.upper())
    scan = user32.MapVirtualKeyW(vk, 0)
    lparam = 1 | (scan << 16) | ((0xC0000000) if not down else 0)
    user32.PostMessageW(hwnd, 0x0100 if down else 0x0101, vk, lparam)     # WM_KEYDOWN / WM_KEYUP


def client_size():
    """The engine window's client size, and its handle (Windows only)."""
    if os.name != "nt":
        return None, SIZE
    import ctypes
    import ctypes.wintypes as wt
    user32 = ctypes.windll.user32
    found = []

    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def visit(hwnd, _):
        pid = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(pid))
        if pid.value == os.getpid() and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True

    user32.EnumWindows(visit, 0)
    if not found:
        return None, SIZE
    rect = wt.RECT()
    user32.GetClientRect(found[0], ctypes.byref(rect))
    return found[0], (rect.right, rect.bottom)


_procedures = []      # the window procedures below, kept alive


def grow_window(hwnd):
    """Give the window SIZE as its client area even where that is larger than
    the screen. Windows limits a window to the screen's size through
    WM_GETMINMAXINFO; answering it with a larger limit lifts that. The part off
    the screen still renders."""
    import ctypes
    import ctypes.wintypes as wt
    user32 = ctypes.windll.user32
    LRESULT = ctypes.c_ssize_t
    WNDPROC = ctypes.WINFUNCTYPE(LRESULT, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM)
    user32.CallWindowProcW.restype = LRESULT
    user32.CallWindowProcW.argtypes = [ctypes.c_void_p, wt.HWND, wt.UINT, wt.WPARAM, wt.LPARAM]
    user32.SetWindowLongPtrW.restype = ctypes.c_void_p
    user32.SetWindowLongPtrW.argtypes = [wt.HWND, ctypes.c_int, ctypes.c_void_p]

    class MINMAXINFO(ctypes.Structure):
        _fields_ = [("reserved", wt.POINT), ("maxSize", wt.POINT), ("maxPosition", wt.POINT),
                    ("minTrackSize", wt.POINT), ("maxTrackSize", wt.POINT)]

    previous = None

    @WNDPROC
    def procedure(window, message, wparam, lparam):
        result = user32.CallWindowProcW(previous, window, message, wparam, lparam)
        if message == 0x0024:                                       # WM_GETMINMAXINFO
            info = MINMAXINFO.from_address(lparam)
            info.maxTrackSize.x = info.maxTrackSize.y = 16384
        return result

    _procedures.append(procedure)
    previous = user32.SetWindowLongPtrW(hwnd, -4, ctypes.cast(procedure, ctypes.c_void_p))   # GWLP_WNDPROC
    rect = wt.RECT(0, 0, SIZE[0], SIZE[1])
    user32.AdjustWindowRectEx(ctypes.byref(rect), user32.GetWindowLongW(hwnd, -16), False,
                              user32.GetWindowLongW(hwnd, -20))
    user32.SetWindowPos(hwnd, None, 0, 0, rect.right - rect.left, rect.bottom - rect.top,
                        0x0002 | 0x0004 | 0x0010)                    # SWP_NOMOVE | NOZORDER | NOACTIVATE


class CaptureEngine:
    def __init__(self, *args, **kw):
        kw.update(width=SIZE[0], height=SIZE[1])
        self._engine = Original(*args, **kw)
        self._post = None
        self._frames = 0
        self._start = None
        self._recorded = None     # frames recorded so far, once recording
        self._done = False
        self._engine.set_on_post_render(self._capture)

    def __getattr__(self, name):
        return getattr(self._engine, name)

    def __setattr__(self, name, value):
        # Properties such as recording_paused and render_scale belong to the engine.
        if name.startswith("_"):
            object.__setattr__(self, name, value)
        else:
            setattr(self._engine, name, value)

    def set_on_post_render(self, callback):
        self._post = callback

    def _finish(self, ok):
        self._done = True
        print("DOCS_CAPTURE_RESULT", bool(ok), flush=True)
        os._exit(0 if ok else 1)

    def _capture(self):
        if self._post:
            self._post()
        if self._done:
            return
        self._frames += 1
        if self._frames == 1:
            self._start = time.monotonic()
            hwnd, size = client_size()
            if hwnd and size != SIZE:
                grow_window(hwnd)
        for frame, key in KEYS:
            if self._frames in (frame, frame + 2):        # held for two frames
                hwnd, _ = client_size()
                if hwnd:
                    post_key(hwnd, key, self._frames == frame)
        if PASSIVE:
            return
        if self._recorded is not None:
            self._recorded += 1
            if self._recorded >= CLIP_FRAMES:
                self._finish(self._engine.stop_recording())
            return
        ready = (self._frames >= FRAMES if FRAMES
                 else time.monotonic() - self._start >= SECONDS)
        if not ready:
            return
        _, size = client_size()
        if size != SIZE:
            print("DOCS_CAPTURE_SIZE_MISMATCH", size, flush=True)
            self._finish(False)
        elif RECORD:
            if not self._engine.start_recording(RECORD, fps=FPS, fixed_step=True):
                self._finish(False)
            self._recorded = 0
        else:
            self._finish(self._engine.capture_screenshot(STILL))


sk.Engine = CaptureEngine
script = sys.argv[1]
sys.argv = [script] + sys.argv[2:]
runpy.run_path(script, run_name="__main__")
