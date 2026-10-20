#include "duepi_room.h"

#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include <cmath>
#include <set>

namespace esphome {
namespace duepi_room {

namespace {

constexpr char TAG[] = "duepi_room";

Config make_config(int native_min, int native_max, float fallback_target, uint32_t max_source_age_seconds, float kp,
                   float ki_per_hour, float integral_limit, float correction_limit) {
  Config config;
  config.native_min = native_min;
  config.native_max = native_max;
  config.fallback_target = fallback_target;
  config.max_source_age_seconds = max_source_age_seconds;
  config.kp = kp;
  config.ki_per_hour = ki_per_hour;
  config.integral_limit = integral_limit;
  config.correction_limit = correction_limit;
  return config;
}

const char *status_name(Status status) {
  switch (status) {
    case Status::LOCAL_ONLY:
      return "local";
    case Status::WAITING_STOVE:
      return "waiting_stove";
    case Status::WAITING_SOURCE:
      return "waiting_source";
    case Status::REGULATING:
      return "regulating";
    case Status::FALLBACK:
      return "fallback";
    case Status::BRIDGE:
      return "bridge";
    case Status::STOVE_INACTIVE:
      return "stove_inactive";
    case Status::AWAITING_CONFIRMATION:
      return "awaiting_confirmation";
    case Status::COMMAND_UNCONFIRMED:
      return "command_unconfirmed";
    case Status::UART_BUSY:
      return "uart_busy";
  }
  return "local";
}

bool same_float(float left, float right) {
  if (std::isnan(left) || std::isnan(right))
    return std::isnan(left) && std::isnan(right);
  return std::fabs(left - right) < 0.01F;
}

bool parse_mode(const std::string &value, Mode *mode, size_t *index) {
  if (value == "Local") {
    *mode = Mode::LOCAL_ONLY;
    *index = 0;
    return true;
  }
  if (value == "Compensation") {
    *mode = Mode::COMPENSATION;
    *index = 1;
    return true;
  }
  if (value == "Compensation + PI") {
    *mode = Mode::PI_CONTROL;
    *index = 2;
    return true;
  }
  return false;
}

const char *mode_name(Mode mode) {
  switch (mode) {
    case Mode::LOCAL_ONLY:
      return "Local";
    case Mode::COMPENSATION:
      return "Compensation";
    case Mode::PI_CONTROL:
      return "Compensation + PI";
  }
  return "Local";
}

}  // namespace

DuepiRoomComponent::DuepiRoomComponent(int native_min, int native_max, float fallback_target,
                                       uint32_t max_source_age_seconds, float kp, float ki_per_hour,
                                       float integral_limit, float correction_limit)
    : PollingComponent(1000),
      controller_(make_config(native_min, native_max, fallback_target, max_source_age_seconds, kp, ki_per_hour,
                              integral_limit, correction_limit),
                  []() { return millis(); }) {}

DuepiRoomComponent *DuepiRoomComponent::instance_ = nullptr;

DuepiRoomComponent *DuepiRoomComponent::get_instance() { return instance_; }

void DuepiRoomComponent::setup() {
  instance_ = this;
  this->last_decision_ = this->controller_.poll();
  this->publish_decision_(this->last_decision_);
}

void DuepiRoomComponent::update() {
  this->last_decision_ = this->controller_.poll();
  this->publish_decision_(this->last_decision_);
  if (this->last_decision_.target_requested) {
    for (auto *trigger : this->rf2_target_triggers_) {
      if (trigger != nullptr)
        trigger->trigger(this->last_decision_.target);
    }
  }
}

void DuepiRoomComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Duepi room controller:");
  ESP_LOGCONFIG(TAG, "  Native range: %d..%d C", this->native_min(), this->native_max());
  ESP_LOGCONFIG(TAG, "  Relay marker: %s (%s)", DUEPI_ROOM_RELAY_NAME, DUEPI_ROOM_RELAY_OBJECT_ID);
  ESP_LOGCONFIG(TAG, "  Relay payload max: 254 bytes; initial mode: Local");
}

void DuepiRoomComponent::set_mode_from_select(Mode mode) {
  this->controller_.set_mode(mode);
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_mode(mode);
  this->publish_decision_(this->controller_.poll());
}

void DuepiRoomComponent::request_native_mode(bool heat) {
  // This is the only command-mode event. Stove observations never enter this path.
  this->controller_.on_explicit_mode_command(heat);
  if (!heat && this->mode_select_ != nullptr)
    this->mode_select_->publish_mode(Mode::LOCAL_ONLY, true);
  this->publish_decision_(this->controller_.poll());
  for (auto *trigger : this->native_mode_triggers_) {
    if (trigger != nullptr)
      trigger->trigger(heat);
  }
}

void DuepiRoomComponent::observe_raw_stove_phase(uint16_t phase) {
  this->controller_.observe_raw_stove_phase(phase);
  climate::ClimateMode observed_mode;
  if (this->room_climate_ != nullptr && map_stove_phase_to_display_mode(phase, &observed_mode))
    this->room_climate_->publish_observed_mode(observed_mode);
}

void DuepiRoomComponent::receive_relay_payload(const std::string &payload) {
  RelayMessage message;
  if (!parse_relay_message(payload, &message)) {
    this->controller_.invalidate_relay_source();
  } else {
    this->controller_.observe_relay(message);
  }
}

bool DuepiRoomComponent::set_room_target(float temperature_c) {
  if (!this->controller_.set_room_target(temperature_c))
    return false;
  if (this->room_climate_ != nullptr)
    this->room_climate_->publish_decision(this->last_decision_, this->controller_.room_target());
  return true;
}

bool DuepiRoomComponent::set_fallback_target(float temperature_c) {
  if (!this->controller_.set_fallback_target(temperature_c))
    return false;
  if (this->fallback_number_ != nullptr)
    this->fallback_number_->publish_state(this->controller_.config().fallback_target);
  this->publish_decision_(this->controller_.poll());
  return true;
}

bool DuepiRoomComponent::on_rf2_emitted(int target) {
  const bool emitted = this->controller_.on_rf2_emitted(target);
  if (emitted)
    this->publish_decision_(this->controller_.poll());
  return emitted;
}

void DuepiRoomComponent::on_rf2_not_emitted() {
  this->controller_.on_rf2_not_emitted();
}

void DuepiRoomComponent::publish_decision_(const Decision &decision) {
  if (this->room_climate_ != nullptr)
    this->room_climate_->publish_decision(decision, this->controller_.room_target());
  if (this->mode_select_ != nullptr)
    this->mode_select_->publish_mode(this->controller_.mode());
  if (this->fallback_number_ != nullptr)
    this->fallback_number_->publish_state(this->controller_.config().fallback_target);
  if (this->status_sensor_ != nullptr)
    this->status_sensor_->publish_decision(decision);
  for (auto *sensor : this->diagnostic_sensors_) {
    if (sensor != nullptr)
      sensor->publish_decision(decision);
  }
}

void DuepiRoomClimate::setup() {
  this->mode = climate::CLIMATE_MODE_OFF;
  this->target_temperature = this->parent_ != nullptr ? this->parent_->controller().room_target() : 20.0F;
  auto restored = this->restore_state_();
  if (restored.has_value()) {
    if (std::isfinite(restored->target_temperature) && this->parent_ != nullptr &&
        this->parent_->set_room_target(restored->target_temperature))
      this->target_temperature = restored->target_temperature;
    if (restored->mode == climate::CLIMATE_MODE_HEAT || restored->mode == climate::CLIMATE_MODE_OFF)
      this->mode = restored->mode;
  }
  this->publish_state();
}

void DuepiRoomClimate::publish_decision(const Decision &decision, float room_target) {
  const float current_temperature = decision.room_temperature_available ? decision.room_temperature : NAN;
  const bool changed = !same_float(this->current_temperature, current_temperature) ||
                       !same_float(this->target_temperature, room_target);
  this->current_temperature = current_temperature;
  this->target_temperature = room_target;
  if (changed)
    this->publish_state();
}

void DuepiRoomClimate::publish_observed_mode(climate::ClimateMode mode) {
  if (this->mode == mode)
    return;
  this->mode = mode;
  this->publish_state();
}

climate::ClimateTraits DuepiRoomClimate::traits() {
  climate::ClimateTraits traits;
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  const int minimum = this->parent_ != nullptr ? this->parent_->native_min() : 10;
  const int maximum = this->parent_ != nullptr ? this->parent_->native_max() : 35;
  traits.set_visual_min_temperature(static_cast<float>(minimum));
  traits.set_visual_max_temperature(static_cast<float>(maximum));
  return traits;
}

void DuepiRoomClimate::control(const climate::ClimateCall &call) {
  if (call.get_target_temperature().has_value() && this->parent_ != nullptr) {
    if (!this->parent_->set_room_target(*call.get_target_temperature()))
      return;
  }
  if (call.get_mode().has_value()) {
    const auto requested_mode = *call.get_mode();
    if (requested_mode != climate::CLIMATE_MODE_OFF && requested_mode != climate::CLIMATE_MODE_HEAT)
      return;
    this->mode = requested_mode;
    if (this->parent_ != nullptr)
      this->parent_->request_native_mode(requested_mode == climate::CLIMATE_MODE_HEAT);
  }
  if (this->parent_ != nullptr)
    this->publish_decision(this->parent_->controller().poll(), this->parent_->controller().room_target());
  else
    this->publish_state();
}

void DuepiRoomModeSelect::setup() {
  Mode selected_mode = Mode::LOCAL_ONLY;
  size_t selected_index = 0;
  this->preference_ = global_preferences->make_preference<size_t>(this->get_preference_hash());
  size_t restored_index = 0;
  if (this->preference_.load(&restored_index) && restored_index < 3) {
    selected_index = restored_index;
    selected_mode = restored_index == 1 ? Mode::COMPENSATION : restored_index == 2 ? Mode::PI_CONTROL : Mode::LOCAL_ONLY;
  }
  if (this->parent_ != nullptr)
    this->parent_->set_mode_from_select(selected_mode);
  this->publish_state(selected_index == 0 ? "Local" : selected_index == 1 ? "Compensation" : "Compensation + PI");
}

void DuepiRoomModeSelect::publish_mode(Mode mode, bool persist) {
  const std::string option = mode_name(mode);
  const optional<size_t> index = this->index_of(option);
  if (!index.has_value())
    return;
  this->publish_state(option);
  const size_t stored_index = *index;
  if (persist)
    this->preference_.save(&stored_index);
}

void DuepiRoomModeSelect::control(const std::string &value) {
  Mode selected_mode;
  size_t selected_index;
  if (!parse_mode(value, &selected_mode, &selected_index))
    return;
  if (this->parent_ != nullptr)
    this->parent_->set_mode_from_select(selected_mode);
  this->publish_state(value);
  this->preference_.save(&selected_index);
}

void DuepiRoomFallbackNumber::setup() {
  this->publish_state(this->parent_ != nullptr ? this->parent_->controller().config().fallback_target : 20.0F);
}

void DuepiRoomFallbackNumber::control(float value) {
  if (this->parent_ == nullptr || !this->parent_->set_fallback_target(value))
    return;
}

void DuepiRoomRelayText::setup() {
  // Stable status only; never echo inbound JSON to HA state.
  this->publish_state("ready");
}

void DuepiRoomRelayText::control(const std::string &value) {
  if (this->parent_ != nullptr)
    this->parent_->receive_relay_payload(value);
}

void DuepiRoomDiagnosticSensor::publish_decision(const Decision &decision) {
  float value = NAN;
  switch (this->metric_) {
    case DiagnosticMetric::SOURCE_AGE:
      if (decision.source_age_available)
        value = static_cast<float>(decision.source_age_seconds);
      break;
    case DiagnosticMetric::PI_CORRECTION:
      value = decision.correction;
      break;
    case DiagnosticMetric::COMPUTED_TARGET:
      if (decision.computed_target_available)
        value = static_cast<float>(decision.computed_target);
      break;
  }
  if (this->has_last_value_ && same_float(this->last_value_, value))
    return;
  this->last_value_ = value;
  this->has_last_value_ = true;
  this->publish_state(value);
}

void DuepiRoomStatusSensor::publish_decision(const Decision &decision) {
  const std::string value = status_name(decision.status);
  if (this->last_value_ == value)
    return;
  this->last_value_ = value;
  this->publish_state(value);
}

}  // namespace duepi_room
}  // namespace esphome

