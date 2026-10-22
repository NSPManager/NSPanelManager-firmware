#include <EpochParser.hpp>
#include <limits>

std::optional<int64_t> EpochParser::parse_ms(const char *data, size_t length) {
  if (data == nullptr || length == 0) {
    return std::nullopt;
  }

  int64_t value = 0;
  for (size_t i = 0; i < length; i++) {
    if (data[i] < '0' || data[i] > '9') {
      return std::nullopt;
    }
    // '0'..'9' are consecutive in ASCII, so subtracting '0' gives the digit's value, ie. '8' - '0' == 8.
    int64_t digit = data[i] - '0';
    // Check before computing value * 10 + digit, as overflowing an int64_t is undefined behaviour:
    // value * 10 + digit <= max exactly when value <= (max - digit) / 10.
    if (value > (std::numeric_limits<int64_t>::max() - digit) / 10) {
      return std::nullopt; // Would overflow
    }
    value = value * 10 + digit;
  }

  if (value < EpochParser::earliest_plausible_ms) {
    return std::nullopt;
  }
  return value;
}
