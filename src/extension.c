#include "globals.h"

#include <GuidDef.h>
#include <Shobjidl.h>
#include <Shlobj.h>
#include <Shlwapi.h>
#include <ObjIdl.h>

#include "FSEntriesContainer.h"
#include "DirectoriesContainer.h"
#include "WorkQueue.h"
#include "WideStringContainer.h"
#include "HMenuStorage.h"
#include "MenuCommandsMapping.h"
#include "HBitmapStorage.h"

/* GLOBALS */
#include "common.c"

#include "com.h"

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

    long int refsCount;

    u16 workingDirectory[MAX_PATH + 1];
    u16 rootDirectory[MAX_PATH + 1];

    WideStringContainer strings;
    WorkQueue workQueue;
    DirectoriesContainer directories;

    HBitmapStorage bitmaps;
    HMenuStorage menus;
    MenuCommandsMapping commandsMapping;
    u16* displayNamesBuffer;
    HANDLE hHeap;
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

static void shell_execute(const u16* fullItemPath, const u16* workingDirectory, int showCmd){
    const bool shiftIsDown = (1 << 15) & (GetAsyncKeyState(VK_SHIFT));
    const wchar_t* verb = shiftIsDown ? L"runAs" : L"open";

    ShellExecuteW(NULL, verb, fullItemPath, NULL, workingDirectory, showCmd);
}

static void execute_item(const u16* fullItemPath, const u16* workingDirectory, int showCmd){
    //See if it is a link file, and if it is, see if it has working directory set.
    //If it is, then we copy link file to a temporary location and set it's working directory to one supplied to the function.
    //Then we tell Windows to execute that, deleting the copy after.
    //I'm doing this to not juggle with expansion of environment variables. Let Windows do that for us.
    do {
        IShellLinkW* pIShellLink = NULL;

        HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void**)&pIShellLink);
        if (hr != S_OK) break;

        IPersistFile* pIPersistFile = NULL;
        hr = COM_CALL(pIShellLink, QueryInterface, &IID_IPersistFile, (void**) &pIPersistFile);
        if (! SUCCEEDED(hr)){
            COM_CALL0(pIShellLink, Release);
            break;
        }

        hr = COM_CALL(pIPersistFile, Load, fullItemPath, 0);
        if (! SUCCEEDED(hr)){
            COM_RELEASE(pIPersistFile);
            COM_RELEASE(pIShellLink);
            break;
        }

        static u16 linkTargetWorkingDirectory[MAX_UNICODE_PATH_LENGTH] = {0};
        SecureZeroMemory(linkTargetWorkingDirectory, sizeof(linkTargetWorkingDirectory));

        hr = COM_CALL(pIShellLink, GetWorkingDirectory, linkTargetWorkingDirectory, MAX_UNICODE_PATH_LENGTH);
        if (! SUCCEEDED(hr)){
            COM_RELEASE(pIShellLink);
            break;
        }

        if (linkTargetWorkingDirectory[0] != 0){
            //working directory is set, we will respect that.
            COM_RELEASE(pIPersistFile);
            COM_RELEASE(pIShellLink);
            break;
        }

        //link working directory is empty, let us update it with our value.
        hr = COM_CALL(pIShellLink, SetWorkingDirectory, workingDirectory);
        if (! SUCCEEDED(hr)){
            COM_RELEASE(pIPersistFile);
            COM_RELEASE(pIShellLink);
            break;
        }

        //TODO: can we reuse linkTargetWorkingDirectory here?
        static u16 tempLinkPath[MAX_UNICODE_PATH_LENGTH] = {0};
        SecureZeroMemory(tempLinkPath, sizeof(tempLinkPath));

        {
            const uint max_size = sizeof(tempLinkPath) / sizeof(tempLinkPath[0]);
            const uint pathLength = GetTempPathW(max_size, tempLinkPath);
            if (pathLength == 0 || pathLength > max_size){
                COM_RELEASE(pIPersistFile);
                COM_RELEASE(pIShellLink);
                break;
            }

            uint result = GetTempFileNameW(tempLinkPath, L"oh", 0, tempLinkPath);
            if (result == 0){
                COM_RELEASE(pIPersistFile);
                COM_RELEASE(pIShellLink);
                break;
            }

            //GetTempFileNameW creates file, so
            DeleteFileW(tempLinkPath);

            u16* pExtension = PathFindExtension(tempLinkPath);
            if (*pExtension == 0){
                //failed for some reason
                COM_RELEASE(pIPersistFile);
                COM_RELEASE(pIShellLink);
                break;
            }
            //pExtension (should) point to '.', so

            pExtension[1] = L'L';
            pExtension[2] = L'N';
            pExtension[3] = L'K';

        }

        hr = COM_CALL(pIPersistFile, Save, tempLinkPath, true);
        if (! SUCCEEDED(hr)){
            COM_RELEASE(pIPersistFile);
            COM_RELEASE(pIShellLink);
            break;
        }
        hr = COM_CALL(pIPersistFile, SaveCompleted, tempLinkPath);
        if (! SUCCEEDED(hr)){
            COM_RELEASE(pIPersistFile);
            COM_RELEASE(pIShellLink);
            break;
        }

        //we are done with COM
        COM_RELEASE(pIPersistFile);
        COM_RELEASE(pIShellLink);

        //execute the thing
        shell_execute(tempLinkPath, workingDirectory, showCmd);

        //delete temp link
        DeleteFileW(tempLinkPath);
        return;
    } while(0);

    //if we are here - it is not a link file or it is, but it's working directory is set by user, so execute it as is
    shell_execute(fullItemPath, workingDirectory, showCmd);
}

