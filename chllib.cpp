// Current source by Sandern (http://www.hl2wars.com/forum/memberlist.php?mode=viewprofile&u=2)
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
static PyObject *
	Package_extract(PackageObject *self, PyObject *args)
{
	hlUInt i;

	hlUInt uiExtractItems = 0;
	hlChar *lpExtractItems[MAX_ITEMS];
	hlChar *lpDestination = 0;;

	HLDirectoryItem *pItem = 0;

	pUpdateFunc = Py_None;

	if (!PyArg_ParseTuple(args, "ss|O", &lpExtractItems[0], &lpDestination, &pUpdateFunc))
	{
		return NULL;
	}

	uiExtractItems = 1;

	// Just before extracting set these globals
	hlSetBoolean(HL_OVERWRITE_FILES, self->bOverwriteFiles);
	hlSetBoolean(HL_FORCE_DEFRAGMENT, self->bForceDefragment);
	bSilent = self->bSilent;

	// Extract the requested items.
	for(i = 0; i < uiExtractItems; i++)
	{
		// Find the item.
		pItem = hlFolderGetItemByPath(hlPackageGetRoot(), lpExtractItems[i], HL_FIND_ALL);

		if(pItem == 0)
		{
			PySys_WriteStdout("%s not found in package.\n", lpExtractItems[i]);
			continue;
		}

		if(!self->bSilent)
		{
			PySys_WriteStdout("Extracting %s...\n\n", hlItemGetName(pItem));
		}

		// Extract the item.
		// Item is extracted to cDestination\Item->GetName().
		g_extract_save = PyEval_SaveThread();
		g_bytesExtracted = 0;
		hlItemExtract(pItem, lpDestination);
		PyEval_RestoreThread(g_extract_save);

		if(!self->bSilent)
		{
			PySys_WriteStdout("\nDone.\n");
		}
	}

	Py_INCREF(Py_None);
	return Py_None;
}

hlVoid ProgressStart()
{
#ifndef _WIN32
	uiProgressLast = 0;
	PySys_WriteStdout("0%%");
#endif
}

hlVoid ProgressUpdate(hlULongLong uiBytesDone, hlULongLong uiBytesTotal)
{
	if(!bSilent)
	{
#ifdef _WIN32
		HANDLE Handle = GetStdHandle(STD_OUTPUT_HANDLE);
		if (Handle != INVALID_HANDLE_VALUE)
		{
			CONSOLE_SCREEN_BUFFER_INFO Info;
			if(GetConsoleScreenBufferInfo(Handle, &Info))
			{
				if(uiBytesTotal == 0)
				{
					PySys_WriteStdout("100.0%%");
				}
				else
				{
					PySys_WriteStdout("%0.0f%%", (hlSingle)((hlDouble)uiBytesDone / (hlDouble)uiBytesTotal * 100.0));
				}
				SetConsoleCursorPosition(Handle, Info.dwCursorPosition);
			}
		}
#else
		hlUInt uiProgress = uiBytesTotal == 0 ? 100 : (hlUInt)((hlUInt64)uiBytesDone * 100 / (hlUInt64)uiBytesTotal);
		while(uiProgress >= uiProgressLast + 10)
		{
			uiProgressLast += 10;
			if(uiProgressLast == 100)
			{
				PySys_WriteStdout("100%% ");
			}
			else if(uiProgressLast == 50)
			{
				PySys_WriteStdout("50%%");
			}
			else
			{
				PySys_WriteStdout(".");
			}
		}
#endif
	}
}

hlVoid ExtractItemStartCallback(HLDirectoryItem *pItem)
{
#if 0
	PyEval_RestoreThread(g_extract_save);

	if( pStartExtractFunc == Py_None )
	{
		if(!bSilent)
		{
			if(hlItemGetType(pItem) == HL_ITEM_FILE)
			{
				PySys_WriteStdout("  Extracting %s: ", hlItemGetName(pItem));
				ProgressStart();
			}
			else
			{
				PySys_WriteStdout("  Extracting %s:\n", hlItemGetName(pItem));
			}
		}
	}
	else
	{
		PyEval_CallFunction(pStartExtractFunc, "si", hlItemGetName(pItem), hlItemGetType(pItem));
	}

	g_extract_save = PyEval_SaveThread();
#endif // 0
}

