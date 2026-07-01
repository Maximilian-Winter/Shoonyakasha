//
// _ecs_bridge.h — Python <-> generic ECS bridge (GIL-safe)
//
// Two things EcsAPI (include/Facade/EcsAPI.h) takes generically that need
// Python-specific marshalling here, at the edge:
//
//   1. Script component payloads: EcsAPI stores std::shared_ptr<void>,
//      opaque to the engine core. Here we wrap a PyObject* in one (owning
//      a reference, GIL-safe on release) and unwrap it back into a new
//      Python reference for Cython to take ownership of.
//   2. System callbacks: EcsAPI systems report success/failure via a
//      std::function<bool(float)> so CallbackSystem can count consecutive
//      failures and auto-disable. Python failure = an exception was
//      raised; we print it (same behavior as _callback_bridge.h's other
//      callbacks) and report false.
//
// Reuses _callback_bridge.h's PyRef for callable lifetime/refcounting.
//

#pragma once

#include "_callback_bridge.h"

#include <Python.h>
#include <functional>
#include <memory>

namespace ShoonyakashaBridge {

// ═══════════════════════════════════════════════════════════════
// Script component payload: PyObject* <-> std::shared_ptr<void>
// ═══════════════════════════════════════════════════════════════

// Wrap a Python object as an opaque shared_ptr<void> for EcsAPI::setComponent.
// Takes a new reference (increfs); the returned shared_ptr's deleter
// decrefs it (GIL-safe) once the last owner (which may be the engine, not
// Python) releases it.
inline std::shared_ptr<void> wrap_py_object(PyObject* obj) {
    Py_XINCREF(obj);
    return std::shared_ptr<void>(static_cast<void*>(obj), [](void* p) {
        if (!p) return;
        PyGILState_STATE gstate = PyGILState_Ensure();
        Py_XDECREF(static_cast<PyObject*>(p));
        PyGILState_Release(gstate);
    });
}

// Unwrap a component payload back into a NEW Python reference, ready for
// Cython to cast to `object` (Cython's <object> cast from a PyObject*
// takes ownership of exactly one reference).
inline PyObject* unwrap_py_object(const std::shared_ptr<void>& ptr) {
    PyObject* obj = static_cast<PyObject*>(ptr.get());
    Py_XINCREF(obj);
    return obj;
}

// ═══════════════════════════════════════════════════════════════
// SystemUpdateCallback: (float dt) → bool (true = ok, false = failed)
// Used by: EcsAPI::addSystem
// ═══════════════════════════════════════════════════════════════

inline std::function<bool(float)> make_system_update_callback(PyObject* callable) {
    PyRef ref(callable);
    return [ref](float dt) -> bool {
        PyGILState_STATE gstate = PyGILState_Ensure();
        PyObject* result = PyObject_CallFunction(ref.obj, "f", dt);
        Py_XDECREF(result);
        bool ok = (result != nullptr) && !PyErr_Occurred();
        if (PyErr_Occurred()) PyErr_Print();  // clears the error indicator
        PyGILState_Release(gstate);
        return ok;
    };
}

} // namespace ShoonyakashaBridge
