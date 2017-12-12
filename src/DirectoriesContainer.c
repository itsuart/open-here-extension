#include "DirectoriesContainer.h"

bool DirectoriesContainer_init(DirectoriesContainer* result, HANDLE hHeap){
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

bool DirectoriesContainer_add(DirectoriesContainer* pContainer, FSEntriesContainer directory){
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

        pContainer->data = newData;
        pContainer->capacity = newCapacity;
    }

    pContainer->data[pContainer->nEntries] = directory;
    pContainer->nEntries += 1;

    return true;
}

void DirectoriesContainer_clear(DirectoriesContainer* pContainer){
    // unfortunately I have to term all my content here to prevent memory leak
    for (uint i = 0; i < pContainer->nEntries; i += 1){
        FSEntriesContainer* termedContainer = pContainer->data + i;
        FSEntriesContainer_term(termedContainer);
    }

    SecureZeroMemory(pContainer->data, pContainer->capacity * sizeof(DirectoriesContainer));
    pContainer->nEntries = 0;
}
