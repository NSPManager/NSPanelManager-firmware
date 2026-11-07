#pragma once

#include <atomic>
#include <cstdint>
#include <protobuf_nspanel.pb-c.h>
#include <protobuf_nspanel_entity.pb-c.h>

/*
 * The cover page: position and open/stop/close controls for blinds, shutters, curtains and
 * garage doors. The HMI page is named "cover"; its components are listed in
 * GUI_COVER_CONTROL_PAGE.
 *
 * EntityPage follows the cover state topic and holds its state, as for every entity, and
 * switches to this page. This class draws the page from that state and handles its touches.
 */
class CoverPage {
public:
  /**
   * Draw the page from the given state.
   * @param cover: The current cover state.
   * @param page_just_opened: EntityPage has just switched to the page.
   */
  static void update_display(const NSPanelEntityState__Cover *cover, bool page_just_opened);

  /**
   * Handle a touch on the page.
   */
  static void handle_touch(uint16_t component_id, bool pressed);

  /**
   * The page has closed.
   */
  static void unshow();

private:
  enum class _action {
    BACK,
    OPEN,
    STOP,
    CLOSE,
    POSITION,
  };

  // A touchable cover component. id starts as the ID in GUI_data.hpp and is replaced by the one
  // the Nextion reports when the page opens.
  struct _component_t {
    const char *name;
    _action action;
    std::atomic<uint8_t> id;
  };
  static _component_t _components[];

  /**
   * Look up the IDs of the touchable cover components by name. Called once per boot, when the
   * page is showing, because the Nextion only answers for components on the current page.
   * @return true if every component was resolved.
   */
  static bool _resolve_component_ids();

  /**
   * Send a command for the currently displayed cover to the manager.
   */
  static void _send_command(NSPanelMQTTManagerCommand__CoverCommand *command);

  static void _send_action(NSPanelMQTTManagerCommand__CoverCommand__Action action);
  static void _send_position(uint8_t position);

  static const char *_state_text(NSPanelEntityState__Cover__CoverState state);
  static void _set_visible(const char *component_name, bool visible);

  // Whether the page is showing. Cleared when the page closes so stale touch events are ignored.
  static inline std::atomic<bool> _showing = false;

  static inline std::atomic<uint32_t> _cover_id = 0;
  static inline std::atomic<bool> _component_ids_resolved = false;

  // The last position asked for from this page. It is shown on the slider until the cover
  // reports that it reached it, or until the request times out.
  static inline std::atomic<int32_t> _pending_position = -1;
  static inline std::atomic<int64_t> _pending_position_requested_at = 0;
  static constexpr int64_t _pending_position_timeout_us = 5 * 1000 * 1000;
};