hlVoid FileProgressCallback(HLDirectoryItem *pFile, hlUInt uiBytesExtracted, hlUInt uiBytesTotal, hlBool *pCancel)
{
#if 0
	PyEval_RestoreThread(g_extract_save);

	if( pUpdateFunc == Py_None )
		ProgressUpdate((hlULongLong)uiBytesExtracted, (hlULongLong)uiBytesTotal);
	else
		PyEval_CallFunction(pUpdateFunc, "II", uiBytesExtracted, uiBytesTotal);

	g_extract_save = PyEval_SaveThread();
#endif // 0
}

hlVoid ExtractItemEndCallback(HLDirectoryItem *pItem, hlBool bSuccess)
{
#if 0
	PyEval_RestoreThread(g_extract_save);

	hlUInt uiSize = 0;
	hlChar lpPath[512] = "";

	if( pEndExtractFunc == Py_None )
	{
		if(bSuccess)
		{
			if(!bSilent)
			{
				hlItemGetSize(pItem, &uiSize);
				if(hlItemGetType(pItem) == HL_ITEM_FILE)
				{
					PySys_WriteStdout("OK");
					PySys_WriteStdout(" (%u B)\n", uiSize);
				}
				else
				{
					PySys_WriteStdout("  Done %s: ", hlItemGetName(pItem));
					PySys_WriteStdout("OK");
					PySys_WriteStdout(" (%u B)\n", uiSize);
				}
			}
		}
		else
		{
			if(!bSilent)
			{
				if(hlItemGetType(pItem) == HL_ITEM_FILE)
				{
					PySys_WriteStdout("Errored\n");
					PySys_WriteStdout("    %s\n", hlGetString(HL_ERROR_SHORT_FORMATED));
				}
				else
				{
					PySys_WriteStdout("  Done %s: ", hlItemGetName(pItem));
					PySys_WriteStdout("Errored\n");
				}
			}
			else
			{
				hlItemGetPath(pItem, lpPath, sizeof(lpPath));
				if(hlItemGetType(pItem) == HL_ITEM_FILE)
				{
					PySys_WriteStdout("  Error extracting %s:\n", lpPath);
					PySys_WriteStdout("    %s\n", hlGetString(HL_ERROR_SHORT_FORMATED));
				}
				else
				{
					PySys_WriteStdout("  Error extracting %s.\n", lpPath);
				}
			}
		}
	}
	else
	{
		hlItemGetSize(pItem, &uiSize);
		hlItemGetPath(pItem, lpPath, sizeof(lpPath));

		PyEval_CallFunction(pEndExtractFunc, "bsisIs", bSuccess, hlItemGetName(pItem), hlItemGetType(pItem), lpPath, uiSize, bSuccess ? "" : hlGetString(HL_ERROR_SHORT_FORMATED));
	}

	g_extract_save = PyEval_SaveThread();
#endif // 0
	hlUInt uiSize = 0;
	hlChar lpPath[512] = "";

	if(hlItemGetType(pItem) == HL_ITEM_FILE) 
	{
		hlItemGetSize(pItem, &uiSize);

		g_bytesExtracted += uiSize;

		if( g_bytesExtracted > 10000000 && pUpdateFunc != Py_None )
		{
			hlItemGetPath(pItem, lpPath, sizeof(lpPath));

			PyEval_RestoreThread(g_extract_save);
			PyEval_CallFunction(pUpdateFunc, "bsisIs", bSuccess, hlItemGetName(pItem), hlItemGetType(pItem), lpPath, g_bytesExtracted, bSuccess ? "" : hlGetString(HL_ERROR_SHORT_FORMATED));
			g_extract_save = PyEval_SaveThread();

			g_bytesExtracted = 0;
		}
	}
}

