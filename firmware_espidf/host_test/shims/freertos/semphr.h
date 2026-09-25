#pragma once
#include "FreeRTOS.h"
#include "task.h"

typedef void *SemaphoreHandle_t;

SemaphoreHandle_t xSemaphoreCreateMutex(void);
SemaphoreHandle_t xSemaphoreCreateRecursiveMutex(void);
SemaphoreHandle_t xSemaphoreCreateBinary(void);
void vSemaphoreDelete(SemaphoreHandle_t sem);
BaseType_t xSemaphoreTake(SemaphoreHandle_t sem, TickType_t ticks);
BaseType_t xSemaphoreGive(SemaphoreHandle_t sem);
TaskHandle_t xSemaphoreGetMutexHolder(SemaphoreHandle_t sem);