//I really hate person that causes me "undefined reference to `___chkstk_ms'" errors. And extra calls to clear the memory.

HRESULT STDMETHODCALLTYPE myIContextMenuImpl_InvokeCommand(MyIContextMenuImpl* pImpl, LPCMINVOKECOMMANDINFO pCommandInfo){
    void* pVerb = pCommandInfo->lpVerb;

    if (HIWORD(pVerb)){
        return E_INVALIDARG;
    }
    IMyObj* pBase = pImpl->pBase;
    uint itemIndex = (WORD)pVerb;

    if (pBase->menus.nEntries == 0){
        //we didn't create any menus. This only can happen if root directory was devoid of displayable content
        //so, open it up in Explorer
        ShellExecuteW(NULL, L"explore", pBase->rootDirectory, NULL, NULL, SW_SHOW);
        return S_OK;
    }

    if (itemIndex >= pBase->commandsMapping.nEntries){
        return E_INVALIDARG;
    }

    MappingEntry commandMapping= pBase->commandsMapping.entries[itemIndex];
    u16 fullItemPath[MAX_PATH];
    SecureZeroMemory(fullItemPath, sizeof(fullItemPath));
    //TODO: erorr check
    WideStringContainer_copy(&pBase->strings, commandMapping.directoryPathIndex, fullItemPath);
    u16* itemName;
    //TODO: error check
    WideStringContainer_getStringPtr(&pBase->strings, commandMapping.itemNameIndex, &itemName);
    PathAppendW(fullItemPath, itemName);

    execute_item(fullItemPath, pBase->workingDirectory, pCommandInfo->nShow);

    return S_OK;
}


static bool is_two_dots(u16* string){
    return string[0] == L'.' && string[1] == L'.' && string[2] == 0;
}

