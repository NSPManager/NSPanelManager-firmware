#include <RtttlParser.hpp>
#include <cctype>

esp_err_t RtttlParser::parse(const std::string &rtttl, std::vector<buzzer_tone_step_t> *steps) {
  // Normalize so the rest of the parser only deals with lower case and no whitespace
  std::string text;
  text.reserve(rtttl.size());
  for (char c : rtttl) {
    if (!std::isspace(static_cast<unsigned char>(c))) {
      text.push_back(std::tolower(static_cast<unsigned char>(c)));
    }
  }

  // Format is "name:defaults:notes", name and defaults may be empty
  size_t first_colon = text.find(':');
  size_t second_colon = first_colon == std::string::npos ? std::string::npos : text.find(':', first_colon + 1);
  if (second_colon == std::string::npos || text.find(':', second_colon + 1) != std::string::npos) {
    return ESP_ERR_INVALID_ARG;
  }

  uint32_t default_duration = 4;
  uint32_t default_octave = 6;
  uint32_t bpm = 63;
  if (!RtttlParser::_parse_defaults(text.substr(first_colon + 1, second_colon - first_colon - 1), &default_duration, &default_octave, &bpm)) {
    return ESP_ERR_INVALID_ARG;
  }

  std::vector<buzzer_tone_step_t> result;
  uint32_t total_duration_ms = 0;
  size_t note_start = second_colon + 1;
  while (note_start <= text.size()) {
    size_t note_end = text.find(',', note_start);
    if (note_end == std::string::npos) {
      note_end = text.size();
    }

    if (note_start == note_end && note_end == text.size() && !result.empty()) {
      break; // Trailing comma
    }

    buzzer_tone_step_t step;
    if (!RtttlParser::_parse_note(text.substr(note_start, note_end - note_start), default_duration, default_octave, bpm, &step)) {
      return ESP_ERR_INVALID_ARG;
    }

    // Two notes of the same pitch back to back would sound like one long note, so cut a short gap from the end of the first
    if (!result.empty() && step.frequency_hz > 0 && result.back().frequency_hz == step.frequency_hz && result.back().duration_ms > RtttlParser::_articulation_gap_ms * 2) {
      result.back().duration_ms -= RtttlParser::_articulation_gap_ms;
      result.push_back({0, RtttlParser::_articulation_gap_ms});
    }

    result.push_back(step);
    total_duration_ms += step.duration_ms;
    if (result.size() > RtttlParser::max_steps || total_duration_ms > RtttlParser::max_total_duration_ms) {
      return ESP_ERR_INVALID_SIZE;
    }
    note_start = note_end + 1;
  }

  if (result.empty()) {
    return ESP_ERR_INVALID_ARG;
  }

  *steps = std::move(result);
  return ESP_OK;
}

bool RtttlParser::_parse_defaults(const std::string &section, uint32_t *duration, uint32_t *octave, uint32_t *bpm) {
  size_t pos = 0;
  while (pos < section.size()) {
    // Each entry is "<key>=<number>", separated by commas
    if (pos + 2 > section.size() || section[pos + 1] != '=') {
      return false;
    }
    char key = section[pos];
    pos += 2;

    uint32_t value;
    if (!RtttlParser::_parse_number(section, &pos, 999, &value)) {
      return false;
    }

    if (key == 'd' && RtttlParser::_is_valid_duration(value)) {
      *duration = value;
    } else if (key == 'o' && value >= 1 && value <= 8) {
      *octave = value;
    } else if (key == 'b' && value >= 1 && value <= 900) {
      *bpm = value;
    } else {
      return false;
    }

    if (pos < section.size()) {
      if (section[pos] != ',') {
        return false;
      }
      pos++;
    }
  }
  return true;
}

bool RtttlParser::_parse_note(const std::string &note, uint32_t default_duration, uint32_t default_octave, uint32_t bpm, buzzer_tone_step_t *step) {
  size_t pos = 0;

  uint32_t duration = default_duration;
  if (pos < note.size() && std::isdigit(static_cast<unsigned char>(note[pos]))) {
    if (!RtttlParser::_parse_number(note, &pos, 64, &duration) || !RtttlParser::_is_valid_duration(duration)) {
      return false;
    }
  }

  if (pos >= note.size()) {
    return false;
  }

  // Semitone within the octave counted from C, -1 for a pause
  int semitone;
  switch (note[pos]) {
  case 'c':
    semitone = 0;
    break;
  case 'd':
    semitone = 2;
    break;
  case 'e':
    semitone = 4;
    break;
  case 'f':
    semitone = 5;
    break;
  case 'g':
    semitone = 7;
    break;
  case 'a':
    semitone = 9;
    break;
  case 'b':
  case 'h':
    semitone = 11;
    break;
  case 'p':
    semitone = -1;
    break;
  default:
    return false;
  }
  pos++;

  if (pos < note.size() && note[pos] == '#') {
    if (semitone < 0) {
      return false;
    }
    semitone++;
    pos++;
  }

  // The dot is placed either before or after the octave depending on who wrote the RTTTL string, accept both
  bool dotted = false;
  if (pos < note.size() && note[pos] == '.') {
    dotted = true;
    pos++;
  }

  uint32_t octave = default_octave;
  if (pos < note.size() && std::isdigit(static_cast<unsigned char>(note[pos]))) {
    octave = note[pos] - '0';
    if (octave < 1 || octave > 8) {
      return false;
    }
    pos++;
  }

  if (!dotted && pos < note.size() && note[pos] == '.') {
    dotted = true;
    pos++;
  }

  if (pos != note.size()) {
    return false;
  }

  // A whole note lasts 4 beats. Dotted notes last 1.5 times as long. Round to the nearest ms.
  uint32_t numerator = 240000 * (dotted ? 3 : 2);
  uint32_t denominator = bpm * duration * 2;
  step->duration_ms = (numerator + denominator / 2) / denominator;

  if (semitone < 0) {
    step->frequency_hz = 0;
  } else {
    // B# is C in the next octave
    if (semitone == 12) {
      semitone = 0;
      octave++;
    }
    // Each octave down halves the frequency. Shift out the table's fixed point bits too, rounding to the nearest Hz.
    uint32_t shift = RtttlParser::_octave8_fraction_bits + 8 - octave;
    step->frequency_hz = (RtttlParser::_octave8_frequencies[semitone] + (1 << (shift - 1))) >> shift;
  }
  return true;
}

bool RtttlParser::_parse_number(const std::string &text, size_t *pos, uint32_t max_value, uint32_t *value) {
  size_t start = *pos;
  uint32_t result = 0;
  while (*pos < text.size() && std::isdigit(static_cast<unsigned char>(text[*pos]))) {
    result = result * 10 + (text[*pos] - '0');
    if (result > max_value) {
      return false;
    }
    (*pos)++;
  }

  if (*pos == start) {
    return false;
  }
  *value = result;
  return true;
}

bool RtttlParser::_is_valid_duration(uint32_t duration) {
  return duration == 1 || duration == 2 || duration == 4 || duration == 8 || duration == 16 || duration == 32 || duration == 64;
}