hlVoid DefragmentProgressCallback(HLDirectoryItem *pFile, hlUInt uiFilesDefragmented, hlUInt uiFilesTotal, hlULongLong uiBytesDefragmented, hlULongLong uiBytesTotal, hlBool *pCancel)
{
	ProgressUpdate(uiBytesDefragmented, uiBytesTotal);
}

static PyObject *
	Package_validate(PackageObject *self, PyObject *args)
{
	hlUInt i;

	hlUInt uiValidateItems = 0;
	hlChar *lpValidateItems[MAX_ITEMS];

	HLDirectoryItem *pItem = 0;

	if (!PyArg_ParseTuple(args, "s", &lpValidateItems[0]))
	{
		return NULL;
	}

	uiValidateItems = 1;

	// Validate the requested items.
	for(i = 0; i < uiValidateItems; i++)
	{
		// Find the item.
		pItem = hlFolderGetItemByPath(hlPackageGetRoot(), lpValidateItems[i], HL_FIND_ALL);

		if(pItem == 0)
		{
			printf("%s not found in package.\n", lpValidateItems[i]);
			continue;
		}

		if(!self->bSilent)
		{
			PySys_WriteStdout("Validating %s...\n\n", hlItemGetName(pItem));
		}

		// Validate the item.
		Validate(self, pItem);

		if(!self->bSilent)
		{
			PySys_WriteStdout("\nDone.\n");
		}
	}

	Py_INCREF(Py_None);
	return Py_None;
}

HLValidation Validate(PackageObject *self, HLDirectoryItem *pItem)
{
	hlUInt i, uiItemCount;
	hlChar lpPath[512] = "";
	HLValidation eValidation = HL_VALIDATES_OK, eTest;

	switch(hlItemGetType(pItem))
	{
	case HL_ITEM_FOLDER:
		if(!self->bSilent)
		{
			PySys_WriteStdout("  Validating %s:\n", hlItemGetName(pItem));
		}

		uiItemCount = hlFolderGetCount(pItem);
		for(i = 0; i < uiItemCount; i++)
		{
			eTest = Validate(self, hlFolderGetItem(pItem, i));
			if(eTest > eValidation)
			{
				eValidation = eTest;
			}
		}

		if(!self->bSilent)
		{
			PySys_WriteStdout("  Done %s: ", hlItemGetName(pItem));
			PrintValidation(eValidation);
			PySys_WriteStdout("\n");
		}
		break;
	case HL_ITEM_FILE:
		if(!self->bSilent)
		{
			PySys_WriteStdout("  Validating %s: ", hlItemGetName(pItem));
			//ProgressStart();
		}

		eValidation = hlFileGetValidation(pItem);

		if(self->bSilent)
		{
			switch(eValidation)
			{
			case HL_VALIDATES_INCOMPLETE:
			case HL_VALIDATES_CORRUPT:
				hlItemGetPath(pItem, lpPath, sizeof(lpPath));
				PySys_WriteStdout("  Validating %s: ", lpPath);
				PrintValidation(eValidation);
				PySys_WriteStdout("\n");
				break;
			}
		}
		else
		{
			PrintValidation(eValidation);
			PySys_WriteStdout("  \n");
		}
		break;
	}

	return eValidation;
}

hlVoid PrintValidation(HLValidation eValidation)
{
	switch(eValidation)
	{
	case HL_VALIDATES_ASSUMED_OK:
		PySys_WriteStdout("Assumed OK");
		break;
	case HL_VALIDATES_OK:
		PySys_WriteStdout("OK");
		break;
	case HL_VALIDATES_INCOMPLETE:
		PySys_WriteStderr("Incomplete");
		break;
	case HL_VALIDATES_CORRUPT:
		PySys_WriteStderr("Corrupt");
		break;
	case HL_VALIDATES_CANCELED:
		PySys_WriteStderr("Canceled");
		break;
	case HL_VALIDATES_ERROR:
		PySys_WriteStderr("Error");
		break;
	default:
		PySys_WriteStderr("Unknown");
		break;
	}
}

