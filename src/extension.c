#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define UNICODE

#include <windows.h>
#include <tchar.h>
#include <stdbool.h>
#include <stdint.h>
#include <GuidDef.h>
#include <Shobjidl.h>
#include <Shlobj.h>

typedef uintmax_t uint;
typedef intmax_t sint;
typedef USHORT u16;
typedef unsigned char u8;

#define ODS(x) OutputDebugStringW(x)
//#define ODS(x) (void)0

/* GLOBALS */
#include "common.c"
#include "fs.c"

static bool fs_entries_walker(const u16* name, bool isDirectory, HMENU contentMenu, UINT* pNextCommandId){
    InsertMenu(contentMenu, -1, MF_BYPOSITION | MF_STRING, *pNextCommandId, name);
    *pNextCommandId = *pNextCommandId + 1;
    return true;
}


/* FORWARD DECLARATION EVERYTHING */
struct tag_IMyObj;

ULONG STDMETHODCALLTYPE myObj_AddRef(struct tag_IMyObj* pMyObj);
ULONG STDMETHODCALLTYPE myObj_Release(struct tag_IMyObj* pMyObj);
HRESULT STDMETHODCALLTYPE myObj_QueryInterface(struct tag_IMyObj* pMyObj, REFIID requestedIID, void **ppv);

/* ALL THE STRUCTS (because it's too much pain to have them in separate places) */
typedef struct tag_MyIContextMenuImpl {
    IContextMenuVtbl* lpVtbl;
    struct tag_IMyObj* pBase;
} MyIContextMenuImpl;

typedef struct tag_MyIShellExtInitImpl {
    IShellExtInitVtbl* lpVtbl;
    struct tag_IMyObj* pBase;
} MyIShellExtInitImpl;

typedef struct tag_IMyObj{
    IUnknownVtbl* lpVtbl;
    MyIContextMenuImpl contextMenuImpl;
    MyIShellExtInitImpl shellExtInitImpl;
    FSEntriesContainer container;
    u16 workingDirectory[MAX_PATH + 1];
    u16 searchBuffer[MAX_PATH + 1];
    HMENU hMenu;
    long int refsCount;
} IMyObj;



/******************************* IContextMenu ***************************/

HRESULT STDMETHODCALLTYPE myIContextMenuImpl_GetCommandString(MyIContextMenuImpl* pImpl, 
                                                              UINT_PTR idCmd, UINT uFlags, UINT* pwReserved,LPSTR pszName,UINT cchMax){
    if ( (uFlags == GCS_VERBW) || (uFlags == GCS_VALIDATEW)){
        ODS(L"GetCommandString S_OK");
        return S_OK;
    } else{
        ODS(L"GetCommandString S_FALSE");        
        return S_FALSE;
    }

    return E_INVALIDARG;
}


//I really hate person that causes me "undefined reference to `___chkstk_ms'" errors. And extra calls to clear the memory.

HRESULT STDMETHODCALLTYPE myIContextMenuImpl_InvokeCommand(MyIContextMenuImpl* pImpl, LPCMINVOKECOMMANDINFO pCommandInfo){
    IMyObj* pBase = pImpl->pBase;
    void* pVerb = pCommandInfo->lpVerb;

    if (HIWORD(pVerb)){
        ODS(L"VERB");
        return E_INVALIDARG;
    }

    uint itemIndex = (WORD)pVerb;
    const FSEntryIndexEntry currentIndexEntry = pBase->container.indexes[itemIndex];
    const u16* itemName = pBase->container.memory + currentIndexEntry.offset;

    u16 fullItemPath[MAX_PATH];
    SecureZeroMemory(fullItemPath, sizeof(fullItemPath));
    lstrcpyW(fullItemPath, pBase->container.rootDirectory);
    PathAppendW(fullItemPath, itemName);
    ODS(fullItemPath);
    ShellExecuteW(NULL, NULL, fullItemPath, NULL, pBase->workingDirectory, pCommandInfo->nShow);

    return S_OK;
}


static bool is_two_dots(u16* string){
    return string[0] == L'.' && string[1] == L'.' && string[2] == 0;
}


