#pragma once
#include "globals.h"

typedef struct tag_WorkQueueEntry {
    u16 fullPath[MAX_PATH + 1];
    sint parentIndex;
    uint indexInParent;
} WorkQueueEntry;

typedef struct tag_WorkQueue {
    HANDLE hHeap;
    WorkQueueEntry* memory;
    uint nextReadIndex;
    uint nextWriteIndex;
    uint capacity;
} WorkQueue;

bool WorkQueue_init (WorkQueue* pQueue, HANDLE hHeap);

void WorkQueue_clear (WorkQueue* pQueue);

bool WorkQueue_enqueue (WorkQueue* pQueue, WorkQueueEntry entry);

bool WorkQueue_dequeue (WorkQueue* pQueue, WorkQueueEntry* value);
