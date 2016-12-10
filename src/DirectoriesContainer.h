#pragma once
#include "globals.h"
#include "FSEntriesContainer.h"

typedef struct tag_DirectoriesContainer {
    HANDLE hHeap;
    FSEntriesContainer* data;
    uint nEntries;
    uint capacity;
} DirectoriesContainer;

bool DirectoriesContainer_init(DirectoriesContainer* result, HANDLE hHeap);

bool DirectoriesContainer_add(DirectoriesContainer* pContainer, FSEntriesContainer directory);

void DirectoriesContainer_clear(DirectoriesContainer* pContainer);
