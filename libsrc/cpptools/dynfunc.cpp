///////////////////////////////////////////////////////////////////////////////
// $Source: x:/prj/tech/libsrc/cpptools/RCS/dynfunc.cpp $
// $Author: TOML $
// $Date: 1996/10/21 14:14:52 $
// $Revision: 1.2 $
//
// (c) Copyright 1994-1996 Tom Leonard. All Rights Reserved. Unlimited license granted to Looking Glass Technologies Inc.
//

#ifdef _WIN32
#include <win32_platform.h>
#else
#include <dlfcn.h>
#endif

#include "dynfunc.h"

///////////////////////////////////////
BOOL cDynFunc::Load()
    {
    if (!fTriedToLoad)
        {
        fTriedToLoad = TRUE;
        // Assert(pszLibName && pszFuncSig);
#ifdef _WIN32
        hInstLib = (void *)LoadLibraryA(pszLibName);
#else
        hInstLib = dlopen(pszLibName, RTLD_NOW | RTLD_LOCAL);
#endif
        if (LoadedDLL(hInstLib))
            {
#ifdef _WIN32
            pfnFunc = (void *)GetProcAddress((HMODULE)hInstLib, pszFuncSig);
#else
            pfnFunc = dlsym(hInstLib, pszFuncSig);
#endif
            //DebugMsgTrue3(pfnFunc && HIWORD(pszFuncSig), "Loaded function %s from %s (%p)", pszFuncSig, pszLibName, pfnFunc);
            //DebugMsgTrue3(pfnFunc && !HIWORD(pszFuncSig), "Loaded function %d from %s (%p)", int(LOWORD(pszFuncSig)), pszLibName, pfnFunc);
            }
        else
            ;//DebugMsg1("Failed to load %s", pszLibName);
        }
    if (pfnFunc)
        return TRUE;
    if (pfnFail)
        pfnFunc = pfnFail;
    return FALSE;
    }

///////////////////////////////////////

cDynFunc::~cDynFunc()
    {
    Unload();
    }

///////////////////////////////////////

void cDynFunc::Unload()
    {
    pfnFunc = NULL;
    fTriedToLoad = FALSE;
    if (LoadedDLL(hInstLib))
        {
#ifdef _WIN32
        FreeLibrary((HMODULE)hInstLib);
#else
        dlclose(hInstLib);
#endif
        }
    hInstLib = 0;
    }

///////////////////////////////////////

void * cDynFunc::FindFunc()
    {
    if (pfnFunc)
        return pfnFunc;

    Load();

    return pfnFunc;
    }
