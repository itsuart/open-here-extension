#include "FSEntriesContainer.h"

static const uint initialCapacity = 20;

bool FSEntriesContainer_init(FSEntriesContainer* pContainer, HANDLE hHeap, WideStringContainerIndex rootDirectory){
    FSEntry* entries = (FSEntry*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity * sizeof(FSEntry));
    if (entries == NULL){
        return false;
    }


    pContainer->hHeap = hHeap;

    pContainer->nameIndex = rootDirectory;

    pContainer->nEntries = 0;
    pContainer->nDirectories = 0;
    pContainer->capacity = initialCapacity;
    pContainer->entries = entries;

    pContainer->parentIndex = -1; //by default assume that this is a root directory
    pContainer->indexInParent = 0;
    
    return true;
}

void FSEntriesContainer_clear(FSEntriesContainer* pContainer){ //TODO: wtf is this? should be removed
    SecureZeroMemory(pContainer->entries, pContainer->nEntries * sizeof(FSEntry));

    pContainer->nEntries = 0;
    pContainer->nDirectories = 0;
    pContainer->parentIndex = -1;
    pContainer->indexInParent = 0;
}

void FSEntriesContainer_term(FSEntriesContainer* pContainer){
    FSEntriesContainer_clear(pContainer);
    HeapFree(pContainer->hHeap, 0, pContainer->entries);
}

bool FSEntriesContainer_add(FSEntriesContainer* pContainer, FSEntry entry){
    if (pContainer->nEntries == pContainer->capacity){
        const uint newCapacity = pContainer->capacity + 10;
        FSEntry* newEntries = HeapReAlloc(pContainer->hHeap, HEAP_ZERO_MEMORY, pContainer->entries, newCapacity * sizeof(FSEntry));
        if (newEntries == NULL){
            ODS(L"Failed to reallocate memory for entries");
            return false;
        }

        pContainer->entries = newEntries;
        pContainer->capacity = newCapacity;
    }

   
    pContainer->entries[pContainer->nEntries] = entry;
    pContainer->nEntries += 1;
 
    if (entry.isDirectory){
        //collect all the directories at the beginning
        if (pContainer->nEntries == 1){
            //it's very first entry, no need to reshuffle
        } else {
            if (pContainer->entries[pContainer->nEntries - 2].isDirectory){
                //if previous entry is also a directory - do nothing
            } else {
                uint indexToSwap = 0;
                for (sint i = pContainer->nEntries - 2; i >= 0; i-= 1){
                    if (pContainer->entries[i].isDirectory){
                        indexToSwap = i + 1; //swap with next non-directory entry
                        break;
                    }
                }

                FSEntry oldEntry = pContainer->entries[indexToSwap];
                pContainer->entries[indexToSwap] = pContainer->entries[pContainer->nEntries - 1];
                pContainer->entries[pContainer->nEntries - 1] = oldEntry;
            }
        }
        
        pContainer->nDirectories += 1;
    }

    return true;
}
