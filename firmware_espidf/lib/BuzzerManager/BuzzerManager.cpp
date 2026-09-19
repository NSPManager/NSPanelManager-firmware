#include <BuzzerManager.hpp>
#include <ConfigManager.hpp>
#include <Nextion_event.hpp>
#include <esp_log.h>

void BuzzerManager::init() {
  esp_log_level_set("BuzzerManager", ConfigManager::log_level);

#if defined(BOARD_SONOFF)
  BuzzerManager::_playback_mutex = xSemaphoreCreateMutex();
  if (BuzzerManager::_playback_mutex == NULL) [[unlikely]] {
    ESP_LOGE("BuzzerManager", "Failed to create buzzer playback mutex.");
    return;
  }

  ledc_timer_config_t timer_config = {};
  timer_config.speed_mode = BuzzerManager::_ledc_mode;
  timer_config.duty_resolution = BuzzerManager::_ledc_resolution;
  timer_config.timer_num = BuzzerManager::_ledc_timer;
  timer_config.freq_hz = BuzzerManager::_ledc_initial_frequency_hz;
  timer_config.clk_cfg = LEDC_AUTO_CLK;
  esp_err_t result = ledc_timer_config(&timer_config);
  if (result != ESP_OK) [[unlikely]] {
    ESP_LOGE("BuzzerManager", "Failed to configure LEDC timer for buzzer. Got error: %s", esp_err_to_name(result));
    return;
  }

  ledc_channel_config_t channel_config = {};
  channel_config.gpio_num = BuzzerManager::_buzzer_pin;
  channel_config.speed_mode = BuzzerManager::_ledc_mode;
  channel_config.channel = BuzzerManager::_ledc_channel;
  channel_config.intr_type = LEDC_INTR_DISABLE;
  channel_config.timer_sel = BuzzerManager::_ledc_timer;
  channel_config.duty = 0; // Silent until a sound is requested
  channel_config.hpoint = 0;
  result = ledc_channel_config(&channel_config);
  if (result != ESP_OK) [[unlikely]] {
    ESP_LOGE("BuzzerManager", "Failed to configure LEDC channel for buzzer. Got error: %s", esp_err_to_name(result));
    return;
  }

  esp_timer_create_args_t step_timer_args = {};
  step_timer_args.callback = BuzzerManager::_step_timer_callback;
  step_timer_args.dispatch_method = ESP_TIMER_TASK;
  step_timer_args.name = "buzzer_step";
  result = esp_timer_create(&step_timer_args, &BuzzerManager::_step_timer);
  if (result != ESP_OK) [[unlikely]] {
    ESP_LOGE("BuzzerManager", "Failed to create buzzer step timer. Got error: %s", esp_err_to_name(result));
    return;
  }

  BuzzerManager::_initialized = true;
  esp_event_handler_register(NEXTION_EVENT, nextion_event_t::TOUCH_EVENT, BuzzerManager::_nextion_event_handler, NULL);
  ESP_LOGI("BuzzerManager", "Buzzer initialized on GPIO %d.", BuzzerManager::_buzzer_pin);
#else
  // TODO: Enable once it is known whether the custom PCB has a buzzer, see _buzzer_pin in BuzzerManager.hpp
  ESP_LOGI("BuzzerManager", "No buzzer defined for this board, buzzer is disabled.");
#endif
}

void BuzzerManager::play(std::shared_ptr<const std::vector<buzzer_tone_step_t>> steps) {
  if (!BuzzerManager::_initialized) [[unlikely]] {
    return;
  }

  if (xSemaphoreTake(BuzzerManager::_playback_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    esp_timer_stop(BuzzerManager::_step_timer); // ESP_ERR_INVALID_STATE if nothing is playing is fine.
    BuzzerManager::_steps = std::move(steps); // Frees the previous sequence unless someone else still holds it
    BuzzerManager::_current_step = 0;
    BuzzerManager::_start_current_step();
    xSemaphoreGive(BuzzerManager::_playback_mutex);
  } else {
    ESP_LOGW("BuzzerManager", "Failed to take playback mutex, skipping sound.");
  }
}

void BuzzerManager::stop() {
  BuzzerManager::play(nullptr);
}

void BuzzerManager::_step_timer_callback(void *arg) {
  if (xSemaphoreTake(BuzzerManager::_playback_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
    // play() may have restarted playback while this callback waited for the mutex
    if (esp_timer_get_time() >= BuzzerManager::_current_step_end_us) {
      BuzzerManager::_current_step++;
      BuzzerManager::_start_current_step();
    }
    xSemaphoreGive(BuzzerManager::_playback_mutex);
  } else {
    ESP_LOGW("BuzzerManager", "Failed to take playback mutex while advancing sound.");
  }
}

void BuzzerManager::_start_current_step() {
  if (!BuzzerManager::_steps || BuzzerManager::_current_step >= BuzzerManager::_steps->size()) {
    BuzzerManager::_set_output(0);
    BuzzerManager::_steps.reset();
    return;
  }

  const buzzer_tone_step_t &step = (*BuzzerManager::_steps)[BuzzerManager::_current_step];
  BuzzerManager::_set_output(step.frequency_hz);
  uint64_t duration_us = static_cast<uint64_t>(step.duration_ms) * 1000;
  BuzzerManager::_current_step_end_us = esp_timer_get_time() + duration_us;
  esp_timer_start_once(BuzzerManager::_step_timer, duration_us);
}

void BuzzerManager::_set_output(uint32_t frequency_hz) {
  if (frequency_hz > 0) {
    esp_err_t result = ledc_set_freq(BuzzerManager::_ledc_mode, BuzzerManager::_ledc_timer, frequency_hz);
    if (result != ESP_OK) [[unlikely]] {
      ESP_LOGE("BuzzerManager", "Failed to set buzzer frequency to %lu Hz. Got error: %s", frequency_hz, esp_err_to_name(result));
      frequency_hz = 0; // Stay silent for this step rather than hold the previous tone
    }
  }
  ledc_set_duty(BuzzerManager::_ledc_mode, BuzzerManager::_ledc_channel, frequency_hz > 0 ? BuzzerManager::_ledc_duty_on : 0);
  ledc_update_duty(BuzzerManager::_ledc_mode, BuzzerManager::_ledc_channel);
}

void BuzzerManager::_nextion_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
  nextion_event_touch_t *data = (nextion_event_touch_t *)event_data;
  if (data->pressed) {
    BuzzerManager::_awaiting_release = true;
    BuzzerManager::_pressed_page = data->page_number;
    BuzzerManager::_pressed_component = data->component_id;
    BuzzerManager::_handle_event(buzzer_event_t::TOUCH);
  } else if (BuzzerManager::_awaiting_release && BuzzerManager::_pressed_page == data->page_number && BuzzerManager::_pressed_component == data->component_id) {
    BuzzerManager::_awaiting_release = false; // Already raised when this component was pressed
  } else {
    // Most components in the NSPanel Manager TFT only send release events
    BuzzerManager::_awaiting_release = false;
    BuzzerManager::_handle_event(buzzer_event_t::TOUCH);
  }
}

void BuzzerManager::_handle_event(buzzer_event_t event) {
  switch (event) {
  case buzzer_event_t::TOUCH:
    ESP_LOGD("BuzzerManager", "Got TOUCH event.");
    break;
  }

  // TODO: Play the sound NSPanelManager configured for this event, once NSPanelConfig carries event to RTTTL
  // mappings. Parse the RTTTL when the config is loaded, not here, and play() the result.
}
