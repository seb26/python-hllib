#include "Python.h"

#define MAX_ITEMS 1024
#define BUFFER_SIZE 1024

#ifdef _WIN32
#	include "hllib242\lib\HLLib.h"
#	ifdef _MSC_VER
#			ifdef _WIN64
#				pragma comment(lib, "hllib242/lib/x64/HLLib.lib")
#			else
#				pragma comment(lib, "hllib242/lib/x86/HLLib.lib")
#			endif
#	endif
#else
#	include <hl.h>
#endif

// Defines the layout for a package object.
typedef struct packageObj
{
	PyObject_HEAD

	hlChar *lpPackage;
	hlChar *lpNCFRootPath;

	FILE *pFile;

	hlBool bFileMapping;
	hlBool bQuickFileMapping;
    hlBool bWriteAccess;
	hlBool bVolatileAccess;

	HLPackageType ePackageType;
	hlUInt uiPackage, uiMode;

} packageObj;

static PyObject * packageNew(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	packageObj *self;

	self = (packageObj *)type->tp_alloc(type, 0);
	if (self != NULL)
	{
		// Set defaults
		self->lpPackage = 0;
		self->lpNCFRootPath = 0;

		self->pFile = 0;

		self->bFileMapping = hlFalse;
		self->bQuickFileMapping = hlFalse;
        self->bWriteAccess = hlFalse;
		self->bVolatileAccess = hlFalse;

		self->ePackageType = HL_PACKAGE_NONE;
		self->uiPackage = HL_ID_INVALID;
		self->uiMode = HL_MODE_INVALID;
	}

	return (PyObject *)self;
}

static int packageInit(packageObj *self, PyObject *args, PyObject *kwargs)
{
    static char *arglist[] = { 
        "argPackageName",
        "argFileMapping",
        "argQuickFileMapping",
        "argWriteAccess",
        "argVolatileAccess"
    };

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "s|i|i|i", 
            &self->lpPackage,
            &self->bFileMapping,
            &self->bQuickFileMapping,
            &self->bWriteAccess,
            &self->bVolatileAccess
            ) )
    {
        return -1;
    }

    // Get the package type from the filename extension.
	self->ePackageType = hlGetPackageTypeFromName(self->lpPackage);

	// If the above fails, try getting the package type from the data at the start of the file.
	if(self->ePackageType == HL_PACKAGE_NONE)
	{
		self->pFile = fopen(self->lpPackage, "rb");
		if(self->pFile != 0)
		{
			hlByte lpBuffer[HL_DEFAULT_PACKAGE_TEST_BUFFER_SIZE];
			hlUInt uiBufferSize = (hlUInt)fread(lpBuffer, 1, HL_DEFAULT_PACKAGE_TEST_BUFFER_SIZE, self->pFile);
			self->ePackageType = hlGetPackageTypeFromMemory(lpBuffer, uiBufferSize);

			fclose(self->pFile);
			self->pFile = 0;
		}
	}

    if (self->ePackageType == HL_PACKAGE_NONE)
	{
		PyErr_Format(PyExc_Exception, "Unsupported package type: %s", self->lpPackage);
		return -1;
	}

    // Create a package element, the element is allocated by the library and cleaned
	// up by the library.  An ID is generated which must be bound to apply operations
	// to the package.
	if(!hlCreatePackage(self->ePackageType, &self->uiPackage))
	{
		PyErr_Format(PyExc_Exception, "Error loading %s: %s", self->lpPackage, hlGetString(HL_ERROR_SHORT_FORMATED));
		return -1;
	}

	hlBindPackage(self->uiPackage);

	self->uiMode = HL_MODE_READ | (self->bWriteAccess ? HL_MODE_WRITE : 0);
	self->uiMode |= !self->bFileMapping ? HL_MODE_NO_FILEMAPPING : 0;
	self->uiMode |= self->bQuickFileMapping ? HL_MODE_QUICK_FILEMAPPING : 0;
	self->uiMode |= self->bVolatileAccess ? HL_MODE_VOLATILE : 0;

    // Open the package.
	// Of the above modes, only HL_MODE_READ is required.  HL_MODE_WRITE is present
	// only for future use.  File mapping is recommended as an efficient way to load
	// packages.  Quick file mapping maps the entire file (instead of bits as they are
	// needed) and thus should only be used in Windows 2000 and up (older versions of
	// Windows have poor virtual memory management which means large files won't be able
	// to find a continues block and will fail to load).  Volatile access allows HLLib
	// to share files with other applications that have those file open for writing.
	// This is useful for, say, loading .gcf files while Steam is running.
	if(!hlPackageOpenFile(self->lpPackage, self->uiMode))
	{
		PyErr_Format(PyExc_Exception, "Error loading %s:\n%s\n", self->lpPackage, hlGetString(HL_ERROR_SHORT_FORMATED));
		return -1;
	}

    // Package opened!

    return 0;

}

static void packageDealloc(packageObj *self)
{
    if ( self->uiPackage != HL_ID_INVALID )
    {
        hlPackageClose();
        hlDeletePackage(self->uiPackage);
    }

    self->ob_type->tp_free( (PyObject*)self );

};

static PyObject * packageClose(packageObj *self, PyObject *args)
{
    hlPackageClose();
    hlDeletePackage(self->uiPackage);
    self->uiPackage = HL_ID_INVALID;
    
    return 0;
}

static PyMethodDef chllibMethods[] = { 
    { "close", (PyCFunction)packageClose, METH_VARARGS, "Close the package" },
    { NULL }
};

static PyTypeObject PackageType = {
	PyVarObject_HEAD_INIT(NULL, 0)
	"chllib.Package",             /* tp_name */
	sizeof(packageObj), /* tp_basicsize */
	0,                         /* tp_itemsize */
	(destructor)packageDealloc,  /*tp_dealloc*/
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_reserved */
	0,                         /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash  */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	"Package objects",           /* tp_doc */
	0,		               /* tp_traverse */
	0,		               /* tp_clear */
	0,		               /* tp_richcompare */
	0,		               /* tp_weaklistoffset */
	0,		               /* tp_iter */
	0,		               /* tp_iternext */
	chllibMethods,             /* tp_methods */
	0,             /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)packageInit,      /* tp_init */
	0,                         /* tp_alloc */
	packageNew,                 /* tp_new */
};

static PyMethodDef hlextractMethods[] = {
	{NULL, NULL, 0, NULL} /* Sentinel */
};

PyMODINIT_FUNC initpyhlextract(void)
{
	PyObject* m;

	if (PyType_Ready(&PackageType) < 0)
		return;

	m = Py_InitModule3("pyhlextract", hlextractMethods, "");

	if (m == NULL)
		return;

	Py_INCREF(&PackageType);
	PyModule_AddObject(m, "Package", (PyObject *)&PackageType);

	// Initialize hllib
	hlInitialize();

};