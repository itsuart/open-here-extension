#pragma once
#include "globals.h"

//TODO: for this to be reusable, we should store length of each string too.

typedef uint WideStringContainerIndex;

typedef struct tag_WideStringContainer {
    HANDLE hHeap;

    uint memoryCapacity;
    u16* memory;
    
    uint entriesCapacity;
    uint* offsets;

    uint nextFreeOffset;
    uint nEntries;
} WideStringContainer;

bool WideStringContainer_init(WideStringContainer* pContainer, HANDLE hHeap);

bool WideStringContainer_add(WideStringContainer* pContainer, u16* string, WideStringContainerIndex* pCopyIndex);

bool WideStringContainer_copy(WideStringContainer* pContainer, WideStringContainerIndex requestedStringIndex, u16* copyBuffer);

bool WideStringContainer_getStringPtr(WideStringContainer* pContainer, WideStringContainerIndex requestedStringIndex, u16** pResult);

void WideStringContainer_clear(WideStringContainer* pContainer);
