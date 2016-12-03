#define _WIN32_WINNT _WIN32_WINNT_WIN7
#define UNICODE

#include <stdbool.h>
#include <stdint.h>
#include <windows.h>
#include <Shlobj.h>
#include <Knownfolders.h>
#include <Objbase.h>
#include <Shlwapi.h>

typedef uintmax_t uint;
typedef intmax_t sint;
typedef USHORT u16;

#define ODS(x) OutputDebugStringW(x)
#define MAX_UNICODE_PATH_LENGTH 32767

typedef struct tag_FSEntryIndexEntry { //TODO: name sucks
    uint offset; //in bytes to first char of a string
    bool isDirectory; //TODO: not perfect memory usage (63 bits wasted)
} FSEntryIndexEntry;

typedef struct tag_FSEntriesContainer {
    HANDLE hHeap;
    uint nEntries;
    uint capacity;
    u16* memory;
    uint nextFreeOffset;
    uint maxChars;
    FSEntryIndexEntry* indexes;
} FSEntriesContainer;


static bool FSEntriesContainer_init(FSEntriesContainer* pContainer, HANDLE hHeap){
    const uint initialMaxChars = 1024;
    u16* memory = (u16*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialMaxChars * sizeof(u16));
    if (memory == NULL){
        ODS(L"Failed to allocate initial memory for the FS Entries");
        return false;
    };

    const uint initialCapacity = 20;
    void* indexesMemory = HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity * sizeof(FSEntryIndexEntry));
    if (indexesMemory == NULL){
        ODS(L"Failed to allocate initial memory for FS Entries index");
        return false;
    }

    pContainer->hHeap = hHeap;

    pContainer->nEntries = 0;
    pContainer->capacity = initialCapacity;
    pContainer->indexes = (FSEntryIndexEntry*)indexesMemory;
    
    pContainer->memory = memory;
    pContainer->nextFreeOffset = 0;
    pContainer->maxChars = initialMaxChars;

    return true;
}

static bool FSEntriesContainer_add(FSEntriesContainer* pContainer, u16* fullPath, bool isDirectory){
    if (pContainer->nEntries == pContainer->capacity){
        const uint newCapacity = pContainer->capacity * 2; //not sure it's wise in our case. maybe +10 or something would be better. TODO: read on max number of fs enries in directory in NTFS
        //NOTE: no need to check for capacity to overflow. We will run out of memory long before that could happen
        FSEntryIndexEntry* newIndexesMemory = HeapReAlloc(pContainer->hHeap, HEAP_ZERO_MEMORY, pContainer->indexes, newCapacity * sizeof(FSEntryIndexEntry));
        if (newIndexesMemory == NULL){
            ODS(L"Failed to reallocate memory for indexes");
            return false;
        }

        pContainer->indexes = newIndexesMemory;
        pContainer->capacity = newCapacity;
    }

   
    FSEntryIndexEntry newEntry = {.isDirectory = isDirectory, .offset = pContainer->nextFreeOffset};
    pContainer->indexes[pContainer->nEntries] = newEntry;
    pContainer->nEntries += 1;
 
    uint fullPathOffset = 0;
    bool keepLooping = true;
    while (keepLooping){
        if (pContainer->maxChars == pContainer->nextFreeOffset){
            //we exhausted currently allocated memory for the strings, reallocate
            const uint newMaxChars = pContainer->maxChars * 2; //again it might be overkill
            u16* newMemory = (u16*)HeapReAlloc(pContainer->hHeap, HEAP_ZERO_MEMORY, pContainer->memory, newMaxChars * sizeof(u16));
            if (newMemory == NULL){
                ODS(L"Failed to realloc memory for strings");
                pContainer->nEntries -= 1;
                return false;
            }

            pContainer->maxChars = newMaxChars;
            pContainer->memory = newMemory;
        }

        const u16 currentPathChar = fullPath[fullPathOffset];
        pContainer->memory[pContainer->nextFreeOffset] = currentPathChar;

        pContainer->nextFreeOffset += 1;
        fullPathOffset += 1;

        if (currentPathChar == 0){
            keepLooping = false;
        }
    }

    if (isDirectory){
        //collect all the directories at the beginning
        if (pContainer->nEntries == 1){
            //it's very first entry, no need to reshuffle
        } else {
            if (pContainer->indexes[pContainer->nEntries - 2].isDirectory){
                //if previous entry is also a directory - do nothing
            } else {
                uint indexToSwap = 0;
                for (sint i = pContainer->nEntries - 2; i >= 0; i-= 1){
                    if (pContainer->indexes[i].isDirectory){
                        indexToSwap = i + 1; //swap with next non-directory entry
                        break;
                    }
                }

                FSEntryIndexEntry oldEntry = pContainer->indexes[indexToSwap];
                pContainer->indexes[indexToSwap] = pContainer->indexes[pContainer->nEntries - 1];
                pContainer->indexes[pContainer->nEntries - 1] = oldEntry;
            }
        }
    }

    return true;
}

