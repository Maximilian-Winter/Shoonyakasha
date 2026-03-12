//
// _callback_bridge.h — Python callable → C++ std::function (GIL-safe)
//
// Header-only bridge for Cython callback wrapping. Each factory function
// takes a raw PyObject* (the Python callable), wraps it in a ref-counted
// holder, and returns a std::function that acquires the GIL before invoking.
//
// 碼道之門 — The gateway of the Way of Code
//

#pragma once

#include <Python.h>
#include <functional>
#include <cstdint>

namespace ShoonyakashaBridge {

// ═══════════════════════════════════════════════════════════════
// PyRef — RAII ref-counted Python object holder (copyable)
// ═══════════════════════════════════════════════════════════════

struct PyRef {
    PyObject* obj;

    explicit PyRef(PyObject* o) : obj(o) { Py_XINCREF(obj); }
    PyRef(const PyRef& other) : obj(other.obj) { Py_XINCREF(obj); }
    PyRef& operator=(const PyRef& other) {
        if (this != &other) {
            Py_XDECREF(obj);
            obj = other.obj;
            Py_XINCREF(obj);
        }
        return *this;
    }
    ~PyRef() { Py_XDECREF(obj); }
};

// ═══════════════════════════════════════════════════════════════
// VoidCallback: () → void
// Used by: setOnInit, setOnPostInit, setOnPostRender, setOnCleanup
// ═══════════════════════════════════════════════════════════════

inline std::function<void()> make_void_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref]() {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallNoArgs(ref.obj);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}

// ═══════════════════════════════════════════════════════════════
// UpdateCallback: (float dt) → void
// Used by: setOnUpdate, setOnPreRender
// ═══════════════════════════════════════════════════════════════

inline std::function<void(float)> make_update_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](float dt) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "f", dt);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}

// ═══════════════════════════════════════════════════════════════
// KeyCallback: (int keyCode) → void
// Used by: setOnKeyPressed
// ═══════════════════════════════════════════════════════════════

inline std::function<void(int)> make_key_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](int key) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "i", key);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}

// ═══════════════════════════════════════════════════════════════
// ResizeCallback: (uint32_t w, uint32_t h) → void
// Used by: setOnResize
// ═══════════════════════════════════════════════════════════════

inline std::function<void(uint32_t, uint32_t)> make_resize_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](uint32_t w, uint32_t h) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "II", w, h);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}

// ═══════════════════════════════════════════════════════════════
// KeyEventCallback: (int keyCode, bool pressed) → void
// Used by: InputAPI::setOnKeyEvent
// ═══════════════════════════════════════════════════════════════

inline std::function<void(int, bool)> make_key_event_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](int key, bool pressed) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "iO",
            key, pressed ? Py_True : Py_False);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}

// ═══════════════════════════════════════════════════════════════
// MouseMoveCallback: (float x, float y) → void
// Used by: InputAPI::setOnMouseMove
// ═══════════════════════════════════════════════════════════════

inline std::function<void(float, float)> make_float2_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](float a, float b) {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "ff", a, b);
        Py_XDECREF(result);
        if (PyErr_Occurred()) PyErr_Print();
        PyGILState_Release(gstate);
    };
}

// ═══════════════════════════════════════════════════════════════
// MouseButtonCallback: (int button, bool pressed) → void
// Used by: InputAPI::setOnMouseButton
// Reuses make_key_event_callback (same signature: int, bool)
// ═══════════════════════════════════════════════════════════════

inline std::function<void(int, bool)> make_int_bool_callback(PyObject* callable) {
    return make_key_event_callback(callable);
}

} // namespace ShoonyakashaBridge