// Base-package YAML uses these no-ID entry points under USE_DUEPI_ROOM. This
// keeps ESPHome's YAML ID resolver independent of the opt-in package.
void duepi_room_observe_raw_stove_temperature(float temperature_c) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_observe_raw_stove_temperature(component, temperature_c);
}

void duepi_room_observe_raw_stove_phase(uint16_t phase) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_observe_raw_stove_phase(component, phase);
}

void duepi_room_observe_native_target(float target_c) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_observe_native_target(component, target_c);
}

void duepi_room_observe_manual_native_target(float target_c) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_observe_manual_native_target(component, target_c);
}

void duepi_room_observe_stove_alarm(bool active) {
  if (auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance())
    component->observe_stove_alarm(active);
}

void duepi_room_observe_bridge_connected(bool connected) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_observe_bridge_connected(component, connected);
}

void duepi_room_observe_uart_availability(bool available) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_observe_uart_availability(component, available);
}

bool duepi_room_authorize_rf2_emission(int target) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  return esphome::duepi_room::duepi_room_authorize_rf2_emission(component, target);
}

int duepi_room_native_min() {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  return component != nullptr ? component->native_min() : 10;
}

int duepi_room_native_max() {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  return component != nullptr ? component->native_max() : 35;
}

bool duepi_room_on_rf2_emitted(int target) {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  return esphome::duepi_room::duepi_room_on_rf2_emitted(component, target);
}

void duepi_room_on_rf2_not_emitted() {
  auto *component = esphome::duepi_room::DuepiRoomComponent::get_instance();
  esphome::duepi_room::duepi_room_on_rf2_not_emitted(component);
}
