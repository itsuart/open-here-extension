#pragma once
#include "globals.h"

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

bool FSEntriesContainer_init(FSEntriesContainer* pContainer, HANDLE hHeap, u16* rootDirectory);

void FSEntriesContainer_clear(FSEntriesContainer* pContainer);

void FSEntriesContainer_term(FSEntriesContainer* pContainer);

bool FSEntriesContainer_add(FSEntriesContainer* pContainer, u16* fullPath, bool isDirectory);

