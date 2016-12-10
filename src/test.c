#include "globals.h"

#include <Shlobj.h>
#include <Knownfolders.h>
#include <Objbase.h>
#include <Shlwapi.h>

#include "FSEntriesContainer.h"
#include "DirectoriesContainer.h"
#include "WorkQueue.h"
#include "WideStringContainer.h"


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
        u16 rootDirectoryPath[MAX_PATH + 1];
            
        ODS(folderPath);

        SecureZeroMemory(rootDirectoryPath, sizeof(rootDirectoryPath));
        lstrcpyW(rootDirectoryPath, folderPath);
        CoTaskMemFree(folderPath);
        folderPath = NULL;

        if (PathAppendW(rootDirectoryPath, L"OpenHereContent")){
            if (! CreateDirectoryW(rootDirectoryPath, NULL)){
                if (GetLastError() != ERROR_ALREADY_EXISTS){
                    ODS(L"My Documents do not exists");
                    goto exit;
                }

                WideStringContainer strings;
                if (! WideStringContainer_init(&strings, hHeap)){
                    ODS(L"Failed to initialize strings container");
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


                WideStringContainerIndex rootDirectoryPathIndex;
                if (! WideStringContainer_add(&strings, rootDirectoryPath, &rootDirectoryPathIndex)){
                    ODS(L"Failed to add rootDirectoryPath to strings");
                    goto exit;
                }

                WorkQueueEntry currentWorkQueueEntry;
                SecureZeroMemory(&currentWorkQueueEntry, sizeof(WorkQueueEntry));
                currentWorkQueueEntry.fullPathIndex = rootDirectoryPathIndex;
                currentWorkQueueEntry.parentIndex = -1;
                

                if (! WorkQueue_enqueue(&workQueue, currentWorkQueueEntry)){
                    ODS(L"Failed to enqueue currentWorkQueueEntry");
                    goto exit;
                }

                SecureZeroMemory(rootDirectoryPath, sizeof(rootDirectoryPath));
                
                while (WorkQueue_dequeue(&workQueue, &currentWorkQueueEntry)){
                    u16* currentlyProcessedDirectoryPath;
                    WideStringContainer_getStringPtr(&strings, currentWorkQueueEntry.fullPathIndex, &currentlyProcessedDirectoryPath);
                    ODS(L"Processing:"); ODS(currentlyProcessedDirectoryPath);

                    
                    FSEntriesContainer container;
                    if (! FSEntriesContainer_init(&container, hHeap, currentWorkQueueEntry.fullPathIndex)){
                        ODS(L"Failed to initialize FS Entries container");
                        goto exit;
                    }

                    container.parentIndex = currentWorkQueueEntry.parentIndex;
                    container.indexInParent = currentWorkQueueEntry.indexInParent;

                    if (! process_directory(&strings, currentWorkQueueEntry.fullPathIndex, &container)){
                        ODS(L"Something went wrong processing following directory:"); ODS(currentlyProcessedDirectoryPath);
                        goto exit;
                    };

                    //if there are subdirectories, add them to the queue
                    for (uint i = 0; i < container.nDirectories; i += 1){
                        const FSEntry subDirectoryEntry = container.entries[i];

                        u16* subDirectoryName;
                        if (! WideStringContainer_getStringPtr(&strings, subDirectoryEntry.nameIndex, &subDirectoryName)){
                            ODS(L"Major fuckup");
                            goto exit;
                        }
                        
                        u16 subDirectoryFullPath[MAX_PATH + 1];
                        SecureZeroMemory(subDirectoryFullPath, sizeof(subDirectoryFullPath));
                        lstrcpyW(subDirectoryFullPath, currentlyProcessedDirectoryPath);
                        PathAppendW(subDirectoryFullPath, subDirectoryName); //TODO: error check

                        WideStringContainerIndex subDirectoryFullPathIndex;
                        if (! WideStringContainer_add(&strings, subDirectoryFullPath, &subDirectoryFullPathIndex)){
                            ODS(L"Another major fuck up");
                            goto exit;
                        }

                        WorkQueueEntry subDirectoryQueueEntry;
                        SecureZeroMemory(&subDirectoryQueueEntry, sizeof(WorkQueueEntry));

                        subDirectoryQueueEntry.parentIndex = directories.nEntries - 1;
                        subDirectoryQueueEntry.indexInParent = i;
                        subDirectoryQueueEntry.fullPathIndex = subDirectoryFullPathIndex;

                        if (! WorkQueue_enqueue(&workQueue, subDirectoryQueueEntry)){
                            ODS(L"Failed to enqueue a path:"); ODS(subDirectoryFullPath);
                            goto exit;
                        };
                    }

                    if (! DirectoriesContainer_add(&directories, container)){
                        ODS(L"DirectoriesContainer_add failed");
                        goto exit;
                    }

                }


                ODS(L"All the strings:");
                for (uint i = 0; i < strings.nEntries; i += 1){
                    u16* string;
                    WideStringContainer_getStringPtr(&strings, i, &string);
                    ODS(string);
                }
                ODS(L"Done");
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
