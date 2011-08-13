// The following ifdef block is the standard way of creating macros which make exporting 
// from a DLL simpler. All files within this DLL are compiled with the PYTHONHLLIB_EXPORTS
// symbol defined on the command line. This symbol should not be defined on any project
// that uses this DLL. This way any other project whose source files include this file see 
// PYTHONHLLIB_API functions as being imported from a DLL, whereas this DLL sees symbols
// defined with this macro as being exported.
#ifdef PYTHONHLLIB_EXPORTS
#define PYTHONHLLIB_API __declspec(dllexport)
#else
#define PYTHONHLLIB_API __declspec(dllimport)
#endif

// This class is exported from the python-hllib.dll
class PYTHONHLLIB_API Cpythonhllib {
public:
	Cpythonhllib(void);
	// TODO: add your methods here.
};

extern PYTHONHLLIB_API int npythonhllib;

PYTHONHLLIB_API int fnpythonhllib(void);
