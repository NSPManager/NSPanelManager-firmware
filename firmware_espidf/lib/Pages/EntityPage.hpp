#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <protobuf_nspanel.pb-c.h>
#include <protobuf_nspanel_entity.pb-c.h>
#include <string>

/*
 * The page that will handle control of individual entities
 */
class EntityPage {
public:
  /**
   * Show the loading page
   */
  static void show(std::string state_topic);

  /**
   * Unshow the loading page
   */
  static void unshow();

private:
  /**
   * Handle events from MQTT, such as new state events:
   */
  static void _handle_mqtt_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle events from Nextion, such as touch events:
   */
  static void _handle_nextion_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Handle event for a new config from MQTTManager:
   */
  static void _handle_config_update(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Delegate to the correct update method
   */
  static void _update_display();

  /**
   * Delegate to the correct touch method
   */
  static void _handle_touch_event(const std::shared_ptr<NSPanelEntityState> &state, uint16_t component_id, bool pressed);

  /*
   * Update displayed page and value of "Light" page.
   */
  static void _update_display_light();

  /*
   * Handle touch event for "Light" page.
   */
  static void _handle_touch_event_light(const std::shared_ptr<NSPanelEntityState> &state, uint16_t component_id, bool pressed);

  /*
   * Update displayed page and value of "Thermostat" page.
   */
  static void _update_display_thermostat();

  /*
   * Handle Nextion string event.
   */
  static void _handle_string_event_thermostat(const std::shared_ptr<NSPanelEntityState> &state, char *data);

  /*
   * Update displayed page and value of "Media player" page.
   */
  static void _update_display_media_player();

  /*
   * Human readable name for a playback state, used by the display and the log.
   */
  static const char *_playback_state_name(NSPanelEntityState__MediaPlayer__PlaybackState state);

  /*
   * Log a decoded media player state at debug level.
   */
  static void _log_media_player_state(NSPanelEntityState__MediaPlayer *media_player);

  /*
   * Handle touch event for "Media player" page.
   */
  static void _handle_touch_event_media_player(uint16_t component_id, bool pressed);

  /*
   * Look up the IDs of the touchable media player components by name and log them. Called with the
   * page showing, as the Nextion only answers for components on the current page. Done once per boot:
   * the IDs only change with a new TFT, and installing one reboots the panel.
   */
  static void _media_player_resolve_component_ids();

  /*
   * Commands for the currently displayed media player. Decisions that depend on the
   * current state (play or pause, mute or unmute) are made from the last state received.
   */
  static void _media_player_play_pause();
  static void _media_player_next_track();
  static void _media_player_previous_track();
  static void _media_player_toggle_mute();
  static void _media_player_set_volume(int32_t volume);
  static void _media_player_set_source_volume(int32_t volume);

  /*
   * Volume up (direction 1) or down (-1) by the player's volume_step, from the volume -/+ buttons.
   * Sets a pending volume that the page shows, and steps from, until the state confirms it.
   */
  static void _media_player_step_volume(int32_t direction);

  /*
   * The volume to show and step from for the player, or for its source: the pending volume if one is
   * outstanding for that target, otherwise the state's.
   */
  static int32_t _media_player_volume(const NSPanelEntityState__MediaPlayer *media_player, bool source);

  /*
   * Clear the pending volume if the given state carries it.
   */
  static void _media_player_confirm_pending_volume(const NSPanelEntityState__MediaPlayer *media_player);

  /*
   * Switch the volume controls between the player and the source feeding it, from b_srcswitch.
   */
  static void _media_player_toggle_source();

  /*
   * Show the volume bar at the given volume. The page's tm_vol timer hides it again.
   */
  static void _media_player_show_volume_bar(int32_t volume);

  /**
   * properly delete pointer and clear old data when shared_ptr expires
   */
  static void _delete_nspanel_entity_state_object(NSPanelEntityState *object);

  // Get a shared_ptr to the current state of the entity.
  static inline std::shared_ptr<NSPanelEntityState> _get_current_state();

  // If currently editing, check what has changed and sent the new setting to the manager to apply setting.
  static void _send_thermostat_option_command();

  // Send new set temp for a thermostat
  static void _send_thermostat_setpoint_command();

  // Send a command for the currently displayed media player to the manager. Fills in the media player ID.
  static void _send_media_player_command(NSPanelMQTTManagerCommand__MediaPlayerCommand *command);

  // What is the page currently showing
  enum _entity_page_modes {
    LIGHT_COLOR_TEMPERATURE,
    LIGHT_RGB,
    THERMOSTAT,
    MEDIA_PLAYER,
  };
  static inline std::atomic<_entity_page_modes> _current_mode;

  // Vars
  // The state topic of the given entity
  static inline std::string _current_entity_mqtt_topic;

  // Are we currently displaying the page?
  static inline std::atomic<bool> _currently_showing = false;

  // The last name to be updated on the display.
  static inline std::string _last_displayed_name = "";

  // Light specific variables
  static inline uint8_t _last_brightness = 0;
  static inline uint8_t _last_kelvin_pct = 0;
  static inline uint8_t _last_hue = 0;
  static inline uint8_t _last_saturation_pct = 0;

  // Media player specific variables
  // Whether the volume controls act on the source feeding the player rather than the player itself.
  // Reset to the player whenever the page opens.
  static inline std::atomic<bool> _media_player_controlling_source = false;
  // Percent per press of the volume -/+ buttons when the manager sends no volume_step. Matches Home
  // Assistant's default.
  static constexpr int32_t _media_player_default_volume_step = 10;
  // The volume the last -/+ press asked for, shown in place of the state's until a state carrying it
  // arrives. Guarded by _entity_page_spinlock: written from the Nextion handler, read from MQTT too.
  struct _media_player_pending_volume_t {
    int32_t volume;       // -1 when nothing is pending
    bool source;          // Whether it is for the source rather than the player
    int64_t requested_at; // esp_timer_get_time() of the press
  };
  static inline _media_player_pending_volume_t _media_player_pending_volume = {.volume = -1, .source = false, .requested_at = 0};
  // How long a pending volume waits for its state before the state wins: in case the manager rounds
  // it differently, or the player ignores it.
  static constexpr int64_t _media_player_pending_volume_timeout_us = 5 * 1000 * 1000;
  // What a touch on a media player component does.
  enum class _media_player_action {
    BACK,
    PREVIOUS_TRACK,
    NEXT_TRACK,
    PLAY_PAUSE,
    MUTE,
    SOURCE_SWITCH,
    VOLUME_UP,
    VOLUME_DOWN,
    ALBUM_ART,
  };
  // A touchable media player component. id starts as the ID in GUI_data.hpp and is replaced by the one
  // the Nextion reports. Written from the MQTT task, read from the Nextion handler.
  struct _media_player_component_t {
    const char *name;
    _media_player_action action;
    std::atomic<uint8_t> id;
  };
  static _media_player_component_t _media_player_components[];
  static inline std::atomic<bool> _media_player_component_ids_resolved = false;
  // The album art URL last handed to AlbumArt, so that new art is drawn when it changes, for example on
  // the next track. Guarded by _media_player_album_art_url_mutex: the page is updated from the MQTT
  // task and from the Nextion handler.
  static inline std::string _media_player_album_art_url;
  static inline std::mutex _media_player_album_art_url_mutex;

  // Thermostat specific variables
  static inline std::atomic<bool> _is_currently_editing = false;
  static inline uint8_t _selected_thermostat_option_index = 0;

  // Written by the MQTT handler, read by the Nextion event loop. Never dereference _current_state directly:
  // `->` on a shared_ptr returns a raw pointer and holds no reference, so a new state arriving mid-read can
  // free the one you are reading. Call _get_current_state() once per entry point, keep that copy for the
  // whole operation, and pass it down by const&. It returns nullptr until the first state has arrived.
  static inline SemaphoreHandle_t _current_state_mutex = NULL;
  static inline std::shared_ptr<NSPanelEntityState> _current_state;

  static inline portMUX_TYPE _entity_page_spinlock = portMUX_INITIALIZER_UNLOCKED;
};
