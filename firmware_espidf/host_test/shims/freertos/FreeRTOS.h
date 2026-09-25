#pragma once
#include <stdint.h>
#include <stddef.h>

#define portMAX_DELAY 0xffffffffUL
#define portTICK_PERIOD_MS 1
#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))
#define pdTRUE 1
#define pdFALSE 0
#define pdPASS 1
#define pdFAIL 0

typedef uint32_t TickType_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

/* portMUX / critical sections -> no-ops backed by a recursive mutex in the impl */
typedef struct { int dummy; } portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED { 0 }
void portENTER_CRITICAL(portMUX_TYPE *mux);
void portEXIT_CRITICAL(portMUX_TYPE *mux);
#include "timers.h"
#define taskENTER_CRITICAL(mux) portENTER_CRITICAL(mux)
#define taskEXIT_CRITICAL(mux) portEXIT_CRITICAL(mux)
#define pdTICKS_TO_MS(ticks) ((uint32_t)(ticks))
#define configMAX_PRIORITIES 25
#define configTICK_RATE_HZ 1000
#define portTICK_RATE_MS portTICK_PERIOD_MS
#define xPortGetCoreID() 0
#define IRAM_ATTR
