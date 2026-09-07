"""Run an unchanged Python example with a native post-render capture callback."""
import os, runpy, sys, time
import shoonyakasha as sk
Original = sk.Engine
class CaptureEngine:
    def __init__(self,*a,**kw):
        kw.update(width=1280, height=720)
        self._engine=Original(*a,**kw)
        self._post=None
        self._start=None
        self._done=False
        self._engine.set_on_post_render(self._capture)
    def __getattr__(self,name): return getattr(self._engine,name)
    def set_on_post_render(self, callback): self._post=callback
    def _capture(self):
        if self._post: self._post()
        if self._start is None: self._start=time.monotonic()
        if not self._done and time.monotonic()-self._start >= float(os.environ.get('DOCS_CAPTURE_SECONDS','3')):
            self._done=True
            ok=self._engine.capture_screenshot(os.environ['DOCS_CAPTURE_PATH'])
            print('DOCS_CAPTURE_RESULT',ok,flush=True)
sk.Engine=CaptureEngine
runpy.run_path(sys.argv[1],run_name='__main__')
