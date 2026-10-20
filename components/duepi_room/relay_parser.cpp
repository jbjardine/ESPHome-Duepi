#include "relay_parser.h"

#include "esphome/components/json/json_util.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

namespace esphome {
namespace duepi_room {

namespace {

constexpr size_t MAX_RELAY_PAYLOAD_BYTES = 254;

bool fail(ParseError value, ParseError *error) {
  if (error != nullptr)
    *error = value;
  return false;
}

bool is_lower_hex_generation(const std::string &value) {
  if (value.size() != 32)
    return false;
  for (const char character : value) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
      return false;
  }
  return true;
}

bool is_decimal_sample_id(const std::string &value, bool allow_empty) {
  if (value.empty())
    return allow_empty;
  if (value.size() > 20)
    return false;
  for (const char character : value) {
    if (character < '0' || character > '9')
      return false;
  }
  return true;
}

bool has_member(JsonObject object, const char *field) {
  for (const auto member : object) {
    if (member.key() == field)
      return true;
  }
  return false;
}

bool read_unsigned_integer(JsonVariantConst value, uint32_t *result) {
  if (!value.is<uint32_t>())
    return false;
  *result = value.as<uint32_t>();
  return true;
}

bool parse_object(JsonObject object, RelayMessage *message, ParseError *error) {
  if (object.isNull()) {
    *error = ParseError::INVALID_OBJECT;
    return false;
  }

  for (const char *field : {"v", "generation", "seq", "valid", "age_s", "sample_id"}) {
    if (!has_member(object, field)) {
      *error = ParseError::MISSING_FIELD;
      return false;
    }
  }

  uint32_t version = 0;
  uint32_t sequence = 0;
  uint32_t age_seconds = 0;
  if (!read_unsigned_integer(object["v"], &version) || !object["generation"].is<const char *>() ||
      !read_unsigned_integer(object["seq"], &sequence) || !object["valid"].is<bool>() ||
      !read_unsigned_integer(object["age_s"], &age_seconds) || !object["sample_id"].is<const char *>()) {
    *error = ParseError::WRONG_TYPE;
    return false;
  }
  if (version != 1) {
    *error = ParseError::UNSUPPORTED_VERSION;
    return false;
  }
  if (sequence == 0) {
    *error = ParseError::INVALID_VALUE;
    return false;
  }

  RelayMessage parsed;
  parsed.version = static_cast<uint8_t>(version);
  parsed.generation = object["generation"].as<std::string>();
  parsed.sequence = sequence;
  parsed.valid = object["valid"].as<bool>();
  parsed.age_seconds = age_seconds;
  parsed.sample_id = object["sample_id"].as<std::string>();
  if (!is_lower_hex_generation(parsed.generation) ||
      !is_decimal_sample_id(parsed.sample_id, !parsed.valid)) {
    *error = ParseError::INVALID_VALUE;
    return false;
  }

  if (parsed.valid) {
    if (!has_member(object, "temp_c")) {
      *error = ParseError::MISSING_FIELD;
      return false;
    }
    const JsonVariantConst temperature = object["temp_c"];
    if (!temperature.is<float>() && !temperature.is<double>() && !temperature.is<int64_t>() &&
        !temperature.is<uint64_t>()) {
      *error = ParseError::WRONG_TYPE;
      return false;
    }
    parsed.temperature_c = temperature.as<float>();
    if (!std::isfinite(parsed.temperature_c)) {
      *error = ParseError::INVALID_VALUE;
      return false;
    }
    parsed.has_temperature = true;
  } else if (has_member(object, "temp_c")) {
    *error = ParseError::INVALID_VALUE;
    return false;
  }

  *message = std::move(parsed);
  *error = ParseError::NONE;
  return true;
}

}  // namespace

bool parse_relay_message(const std::string &payload, RelayMessage *message, ParseError *error) {
  if (error != nullptr)
    *error = ParseError::NONE;
  if (message == nullptr)
    return fail(ParseError::INVALID_VALUE, error);
  if (payload.size() > MAX_RELAY_PAYLOAD_BYTES)
    return fail(ParseError::TOO_LARGE, error);
  for (const unsigned char character : payload) {
    if (character > 0x7F)
      return fail(ParseError::NON_ASCII, error);
  }

  ParseError parse_error = ParseError::MALFORMED_JSON;
  const bool parsed = json::parse_json(payload, [&message, &parse_error](JsonObject root) {
    return parse_object(root, message, &parse_error);
  });
  if (parsed)
    return true;
  if (parse_error == ParseError::NONE)
    parse_error = ParseError::MALFORMED_JSON;
  return fail(parse_error, error);
}

}  // namespace duepi_room
}  // namespace esphome
