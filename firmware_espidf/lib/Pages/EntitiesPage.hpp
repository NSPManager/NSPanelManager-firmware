#pragma once
#include <MutexWrapper.hpp>
#include <atomic>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <memory>
#include <protobuf_nspanel.pb-c.h>
#include <string>

class EntitiesPage {
public:
  enum display_type_t {
    ENTITIES,
    SCENES,
    GLOBAL_SCENES
  };

  /**
   * Show the entities page for entity control
   * @param display_type: What should be displayed
   */
  static void show(display_type_t display_type);

  /**
   * Unshow the entities page
   */
  static void unshow();

  /**
   * Touch input from the page's widgets. The LVGL callbacks only queue these (they run on the LVGL
   * task with the LVGL lock held); _task_handle_input acts on them, as that publishes to MQTT and
   * switches pages.
   */
  struct InputEvent {
    enum Type : uint8_t {
      BACK,
      PREVIOUS_PAGE,
      NEXT_PAGE,
      ICON_PRESSED,  // Touch down on a slot's icon: starts saving a scene
      ICON_RELEASED, // Touch up on a slot's icon: cancels saving a scene that is not saved yet
      ICON_CLICKED,  // Tap on a slot's icon: toggles the entity
      NAME_CLICKED,  // Tap on a slot's name: opens the entity, or activates the scene
    } type;
    uint8_t slot; // For the slot events: which slot, 0 to the page type - 1
  };

  /**
   * Queue touch input for _task_handle_input. Never blocks; input is dropped if the queue is full.
   */
  static void queue_input(InputEvent event);

  /**
   * The icon pack changed: rebuild the slots with its fonts, and redraw the page if it is shown.
   * Called with the LVGL lock held, see IconPackManager::init().
   */
  static void refresh_icons();

private:
  /**
   * Show the given entities page. Runs on the RoomManager event loop.
   */
  static void _update_display(const std::shared_ptr<NSPanelRoomEntitiesPage> &page);

  /**
   * Handle RoomManager event
   */
  static void _handle_roommanager_event(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

  /**
   * Act on queued touch input, see InputEvent.
   */
  static void _task_handle_input(void *param);

  /**
   * Show the previous or next entities, scenes or global scenes page.
   */
  static void _go_to_previous_page();
  static void _go_to_next_page();

  /**
   * Get the entity in the given slot of the given entities page, or nullptr if the slot is empty.
   */
  static NSPanelRoomEntitiesPage__EntitySlot *_get_entity_in_slot(const std::shared_ptr<NSPanelRoomEntitiesPage> &page, int32_t slot);

  /**
   * Handle a press on the entity button in the given slot.
   * Media players are opened on the entity page, all other entities are toggled.
   */
  static void _handle_entity_button_pressed(const std::shared_ptr<NSPanelRoomEntitiesPage> &page, int32_t slot);

  /**
   * Send toggle command to manager
   */
  static void _send_entity_toggle_command_to_manager(uint32_t entities_page_id, uint32_t entity_slot);

  /**
   * Count to three seconds and update progress on screen.
   * If the task has not been stopped it will send the command to save the scene.
   * @param scene_slot: int32_t of which slot to save scene.
   */
  static void _task_save_scene_progress(void *scene_slot);

  // Vars
  // Shared between tasks: written by the RoomManager event loop and unshow(), read by the input task
  // and the save-scene task.
  //
  // Never dereference this directly. `->` on a shared_ptr returns a raw pointer and holds no reference,
  // so if another task replaces or clears the page while you are reading through it, the old page can be
  // freed underneath you (this member is often its last owner). Instead, call get() once per entry point,
  // keep that local copy for the whole operation, and pass it down by const&. The copy holds a reference,
  // so the page stays alive until you are done.
  //
  // The mutex protects taking the copy itself: only the refcount inside a shared_ptr is atomic, so copying
  // it while another task assigns it is a data race.
  static inline MutexWrapped<std::shared_ptr<NSPanelRoomEntitiesPage>> _current_entities_page;

  static inline std::atomic<display_type_t> _display_type = display_type_t::ENTITIES;

  // Task handle for task that will update the screen and save a scene.
  static inline TaskHandle_t _save_scene_task_handle = NULL;

  // The entities page slot to save the scene.
  static inline std::atomic<bool> _save_scene = false;

  // Touch input waiting for _task_handle_input
  static inline QueueHandle_t _input_queue = NULL;
};
