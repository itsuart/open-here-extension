typedef struct tag_FSEntryIndexEntry { //TODO: name sucks
    uint offset; //in bytes to first char of a string
    bool isDirectory; //TODO: not perfect memory usage (63 bits wasted)
} FSEntryIndexEntry;

typedef struct tag_FSEntriesContainer {
    HANDLE hHeap;
    u16 rootDirectory[MAX_PATH + 1];
    uint nEntries;
    uint capacity;
    u16* memory;
    uint nextFreeOffset;
    uint maxChars;
    FSEntryIndexEntry* indexes;
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
    pContainer->capacity = initialCapacity;
    pContainer->indexes = (FSEntryIndexEntry*)indexesMemory;
    
    pContainer->memory = memory;
    pContainer->nextFreeOffset = 0;
    pContainer->maxChars = initialMaxChars;

    return true;
}

static void FSEntriesContainer_clear(FSEntriesContainer* pContainer){
    SecureZeroMemory(pContainer->indexes, pContainer->nEntries * sizeof(FSEntryIndexEntry));
    SecureZeroMemory(pContainer->memory, pContainer->maxChars * sizeof(u16));

    pContainer->nEntries = 0;
    pContainer->nextFreeOffset = 0;
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

typedef bool FSEntriesWalker(const u16* name, bool isDirectory, HMENU contentMenu, UINT* pNextCommandId);

static void FSEntriesContainer_walk(FSEntriesContainer* pContainer, FSEntriesWalker* callback, HMENU contentMenu, UINT* pNextCommandId){
    for (uint i = 0; i < pContainer->nEntries; i+= 1){
        const FSEntryIndexEntry currentIndexEntry = pContainer->indexes[i];
        const bool keepWalking = callback(pContainer->memory + currentIndexEntry.offset, currentIndexEntry.isDirectory, contentMenu, pNextCommandId);
        if (! keepWalking){
            break;
        }
    }
}
