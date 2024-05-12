#include "defs.h"
#include "debug.h"
#include "version.h"

#include "dfsan/dfsan.h"

extern "C" {
#include "launch.h"
}

#include "parse.h"

#include <z3++.h>

#include <memory>
#include <utility>
#include <vector>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <fcntl.h>

#define PY_SSIZE_T_CLEAN
#include <Python.h>

// z3parser
static z3::context __z3_context;
symsan::Z3ParserSolver *__z3_parser = nullptr;


static PyObject* SymSanInit(PyObject *self, PyObject *args) {
  const char *program;
  unsigned long long ut_size = uniontable_size;

  if (!PyArg_ParseTuple(args, "s|K", &program, &ut_size)) {
    return NULL;
  }

  // setup launcher
  void *shm_base = symsan_init(program, ut_size);
  if (shm_base == (void *)-1) {
    fprintf(stderr, "Failed to map shm: %s\n", strerror(errno));
    return PyErr_SetFromErrno(PyExc_OSError);
  }

  // setup parser
  __z3_parser = new symsan::Z3ParserSolver(shm_base, ut_size, __z3_context);
  if (__z3_parser == nullptr) {
    fprintf(stderr, "Failed to initialize parser\n");
    return PyErr_NoMemory();
  }

  return PyCapsule_New(shm_base, "dfsan_label_info", NULL);
}

static PyObject* SymSanConfig(PyObject *self, PyObject *args, PyObject *keywds) {
  static const char *kwlist[] = {"input", "args", "debug", "bounds", NULL};
  const char *input = NULL;
  PyObject *iargs = NULL;
  int debug = 0;
  int bounds = 0;

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "s|O!ii",
      const_cast<char**>(kwlist), &input, &PyList_Type, &iargs, &debug, &bounds)) {
    return NULL;
  }

  if (input == NULL) {
    PyErr_SetString(PyExc_ValueError, "missing input");
    return NULL;
  }

  if (symsan_set_input(input) != 0) {
    PyErr_SetString(PyExc_ValueError, "invalid input");
    return NULL;
  }

  if (args != NULL) {
    Py_ssize_t argc = PyList_Size(iargs);
    char *argv[argc];
    for (Py_ssize_t i = 0; i < argc; i++) {
      PyObject *item = PyList_GetItem(iargs, i);
      if (item == NULL) {
        PyErr_SetString(PyExc_RuntimeError, "failed to retrieve args list");
        return NULL;
      }
      if (!PyUnicode_Check(item)) {
        PyErr_SetString(PyExc_TypeError, "args must be a list of strings");
        return NULL;
      }
      argv[i] = const_cast<char*>(PyUnicode_AsUTF8(item));
    }
    if (symsan_set_args(argc, argv) != 0) {
      PyErr_SetString(PyExc_ValueError, "invalid args");
      return NULL;
    }
  }

  if (symsan_set_debug(debug) != 0) {
    PyErr_SetString(PyExc_ValueError, "invalid debug");
    return NULL;
  }

  if (symsan_set_bounds_check(bounds) != 0) {
    PyErr_SetString(PyExc_ValueError, "invalid bounds");
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* SymSanRun(PyObject *self, PyObject *args, PyObject *keywds) {
  static const char *kwlist[] = {"stdin", NULL};
  const char *file = NULL;
  int fd = 0;

  if (!PyArg_ParseTupleAndKeywords(args, keywds, "|s", const_cast<char**>(kwlist), &file)) {
    return NULL;
  }

  if (file) {
    fd = open(file, O_RDONLY);
    if (fd < 0) {
      PyErr_SetFromErrno(PyExc_OSError);
      return NULL;
    }
  }

  int ret = symsan_run(fd);

  if (file) {
    close(fd);
  }
  
  if (ret < 0) {
    PyErr_SetString(PyExc_ValueError, "failed to launch target");
    return NULL;
  }

  Py_RETURN_NONE;
}

static PyObject* SymSanReadEvent(PyObject *self, PyObject *args) {
  PyObject *ret;
  char *buf;
  Py_ssize_t size;
  unsigned timeout = 0;

  if (!PyArg_ParseTuple(args, "n|I", &size, &timeout)) {
    return NULL;
  }

  if (size <= 0) {
    PyErr_SetString(PyExc_ValueError, "invalid buffer size");
    return NULL;
  }

  buf = (char *)malloc(size);

  ssize_t read = symsan_read_event(buf, size, timeout);
  if (read < 0) {
    PyErr_SetFromErrno(PyExc_OSError);
    free(buf);
    return NULL;
  }

  ret = PyBytes_FromStringAndSize(buf, read);
  free(buf);

  return ret;
}

static PyObject* SymSanTerminate(PyObject *self) {
  if (symsan_terminate() != 0) {
    PyErr_SetString(PyExc_RuntimeError, "failed to terminate target");
    return NULL;
  }
  Py_RETURN_NONE;
}

static PyObject* SymSanDestroy(PyObject *self) {
  if (__z3_parser != nullptr) {
    delete __z3_parser;
    symsan_destroy();
    __z3_parser = nullptr;
  }
  Py_RETURN_NONE;
}

static PyMethodDef SymSanMethods[] = {
  {"init", SymSanInit, METH_VARARGS, "initialize symsan target"},
  {"config", (PyCFunction)SymSanConfig, METH_VARARGS | METH_KEYWORDS, "config symsan"},
  {"run", (PyCFunction)SymSanRun, METH_VARARGS, "run symsan target, optional stdin=file"},
  {"read_event", SymSanReadEvent, METH_VARARGS, "read a symsan event"},
  {"terminate", (PyCFunction)SymSanTerminate, METH_NOARGS, "terminate current symsan instance"},
  {"destroy", (PyCFunction)SymSanDestroy, METH_NOARGS, "destroy symsan target"},
  // {"init_parser", InitParser, METH_VARARGS, "initialize symbolic expression parser"},
  // {"parse_cond", ParseCond, METH_VARARGS, "parse trace_cond event into solving tasks"},
  // {"parse_gep", ParseGEP, METH_VARARGS, "parse trace_gep event into solving tasks"},
  // {"add_constraint", AddConstraint, METH_VARARGS, "add a constraint"},
  // {"solve_task", SolveTask, METH_VARARGS, "solve a task"},
  {NULL, NULL, 0, NULL}  /* Sentinel */
};

static char SymSanDoc[] = "Python3 wrapper over SymSan launch, parser, and solver.";

static PyModuleDef SymSanModule = {
  PyModuleDef_HEAD_INIT,
  "symsan",   /* name of module */
  SymSanDoc,  /* module documentation, may be NULL */
  -1,         /* size of per-interpreter state of the module,
                 or -1 if the module keeps state in global variables. */
  SymSanMethods
};

PyMODINIT_FUNC
PyInit_symsan(void) {
  // check if initialized before?
  if (__z3_parser != nullptr) {
    delete __z3_parser;
    symsan_destroy();
  }
  return PyModule_Create(&SymSanModule);
}
