#pragma once
#include "globals.h"
#include "WideStringContainer.h"

typedef struct tag_FSEntry {
    WideStringContainerIndex nameIndex;
    bool isDirectory; //TODO: not perfect memory usage (63 bits wasted)
} FSEntry;

typedef struct tag_FSEntriesContainer {
    HANDLE hHeap;
    WideStringContainerIndex nameIndex;
    uint nEntries;
    uint nDirectories;
    uint capacity;
    FSEntry* entries;
    uint indexInParent;
    sint parentIndex;
} FSEntriesContainer;

bool FSEntriesContainer_init(FSEntriesContainer* pContainer, HANDLE hHeap, WideStringContainerIndex rootDirectory);

void FSEntriesContainer_clear(FSEntriesContainer* pContainer);

void FSEntriesContainer_term(FSEntriesContainer* pContainer);

bool FSEntriesContainer_add(FSEntriesContainer* pContainer, FSEntry entry);

