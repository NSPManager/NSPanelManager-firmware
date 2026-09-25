#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include <stdint.h>
typedef struct i2c_master_bus_t *i2c_master_bus_handle_t;
typedef struct i2c_master_dev_t *i2c_master_dev_handle_t;
typedef int i2c_port_num_t;
typedef enum { I2C_ADDR_BIT_LEN_7, I2C_ADDR_BIT_LEN_10 } i2c_addr_bit_len_t;
typedef enum { I2C_CLK_SRC_DEFAULT } i2c_clock_source_t;
typedef struct { i2c_port_num_t i2c_port; int sda_io_num; int scl_io_num; i2c_clock_source_t clk_source; uint8_t glitch_ignore_cnt; int intr_priority; struct { uint32_t enable_internal_pullup : 1; } flags; } i2c_master_bus_config_t;
typedef struct { i2c_addr_bit_len_t dev_addr_length; uint16_t device_address; uint32_t scl_speed_hz; } i2c_device_config_t;
esp_err_t i2c_new_master_bus(const i2c_master_bus_config_t *config, i2c_master_bus_handle_t *out);
esp_err_t i2c_master_bus_add_device(i2c_master_bus_handle_t bus, const i2c_device_config_t *config, i2c_master_dev_handle_t *out);
esp_err_t i2c_master_bus_reset(i2c_master_bus_handle_t bus);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t dev, const uint8_t *data, size_t len, int timeout_ms);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t dev, const uint8_t *tx, size_t tx_len, uint8_t *rx, size_t rx_len, int timeout_ms);
esp_err_t i2c_master_receive(i2c_master_dev_handle_t dev, uint8_t *rx, size_t rx_len, int timeout_ms);
