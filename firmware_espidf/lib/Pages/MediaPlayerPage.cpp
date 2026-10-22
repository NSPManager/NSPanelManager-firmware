#include <EntitiesPage.hpp>
#include <EntityPage.hpp>
#include <GUI_data.hpp>
#include <MediaPlayerPage.hpp>
#include <MqttManager.hpp>
#include <NSPM_ConfigManager.hpp>
#include <Nextion.hpp>
#include <algorithm>
#include <esp_log.h>
#include <esp_timer.h>
#include <sys/time.h>

MediaPlayerPage::_component_t MediaPlayerPage::_components[] = {
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::back_button_name, _action::BACK, GUI_MEDIA_PLAYER_CONTROL_PAGE::back_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::previous_track_button_name, _action::PREVIOUS_TRACK, GUI_MEDIA_PLAYER_CONTROL_PAGE::previous_track_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::next_track_button_name, _action::NEXT_TRACK, GUI_MEDIA_PLAYER_CONTROL_PAGE::next_track_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_name, _action::PLAY_PAUSE, GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_name, _action::MUTE, GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_up_button_name, _action::VOLUME_UP, GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_up_button_id},
    {GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_down_button_name, _action::VOLUME_DOWN, GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_down_button_id},
};

void MediaPlayerPage::handle_state(const NSPanelEntityState__MediaPlayer *media_player) {
  MediaPlayerPage::_log_state(media_player);
  MediaPlayerPage::_confirm_pending_volume(media_player);
}

void MediaPlayerPage::unshow() {
  MediaPlayerPage::_showing = false;
}

void MediaPlayerPage::_redraw() {
  if (!MediaPlayerPage::_showing) {
    return;
  }
  std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
  if (state != nullptr && state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) {
    MediaPlayerPage::update_display(state->media_player, false);
  }
}

const char *MediaPlayerPage::_playback_state_name(NSPanelEntityState__MediaPlayer__PlaybackState state) {
  switch (state) {
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__OFF:
    return "Off";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__ON:
    return "On";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__IDLE:
    return "Idle";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING:
    return "Playing";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PAUSED:
    return "Paused";
  case NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING:
    return "Buffering";
  default:
    return "";
  }
}

void MediaPlayerPage::_log_state(const NSPanelEntityState__MediaPlayer *media_player) {
  if (media_player == NULL) [[unlikely]] {
    ESP_LOGE("MediaPlayerPage", "Media player state case set but no media player payload.");
    return;
  }

  // The whole decode path runs before anything touches the display, so this verifies the protobuf
  // round trip even on a panel whose TFT has no media player page.
  ESP_LOGD("MediaPlayerPage", "Media player %ld '%s': %s", media_player->media_player_id, media_player->name != NULL ? media_player->name : "(no name)", MediaPlayerPage::_playback_state_name(media_player->state));
  ESP_LOGD("MediaPlayerPage", "  title '%s' artist '%s'", media_player->media_title != NULL ? media_player->media_title : "", media_player->media_artist != NULL ? media_player->media_artist : "");
  ESP_LOGD("MediaPlayerPage", "  volume %ld, muted %s, source volume %ld (present: %s)", media_player->volume, media_player->is_muted ? "yes" : "no", media_player->source_volume, media_player->has_source_volume ? "yes" : "no");
  ESP_LOGD("MediaPlayerPage", "  can: play %d pause %d next %d prev %d set_volume %d mute %d", media_player->can_play, media_player->can_pause, media_player->can_next_track, media_player->can_previous_track, media_player->can_set_volume, media_player->can_mute);
  ESP_LOGD("MediaPlayerPage", "  album art: %s", media_player->album_art_url != NULL && media_player->album_art_url[0] != '\0' ? media_player->album_art_url : "(none)");
  if (media_player->has_media_position) {
    ESP_LOGD("MediaPlayerPage", "  position %lds of %lds at %lld", media_player->media_position, media_player->media_duration, media_player->media_position_updated_at);
  }
}

