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

//had to copy-paste this from https://opensource.apple.com/source/xnu/xnu-2050.18.24/libsyscall/wrappers/memcpy.c
//because gcc included implicit calls to memcpy

/*
 * sizeof(word) MUST BE A POWER OF TWO
 * SO THAT wmask BELOW IS ALL ONES
 */
typedef	int word;		/* "word" used for optimal copy speed */

#define	wsize	sizeof(word)
#define	wmask	(wsize - 1)

void * memcpy(void *dst0, const void *src0, size_t length)
{
	char *dst = dst0;
	const char *src = src0;
	size_t t;
	
	if (length == 0 || dst == src)		/* nothing to do */
		goto done;
	
	/*
	 * Macros: loop-t-times; and loop-t-times, t>0
	 */
#define	TLOOP(s) if (t) TLOOP1(s)
#define	TLOOP1(s) do { s; } while (--t)
	
	if ((unsigned long)dst < (unsigned long)src) {
		/*
		 * Copy forward.
		 */
		t = (uintptr_t)src;	/* only need low bits */
		if ((t | (uintptr_t)dst) & wmask) {
			/*
			 * Try to align operands.  This cannot be done
			 * unless the low bits match.
			 */
			if ((t ^ (uintptr_t)dst) & wmask || length < wsize)
				t = length;
			else
				t = wsize - (t & wmask);
			length -= t;
			TLOOP1(*dst++ = *src++);
		}
		/*
		 * Copy whole words, then mop up any trailing bytes.
		 */
		t = length / wsize;
		TLOOP(*(word *)dst = *(word *)src; src += wsize; dst += wsize);
		t = length & wmask;
		TLOOP(*dst++ = *src++);
	} else {
		/*
		 * Copy backwards.  Otherwise essentially the same.
		 * Alignment works as before, except that it takes
		 * (t&wmask) bytes to align, not wsize-(t&wmask).
		 */
		src += length;
		dst += length;
		t = (uintptr_t)src;
		if ((t | (uintptr_t)dst) & wmask) {
			if ((t ^ (uintptr_t)dst) & wmask || length <= wsize)
				t = length;
			else
				t &= wmask;
			length -= t;
			TLOOP1(*--dst = *--src);
		}
		t = length / wsize;
		TLOOP(src -= wsize; dst -= wsize; *(word *)dst = *(word *)src);
		t = length & wmask;
		TLOOP(*--dst = *--src);
	}
done:
	return (dst0);
}


#define ODS(x) OutputDebugStringW(x)
#define MAX_UNICODE_PATH_LENGTH 32767

typedef struct tag_FSEntryIndexEntry { //TODO: name sucks
    uint offset; //in characters (u16) to first char of a string
    bool isDirectory; //TODO: not perfect memory usage (63 bits wasted)
} FSEntryIndexEntry;

typedef struct tag_FSEntriesContainer {
    HANDLE hHeap;
    u16 rootDirectory[MAX_PATH + 1];
    uint nEntries;
    uint nDirectories;
    uint capacity;
    u16* memory;
    uint nextFreeOffset;
    uint maxChars;
    FSEntryIndexEntry* indexes;
    uint indexInParent;
    sint parentIndex;
} FSEntriesContainer;



static bool FSEntriesContainer_init(FSEntriesContainer* pContainer, HANDLE hHeap, u16* rootDirectory){
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

    SecureZeroMemory(pContainer->rootDirectory, sizeof(pContainer->rootDirectory));
    lstrcpyW(pContainer->rootDirectory, rootDirectory);

    pContainer->nEntries = 0;
    pContainer->nDirectories = 0;
    pContainer->capacity = initialCapacity;
    pContainer->indexes = (FSEntryIndexEntry*)indexesMemory;
    
    pContainer->memory = memory;
    pContainer->nextFreeOffset = 0;
    pContainer->maxChars = initialMaxChars;

    pContainer->parentIndex = -1; //by default assume that this is a root directory
    pContainer->indexInParent = 0;
    
    return true;
}

static void FSEntriesContainer_clear(FSEntriesContainer* pContainer){
    SecureZeroMemory(pContainer->indexes, pContainer->nEntries * sizeof(FSEntryIndexEntry));
    SecureZeroMemory(pContainer->memory, pContainer->maxChars * sizeof(u16));

    pContainer->nEntries = 0;
    pContainer->nDirectories = 0;
    pContainer->nextFreeOffset = 0;

}

