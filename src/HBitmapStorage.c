#include "HBitmapStorage.h"

static const uint initialCapacity = 20;

bool HBitmapStorage_init(HBitmapStorage* pStorage, HANDLE hHeap){
    HBITMAP* entries = (HBITMAP*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity * sizeof(HBITMAP));
    if (entries == NULL){
        return false;
    }

    pStorage->hHeap = hHeap;
    pStorage->entries = entries;
    pStorage->nEntries = 0;
    pStorage->capacity = initialCapacity;
    
    return true;
}

void HBitmapStorage_clear(HBitmapStorage* pStorage){
    for (uint i = 0; i < pStorage->nEntries; i += 1){
        DeleteObject(pStorage->entries[i]);
    }
    SecureZeroMemory(pStorage->entries, pStorage->capacity * sizeof(HBITMAP));
    pStorage->nEntries = 0;
}

bool HBitmapStorage_add(HBitmapStorage* pStorage, HBITMAP menu, uint* pIndex){
    if (pStorage->nEntries == pStorage->capacity){
        const uint newCapacity = pStorage->capacity + 10;
        HBITMAP* newEntries = (HBITMAP*) HeapReAlloc(pStorage->hHeap, HEAP_ZERO_MEMORY, pStorage->entries,
                                                 newCapacity * sizeof(HBITMAP));
        if (newEntries == NULL){
            return false;
        }
        
        pStorage->capacity = newCapacity;
        pStorage->entries = newEntries;
    }

    pStorage->entries[pStorage->nEntries] = menu;
    if (pIndex != NULL){
        *pIndex = pStorage->nEntries;
    }
    pStorage->nEntries += 1;
    return true;
}