static PyObject *
	Package_listdir(PackageObject *self, PyObject *args)
{
	hlUInt i, iCount;
	hlChar *lpPath;
	const hlChar *lpItemName;

	HLDirectoryItem *pItem = 0;
	HLDirectoryItem *pSubItem = 0;

	PyObject *d, *v;

	if (!PyArg_ParseTuple(args, "s", &lpPath))
	{
		return NULL;
	}

	// Find the item.
	pItem = hlFolderGetItemByPath(hlPackageGetRoot(), lpPath, HL_FIND_ALL);
	if(pItem == 0)
	{
		PyErr_SetString(PyExc_ValueError, "x not found in package.\n");
		return NULL;
	}

	// Allocate a list
    if ((d = PyList_New(0)) == NULL) {
        return NULL;
    }

	// List items
	iCount = hlFolderGetCount(pItem);
	for( i = 0; i < iCount; i++ )
	{
		pSubItem = hlFolderGetItem(pItem, i);
		lpItemName = hlItemGetName(pSubItem);

		v = PyString_FromString(lpItemName);
        if (v == NULL) {
            Py_DECREF(d);
            d = NULL;
            break;
        }
        if (PyList_Append(d, v) != 0) {
            Py_DECREF(v);
            Py_DECREF(d);
            d = NULL;
            break;
        }
        Py_DECREF(v);
	}

	return d;
}

static PyObject *
	Package_getsize(PackageObject *self, PyObject *args)
{
	hlULongLong ulSize;
	hlChar *lpPath;
	HLDirectoryItem *pItem = 0;

	PyObject *v;

	if (!PyArg_ParseTuple(args, "s", &lpPath))
	{
		return NULL;
	}

	// Find the item.
	pItem = hlFolderGetItemByPath(hlPackageGetRoot(), lpPath, HL_FIND_ALL);
	if(pItem == 0)
	{
		PyErr_SetString(PyExc_ValueError, "x not found in package.\n");
		return NULL;
	}

	// Get size and return it
	hlItemGetSizeEx(pItem, &ulSize);
	v = Py_BuildValue("K", ulSize);
    if (v == NULL) {
        return NULL;
    }

	return v;
}

static PyObject *
		Package_getattributename(PackageObject *self, PyObject *args)
{
	const hlChar *pAttributeName;
	HLPackageAttribute pa;

	PyObject *v;

	if (!PyArg_ParseTuple(args, "i", &pa))
	{
		return NULL;
	}

	pAttributeName = hlPackageGetAttributeName(pa);
	v = PyString_FromString(pAttributeName);
    if (v == NULL) {
        return NULL;
    }
	return v;
}

static PyObject *
		Package_getattribute(PackageObject *self, PyObject *args)
{
	HLAttribute attr;
	HLPackageAttribute pa;

	PyObject *v;

	if (!PyArg_ParseTuple(args, "i", &pa))
	{
		return NULL;
	}

	// Get the attribute
	if( !hlPackageGetAttribute(pa, &attr) )
	{
		PyErr_SetString(PyExc_ValueError, "Failed to retrieve attribute x.\n");
		return NULL;
	}

	// Build return value, depends on type
	switch(attr.eAttributeType)
	{
	case HL_ATTRIBUTE_BOOLEAN:
		v = Py_BuildValue("b", attr.Value.Boolean.bValue);
		break;
	case HL_ATTRIBUTE_INTEGER:
		v = Py_BuildValue("i", attr.Value.Integer.iValue);
		break;
	case HL_ATTRIBUTE_UNSIGNED_INTEGER:
		v = Py_BuildValue("I", attr.Value.UnsignedInteger.uiValue);
		break;
	case HL_ATTRIBUTE_FLOAT:
		v = Py_BuildValue("f", attr.Value.Float.fValue);
		break;
	case HL_ATTRIBUTE_STRING:
		v = PyString_FromString(attr.Value.String.lpValue);
		break;
	default:
		v = NULL;
		break;
	}

	if( !v )
	{
		PyErr_SetString(PyExc_ValueError, "Invalid attribute x.\n");
		return NULL;	
	}

	return v;
}

