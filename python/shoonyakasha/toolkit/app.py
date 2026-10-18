"""Optional lifecycle runner and explicit, independently usable component context."""
from .ui import Theme, Tooltip
import weakref


class Context:
    def __init__(self, engine, parent=None, theme=None):
        self.engine, self.ui = engine, engine.ui
        self.theme = theme or Theme()
        self._owns_canvas = parent is None
        self.root = self.ui.create_canvas(reference_size=(1920, 1080)) if parent is None else parent
        self._components, self._subscriptions = [], []
        self.modal = None
        self.disposed, self.locked = False, False

    def subscribe(self, value, callback, *, immediate=True):
        handle = value.subscribe(callback, immediate=immediate)
        self._subscriptions.append(handle)
        return handle

    def apply_theme(self, theme):
        self.theme = theme
        for component in tuple(self._components):
            component.style()

    @property
    def ready(self):
        """Whether visible retained drawings have completed their initial layout."""
        return all(not c._active() or getattr(c, "_submitted", True) is not None
                   for c in self._components)

    def update(self, dt):
        if self.disposed:
            return
        for component in tuple(self._components):
            if component.disposed:
                continue
            active = component._active()
            modal_allowed = True
            if self.modal:
                ancestor = component
                while ancestor is not None and ancestor is not self.modal:
                    ancestor = ancestor.parent
                modal_allowed = ancestor is self.modal
            value = getattr(component, "value", None)
            interactive = (active and modal_allowed and
                           (not self.locked or getattr(component, "allow_when_locked", False)) and
                           not getattr(value, "read_only", False))
            if component.native_widget:
                component._apply_enabled(interactive)
            if isinstance(component, Tooltip) or (active and (not component.native_widget or interactive)):
                component._poll(dt)

    def dispose(self):
        if self.disposed:
            return
        self.disposed = True
        for component in tuple(self._components):
            component.dispose()
        for handle in self._subscriptions:
            handle()
        self._subscriptions.clear()
        if self._owns_canvas and self.engine.scene.is_valid(self.root):
            self.engine.scene.destroy_entity(self.root)


class Screen:
    def mount(self, context):
        """Create components here. Unmount must release any external resources."""

    def update(self, dt):
        pass

    def unmount(self):
        pass


class Application:
    def __init__(self, screen=None, *, theme=None, **engine_options):
        self.screen, self.theme, self.engine_options = screen, theme, engine_options
        self.engine = self.context = self.playback = self.exporter = None
        self._error = None
        self._cleaned = False

    def on_init(self):
        pass

    def on_post_init(self):
        pass

    def on_pre_render(self, dt):
        pass

    def on_update(self, dt):
        pass

    def on_post_render(self):
        pass

    def on_key_pressed(self, key):
        pass

    def on_resize(self, width, height):
        if self.exporter:
            self.exporter.on_resize(width, height)

    def on_cleanup(self):
        pass

    def _guard(self, name, *, always=False):
        # Native std::function owns a Python reference outside the GC graph.
        # A bound method here would keep application -> engine -> callback alive.
        owner = weakref.ref(self)
        def guarded(*args):
            application = owner()
            if application is None or (application._error and not always):
                return
            try:
                return getattr(application, name)(*args)
            except BaseException as error:
                application._error = error
                application.engine.request_quit()
        return guarded

    def _init(self):
        self.context = Context(self.engine, theme=self.theme)
        self.on_init()
        if self.screen:
            self.screen.mount(self.context)

    def set_screen(self, screen):
        if self.screen:
            self.screen.unmount()
        if self.context:
            self.context.dispose()
        self.screen = screen
        self.context = Context(self.engine, theme=self.theme)
        if screen:
            screen.mount(self.context)

    def _update(self, dt):
        self.context.update(dt)
        exporting = self.exporter is not None and self.exporter.active
        if exporting:
            self.exporter.update(dt)
        elif self.playback:
            self.playback.update(dt)
        if self.screen:
            self.screen.update(dt)
        self.on_update(dt)

    def _post_render(self):
        if self.exporter:
            self.exporter.post_render()
        self.on_post_render()

    def _cleanup(self):
        if self._cleaned:
            return
        self._cleaned = True
        actions = [lambda: self.exporter.cancel() if self.exporter else None,
                   lambda: self.screen.unmount() if self.screen else None,
                   lambda: self.context.dispose() if self.context else None, self.on_cleanup]
        for action in actions:
            try:
                action()
            except BaseException as error:
                if self._error is None:
                    self._error = error

    def callbacks(self, engine):
        """Explicit forwarding for an existing engine; installs no callbacks."""
        if self.engine is not None:
            raise RuntimeError("an application can be attached only once")
        self.engine = engine
        return {"init": self._guard("_init"), "post_init": self._guard("on_post_init"),
                "update": self._guard("_update"), "pre_render": self._guard("on_pre_render"),
                "post_render": self._guard("_post_render"), "key_pressed": self._guard("on_key_pressed"),
                "resize": self._guard("on_resize"), "cleanup": self._guard("_cleanup", always=True)}

    def run(self):
        import shoonyakasha as sk
        engine = sk.Engine(**self.engine_options)
        for name, callback in self.callbacks(engine).items():
            getattr(engine, "set_on_" + name)(callback)
        try:
            engine.run()
        finally:
            self._cleanup()
        self.raise_if_failed()

    def raise_if_failed(self):
        """Call after the native run loop when forwarding callbacks manually."""
        if self._error:
            raise self._error