static bool process_directory(WideStringContainer* strings, WideStringContainerIndex directoryNameIndex
                              , FSEntriesContainer* result){
    u16 searchPathBuffer[MAX_PATH + 1];
    SecureZeroMemory(searchPathBuffer, sizeof(searchPathBuffer));
    if (! WideStringContainer_copy(strings, directoryNameIndex, searchPathBuffer)){
        ODS(L"Failed to get processed directory name from the string");
        return false;
    }

    //this is stupid, but hey such a life of a programmer
    if (! PathAppendW(searchPathBuffer, L"\\*")){
        ODS(L"Appending * failed");
        return false;
    }

    WIN32_FIND_DATAW findData;
    HANDLE searchHandle = FindFirstFileExW(searchPathBuffer, FindExInfoBasic, &findData, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (INVALID_HANDLE_VALUE == searchHandle){
        const DWORD lastError = GetLastError();
        switch (lastError){
           case ERROR_ACCESS_DENIED:{
               //it's ok
               return true;
           } break;
           case ERROR_FILE_NOT_FOUND:{
               ODS(L"Directory doesn't exists:"); ODS(searchPathBuffer);
               return false;
           } break;
           default:{
               ODS(L"FindFirstFileW failed");
               return false;
           }
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
                continue;
            }

            WideStringContainerIndex nameIndex;
            if (! WideStringContainer_add(strings, findData.cFileName, &nameIndex)){
                hasError = true;
                ODS(L"Failed to add string to the collection"); ODS(findData.cFileName);
                break;
            }

            FSEntry entry = {.isDirectory = isDirectory, .nameIndex = nameIndex};
            if (! FSEntriesContainer_add(result, entry)){
                ODS(L"Failed to add item to the container");
                keepSearching = false;
                hasError = true;
            }
        } else {
            //what went wrong?
            if (ERROR_NO_MORE_FILES == GetLastError()){
                //that's ok
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

    return (! hasError);
}

static bool collect_content(IMyObj* pBase){
    //see if root directory exists
    if (! CreateDirectoryW(pBase->rootDirectory, NULL)){
        if (GetLastError() != ERROR_ALREADY_EXISTS){
            //Well, what can you do? Absolutely nothing
            return false;
        }
    }

    //initialize or work queue with root directory
    WideStringContainerIndex rootDirectoryPathIndex;
    if (! WideStringContainer_add(&pBase->strings, pBase->rootDirectory, &rootDirectoryPathIndex)){
        ODS(L"Failed to add rootDirectoryPath to strings");
        return false;
    }

    WorkQueueEntry rootWorkQueueEntry = {
        .fullPathIndex = rootDirectoryPathIndex,
        .parentIndex = -1
    };

    if (! WorkQueue_enqueue(&pBase->workQueue, rootWorkQueueEntry)){
        ODS(L"Failed to enqueue root directory for processing");
        return false;
    }

    //collect the content
    WorkQueueEntry currentWorkQueueEntry = {.parentIndex = -1, .indexInParent = 0};
    while (WorkQueue_dequeue(&pBase->workQueue, &currentWorkQueueEntry)){

        FSEntriesContainer currentDirectoryContent;
        if (! FSEntriesContainer_init(&currentDirectoryContent, pBase->hHeap, currentWorkQueueEntry.fullPathIndex)){
            ODS(L"Failed to initialize FS Entries container");
            return false;
        }

        currentDirectoryContent.parentIndex = currentWorkQueueEntry.parentIndex;
        currentDirectoryContent.indexInParent = currentWorkQueueEntry.indexInParent;

        if (! process_directory(&pBase->strings, currentWorkQueueEntry.fullPathIndex, &currentDirectoryContent)){
            u16* currentlyProcessedDirectoryPath;
            WideStringContainer_getStringPtr(&pBase->strings, currentWorkQueueEntry.fullPathIndex,
                                         &currentlyProcessedDirectoryPath);

            ODS(L"Something went wrong processing following directory:"); ODS(currentlyProcessedDirectoryPath);
            return false;
        };

        if (! DirectoriesContainer_add(&pBase->directories, currentDirectoryContent)){
            ODS(L"DirectoriesContainer_add failed");
            return false;
        }

        //we have to get that path here because process_directory may add enough entries to cause reallocation in the WideStringContainer
        u16* currentlyProcessedDirectoryPath;
        WideStringContainer_getStringPtr(&pBase->strings, currentWorkQueueEntry.fullPathIndex, &currentlyProcessedDirectoryPath);


        //if there are subdirectories, add them to the queue
        for (uint i = 0; i < currentDirectoryContent.nDirectories; i += 1){
            const FSEntry subDirectoryEntry = currentDirectoryContent.entries[i];

            u16* subDirectoryName;
            if (! WideStringContainer_getStringPtr(&pBase->strings, subDirectoryEntry.nameIndex, &subDirectoryName)){
                ODS(L"Major fuckup");
                return false;
            }

            u16 subDirectoryFullPath[MAX_PATH + 1];
            SecureZeroMemory(subDirectoryFullPath, sizeof(subDirectoryFullPath));
            lstrcpyW(subDirectoryFullPath, currentlyProcessedDirectoryPath);
            PathAppendW(subDirectoryFullPath, subDirectoryName); //TODO: error check

            WideStringContainerIndex subDirectoryFullPathIndex;
            if (! WideStringContainer_add(&pBase->strings, subDirectoryFullPath, &subDirectoryFullPathIndex)){
                ODS(L"Another major fuck up");
                return false;
            }

            WorkQueueEntry subDirectoryQueueEntry;
            SecureZeroMemory(&subDirectoryQueueEntry, sizeof(WorkQueueEntry));

            subDirectoryQueueEntry.parentIndex = pBase->directories.nEntries - 1;
            subDirectoryQueueEntry.indexInParent = i;
            subDirectoryQueueEntry.fullPathIndex = subDirectoryFullPathIndex;

            if (! WorkQueue_enqueue(&pBase->workQueue, subDirectoryQueueEntry)){
                ODS(L"Failed to enqueue a path:"); ODS(subDirectoryFullPath);
                return false;
            };
        }
    }

    return true;
}

static bool create_menus(IMyObj* pBase, uint idCmdFirst, uint* pNextFreeCommandId){
    //TODO: make this more memory efficient
    pBase->displayNamesBuffer = (u16*)HeapAlloc(pBase->hHeap, HEAP_ZERO_MEMORY, pBase->strings.nEntries * (MAX_PATH + 1) * sizeof(u16));
    if (pBase->displayNamesBuffer == NULL){
        ODS(L"Failed to allocate displayNamesBuffer");
        return false;
    }
    u16* currentDisplayName = pBase->displayNamesBuffer;

    uint nextFreeCommandId = idCmdFirst;

    //go backwards and build up our menus
    for (sint i = pBase->directories.nEntries - 1; i >= 0; i -= 1){
        FSEntriesContainer currentDirectoryContents = pBase->directories.data[i];
        if (currentDirectoryContents.nEntries == 0){
            //directory is empty
            if (currentDirectoryContents.parentIndex >= 0){
                pBase->directories
                    .data[currentDirectoryContents.parentIndex]
                    .entries[currentDirectoryContents.indexInParent]
                    .isEmptyDirectory = true;
            }
            continue;
        }

        //we have some entries in this directory, let put them into a menu
        HMENU menu = CreateMenu();
        if (menu == NULL){
            //TODO: do some proper error handling and reporting here
            ODS(L"Failed to create menu");
            HeapFree(pBase->hHeap, 0, pBase->displayNamesBuffer);
            pBase->displayNamesBuffer = NULL;
            return false;
        }

        uint menuIndex = 0;
        HMenuStorage_add(&pBase->menus, menu, &menuIndex);

        if (currentDirectoryContents.parentIndex >= 0){
            pBase->directories
                .data[currentDirectoryContents.parentIndex]
                .entries[currentDirectoryContents.indexInParent]
                .subMenuIndex = menuIndex;
        }

        for (uint fsEntryIndex = 0; fsEntryIndex < currentDirectoryContents.nEntries; fsEntryIndex += 1){
            FSEntry currentFSEntry = currentDirectoryContents.entries[fsEntryIndex];

            u16* currentEntryName = NULL;
            if (! WideStringContainer_getStringPtr(&pBase->strings, currentFSEntry.nameIndex, &currentEntryName)){
                ODS(L"Oh wow");
                HeapFree(pBase->hHeap, 0, pBase->displayNamesBuffer);
                pBase->displayNamesBuffer = NULL;
                return false;
            };

            HBITMAP menuImage = NULL;

            { //extract icon if exists
                u16 fullEntryPath [MAX_PATH + 1];
                SecureZeroMemory(fullEntryPath, sizeof(fullEntryPath));
                WideStringContainer_copy(&pBase->strings, currentDirectoryContents.nameIndex, fullEntryPath);
                PathAppendW(fullEntryPath, currentEntryName);

                SHFILEINFOW fileInfo = {0};
                //
                if ( SHGetFileInfoW(fullEntryPath, 0, &fileInfo, sizeof(fileInfo), SHGFI_ICON  | SHGFI_SMALLICON | SHGFI_DISPLAYNAME  /*| SHGFI_ADDOVERLAYS */)){
                    if (currentDisplayName != NULL){
                        lstrcpyW(currentDisplayName, fileInfo.szDisplayName);
                    }

                    //                    ODS(fileInfo.szDisplayName);
                    ICONINFO iconInfo = {0};
                    if ( GetIconInfo(fileInfo.hIcon, &iconInfo)){
                        DeleteObject(iconInfo.hbmMask);

                        menuImage = CopyImage(iconInfo.hbmColor, IMAGE_BITMAP, 0, 0, LR_COPYDELETEORG | LR_CREATEDIBSECTION);
                        HBitmapStorage_add(&pBase->bitmaps, menuImage, NULL);
                    }
                    DestroyIcon(fileInfo.hIcon);

                    //TODO: do something about displayName: we can't modify global WideStringStorage at this point.
                } else {
                    ODS(L"THE FUCK");
                    currentDisplayName = NULL;
                }
            }

            u16* effectiveDisplayName = currentDisplayName != NULL ? currentDisplayName : currentEntryName;

            if (currentFSEntry.isDirectory){
                if (currentFSEntry.isEmptyDirectory){

                    if (menuImage != NULL){
                        MENUITEMINFOW menuItemInfo = {0};
                        menuItemInfo.cbSize = sizeof(menuItemInfo);
                        menuItemInfo.fMask = MIIM_BITMAP | MIIM_STRING | MIIM_ID;
                        menuItemInfo.wID = nextFreeCommandId;
                        menuItemInfo.hbmpItem = menuImage;
                        menuItemInfo.dwTypeData = effectiveDisplayName;
                        InsertMenuItemW(menu, -1, true, &menuItemInfo);
                    } else {
                        InsertMenuW(menu, -1, MF_BYPOSITION | MF_STRING, nextFreeCommandId, effectiveDisplayName);
                    }

                    MappingEntry mappingEntry = {
                        .directoryPathIndex = currentDirectoryContents.nameIndex,
                        .itemNameIndex = currentFSEntry.nameIndex
                    };
                    //TODO: error check
                    MenuCommandsMapping_add(&pBase->commandsMapping, mappingEntry, NULL);

                    nextFreeCommandId += 1;
                } else {
                    //non-empty directory
                    if (menuImage != NULL){
                        MENUITEMINFOW menuItemInfo = {0};
                        menuItemInfo.cbSize = sizeof(menuItemInfo);
                        menuItemInfo.fMask = MIIM_BITMAP | MIIM_STRING | MIIM_SUBMENU;
                        menuItemInfo.hSubMenu = pBase->menus.entries[currentFSEntry.subMenuIndex];
                        menuItemInfo.hbmpItem = menuImage;
                        menuItemInfo.dwTypeData = effectiveDisplayName;
                        InsertMenuItemW(menu, -1, true, &menuItemInfo);
                    } else {
                        InsertMenuW(menu, -1, MF_BYPOSITION | MF_STRING | MF_POPUP, pBase->menus.entries[currentFSEntry.subMenuIndex], effectiveDisplayName);
                    }
                }
            } else {
                //NOT DIRECTORY

                if (menuImage != NULL){
                    MENUITEMINFOW menuItemInfo = {0};
                    menuItemInfo.cbSize = sizeof(menuItemInfo);
                    menuItemInfo.fMask = MIIM_BITMAP | MIIM_STRING | MIIM_ID;
                    menuItemInfo.wID = nextFreeCommandId;
                    menuItemInfo.hbmpItem = menuImage;
                    menuItemInfo.dwTypeData = effectiveDisplayName;
                    InsertMenuItemW(menu, -1, true, &menuItemInfo);
                } else {
                    InsertMenuW(menu, -1, MF_BYPOSITION | MF_STRING, nextFreeCommandId, effectiveDisplayName);
                }


                MappingEntry mappingEntry = {
                    .directoryPathIndex = currentDirectoryContents.nameIndex,
                    .itemNameIndex = currentFSEntry.nameIndex
                };
                //TODO: error check this too
                MenuCommandsMapping_add(&pBase->commandsMapping, mappingEntry, NULL);

                nextFreeCommandId += 1;
            }
        }
        currentDisplayName += (MAX_PATH + 1);

    }

    *pNextFreeCommandId = nextFreeCommandId;
    return true;
}


HRESULT STDMETHODCALLTYPE myIContextMenuImpl_QueryContextMenu(MyIContextMenuImpl* pImpl,
                                                           HMENU hmenu, UINT indexMenu, UINT idCmdFirst, UINT idCmdLast, UINT uFlags){
    ODS(L"QueryContextMenu START");
    IMyObj* pBase = pImpl->pBase;

    //if we failed to acquire worknig directory...
    if (pBase->workingDirectory[0] == 0){
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    //same with root directory
    if (pBase->rootDirectory[0] == 0){
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    if (CMF_DEFAULTONLY == (CMF_DEFAULTONLY & uFlags)
        || CMF_VERBSONLY == (CMF_VERBSONLY & uFlags)
        || CMF_NOVERBS == (CMF_NOVERBS & uFlags)){

        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    //clear all previous data if any
    HBitmapStorage_clear(&pBase->bitmaps);
    HMenuStorage_clear(&pBase->menus);
    MenuCommandsMapping_clear(&pBase->commandsMapping);
    WideStringContainer_clear(&pBase->strings);
    DirectoriesContainer_clear(&pBase->directories);
    WorkQueue_clear(&pBase->workQueue);
    if (pBase->displayNamesBuffer != NULL){
        HeapFree(pBase->hHeap, 0, pBase->displayNamesBuffer);
        pBase->displayNamesBuffer = NULL;
    }


    //return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);

    ODS(L"Collecting content");

    if (! collect_content(pBase)){
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    ODS(L"Creating menus");
    uint nextFreeCommandId = idCmdFirst;
    if (! create_menus(pBase, idCmdFirst, &nextFreeCommandId)){
        return MAKE_HRESULT(SEVERITY_SUCCESS, 0, 0);
    }

    if (pBase->menus.nEntries == 0){
        //root directory was empty
        InsertMenu(hmenu, -1, MF_BYPOSITION | MF_SEPARATOR, nextFreeCommandId, NULL);
        nextFreeCommandId += 1;
        InsertMenu(hmenu, -1, MF_BYPOSITION | MF_STRING, nextFreeCommandId, L"Open OpenHereContent");
    } else {
        //Since we going backwards, last entry in our menu storage is our content menu
        HMENU contentMenu = pBase->menus.entries[pBase->menus.nEntries - 1];

        //OK, now create root menu and link it with content menu
        InsertMenu(hmenu, -1, MF_BYPOSITION | MF_SEPARATOR, nextFreeCommandId, NULL);
        InsertMenu(hmenu, -1, MF_BYPOSITION | MF_POPUP | MF_STRING, contentMenu, L"Open Here");

    }

    ODS(L"QueryContextMenu END");

    return MAKE_HRESULT(SEVERITY_SUCCESS, 0, nextFreeCommandId - idCmdFirst + 1);
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
    ODS(L"Initialize");
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

    InterlockedIncrement(&pMyObj->refsCount);
    return pMyObj->refsCount;
}

ULONG STDMETHODCALLTYPE myObj_Release(IMyObj* pMyObj){
    InterlockedDecrement(&nObjectsAndRefs);

    InterlockedDecrement(&(pMyObj->refsCount));
    long int nRefs = pMyObj->refsCount;

    if (nRefs == 0){
        HMenuStorage_clear(&pMyObj->menus);
        HBitmapStorage_clear(&pMyObj->bitmaps);

        if (pMyObj->hHeap != NULL){
            HeapDestroy(pMyObj->hHeap);
            pMyObj->hHeap = NULL;
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

       HANDLE hHeap = HeapCreate(0, 0, 0);
       if (hHeap == NULL){
           ODS(L"Failed to create heap");
           return E_OUTOFMEMORY;
       }

       if (! WideStringContainer_init(&pMyObj->strings, hHeap)){
           ODS(L"Failed to initialize strings container");
           HeapDestroy(hHeap);
           return E_OUTOFMEMORY;
       }

       if (! WorkQueue_init(&pMyObj->workQueue, hHeap)){
           ODS(L"Failed to initialize working queue");
           HeapDestroy(hHeap);
           return E_OUTOFMEMORY;
       }

       if (! DirectoriesContainer_init(&pMyObj->directories, hHeap)){
           ODS(L"Failed to initialize directories container");
           HeapDestroy(hHeap);
           return E_OUTOFMEMORY;
       }

       if (! HMenuStorage_init(&pMyObj->menus, hHeap)){
           ODS(L"Failed to initialize HMenuStorage");
           HeapDestroy(hHeap);
           return E_OUTOFMEMORY;
       }

       if (! MenuCommandsMapping_init(&pMyObj->commandsMapping, hHeap)){
           ODS(L"Failed to initialize MenuCommandsMapping");
           HeapDestroy(hHeap);
           return E_OUTOFMEMORY;
       }

       if (! HBitmapStorage_init(&pMyObj->bitmaps, hHeap)){
           ODS(L"Failed to initialize HBitmapStorage");
       }

       //Get path to "my documents"
       u16* folderPath = NULL;
       GUID documentsFolderId = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
       HRESULT ret = SHGetKnownFolderPath(&documentsFolderId, KF_FLAG_DEFAULT, NULL, &folderPath);
       if (S_OK == ret){
           ODS(folderPath);

           SecureZeroMemory(pMyObj->rootDirectory, sizeof(pMyObj->rootDirectory));

           lstrcpyW(pMyObj->rootDirectory, folderPath);
           CoTaskMemFree(folderPath);

           if (! PathAppendW(pMyObj->rootDirectory, L"OpenHereContent")){
               ODS(L"Failed to append 'OpenHereContent' to form root path");
               HeapDestroy(hHeap);
               return E_OUTOFMEMORY;
           }

       } else {
           ODS(L"Failed to resolve My Documents path");
           HeapDestroy(hHeap);
           return E_OUTOFMEMORY;
       }

       pMyObj->hHeap = hHeap;

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
