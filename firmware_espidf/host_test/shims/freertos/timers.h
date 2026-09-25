#pragma once
#include "FreeRTOS.h"
typedef void *TimerHandle_t;
typedef void (*TimerCallbackFunction_t)(TimerHandle_t);
TimerHandle_t xTimerCreate(const char *name, TickType_t period, BaseType_t reload, void *id, TimerCallbackFunction_t cb);
BaseType_t xTimerStart(TimerHandle_t t, TickType_t ticks);
BaseType_t xTimerStop(TimerHandle_t t, TickType_t ticks);
