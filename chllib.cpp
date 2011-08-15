#include "Python.h"
#include "structmember.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <linux/limits.h>
#	define MAX_PATH PATH_MAX

#	define FOREGROUND_BLUE      0x0001
#	define FOREGROUND_GREEN     0x0002
#	define FOREGROUND_RED       0x0004
#	define FOREGROUND_INTENSITY 0x0008

#	define stricmp strcasecmp
#	define strnicmp strncasecmp
#endif

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

// Set this to package bSilent before functions are executed 
// where we can't pass in the package instance (callbacks)
static hlBool bSilent = hlFalse;
static PyObject *pUpdateFunc = Py_None;

typedef struct PackageObject
{
	PyObject_HEAD

	hlChar *lpPackage;
	hlBool bDefragment;
	hlChar *lpNCFRootPath;

	FILE *pFile;

	hlBool bFileMapping;
	hlBool bQuickFileMapping;
	hlBool bVolatileAccess;
	hlBool bOverwriteFiles;
	hlBool bForceDefragment;

	HLPackageType ePackageType;
	hlUInt uiPackage, uiMode;

	char bSilent;

} PackageObject;


HLValidation Validate(PackageObject *self, HLDirectoryItem *pItem);
hlVoid PrintAttribute(hlChar *lpPrefix, HLAttribute *pAttribute, hlChar *lpPostfix);
hlVoid PrintValidation(HLValidation eValidation);

static void
	Package_dealloc(PackageObject* self)
{
	if( self->uiPackage != HL_ID_INVALID )
	{
		// Close the package.
		hlPackageClose();

		if(!self->bSilent)
			PySys_WriteStdout("%s closed.\n", self->lpPackage);

		// Free up the allocated memory.
		hlDeletePackage(self->uiPackage);
	}

	self->ob_type->tp_free((PyObject*)self);
}

static PyObject *
	Package_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
	PackageObject *self;

	self = (PackageObject *)type->tp_alloc(type, 0);
	if (self != NULL)
	{
		// Set defaults
		self->lpPackage = 0;
		self->bDefragment = hlFalse;
		self->lpNCFRootPath = 0;

		self->pFile = 0;

		self->bFileMapping = hlFalse;
		self->bQuickFileMapping = hlFalse;
		self->bVolatileAccess = hlFalse;
		self->bOverwriteFiles = hlTrue;
		self->bForceDefragment = hlFalse;

		self->ePackageType = HL_PACKAGE_NONE;
		self->uiPackage = HL_ID_INVALID;
		self->uiMode = HL_MODE_INVALID;

		self->bSilent = 0;
	}

	return (PyObject *)self;
}

static int
	Package_init(PackageObject *self, PyObject *args, PyObject *kwds)
{
	int volatileaccess;

	static char *kwlist[] = {"packagename", "volatileaccess", NULL};

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", kwlist, 
                                     &self->lpPackage, &volatileaccess))
	{
		return -1;
	}

	self->bVolatileAccess = volatileaccess;
	/*
	if (!PyArg_ParseTuple(args, "s", &self->lpPackage))
	{
		return -1;
	}*/
	
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

	if(self->ePackageType == HL_PACKAGE_NONE)
	{
		PyErr_Format(PyExc_Exception, "Error loading %s:\nUnsupported package type.\n", self->lpPackage);
		return -1;
	}

	// Create a package element, the element is allocated by the library and cleaned
	// up by the library.  An ID is generated which must be bound to apply operations
	// to the package.
	if(!hlCreatePackage(self->ePackageType, &self->uiPackage))
	{
		PyErr_Format(PyExc_Exception, "Error loading %s:\n%s\n", self->lpPackage, hlGetString(HL_ERROR_SHORT_FORMATED));
		return -1;
	}

	hlBindPackage(self->uiPackage);

	self->uiMode = HL_MODE_READ | (self->bDefragment ? HL_MODE_WRITE : 0);
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

	if(!self->bSilent)
		PySys_WriteStdout("%s opened.\n", self->lpPackage);

	// Package opened!

	return 0;
}

PyThreadState *g_extract_save;
static hlUInt g_bytesExtracted;

#define MAX_ITEMS 1024
#define BUFFER_SIZE 1024

// Package_extract()

// Callbacks

// Other functions

