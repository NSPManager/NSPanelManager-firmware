#pragma once
#include <string>

class LoadingPage {
public:
  /**
   * Show the loading page
   */
  static void show();

  /**
   * Unshow the loading page
   */
  static void unshow();

  /**
   * Is the loading page the page that is currently being shown on the Nextion display
   */
  static bool showing();

  /**
   * Set the primary text on the loading page
   */
  static void set_loading_text(std::string text);

  /**
   * Set the second row of text on the loading page
   */
  static void set_secondary_text(std::string text);

private:
  // Vars
  // Is the loading page the currently showing page
  static inline bool _currently_showing = false;
};