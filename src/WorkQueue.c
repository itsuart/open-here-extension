#include "WorkQueue.h"

bool WorkQueue_init (WorkQueue* pQueue, HANDLE hHeap){
    const uint initialCapacity = 20;
    WorkQueueEntry* memory = 
        (WorkQueueEntry*) HeapAlloc(hHeap, HEAP_ZERO_MEMORY, initialCapacity *  sizeof(WorkQueueEntry));
    if (memory == NULL){
        return false;
    }

    pQueue->hHeap = hHeap;
    pQueue->memory = memory;
    pQueue->nextReadIndex = 0;
    pQueue->nextWriteIndex = 0;
    pQueue->capacity = initialCapacity;

    return true;
}

void WorkQueue_clear (WorkQueue* pQueue){
    SecureZeroMemory(pQueue->memory, pQueue->capacity * sizeof(WorkQueueEntry));
    pQueue->nextReadIndex = 0;
    pQueue->nextWriteIndex = 0;
}

bool WorkQueue_enqueue (WorkQueue* pQueue, WorkQueueEntry entry){
    if (pQueue->capacity == pQueue->nextWriteIndex){
        const uint newCapacity = pQueue->capacity + 10;
        WorkQueueEntry* newMemory = (WorkQueueEntry*) HeapReAlloc(pQueue->hHeap, HEAP_ZERO_MEMORY, 
                                            pQueue->memory, newCapacity * sizeof(WorkQueueEntry));
        if (newMemory == NULL){
            return false;
        }

        pQueue->capacity = newCapacity;
        pQueue->memory = newMemory;
    }

    pQueue->memory[pQueue->nextWriteIndex] = entry;
    pQueue->nextWriteIndex += 1;

    return true;
}


bool WorkQueue_dequeue (WorkQueue* pQueue, WorkQueueEntry* value){
    if (pQueue->nextReadIndex == pQueue->nextWriteIndex){
        //we don't have anything new to dequeue
        return false;
    }

    *value = pQueue->memory[pQueue->nextReadIndex];
    pQueue->nextReadIndex += 1;

    return true;
}