static PyObject *
	Package_getitemtype(PackageObject *self, PyObject *args)
{
	hlChar *lpPath;
	HLDirectoryItem *pItem = 0;

	PyObject *v;

	if (!PyArg_ParseTuple(args, "s", &lpPath))
	{
		return NULL;
	}

	// Find the item.
	pItem = hlFolderGetItemByPath(hlPackageGetRoot(), lpPath, HL_FIND_ALL);
	if(pItem == 0)
	{
		PyErr_SetString(PyExc_ValueError, "x not found in package.\n");
		return NULL;
	}

	
	// Get type and return it
	v = Py_BuildValue("i", hlItemGetType(pItem));
    if (v == NULL) {
        return NULL;
    }

	return v;
}

static PyObject *
    Package_info(PackageObject *self, PyObject *args)
{
    HLDirectoryItem *pItem = 0, *pSubItem = 0;
    hlChar *lpInfoItem;
    hlChar lpTempBuffer[BUFFER_SIZE];

    if (!PyArg_ParseTuple(args, "s", &lpInfoItem))
    {
        return NULL;
    }

    pItem = hlPackageGetRoot();
    pSubItem = hlFolderGetItemByPath(pItem, lpInfoItem, HL_FIND_ALL);

    if(pSubItem != 0)
    {
        PyObject *infodict;
        *lpTempBuffer = 0;
        hlItemGetPath(pSubItem, lpTempBuffer, sizeof(lpTempBuffer));

        infodict = Py_BuildValue("{s:s}", "dir", lpInfoItem);

        switch(hlItemGetType(pSubItem))
        {
        case HL_ITEM_FOLDER:
            PyDict_SetItemString(infodict, "type", Py_BuildValue("s", "folder"));
            PyDict_SetItemString(infodict, "size", Py_BuildValue("k", hlFolderGetSizeEx(pSubItem, hlTrue)));
            PyDict_SetItemString(infodict, "sizedisk", Py_BuildValue("k", hlFolderGetSizeOnDiskEx(pSubItem, hlTrue)));
            PyDict_SetItemString(infodict, "folders", Py_BuildValue("k", hlFolderGetFolderCount(pSubItem, hlTrue)));
            PyDict_SetItemString(infodict, "files", Py_BuildValue("k", hlFolderGetFileCount(pSubItem, hlTrue)));
            break;
        case HL_ITEM_FILE:
            PyDict_SetItemString(infodict, "type", Py_BuildValue("s", "file"));
            PyDict_SetItemString(infodict, "extractable", Py_BuildValue("s", hlFileGetExtractable(pSubItem) ? "True" : "False"));
            PyDict_SetItemString(infodict, "size", Py_BuildValue("k", hlFileGetSize(pSubItem)));
            PyDict_SetItemString(infodict, "sizedisk", Py_BuildValue("k", hlFileGetSizeOnDisk(pSubItem)));
        }

        return infodict;

    }
    else
    {
        return NULL;
    }

}


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
	{"extract", (PyCFunction)Package_extract, METH_VARARGS, "Extract content"},
	{"validate", (PyCFunction)Package_validate, METH_VARARGS, "Validate content"},
	{"listdir", (PyCFunction)Package_listdir, METH_VARARGS, "List directory"},
	{"getsize", (PyCFunction)Package_getsize, METH_VARARGS, "Get size of an item"},
	{"getattributename", (PyCFunction)Package_getattributename, METH_VARARGS, "Get attribute name"},
	{"getattribute", (PyCFunction)Package_getattribute, METH_VARARGS, "Get attribute"},
	{"getitemtype", (PyCFunction)Package_getitemtype, METH_VARARGS, "Get item type"},
    {"info", (PyCFunction)Package_info, METH_VARARGS, "Returns info about a file or folder"},
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