#include "HMenuStorage.h"

static const uint initialCapacity = 20;

bool HMenuStorage_init(HMenuStorage* pStorage, HANDLE hHeap){
    HMENU* entries = (HMENU*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity * sizeof(HMENU));
    if (entries == NULL){
        return false;
    }

    pStorage->hHeap = hHeap;
    pStorage->entries = entries;
    pStorage->nEntries = 0;
    pStorage->capacity = initialCapacity;
    
    return true;
}

void HMenuStorage_clear(HMenuStorage* pStorage){
    for (uint i = 0; i < pStorage->nEntries; i += 1){
        DestroyMenu(pStorage->entries[i]);
    }
    SecureZeroMemory(pStorage->entries, pStorage->capacity * sizeof(HMENU));
    pStorage->nEntries = 0;
}

bool HMenuStorage_add(HMenuStorage* pStorage, HMENU menu, uint* pIndex){
    if (pStorage->nEntries == pStorage->capacity){
        const uint newCapacity = pStorage->capacity + 10;
        HMENU* newEntries = (HMENU*) HeapReAlloc(pStorage->hHeap, HEAP_ZERO_MEMORY, pStorage->entries,
                                                 newCapacity * sizeof(HMENU));
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

