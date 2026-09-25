#pragma once
#include "adc_cali.h"
#include "adc_oneshot.h"
typedef struct { adc_unit_t unit_id; adc_atten_t atten; adc_bitwidth_t bitwidth; uint32_t default_vref; } adc_cali_line_fitting_config_t;
esp_err_t adc_cali_create_scheme_line_fitting(const adc_cali_line_fitting_config_t *config, adc_cali_handle_t *out);