typedef bool FSEntriesWalker(const u16* fullPath, bool isDirectory);

static void FSEntriesContainer_walk(FSEntriesContainer* pContainer, FSEntriesWalker* callback){
    for (uint i = 0; i < pContainer->nEntries; i+= 1){
        const FSEntryIndexEntry currentIndexEntry = pContainer->indexes[i];
        const bool keepWalking = callback(pContainer->memory + currentIndexEntry.offset, currentIndexEntry.isDirectory);
        if (! keepWalking){
            break;
        }
    }
}

static bool fs_enries_walker(const u16* fullPath, bool isDirectory){
    ODS(fullPath);
    ODS(isDirectory ? L" and it is a directory" : L" and it is a file");
    return true;
}

static bool is_two_dots(u16* string){
    return string[0] == L'.' && string[1] == L'.' && string[2] == 0;
}

static u16 rootDirectoryPathBuffer[MAX_UNICODE_PATH_LENGTH + 1]; //TODO: to actually be able to use long unicode paths, you have to prepend \\?\ or do windows 10 anniversary edition tricks
static u16 itemFullPathBuffer[MAX_UNICODE_PATH_LENGTH + 1]; //TODO: the same
static u16 searchPathBuffer[MAX_UNICODE_PATH_LENGTH + 1];
static FSEntriesContainer container;
void entry_point(){
    if (MAX_PATH > 300){
        ODS(L"Unicode path is long");
    } else {
        ODS(L"Unicode path is short");
    }
    
    //Get path to "my documents"
    u16* folderPath = NULL;
    GUID documentsFolderId = {0xFDD39AD0, 0x238F, 0x46AF, {0xAD, 0xB4, 0x6C, 0x85, 0x48, 0x03, 0x69, 0xC7}};
    HRESULT ret = SHGetKnownFolderPath(&documentsFolderId, KF_FLAG_DEFAULT, NULL, &folderPath);
    if (S_OK == ret){
        ODS(folderPath);

        SecureZeroMemory(rootDirectoryPathBuffer, sizeof(rootDirectoryPathBuffer));
        lstrcpyW(rootDirectoryPathBuffer, folderPath);
        CoTaskMemFree(folderPath);
        folderPath = NULL;

        if (PathAppendW(rootDirectoryPathBuffer, L"OpenHereContent")){
            if (! CreateDirectoryW(rootDirectoryPathBuffer, NULL)){
                if (GetLastError() != ERROR_ALREADY_EXISTS){
                    ODS(L"My Documents do not exists");
                    goto exit;
                }
            }

            SecureZeroMemory(searchPathBuffer, sizeof(searchPathBuffer));
            lstrcpyW(searchPathBuffer, rootDirectoryPathBuffer);
            //this is stupid, but hey such a life of a programmer
            if (! PathAppendW(searchPathBuffer, L"\\*")){
                ODS(L"Appending * failed");
                goto exit;
            }

            //initialize the container
            HANDLE hContainerHeap = HeapCreate(0, sizeof(FSEntriesContainer), 0);
            if (hContainerHeap == NULL){
                ODS(L"Filed to create heap for FS entries container");
                goto exit;
            }
            if (! FSEntriesContainer_init(&container, hContainerHeap)){
                ODS(L"Failed to initialize FS Entries container");
                goto exit;
            }
            
            WIN32_FIND_DATAW findData;
            HANDLE searchHandle = FindFirstFileExW(searchPathBuffer, FindExInfoBasic, &findData, FindExSearchNameMatch, NULL, FIND_FIRST_EX_LARGE_FETCH);
            if (INVALID_HANDLE_VALUE == searchHandle){
                if (ERROR_FILE_NOT_FOUND == GetLastError()){
                    ODS(L"OpenHereContent is empty");
                    goto exit;
                } else {
                    ODS(L"FindFirstFileW failed");
                    goto exit;
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
                    if ( isDirectory && isReparsePoint){
                        continue;
}
                    if (is_two_dots(findData.cFileName)){
                        continue;
                    }
                    //TODO: this is not efficient
                    SecureZeroMemory(itemFullPathBuffer, sizeof(itemFullPathBuffer));
                    lstrcpyW(itemFullPathBuffer, rootDirectoryPathBuffer);
                    PathAppendW(itemFullPathBuffer, findData.cFileName);
                    ODS(itemFullPathBuffer);

                    
                    if (! FSEntriesContainer_add(&container, itemFullPathBuffer, isDirectory)){
                        ODS(L"Failed to add item to the container");
                        goto exit;
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

            ODS(L"Walking...");
            FSEntriesContainer_walk(&container, &fs_enries_walker);
            ODS(L"Walking is done");

        } else {
            ODS(L"PathAppendW failed");
        };
    } else {
        ODS(L"SHGetKnownFolderPath failed");
    }

 exit:
    ExitProcess(0);
}