void MediaPlayerPage::update_display(const NSPanelEntityState__MediaPlayer *media_player, bool page_just_opened) {
  if (page_just_opened) {
    MediaPlayerPage::_showing = true;
    MediaPlayerPage::_resolve_component_ids();

    MediaPlayerPage::_position_bar_value = -2;
    if (MediaPlayerPage::_position_task_handle == NULL) {
      if (xTaskCreatePinnedToCore(MediaPlayerPage::_task_position, "media_position", 3072, NULL, 2, &MediaPlayerPage::_position_task_handle, 1) != pdPASS) [[unlikely]] {
        ESP_LOGE("MediaPlayerPage", "Failed to create the media player position task. The position bar won't advance while playing.");
        MediaPlayerPage::_position_task_handle = NULL;
      }
    }
    Nextion::set_timer_value(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_timer_name, MediaPlayerPage::_volume_bar_timeout_ms, 250);
    taskENTER_CRITICAL(&MediaPlayerPage::_spinlock);
    MediaPlayerPage::_pending_volume.volume = -1; // A press from the last time the page was open no longer counts.
    taskEXIT_CRITICAL(&MediaPlayerPage::_spinlock);
  }

  bool is_playing = media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING ||
                    media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING;

  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::title_label_name, media_player->media_title, 1000);
  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::artist_label_name, media_player->media_artist, 1000);

  // Icons follow the player's state rather than the press, so the page stays right when something
  // else controls the player.
  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::play_pause_button_name, is_playing ? GUI_MEDIA_ICONS::pause : GUI_MEDIA_ICONS::play, 250);
  // Buttons the player can't use now are greyed out rather than hidden, so the layout stays put.
  for (const _component_t &component : MediaPlayerPage::_components) {
    switch (component.action) {
    case _action::PREVIOUS_TRACK:
    case _action::NEXT_TRACK:
    case _action::PLAY_PAUSE:
    case _action::MUTE:
    case _action::VOLUME_UP:
    case _action::VOLUME_DOWN:
      MediaPlayerPage::_set_button_enabled(component.name, MediaPlayerPage::_action_enabled(component.action, media_player));
      break;
    default:
      break;
    }
  }

  // Switching the volume controls to the source is not supported yet, so hide the switch.
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::source_switch_button_name, false, 250);

  Nextion::set_component_text(GUI_MEDIA_PLAYER_CONTROL_PAGE::mute_button_name, media_player->is_muted ? GUI_MEDIA_ICONS::volume_mute : GUI_MEDIA_ICONS::volume_high, 250);
  // Only the value: the bar is shown briefly when the volume buttons are pressed, not on every update.
  Nextion::set_component_value(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_name, MediaPlayerPage::_volume(media_player), 250);

  MediaPlayerPage::_update_position_bar(media_player);
  // The task advances the bar while playing, and sleeps otherwise, so let it look at the new state.
  if (MediaPlayerPage::_position_task_handle != NULL) {
    xTaskNotifyGive(MediaPlayerPage::_position_task_handle);
  }
}

int32_t MediaPlayerPage::_position_percent(const NSPanelEntityState__MediaPlayer *media_player) {
  if (!media_player->has_media_position || media_player->media_duration <= 0) {
    return -1; // A duration of 0 is unknown, as for a live stream.
  }

  int64_t position = media_player->media_position;
  // The manager only sends the position when playback starts, stops or seeks, with the time it was
  // taken. While playing, advance it by the time since then. The state is retained, so it may have
  // arrived long after it was sent, and the time since it arrived would not do.
  if (media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING) {
    struct timeval now;
    gettimeofday(&now, NULL);
    if (now.tv_sec < MediaPlayerPage::_min_wall_clock) {
      return -1;
    }
    // Clocks a little apart could put the update in the future.
    position += std::max<int64_t>(0, now.tv_sec - media_player->media_position_updated_at);
  }

  position = std::clamp<int64_t>(position, 0, media_player->media_duration);
  return position * 100 / media_player->media_duration;
}

void MediaPlayerPage::_update_position_bar(const NSPanelEntityState__MediaPlayer *media_player) {
  int32_t value = MediaPlayerPage::_position_percent(media_player);
  int32_t shown = MediaPlayerPage::_position_bar_value.exchange(value);
  if (value == shown) {
    return;
  }

  if (value < 0) {
    Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::position_bar_name, false, 250);
    return;
  }
  // Set the value before showing it, so a hidden bar doesn't flash its old value.
  Nextion::set_component_value(GUI_MEDIA_PLAYER_CONTROL_PAGE::position_bar_name, value, 250);
  if (shown < 0) {
    Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::position_bar_name, true, 250);
  }
}

void MediaPlayerPage::_task_position(void *param) {
  for (;;) {
    bool playing = false;
    if (MediaPlayerPage::_showing) {
      std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
      if (state != nullptr && state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) {
        playing = state->media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING;
        if (playing) {
          MediaPlayerPage::_update_position_bar(state->media_player);
        }
      }
    }
    // The bar moves 1% at a time, so it only reaches the display every few seconds for most tracks.
    ulTaskNotifyTake(pdTRUE, playing ? pdMS_TO_TICKS(1000) : portMAX_DELAY);
  }
}

bool MediaPlayerPage::_action_enabled(_action action, const NSPanelEntityState__MediaPlayer *media_player) {
  switch (action) {
  case _action::PREVIOUS_TRACK:
    return media_player->can_previous_track;
  case _action::NEXT_TRACK:
    return media_player->can_next_track;
  case _action::PLAY_PAUSE: {
    bool is_playing = media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING ||
                      media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING;
    return is_playing ? media_player->can_pause : media_player->can_play;
  }
  case _action::MUTE:
    return media_player->can_mute;
  case _action::VOLUME_UP:
  case _action::VOLUME_DOWN:
    return media_player->can_set_volume;
  default:
    return true;
  }
}

