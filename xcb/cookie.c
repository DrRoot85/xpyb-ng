#include "module.h"
#include "except.h"
#include "conn.h"
#include "cookie.h"
#include "protobj.h"

/*
 * Helpers
 */


/*
 * Infrastructure
 */

static PyObject *
xpybCookie_new(PyTypeObject *self, PyObject *args, PyObject *kw)
{
    return PyType_GenericNew(self, args, kw);
}

static void
xpybCookie_dealloc(xpybCookie *self)
{
    if (self->conn->conn)
        xcb_discard_reply(self->conn->conn, self->cookie.sequence);

    Py_CLEAR(self->reply_type);
    Py_CLEAR(self->request);
    Py_CLEAR(self->conn);
    self->ob_type->tp_free((PyObject *)self);
}


/*
 * Members
 */


/*
 * Methods
 */

static PyObject *
xpybCookie_check(xpybCookie *self, PyObject *args)
{
    xcb_generic_error_t *error;
    int is_void, is_checked;

    xpybRequest_get_attributes(self->request, &is_void, NULL, &is_checked);

    if (!(is_void && is_checked)) {
        PyErr_SetString(xpybExcept_base, "Request is not void and checked.");
        return NULL;
    }
    if (xpybConn_invalid(self->conn))
        return NULL;

    error = xcb_request_check(self->conn->conn, self->cookie);
    if (xpybError_set(self->conn, error))
        return NULL;

    Py_RETURN_NONE;
}

static PyObject *
xpybCookie_reply(xpybCookie *self, PyObject *args)
{
    xcb_generic_error_t *error;
    xcb_generic_reply_t *data;
    PyObject *shim, *reply;
    int is_void;
    void *buf;
    Py_ssize_t len;

    xpybRequest_get_attributes(self->request, &is_void, NULL, NULL);

    /* Check arguments and connection. */
    if (is_void) {
        PyErr_SetString(xpybExcept_base, "Request has no reply.");
        return NULL;
    }
    if (xpybConn_invalid(self->conn))
        return NULL;

    /* Make XCB call */
    data = xcb_wait_for_reply(self->conn->conn, self->cookie.sequence, &error);
    if (xpybError_set(self->conn, error))
        return NULL;
    if (data == NULL) {
        PyErr_SetString(PyExc_IOError, "I/O error on X server connection.");
        return NULL;
    }

    /* Create a shim protocol object */
    shim = PyBuffer_New(32 + data->length * 4);
    if (shim == NULL)
        goto err1;
    if (PyObject_AsWriteBuffer(shim, &buf, &len) < 0)
        goto err2;
    memcpy(buf, data, len);
    free(data);

    /* Call the reply type object to get a new xcb.Reply instance */
    reply = PyObject_CallFunctionObjArgs((PyObject *)self->reply_type, shim, NULL);
    Py_DECREF(shim);
    return reply;

err2:
    Py_DECREF(shim);
err1:
    free(data);
    return NULL;
}

static PyMethodDef xpybCookie_methods[] = {
    { "check",
      (PyCFunction)xpybCookie_check,
      METH_NOARGS,
      "Raise an error if one occurred on the request." },

    { "reply",
      (PyCFunction)xpybCookie_reply,
      METH_NOARGS,
      "Return the reply or raise an error." },

    { NULL } /* terminator */
};


/*
 * Definition
 */

PyTypeObject xpybCookie_type = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "xcb.Cookie",
    .tp_basicsize = sizeof(xpybCookie),
    .tp_new = xpybCookie_new,
    .tp_dealloc = (destructor)xpybCookie_dealloc,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
    .tp_doc = "XCB generic cookie object",
    .tp_methods = xpybCookie_methods
};


/*
 * Module init
 */
int xpybCookie_modinit(PyObject *m)
{
    if (PyType_Ready(&xpybCookie_type) < 0)
        return -1;
    Py_INCREF(&xpybCookie_type);
    if (PyModule_AddObject(m, "Cookie", (PyObject *)&xpybCookie_type) < 0)
        return -1;

    return 0;
}