static void FSEntriesContainer_term(FSEntriesContainer* pContainer){
    FSEntriesContainer_clear(pContainer);
    HeapFree(pContainer->hHeap, 0, pContainer->indexes);
    HeapFree(pContainer->hHeap, 0, pContainer->memory);
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
    while (keepLooping){ //BUG: this could still cause buffer overflow due path being longer than reallocated space!
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
        
        pContainer->nDirectories += 1;
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
    return true;
}


typedef struct tag_DirectoriesContainer {
    HANDLE hHeap;
    FSEntriesContainer* data;
    uint nEntries;
    uint capacity;
} DirectoriesContainer;

static bool DirectoriesContainer_init(DirectoriesContainer* result, HANDLE hHeap){
    const uint initialCapacity = 20;

    FSEntriesContainer* data = (FSEntriesContainer*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity * sizeof(FSEntriesContainer));
    if (data == NULL){
        return false;
    }

    result->hHeap = hHeap;
    result->data = data;
    result->nEntries = 0;
    result->capacity = initialCapacity;

    return true;
}

static bool DirectoriesContainer_add(DirectoriesContainer* pContainer, FSEntriesContainer directory){
    if (pContainer->nEntries == pContainer->capacity){

        const uint newCapacity = pContainer->capacity * 2;
        if (newCapacity <= pContainer->capacity){
            return false; 
        }

        FSEntriesContainer* newData = (FSEntriesContainer*)
            HeapReAlloc(pContainer->hHeap, HEAP_ZERO_MEMORY, pContainer->data, newCapacity * sizeof(FSEntriesContainer));
        if (newData == NULL){
            return false;
        }
    }

    pContainer->data[pContainer->nEntries] = directory;
    pContainer->nEntries += 1;

    return true;
}

static void DirectoriesContainer_clear(DirectoriesContainer* pContainer){
    // unfortunately I have to term all my content here to prevent memory leak
    for (uint i = 0; i < pContainer->nEntries; i += 1){
        FSEntriesContainer* termedContainer = pContainer->data + i;
        FSEntriesContainer_term(termedContainer);
    }
    
    SecureZeroMemory(pContainer->data, pContainer->capacity * sizeof(DirectoriesContainer));
    pContainer->nEntries = 0;
}


typedef struct tag_WorkQueueEntry {
    u16 fullPath[MAX_PATH + 1];
    sint parentIndex;
    uint indexInParent;
} WorkQueueEntry;

typedef struct tag_WorkQueue {
    HANDLE hHeap;
    WorkQueueEntry* memory;
    uint nextReadIndex;
    uint nextWriteIndex;
    uint capacity;
} WorkQueue;

static bool WorkQueue_init (WorkQueue* pQueue, HANDLE hHeap){
    const uint initialCapacity = 20;
    u16* memory = (u16*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity *  sizeof(WorkQueueEntry));
    if (memory == NULL){
        return false;
    }

    pQueue->hHeap = hHeap;
    pQueue->memory = memory;
    pQueue->nextReadIndex = 0;
    pQueue->nextWriteIndex = 0;
    pQueue->capacity = initialCapacity;

    return true;
}

static void WorkQueue_clear (WorkQueue* pQueue){
    SecureZeroMemory(pQueue->memory, pQueue->capacity * sizeof(WorkQueueEntry));
    pQueue->nextReadIndex = 0;
    pQueue->nextWriteIndex = 0;
}

static bool WorkQueue_enqueue (WorkQueue* pQueue, WorkQueueEntry entry){
    if (pQueue->capacity == pQueue->nextWriteIndex){
        const uint newCapacity = pQueue->capacity + 10;
        u16* newMemory = (u16*) HeapReAlloc(pQueue->hHeap, HEAP_ZERO_MEMORY, 
                                            pQueue->memory, newCapacity * sizeof(WorkQueueEntry));
        if (newMemory == NULL){
            return false;
        }

        pQueue->capacity = newCapacity;
        pQueue->memory = newMemory;
    }

    pQueue->memory[pQueue->nextWriteIndex] = entry;
    pQueue->nextWriteIndex += 1;

    return true;
}


static bool WorkQueue_dequeue (WorkQueue* pQueue, WorkQueueEntry* value){
    if (pQueue->nextReadIndex == pQueue->nextWriteIndex){
        //we don't have anything new to dequeue
        return false;
    }

    *value = pQueue->memory[pQueue->nextReadIndex];
    pQueue->nextReadIndex += 1;

    return true;
}


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
                    FSEntriesContainer_walk(&directory, &fs_enries_walker);
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
