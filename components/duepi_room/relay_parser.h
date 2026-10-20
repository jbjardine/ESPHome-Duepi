#pragma once

#include "controller.h"

#include <string>

namespace esphome {
namespace duepi_room {

enum class ParseError : uint8_t {
  NONE,
  TOO_LARGE,
  NON_ASCII,
  MALFORMED_JSON,
  INVALID_OBJECT,
  MISSING_FIELD,
  WRONG_TYPE,
  UNSUPPORTED_VERSION,
  INVALID_VALUE,
};

// Parses exactly the frozen relay v1 envelope. Call invalidate_relay_source() on failure.
bool parse_relay_message(const std::string &payload, RelayMessage *message, ParseError *error = nullptr);

}  // namespace duepi_room
}  // namespace esphome