void MediaPlayerPage::_set_button_enabled(const char *button_name, bool enabled) {
  Nextion::set_component_foreground(button_name, enabled ? GUI_MEDIA_PLAYER_CONTROL_PAGE::button_pco : GUI_MEDIA_PLAYER_CONTROL_PAGE::button_disabled_pco, 250);
  Nextion::set_component_pco2(button_name, enabled ? GUI_MEDIA_PLAYER_CONTROL_PAGE::button_pressed_pco2 : GUI_MEDIA_PLAYER_CONTROL_PAGE::button_disabled_pco, 250);
}

void MediaPlayerPage::handle_touch(uint16_t component_id, bool pressed) {
  ESP_LOGD("MediaPlayerPage", "Touch component %d, pressed %s", component_id, pressed ? "Yes" : "No");
  if (pressed) {
    return; // The page sends release events only.
  }

  for (const _component_t &component : MediaPlayerPage::_components) {
    if (component.id != component_id) {
      continue;
    }

    // The button is greyed out, but the Nextion still reports presses.
    std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
    if (state != nullptr && state->entity_case == NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER &&
        !MediaPlayerPage::_action_enabled(component.action, state->media_player)) {
      ESP_LOGD("MediaPlayerPage", "Ignoring a press on %s, which the media player can't do now.", component.name);
      return;
    }

    switch (component.action) {
    case _action::BACK:
      ESP_LOGD("MediaPlayerPage", "Received touch event to go back.");
      EntitiesPage::show(EntitiesPage::display_type_t::ENTITIES);
      break;

    case _action::PLAY_PAUSE:
      MediaPlayerPage::_play_pause();
      break;

    case _action::PREVIOUS_TRACK:
      MediaPlayerPage::_previous_track();
      break;

    case _action::NEXT_TRACK:
      MediaPlayerPage::_next_track();
      break;

    case _action::MUTE:
      MediaPlayerPage::_toggle_mute();
      break;

    case _action::VOLUME_UP:
      MediaPlayerPage::_step_volume(1);
      break;

    case _action::VOLUME_DOWN:
      MediaPlayerPage::_step_volume(-1);
      break;
    }
    return;
  }
}

void MediaPlayerPage::_resolve_component_ids() {
  if (MediaPlayerPage::_component_ids_resolved) {
    return;
  }

  bool all_resolved = true;
  std::string resolved_ids;
  for (_component_t &component : MediaPlayerPage::_components) {
    int32_t id = -1;
    if (Nextion::get_component_integer_attribute(component.name, "id", &id, 250, 250) == ESP_OK && id >= 0 && id <= UINT8_MAX) {
      component.id = id;
    } else {
      // Keep the ID from GUI_data.hpp and try again the next time the page opens.
      ESP_LOGE("MediaPlayerPage", "Failed to look up the ID of media player component %s, will use %d.", component.name, component.id.load());
      all_resolved = false;
    }
    resolved_ids.append(" ").append(component.name).append("=").append(std::to_string(component.id.load()));
  }
  ESP_LOGI("MediaPlayerPage", "Media player component IDs:%s", resolved_ids.c_str());
  MediaPlayerPage::_component_ids_resolved = all_resolved;
}

void MediaPlayerPage::_play_pause() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("MediaPlayerPage", "Tried to play/pause without a media player state.");
    return;
  }

  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  if (state->media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__PLAYING || state->media_player->state == NSPANEL_ENTITY_STATE__MEDIA_PLAYER__PLAYBACK_STATE__BUFFERING) {
    command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__PAUSE;
  } else {
    command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__PLAY;
  }
  MediaPlayerPage::_send_command(&command);
}

void MediaPlayerPage::_next_track() {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__NEXT_TRACK;
  MediaPlayerPage::_send_command(&command);
}

void MediaPlayerPage::_previous_track() {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.playback_action = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__PLAYBACK_ACTION__PREVIOUS_TRACK;
  MediaPlayerPage::_send_command(&command);
}

void MediaPlayerPage::_toggle_mute() {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("MediaPlayerPage", "Tried to toggle mute without a media player state.");
    return;
  }

  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.has_muted = true;
  command.muted = !state->media_player->is_muted;
  MediaPlayerPage::_send_command(&command);
}

void MediaPlayerPage::_set_volume(int32_t volume) {
  NSPanelMQTTManagerCommand__MediaPlayerCommand command = NSPANEL_MQTTMANAGER_COMMAND__MEDIA_PLAYER_COMMAND__INIT;
  command.has_volume = true;
  command.volume = volume;
  MediaPlayerPage::_send_command(&command);
}

