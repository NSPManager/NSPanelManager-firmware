#pragma once
#include <stddef.h>
#define MALLOC_CAP_8BIT (1 << 2)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT (1 << 12)
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_total_size(uint32_t caps);