static PyObject *
	Package_close(PackageObject *self, PyObject *args)
{
	// Close the package.
	hlPackageClose();

	if(!self->bSilent)
		PySys_WriteStdout("%s closed.\n", self->lpPackage);

	// Free up the allocated memory.
	hlDeletePackage(self->uiPackage);	

	self->uiPackage = HL_ID_INVALID;

	Py_INCREF(Py_None);
	return Py_None;
}

static PyMethodDef Package_methods[] = {
	{"close", (PyCFunction)Package_close, METH_VARARGS, "Close package"},
	{NULL}  /* Sentinel */
};

static PyMemberDef Package_members[] = {
	{"silent", T_BOOL, offsetof(PackageObject, bSilent), 0,
	"silent"},
	{NULL}  /* Sentinel */
};

static PyTypeObject PackageType = {
	PyVarObject_HEAD_INIT(NULL, 0) 
	"pyhlextract.Package",             /* tp_name */
	sizeof(PackageObject), /* tp_basicsize */
	0,                         /* tp_itemsize */
	(destructor)Package_dealloc,  /*tp_dealloc*/
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
	Package_methods,             /* tp_methods */
	Package_members,             /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	(initproc)Package_init,      /* tp_init */
	0,                         /* tp_alloc */
	Package_new,                 /* tp_new */
};



static PyMethodDef hlextractMethods[] = {
	{NULL, NULL, 0, NULL} /* Sentinel */
};

static int
all_ins(PyObject *d)
{
	// Package attributes
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_VERSION", (long)HL_GCF_PACKAGE_VERSION)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_ID", (long)HL_GCF_PACKAGE_ID)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_ALLOCATED_BLOCKS", (long)HL_GCF_PACKAGE_ALLOCATED_BLOCKS)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_USED_BLOCKS", (long)HL_GCF_PACKAGE_USED_BLOCKS)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_BLOCK_LENGTH", (long)HL_GCF_PACKAGE_BLOCK_LENGTH)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_LAST_VERSION_PLAYED", (long)HL_GCF_PACKAGE_LAST_VERSION_PLAYED)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_PACKAGE_COUNT", (long)HL_GCF_PACKAGE_COUNT)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_ENCRYPTED", (long)HL_GCF_ITEM_ENCRYPTED)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_COPY_LOCAL", (long)HL_GCF_ITEM_COPY_LOCAL)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_OVERWRITE_LOCAL", (long)HL_GCF_ITEM_OVERWRITE_LOCAL)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_BACKUP_LOCAL", (long)HL_GCF_ITEM_BACKUP_LOCAL)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_FLAGS", (long)HL_GCF_ITEM_FLAGS)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_FRAGMENTATION", (long)HL_GCF_ITEM_FRAGMENTATION)) return -1;
	if (PyModule_AddIntConstant(d, "HL_GCF_ITEM_COUNT", (long)HL_GCF_ITEM_COUNT)) return -1;

	// Item types
    if (PyModule_AddIntConstant(d, "HL_ITEM_NONE", (long)HL_ITEM_NONE)) return -1;
	if (PyModule_AddIntConstant(d, "HL_ITEM_FOLDER", (long)HL_ITEM_FOLDER)) return -1;
	if (PyModule_AddIntConstant(d, "HL_ITEM_FILE", (long)HL_ITEM_FILE)) return -1;

	return 0;
}

PyMODINIT_FUNC
	initchllib(void)
{
	PyObject* m;

	if (PyType_Ready(&PackageType) < 0)
		return;

	m = Py_InitModule3("chllib", hlextractMethods, "");

	if (m == NULL)
		return;

    if (all_ins(m))
        return;

	Py_INCREF(&PackageType);
	PyModule_AddObject(m, "Package", (PyObject *)&PackageType);

	// Initialize hllib
	hlInitialize();

	// Callbacks
	hlSetVoid(HL_PROC_EXTRACT_ITEM_START, ExtractItemStartCallback);
	hlSetVoid(HL_PROC_EXTRACT_ITEM_END, ExtractItemEndCallback);
	hlSetVoid(HL_PROC_EXTRACT_FILE_PROGRESS, FileProgressCallback);
	hlSetVoid(HL_PROC_VALIDATE_FILE_PROGRESS, FileProgressCallback);
	hlSetVoid(HL_PROC_DEFRAGMENT_PROGRESS_EX, DefragmentProgressCallback);
}