// Copyright (c) 2011 seb26. All rights reserved.
// Source code is licensed under the terms of the Modified BSD License.

#include "C:\Python26\include\Python.h"
#include "StdAfx.h"

#ifdef _WIN32
#	define WIN32_LEAN_AND_MEAN
#	include <windows.h>
#else
#	include <linux/limits.h>
#	define MAX_PATH PATH_MAX
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



static PyObject *
    newPackage(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    packageObj *self;

    self = (packageObj *)type->tp_alloc(type, 0);

    if (self != NULL)
    {
        // Defaults.
        self->lpPackage = 0;
        self->lpNCFRootPath = 0;

        self->pFile = 0;

        self->bFileMapping = hlFalse;
        self->bQuickFileMapping = hlFalse;
        self->bVolatileAccess = hlFalse;

        self->ePackageType = HL_PACKAGE_NONE;
        self->uiPackage = HL_ID_INVALID;
        self->uiMode = HL_MODE_INVALID;

        self->bSilent = 0;
    }

    return (PyObject *)self;

}


static int
    initPackage(packageObj *self, PyObject *args, PyObject *kwds)
{
    static char *kwlist[] = { "packageName", "volatile", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s|i", kwlist,
        &self->lpPackage, &volatile))
    {
        return -1;
    }

    self->bVolatileAccess = volatile;

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
static PyObject *
    extractPackage(packageObj *self, PyObject *args)
{

	hlUInt uiExtractItems = 0;
	hlChar *lpExtractItems[MAX_ITEMS];
	hlChar *lpDestination = 0;;

	HLDirectoryItem *pItem = 0;

	pUpdateFunc = Py_None;

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

    Py_INCREF(Py_None);
	return Py_None;

}