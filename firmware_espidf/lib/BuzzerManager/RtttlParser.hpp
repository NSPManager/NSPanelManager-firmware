#pragma once
#include <BuzzerManager.hpp>
#include <esp_err.h>
#include <string>
#include <vector>

class RtttlParser {
public:
  /**
   * @brief Parse an RTTTL string, ie. "name:d=4,o=6,b=63:8c,8p,16e.7", into a sequence of tones and pauses.
   * Letters are case-insensitive and whitespace is ignored. Consecutive notes of the same pitch get a short
   * pause between them so they sound as separate notes on the buzzer.
   * @param rtttl: The RTTTL string to parse
   * @param steps: Replaced with the parsed sequence on success, left untouched on failure
   * @return ESP_OK on success, ESP_ERR_INVALID_ARG if the string could not be parsed or is empty,
   * ESP_ERR_INVALID_SIZE if the sequence is longer than max_steps or max_total_duration_ms.
   */
  static esp_err_t parse(const std::string &rtttl, std::vector<buzzer_tone_step_t> *steps);

  // Limits that keep a bad string from using a lot of memory or holding the buzzer on for long
  static constexpr const size_t max_steps = 256;
  static constexpr const uint32_t max_total_duration_ms = 30000;

private:
  /**
   * @brief Parse the defaults section, ie. "d=4,o=6,b=63". Missing values keep their current value.
   * @return true on success, false if the section is invalid
   */
  static bool _parse_defaults(const std::string &section, uint32_t *duration, uint32_t *octave, uint32_t *bpm);

  /**
   * @brief Parse a single note, ie. "8c#.7", into a step.
   * @return true on success, false if the note is invalid
   */
  static bool _parse_note(const std::string &note, uint32_t default_duration, uint32_t default_octave, uint32_t bpm, buzzer_tone_step_t *step);

  /**
   * @brief Parse a run of decimal digits starting at *pos, advancing *pos past them.
   * @return true if at least one digit was read and the value fits in max_value
   */
  static bool _parse_number(const std::string &text, size_t *pos, uint32_t max_value, uint32_t *value);

  static bool _is_valid_duration(uint32_t duration);

  // Gap inserted between consecutive notes of the same pitch
  static constexpr const uint32_t _articulation_gap_ms = 10;

  // Equal temperament frequencies (A4 = 440 Hz) of C8 through B8, in fixed point with _octave8_fraction_bits
  // fractional bits so lower octaves still round to the nearest Hz. Avoids pulling pow() into the firmware.
  static constexpr const uint32_t _octave8_fraction_bits = 4;
  static constexpr const uint32_t _octave8_frequencies[12] = {
      66976,  // C8  4186.01 Hz
      70959,  // C#8 4434.92 Hz
      75178,  // D8  4698.64 Hz
      79649,  // D#8 4978.03 Hz
      84385,  // E8  5274.04 Hz
      89402,  // F8  5587.65 Hz
      94719,  // F#8 5919.91 Hz
      100351, // G8  6271.93 Hz
      106318, // G#8 6644.88 Hz
      112640, // A8  7040.00 Hz
      119338, // A#8 7458.62 Hz
      126434, // B8  7902.13 Hz
  };
};