void MediaPlayerPage::_step_volume(int32_t direction) {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("MediaPlayerPage", "Tried to change volume without a media player state.");
    return;
  }
  NSPanelEntityState__MediaPlayer *media_player = state->media_player;

  // A manager from before volume_step was added sends 0.
  int32_t step = media_player->volume_step > 0 ? media_player->volume_step : MediaPlayerPage::_default_volume_step;
  // Step from the pending volume if there is one, so quick presses compound instead of each
  // starting from a state that has not caught up yet.
  int32_t volume = std::clamp<int32_t>(MediaPlayerPage::_volume(media_player) + direction * step, 0, 100);

  taskENTER_CRITICAL(&MediaPlayerPage::_spinlock);
  MediaPlayerPage::_pending_volume = {
      .volume = volume,
      .requested_at = esp_timer_get_time(),
  };
  taskEXIT_CRITICAL(&MediaPlayerPage::_spinlock);

  MediaPlayerPage::_set_volume(volume);
  MediaPlayerPage::_show_volume_bar(volume);
}

int32_t MediaPlayerPage::_volume(const NSPanelEntityState__MediaPlayer *media_player) {
  int32_t volume = media_player->volume;

  taskENTER_CRITICAL(&MediaPlayerPage::_spinlock);
  const _pending_volume_t pending = MediaPlayerPage::_pending_volume;
  taskEXIT_CRITICAL(&MediaPlayerPage::_spinlock);

  if (pending.volume >= 0 && esp_timer_get_time() - pending.requested_at < MediaPlayerPage::_pending_volume_timeout_us) {
    volume = pending.volume;
  }
  return volume;
}

void MediaPlayerPage::_confirm_pending_volume(const NSPanelEntityState__MediaPlayer *media_player) {
  if (media_player == NULL) [[unlikely]] {
    return;
  }

  // Only the state for the last press confirms it. States for earlier presses in a quick run arrive
  // in between, and dropping the pending volume on those would make the bar jump back.
  taskENTER_CRITICAL(&MediaPlayerPage::_spinlock);
  _pending_volume_t &pending = MediaPlayerPage::_pending_volume;
  if (pending.volume >= 0 && pending.volume == media_player->volume) {
    pending.volume = -1;
  }
  taskEXIT_CRITICAL(&MediaPlayerPage::_spinlock);
}

void MediaPlayerPage::_show_volume_bar(int32_t volume) {
  Nextion::set_component_value(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_name, volume, 250);
  Nextion::set_component_visibility(GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_name, true, 250);

  // The timer hides the bar. Stop it before starting it so that every press restarts the countdown,
  // and holding the buttons keeps the bar up.
  std::string timer = GUI_MEDIA_PLAYER_CONTROL_PAGE::volume_bar_timer_name;
  Nextion::send_raw_command((timer + ".en=0").c_str(), 250);
  Nextion::send_raw_command((timer + ".en=1").c_str(), 250);
}

void MediaPlayerPage::_send_command(NSPanelMQTTManagerCommand__MediaPlayerCommand *command) {
  std::shared_ptr<NSPanelEntityState> state = EntityPage::get_current_state();
  if (state == nullptr || state->entity_case != NSPANEL_ENTITY_STATE__ENTITY_MEDIA_PLAYER) [[unlikely]] {
    ESP_LOGE("MediaPlayerPage", "Tried to send media player command without a media player state.");
    return;
  }
  command->media_player_id = state->media_player->media_player_id;

  NSPanelMQTTManagerCommand cmd = NSPANEL_MQTTMANAGER_COMMAND__INIT;
  cmd.command_data_case = NSPANEL_MQTTMANAGER_COMMAND__COMMAND_DATA_MEDIA_PLAYER_COMMAND;
  cmd.media_player_command = command;
  cmd.nspanel_id = NSPM_ConfigManager::get_nspanel_id();

  uint32_t packed_length = nspanel_mqttmanager_command__get_packed_size(&cmd);
  std::vector<uint8_t> buffer(packed_length); // Use vector for automatic cleanup of data when going out of scope
  size_t packed_data_size = nspanel_mqttmanager_command__pack(&cmd, buffer.data());
  if (packed_data_size == packed_length) [[likely]] {
    if (MqttManager::publish(NSPM_ConfigManager::get_manager_command_topic(), (const char *)buffer.data(), packed_length, false) != ESP_OK) [[unlikely]] {
      ESP_LOGE("MediaPlayerPage", "Failed to send MQTT message with command payload.");
      MediaPlayerPage::_redraw(); // Update display to reset values to those stored
    }
  } else {
    ESP_LOGE("MediaPlayerPage", "Failed to pack protobuf command.");
    MediaPlayerPage::_redraw(); // Update display to reset values to those stored
  }
}
