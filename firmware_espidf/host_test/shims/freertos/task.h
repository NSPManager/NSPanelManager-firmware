#pragma once
#include "FreeRTOS.h"

typedef void *TaskHandle_t;
typedef void (*TaskFunction_t)(void *);

BaseType_t xTaskCreate(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out);
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *arg, UBaseType_t prio, TaskHandle_t *out, BaseType_t core);
void vTaskDelay(TickType_t ticks);
void vTaskDelete(TaskHandle_t task);
TaskHandle_t xTaskGetCurrentTaskHandle(void);
BaseType_t xTaskNotifyGive(TaskHandle_t task);
uint32_t ulTaskNotifyTake(BaseType_t clear_on_exit, TickType_t ticks);
void vTaskSuspend(TaskHandle_t task);
void vTaskResume(TaskHandle_t task);
char *pcTaskGetName(TaskHandle_t task); /* IDF FreeRTOS returns non-const, unlike upstream */
UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t task);
TickType_t xTaskGetTickCount(void);
BaseType_t xTaskNotifyFromISR(TaskHandle_t task, uint32_t value, int action, BaseType_t *woken);
void portYIELD_FROM_ISR(void);
#define INCLUDE_vTaskSuspend 1
typedef uint32_t StackType_t;
typedef struct { TaskHandle_t xHandle; const char *pcTaskName; UBaseType_t xTaskNumber; UBaseType_t uxCurrentPriority; UBaseType_t uxBasePriority; uint32_t ulRunTimeCounter; StackType_t *pxStackBase; uint32_t usStackHighWaterMark; } TaskStatus_t;
UBaseType_t uxTaskGetNumberOfTasks(void);
UBaseType_t uxTaskGetSystemState(TaskStatus_t *array, UBaseType_t count, uint32_t *total_run_time);
void *pvPortMalloc(size_t size);
void vPortFree(void *ptr);
