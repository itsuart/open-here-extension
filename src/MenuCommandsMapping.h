#pragma once
#include "globals.h"
#include "WideStringContainer.h"

typedef struct tag_MappingEntry {
    WideStringContainerIndex directoryPathIndex;
    WideStringContainerIndex itemNameIndex;
} MappingEntry;


typedef struct tag_MenuCommandsMapping {
    HANDLE hHeap;
    uint nEntries;
    uint capacity;
    MappingEntry* entries;
} MenuCommandsMapping;

bool MenuCommandsMapping_init(MenuCommandsMapping* pMapping, HANDLE hHeap);

bool MenuCommandsMapping_add (MenuCommandsMapping* pMapping, MappingEntry entry, uint* pIndex);

void MenuCommandsMapping_clear(MenuCommandsMapping* pMapping);
