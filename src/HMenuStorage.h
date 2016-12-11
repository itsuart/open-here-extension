#pragma once
#include "globals.h"

typedef struct tag_HMenuStorage {
    HANDLE hHeap;
    HMENU* entries;
    uint nEntries;
    uint capacity;
} HMenuStorage;

bool HMenuStorage_init(HMenuStorage* pStorage, HANDLE hHeap);

void HMenuStorage_clear(HMenuStorage* pStorage);

bool HMenuStorage_add(HMenuStorage* pStorage, HMENU menu, uint* pIndex);

