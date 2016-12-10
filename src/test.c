#include "globals.h"

#include <Shlobj.h>
#include <Knownfolders.h>
#include <Objbase.h>
#include <Shlwapi.h>

#include "FSEntriesContainer.h"
#include "DirectoriesContainer.h"
#include "WorkQueue.h"

#define MAX_UNICODE_PATH_LENGTH 32767



static bool is_two_dots(u16* string){
    return string[0] == L'.' && string[1] == L'.' && string[2] == 0;
}

static bool process_directory(u16* directoryPath, FSEntriesContainer* result){
    u16 searchPathBuffer[MAX_PATH + 1];
    SecureZeroMemory(searchPathBuffer, sizeof(searchPathBuffer));
    lstrcpyW(searchPathBuffer, directoryPath);
    //this is stupid, but hey such a life of a programmer
    if (! PathAppendW(searchPathBuffer, L"\\*")){
        ODS(L"Appending * failed");
        return false;
    }

    WIN32_FIND_DATAW findData;
    HANDLE searchHandle = FindFirstFileExW(searchPathBuffer, FindExInfoBasic, &findData, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
    if (INVALID_HANDLE_VALUE == searchHandle){
        if (ERROR_FILE_NOT_FOUND == GetLastError()){
            ODS(L"Directory doesn't exists");
            return false;
        } else {
            ODS(L"FindFirstFileW failed");
            return false;
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
                    
            if (! FSEntriesContainer_add(result, findData.cFileName, isDirectory)){
                ODS(L"Failed to add item to the container");
                keepSearching = false;
                hasError = true;
            } else {
                ODS(findData.cFileName);
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

    return (! hasError);
}



static WorkQueueEntry currentWorkQueueEntry; //I HATE GCC
void entry_point(){
    HANDLE hHeap = HeapCreate(0, 0, 0);
    if (hHeap == NULL){
        ODS(L"Failed to create heap");
        goto exit;
    }
    //Get path to "my documents"
    u16* folderPath = NULL;
    GUID documentsFolderId = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
    HRESULT ret = SHGetKnownFolderPath(&documentsFolderId, KF_FLAG_DEFAULT, NULL, &folderPath);
    if (S_OK == ret){
        u16 currentlyProcessedDirectoryPath[MAX_PATH + 1];
            
        ODS(folderPath);

        SecureZeroMemory(currentlyProcessedDirectoryPath, sizeof(currentlyProcessedDirectoryPath));
        lstrcpyW(currentlyProcessedDirectoryPath, folderPath);
        CoTaskMemFree(folderPath);
        folderPath = NULL;

        if (PathAppendW(currentlyProcessedDirectoryPath, L"OpenHereContent")){
            if (! CreateDirectoryW(currentlyProcessedDirectoryPath, NULL)){
                if (GetLastError() != ERROR_ALREADY_EXISTS){
                    ODS(L"My Documents do not exists");
                    goto exit;
                }

                DirectoriesContainer directories;
                if (! DirectoriesContainer_init(&directories, hHeap)){
                    ODS(L"DirectoriesContainer_init failed");
                    goto exit;
                }

                WorkQueue workQueue;
                if (! WorkQueue_init(&workQueue, hHeap)){
                    ODS(L"Failed to init working queue");
                    goto exit;
                }


                SecureZeroMemory(&currentWorkQueueEntry, sizeof(WorkQueueEntry));
                lstrcpyW(currentWorkQueueEntry.fullPath, currentlyProcessedDirectoryPath);
                currentWorkQueueEntry.parentIndex = -1;
                

                if (! WorkQueue_enqueue(&workQueue, currentWorkQueueEntry)){
                    ODS(L"Failed to enqueue currentWorkQueueEntry");
                    goto exit;
                }

                SecureZeroMemory(currentlyProcessedDirectoryPath, sizeof(currentlyProcessedDirectoryPath));

                while (WorkQueue_dequeue(&workQueue, &currentWorkQueueEntry)){
                    ODS(L"Processing:"); ODS(currentWorkQueueEntry.fullPath);
                    FSEntriesContainer container;
                    if (! FSEntriesContainer_init(&container, hHeap, currentWorkQueueEntry.fullPath)){
                        ODS(L"Failed to initialize FS Entries container");
                        goto exit;
                    }

                    container.parentIndex = currentWorkQueueEntry.parentIndex;
                    container.indexInParent = currentWorkQueueEntry.indexInParent;

                    if (! process_directory(currentWorkQueueEntry.fullPath, &container)){
                        ODS(L"Something went wrong processing following directory:"); ODS(currentWorkQueueEntry.fullPath);
                        goto exit;
                    };

                    //if there are subdirectories, add them to the queue
                    for (uint i = 0; i < container.nDirectories; i += 1){
                        WorkQueueEntry subDirectoryEntry;
                        SecureZeroMemory(&subDirectoryEntry, sizeof(WorkQueueEntry));
                        subDirectoryEntry.parentIndex = directories.nEntries - 1;
                        subDirectoryEntry.indexInParent = i;

                        const u16* subDirectoryName = container.memory + container.indexes[i].offset;
                        lstrcpyW(subDirectoryEntry.fullPath, currentWorkQueueEntry.fullPath);
                        PathAppendW(subDirectoryEntry.fullPath, subDirectoryName);

                        if (! WorkQueue_enqueue(&workQueue, subDirectoryEntry)){
                            ODS(L"Failed to enqueue a path:"); ODS(subDirectoryEntry.fullPath);
                            goto exit;
                        };
                    }

                    if (! DirectoriesContainer_add(&directories, container)){
                        ODS(L"DirectoriesContainer_add failed");
                        goto exit;
                    }

                    SecureZeroMemory(currentlyProcessedDirectoryPath, sizeof(currentlyProcessedDirectoryPath));
                }


                for (uint i = 0; i < directories.nEntries; i += 1){
                    FSEntriesContainer directory = directories.data[i];
                    ODS(L"Walking:"); ODS(directory.rootDirectory);
                }
                ODS(L"Walking is done");
            }


        } else {
            ODS(L"PathAppendW failed");
        };
    } else {
        ODS(L"SHGetKnownFolderPath failed");
    }

 exit:
    ExitProcess(0);
}
