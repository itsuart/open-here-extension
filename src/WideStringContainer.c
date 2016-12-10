#include "WideStringContainer.h"

static const uint initialMemCapacity = 512; //1K initial memory
static const uint initialEntriesCapacity = 20; //20 strings

bool WideStringContainer_init(WideStringContainer* pContainer, HANDLE hHeap){
    u16* memory = (u16*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialMemCapacity * sizeof(u16));
    if (memory == NULL){
        return false;
    }

    uint* offsets = (uint*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialEntriesCapacity * sizeof(uint));
    if (offsets == NULL){
        HeapFree(hHeap, 0, memory);
        return false;
    }

    SecureZeroMemory(pContainer, sizeof(WideStringContainer));

    pContainer->hHeap = hHeap;

    pContainer->memoryCapacity = initialMemCapacity;
    pContainer->memory = memory;

    pContainer->entriesCapacity = initialEntriesCapacity;
    pContainer->offsets = offsets;
    
    pContainer->nextFreeOffset = 0;
    pContainer->nEntries = 0;

    return true;
}


bool WideStringContainer_add(WideStringContainer* pContainer, u16* string, uint* pCopyIndex){
    if (pContainer->nEntries == pContainer->entriesCapacity){
        uint newEntriesCapacity = pContainer->entriesCapacity + 10;
        uint* newOffsets = HeapReAlloc(pContainer->hHeap, HEAP_ZERO_MEMORY, 
                                       pContainer->offsets, newEntriesCapacity * sizeof(uint));
        if (newOffsets == NULL){
            return false;
        }

        pContainer->offsets = newOffsets;
        pContainer->entriesCapacity = newEntriesCapacity;
    }

    uint startingOffset = pContainer->nextFreeOffset;
    uint currentOffset = startingOffset;
    
    while (true){
        if (currentOffset >= pContainer->memoryCapacity){
            //time to realloc the stuff!
            uint newMemoryCapacity = pContainer->memoryCapacity * 2;
            u16* newMemory = (u16*) HeapReAlloc(pContainer->hHeap, HEAP_ZERO_MEMORY,
                                                pContainer->memory, newMemoryCapacity * sizeof(u16));
            if (newMemory == NULL){
                pContainer->nextFreeOffset = startingOffset;
                return false;
            }

            pContainer->memory = newMemory;
            pContainer->memoryCapacity = newMemoryCapacity;
        }

        u16 currentChar = *string;
        pContainer->memory[currentOffset] = currentChar;
        string += 1;
        currentOffset += 1;

        if (currentChar == 0){
            pContainer->offsets[pContainer->nEntries] = startingOffset;
            *pCopyIndex = pContainer->nEntries;
            
            pContainer->nextFreeOffset = currentOffset;
            pContainer->nEntries += 1;
            return true;
        }
    }
}

bool WideStringContainer_copy(WideStringContainer* pContainer, uint requestedStringIndex, 
                              u16* copyBuffer){
    if (requestedStringIndex >= pContainer->nEntries){
        return false;
    }

    uint offset = pContainer->offsets[requestedStringIndex];
    lstrcpyW(copyBuffer, pContainer->memory + offset);
    return true;
}


bool WideStringContainer_getStringPtr(WideStringContainer* pContainer, uint requestedStringIndex, 
                                      u16** pResult){
    if (requestedStringIndex >= pContainer->nEntries){
        return false;
    }

    uint offset = pContainer->offsets[requestedStringIndex];
    *pResult = pContainer->memory + offset;

    return true;
}

void WideStringContainer_clear(WideStringContainer* pContainer){
    SecureZeroMemory(pContainer->memory, pContainer->memoryCapacity * sizeof(u16));
    SecureZeroMemory(pContainer->offsets, pContainer->entriesCapacity * sizeof(uint));
    pContainer->nEntries = 0;
    pContainer->nextFreeOffset = 0;
}
