#pragma once
#include "globals.h"
#include "WideStringContainer.h"

typedef struct tag_WorkQueueEntry {
    WideStringContainerIndex fullPathIndex;
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
