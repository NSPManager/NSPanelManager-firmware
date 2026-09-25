#pragma once
#include "esp_err.h"
typedef struct { uint32_t type; uint32_t subtype; size_t address; size_t size; size_t erase_size; char label[17]; bool encrypted; } esp_partition_t;
#include <stdint.h>
#include <stddef.h>
typedef enum { ESP_PARTITION_TYPE_APP = 0x00, ESP_PARTITION_TYPE_DATA = 0x01, ESP_PARTITION_TYPE_ANY = 0xff } esp_partition_type_t;
typedef enum { ESP_PARTITION_SUBTYPE_DATA_SPIFFS = 0x82, ESP_PARTITION_SUBTYPE_DATA_LITTLEFS = 0x83, ESP_PARTITION_SUBTYPE_ANY = 0xff } esp_partition_subtype_t;
typedef struct esp_partition_iterator_opaque_ *esp_partition_iterator_t;
const esp_partition_t *esp_partition_get(esp_partition_iterator_t it);
esp_partition_iterator_t esp_partition_find(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label);
const esp_partition_t *esp_partition_find_first(esp_partition_type_t type, esp_partition_subtype_t subtype, const char *label);
esp_partition_iterator_t esp_partition_next(esp_partition_iterator_t it);
void esp_partition_iterator_release(esp_partition_iterator_t it);
esp_err_t esp_partition_erase_range(const esp_partition_t *partition, size_t offset, size_t size);
esp_err_t esp_partition_write(const esp_partition_t *partition, size_t offset, const void *src, size_t size);
esp_err_t esp_partition_read(const esp_partition_t *partition, size_t offset, void *dst, size_t size);
