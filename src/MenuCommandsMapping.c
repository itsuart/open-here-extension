#include "MenuCommandsMapping.h"

static const uint initialCapacity = 20;

bool MenuCommandsMapping_init(MenuCommandsMapping* pMapping, HANDLE hHeap){
    MappingEntry* entries = (MappingEntry*)HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity * sizeof(MappingEntry));
    if (entries == NULL){
        return false;
    }

    pMapping->hHeap = hHeap;
    pMapping->nEntries = 0;
    pMapping->capacity = initialCapacity;
    pMapping->entries = entries;

    return true;
}

bool MenuCommandsMapping_add (MenuCommandsMapping* pMapping, MappingEntry entry, uint* pIndex){
    if (pMapping->capacity == pMapping->nEntries){
        uint newCapacity = pMapping->capacity + 10;
        MappingEntry* newEntries = (MappingEntry*) HeapReAlloc(pMapping->hHeap, HEAP_ZERO_MEMORY, pMapping->entries, newCapacity * sizeof(MappingEntry));
        if (newEntries == NULL){
            return false;
        }

        pMapping->capacity = newCapacity;
        pMapping->entries = newEntries;
    }

    pMapping->entries[pMapping->nEntries] = entry;
    if (pIndex != NULL){
        *pIndex = pMapping->nEntries;
    }
    pMapping->nEntries += 1;

    return true;
}

void MenuCommandsMapping_clear(MenuCommandsMapping* pMapping){
    SecureZeroMemory(pMapping->entries, pMapping->capacity * sizeof(MappingEntry));
    pMapping->nEntries = 0;
}
