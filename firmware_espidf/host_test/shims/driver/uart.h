#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include <stdint.h>

#define UART_NUM_0 0
#define UART_NUM_1 1
#define UART_NUM_2 2
#define UART_PIN_NO_CHANGE (-1)

typedef int uart_port_t;
typedef enum { UART_DATA_5_BITS, UART_DATA_6_BITS, UART_DATA_7_BITS, UART_DATA_8_BITS } uart_word_length_t;
typedef enum { UART_PARITY_DISABLE, UART_PARITY_EVEN, UART_PARITY_ODD } uart_parity_t;
typedef enum { UART_STOP_BITS_1 = 1, UART_STOP_BITS_1_5, UART_STOP_BITS_2 } uart_stop_bits_t;
typedef enum { UART_HW_FLOWCTRL_DISABLE, UART_HW_FLOWCTRL_RTS, UART_HW_FLOWCTRL_CTS, UART_HW_FLOWCTRL_CTS_RTS } uart_hw_flowcontrol_t;
typedef enum { UART_SCLK_DEFAULT, UART_SCLK_APB } uart_sclk_t;
typedef enum { UART_DATA, UART_BREAK, UART_BUFFER_FULL, UART_FIFO_OVF, UART_FRAME_ERR, UART_PARITY_ERR, UART_PATTERN_DET, UART_EVENT_MAX } uart_event_type_t;

typedef struct {
  int baud_rate;
  uart_word_length_t data_bits;
  uart_parity_t parity;
  uart_stop_bits_t stop_bits;
  uart_hw_flowcontrol_t flow_ctrl;
  uint8_t rx_flow_ctrl_thresh;
  uart_sclk_t source_clk;
} uart_config_t;

typedef struct { uart_event_type_t type; size_t size; bool timeout_flag; } uart_event_t;

esp_err_t uart_driver_install(uart_port_t port, int rx_buf, int tx_buf, int queue_size, QueueHandle_t *queue, int flags);
esp_err_t uart_driver_delete(uart_port_t port);
esp_err_t uart_param_config(uart_port_t port, const uart_config_t *config);
esp_err_t uart_set_pin(uart_port_t port, int tx, int rx, int rts, int cts);
esp_err_t uart_set_baudrate(uart_port_t port, uint32_t baud);
int uart_write_bytes(uart_port_t port, const void *src, size_t size);
int uart_read_bytes(uart_port_t port, void *buf, uint32_t len, TickType_t ticks);
esp_err_t uart_flush(uart_port_t port);
esp_err_t uart_flush_input(uart_port_t port);
esp_err_t uart_wait_tx_done(uart_port_t port, TickType_t ticks);
esp_err_t uart_get_buffered_data_len(uart_port_t port, size_t *size);
esp_err_t uart_enable_pattern_det_baud_intr(uart_port_t port, char pattern, uint8_t count, int t1, int t2, int t3);
esp_err_t uart_pattern_queue_reset(uart_port_t port, int len);
int uart_pattern_pop_pos(uart_port_t port);