HRESULT STDMETHODCALLTYPE myIContextMenuImpl_QueryContextMenu(MyIContextMenuImpl* pImpl, 
                                                           HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags){
    IMyObj* pBase = pImpl->pBase;
    
    //if we failed to acquire worknig directory...
    if (pBase->workingDirectory[0] == 0){
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    //same with root directory
    if (pBase->container.rootDirectory[0] == 0){
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    if (CMF_DEFAULTONLY == (CMF_DEFAULTONLY & uFlags)
        || CMF_VERBSONLY == (CMF_VERBSONLY & uFlags)
        || CMF_NOVERBS == (CMF_NOVERBS & uFlags)){

        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    if (pBase->hMenu != NULL){
        DestroyMenu(pBase->hMenu);
        pBase->hMenu = NULL;
    }

    //walk the FS and collect entries
    
    WIN32_FIND_DATAW findData;
    HANDLE searchHandle = FindFirstFileExW(pBase->searchBuffer, FindExInfoBasic, &findData, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (INVALID_HANDLE_VALUE == searchHandle){
        if (ERROR_FILE_NOT_FOUND == GetLastError()){
            ODS(L"OpenHereContent is empty");
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        } else {
            ODS(L"FindFirstFileW failed");
            return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
        }
    }

    bool keepSearching = true;
    bool hasError = false;
    while (keepSearching){
        if (FindNextFileW(searchHandle, &findData)){
            //ignore directory junctions for now: care required to handle those without "endless" recursion
            const DWORD attributes = findData.dwFileAttributes;
            const bool isDirectory = ((attributes & FILE_ATTRIBUTE_DIRECTORY) == FILE_ATTRIBUTE_DIRECTORY);
            const bool isReparsePoint = ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == FILE_ATTRIBUTE_REPARSE_POINT);
            const bool isHidden = ((attributes & FILE_ATTRIBUTE_HIDDEN) == FILE_ATTRIBUTE_HIDDEN);
            if ( isDirectory && isReparsePoint){
                continue;
            }
            if (is_two_dots(findData.cFileName)){
                continue;
            }
                    
            if (isHidden){
                continue; //ignore hidden files and directories
            }

            if (! FSEntriesContainer_add(&pBase->container, findData.cFileName, isDirectory)){
                ODS(L"Failed to add item to the container");
                FSEntriesContainer_clear(&pBase->container);
                return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
            }
        } else {
            //what went wrong?
            if (ERROR_NO_MORE_FILES == GetLastError()){
                //that's ok
                ODS(L"Search space exhausted");
                keepSearching = false;
            } else {
                ODS(L"FindNextFileW failed");
                hasError = true;
                keepSearching = false;
            }
        }
    }


    if (! FindClose(searchHandle)){
        ODS(L"FindClose failed");
    }

    HMENU contentMenu = CreateMenu();
    if (contentMenu == NULL){
        ODS(L"CreateMenu failed");
        FSEntriesContainer_clear(&pBase->container);
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    UINT nextFreeCommandId = idCmdFirst;

    ODS(L"Walking...");
    FSEntriesContainer_walk(&pBase->container, &fs_entries_walker, contentMenu, &nextFreeCommandId);
    ODS(L"Walking is done");

    pBase->hMenu = contentMenu;


    //insert our menu items: separator, copy and move in that order.
    InsertMenu(hmenu, -1, MF_BYPOSITION | MF_SEPARATOR, nextFreeCommandId, NULL);
    InsertMenu(hmenu, -1, MF_BYPOSITION | MF_POPUP | MF_STRING, contentMenu, L"Open Here");

    ODS(L"QueryContextMenu");

    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, nextFreeCommandId + 1);
}


ULONG STDMETHODCALLTYPE myIContextMenuImpl_AddRef(MyIContextMenuImpl* pImpl){
    return myObj_AddRef(pImpl->pBase);
}

ULONG STDMETHODCALLTYPE myIContextMenuImpl_Release(MyIContextMenuImpl* pImpl){
    return myObj_Release(pImpl->pBase);
}

HRESULT STDMETHODCALLTYPE myIContextMenuImpl_QueryInterface(MyIContextMenuImpl* pImpl, REFIID requestedIID, void **ppv){
    if (ppv == NULL){
        return E_POINTER;
    }

    *ppv = NULL;

    if (IsEqualGUID(requestedIID, &IID_IUnknown)){
        *ppv = pImpl;
        myObj_AddRef(pImpl->pBase);
        return S_OK;
    }

    return myObj_QueryInterface(pImpl->pBase, requestedIID, ppv);

}

static IContextMenuVtbl IMyIContextMenuVtbl = {
    .QueryInterface = &myIContextMenuImpl_QueryInterface,
    .AddRef = &myIContextMenuImpl_AddRef,
    .Release = &myIContextMenuImpl_Release,
    .GetCommandString = &myIContextMenuImpl_GetCommandString,
    .QueryContextMenu = &myIContextMenuImpl_QueryContextMenu,
    .InvokeCommand = &myIContextMenuImpl_InvokeCommand
};



/************************ IShellExtInit **********************************/
HRESULT STDMETHODCALLTYPE myIShellExtInit_AddRef(MyIShellExtInitImpl* pImpl){
    return myObj_AddRef(pImpl->pBase);
}

ULONG STDMETHODCALLTYPE myIShellExtInit_Release(MyIShellExtInitImpl* pImpl){
    return myObj_Release(pImpl->pBase);
}

HRESULT STDMETHODCALLTYPE myIShellExtInit_QueryInterface(MyIShellExtInitImpl* pImpl, REFIID requestedIID, void **ppv){
    if (ppv == NULL){
        return E_POINTER;
    }

    *ppv = NULL;

    if (IsEqualGUID(requestedIID, &IID_IUnknown)){
        *ppv = pImpl;
        myObj_AddRef(pImpl->pBase);
        return S_OK;
    }

    return myObj_QueryInterface(pImpl->pBase, requestedIID, ppv);

}

HRESULT STDMETHODCALLTYPE myIShellExtInit_Initialize(MyIShellExtInitImpl* pImpl, LPCITEMIDLIST pIDFolder, IDataObject* pDataObj, HKEY hRegKey){
    SecureZeroMemory(pImpl->pBase->workingDirectory, sizeof(pImpl->pBase->workingDirectory));
    if (! SHGetPathFromIDListW(pIDFolder, pImpl->pBase->workingDirectory)){
        ODS(L"Failed to get path to working directory");
        return E_FAIL;
    };

    ODS(L"Initialize success");

    return S_OK;
}

static IShellExtInitVtbl IMyIShellExtVtbl = {
    .AddRef = &myIShellExtInit_AddRef,
    .Release = &myIShellExtInit_Release,
    .QueryInterface = &myIShellExtInit_QueryInterface,
    .Initialize = &myIShellExtInit_Initialize
};



/******************* IMyObj  *******************************/
static long int nObjectsAndRefs = 0;

ULONG STDMETHODCALLTYPE myObj_AddRef(IMyObj* pMyObj){
    InterlockedIncrement(&nObjectsAndRefs);

    InterlockedIncrement(&(pMyObj->refsCount));
    return pMyObj->refsCount;
}

ULONG STDMETHODCALLTYPE myObj_Release(IMyObj* pMyObj){
    InterlockedDecrement(&nObjectsAndRefs);

    InterlockedDecrement(&(pMyObj->refsCount));
    long int nRefs = pMyObj->refsCount;

    if (nRefs == 0){
        if (pMyObj->container.hHeap != NULL){
            HeapDestroy(pMyObj->container.hHeap);
            pMyObj->container.hHeap = NULL;
        }
        if (pMyObj->hMenu != NULL){
            DestroyMenu(pMyObj->hMenu);
            pMyObj->hMenu = NULL;
        }
        GlobalFree(pMyObj);
    }

    return nRefs;
}

HRESULT STDMETHODCALLTYPE myObj_QueryInterface(IMyObj* pMyObj, REFIID requestedIID, void **ppv){
    if (ppv == NULL){
        return E_POINTER;
    }
    *ppv = NULL;

    if (IsEqualGUID(requestedIID, &IID_IUnknown)){
        *ppv = pMyObj;
        myObj_AddRef(pMyObj);
        return S_OK;
    }

    if (IsEqualGUID(requestedIID, &IID_IContextMenu)){
        *ppv = &(pMyObj->contextMenuImpl);
        myObj_AddRef(pMyObj);

        return S_OK;
    }

    if (IsEqualGUID(requestedIID, &IID_IShellExtInit)){
        *ppv = &(pMyObj->shellExtInitImpl);
        myObj_AddRef(pMyObj);

        return S_OK;
    }


    return E_NOINTERFACE;
}

static IUnknownVtbl IMyObjVtbl = {
    .QueryInterface = &myObj_QueryInterface,
    .AddRef = &myObj_AddRef,
    .Release = &myObj_Release
};



/************** IClassFactory *****************************/
HRESULT STDMETHODCALLTYPE classCreateInstance(IClassFactory* pClassFactory, IUnknown* punkOuter, REFIID pRequestedIID, void** ppv){
    if (ppv == NULL){
        return E_POINTER;
    }

   // Assume an error by clearing caller's handle.
   *ppv = 0;

   // We don't support aggregation in IExample.
   if (punkOuter){
       return CLASS_E_NOAGGREGATION;
   }

   if (IsEqualGUID(pRequestedIID, &IID_IUnknown) 
       || IsEqualGUID(pRequestedIID, &IID_IContextMenu)
       || IsEqualGUID(pRequestedIID, &IID_IShellExtInit)
       ){
       IMyObj* pMyObj = GlobalAlloc(GPTR, sizeof(IMyObj)); //Zero the memory
       if (pMyObj == NULL){
           return E_OUTOFMEMORY;
       }

       HANDLE hHeap = HeapCreate(0, sizeof(FSEntriesContainer), 0);
       if (hHeap == NULL){
           ODS(L"Failed to create heap");
           return E_OUTOFMEMORY;
       }

       //Get path to "my documents"
       u16* folderPath = NULL;
       GUID documentsFolderId = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
       HRESULT ret = SHGetKnownFolderPath(&documentsFolderId, KF_FLAG_DEFAULT, NULL, &folderPath);
       if (S_OK == ret){
           ODS(folderPath);

           u16 tmpBuffer[MAX_PATH + 1];
           SecureZeroMemory(tmpBuffer, sizeof(tmpBuffer));
           lstrcpyW(tmpBuffer, folderPath);
           PathAppendW(tmpBuffer, L"OpenHereContent");
           CoTaskMemFree(folderPath);
           folderPath = NULL;

           if (! FSEntriesContainer_init(&pMyObj->container, hHeap, tmpBuffer)){
               ODS(L"Failed to create entries container");
               return E_OUTOFMEMORY;
           }

           SecureZeroMemory(pMyObj->searchBuffer, sizeof(pMyObj->searchBuffer));
           lstrcpyW(pMyObj->searchBuffer, tmpBuffer);
           //this is stupid, but hey such a life of a programmer
           if (! PathAppendW(pMyObj->searchBuffer, L"\\*")){
               ODS(L"Appending * failed");
               return E_OUTOFMEMORY;
           }

       } else {
           ODS(L"Failed to resolve My Documents path");
           return E_OUTOFMEMORY;
       }

       pMyObj->lpVtbl = &IMyObjVtbl;

       pMyObj->contextMenuImpl.lpVtbl = &IMyIContextMenuVtbl;
       pMyObj->contextMenuImpl.pBase = pMyObj;

       pMyObj->shellExtInitImpl.lpVtbl = &IMyIShellExtVtbl;
       pMyObj->shellExtInitImpl.pBase = pMyObj;

       return myObj_QueryInterface(pMyObj, pRequestedIID, ppv);
   }

   return E_NOINTERFACE;
}

ULONG STDMETHODCALLTYPE classAddRef(IClassFactory* pClassFactory){
   return 1;
}

ULONG STDMETHODCALLTYPE classRelease(IClassFactory* pClassFactory){
   return 1;
}

long int nLocks = 0;

HRESULT STDMETHODCALLTYPE classLockServer(IClassFactory* pClassFactory, BOOL flock){
    if (flock){
        InterlockedIncrement(&nLocks);
    } else {
        InterlockedDecrement(&nLocks);
    }

    return S_OK;
}

HRESULT STDMETHODCALLTYPE classQueryInterface(IClassFactory* pClassFactory, REFIID requestedIID, void **ppv){
   // Check if the GUID matches an IClassFactory or IUnknown GUID.
   if (! IsEqualGUID(requestedIID, &IID_IUnknown) && 
       ! IsEqualGUID(requestedIID, &IID_IClassFactory)){
      // It doesn't. Clear his handle, and return E_NOINTERFACE.
      *ppv = 0;
      return E_NOINTERFACE;
   }

   *ppv = pClassFactory;
    pClassFactory->lpVtbl->AddRef(pClassFactory);
   return S_OK;
}

static const IClassFactoryVtbl IClassFactory_Vtbl = {
    classQueryInterface,
    classAddRef,
    classRelease,
    classCreateInstance,
    classLockServer
};

//since we need only one ClassFactory ever:
static IClassFactory IClassFactoryObj = {
    .lpVtbl = &IClassFactory_Vtbl
};


/************************** DLL STUFF ****************************************/

/*
  COM subsystem will call this function in attempt to retrieve an implementation of an IID.
  Usually it's IClassFactory that is requested (but could be anything else?)
 */
__declspec(dllexport) HRESULT __stdcall DllGetClassObject(REFCLSID pCLSID, REFIID pIID, void** ppv){
    if (ppv == NULL){
        return E_POINTER;
    }

    *ppv = NULL;
    if (! IsEqualGUID(pCLSID, &SERVER_GUID)){
        //that's not our CLSID, ignore
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    if (! IsEqualGUID(pIID, &IID_IClassFactory)){
        //we only providing ClassFactories in this function
        return CLASS_E_CLASSNOTAVAILABLE;
    }

    //alright, return our implementation of IClassFactory
    *ppv = &IClassFactoryObj;
    return S_OK;
}

__declspec(dllexport) HRESULT PASCAL DllCanUnloadNow(){
    if (nLocks == 0 && nObjectsAndRefs == 0){
        return S_OK;
    }
    return S_FALSE;
}

bool WINAPI entry_point(HINSTANCE hInst, DWORD reason, void* pReserved){
    return true;
}
