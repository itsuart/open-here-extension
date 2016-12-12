#pragma once
#include "globals.h"

typedef struct tag_HBitmapStorage {
    HANDLE hHeap;
    HBITMAP* entries;
    uint nEntries;
    uint capacity;
} HBitmapStorage;

bool HBitmapStorage_init(HBitmapStorage* pStorage, HANDLE hHeap);

void HBitmapStorage_clear(HBitmapStorage* pStorage);

bool HBitmapStorage_add(HBitmapStorage* pStorage, HBITMAP bitmap, uint* pIndex);

