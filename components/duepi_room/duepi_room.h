#pragma once

#include "controller.h"
#include "relay_parser.h"

#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text/text.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"

#include <string>
#include <vector>

namespace esphome {
namespace duepi_room {

class DuepiRoomClimate;
class DuepiRoomModeSelect;
class DuepiRoomFallbackNumber;
class DuepiRoomRelayText;
class DuepiRoomDiagnosticSensor;
class DuepiRoomStatusSensor;

// Keep this enum unscoped so ESPHome's Python codegen can emit the enumerators
// from the duepi_room namespace when wiring metric configuration.
enum DiagnosticMetric : uint8_t {
  SOURCE_AGE,
  PI_CORRECTION,
  COMPUTED_TARGET,
};

inline bool map_stove_phase_to_display_mode(uint16_t phase, climate::ClimateMode *mode) {
  if (mode == nullptr)
    return false;
  switch (phase) {
    case 0x0000:  // Off
    case 0x0301:  // Stop sequence
    case 0x0801:  // Cooldown 1
    case 0x0802:  // Cooldown 2
      *mode = climate::CLIMATE_MODE_OFF;
      return true;
    case 0x0101:  // Ignition
    case 0x0102:  // Load wood
    case 0x0103:  // Fire on
    case 0x0104:  // Fire and vent on
    case 0x0105:  // Fire on E693
    case 0x010A:  // Delayed on
    case 0x0132:  // Load wood V100
    case 0x0133:  // Ignition V100
    case 0x0200:  // On, cleaning
    case 0x0201:  // On
    case 0x0401:  // Fire cleaning
    case 0x1000:  // Eco
    case 0x1001:  // Eco cooldown 1
    case 0x1002:  // Eco cooldown 2
    case 0x1003:  // Eco off (legacy climate reports on)
      *mode = climate::CLIMATE_MODE_HEAT;
      return true;
    default:
      return false;
  }
}

class DuepiRoomComponent : public PollingComponent {
 public:
  DuepiRoomComponent(int native_min, int native_max, float fallback_target, uint32_t max_source_age_seconds,
                     float kp, float ki_per_hour, float integral_limit, float correction_limit);

  void setup() override;
  void update() override;
  void dump_config() override;

  static DuepiRoomComponent *get_instance();

  Controller &controller() { return this->controller_; }
  const Controller &controller() const { return this->controller_; }
  void set_room_climate(DuepiRoomClimate *climate) { this->room_climate_ = climate; }
  void set_mode_select(DuepiRoomModeSelect *select) { this->mode_select_ = select; }
  void set_fallback_number(DuepiRoomFallbackNumber *number) { this->fallback_number_ = number; }
  void set_relay_text(DuepiRoomRelayText *text) { this->relay_text_ = text; }
  void add_diagnostic_sensor(DuepiRoomDiagnosticSensor *sensor) { this->diagnostic_sensors_.push_back(sensor); }
  void set_status_sensor(DuepiRoomStatusSensor *sensor) { this->status_sensor_ = sensor; }

  void set_mode_from_select(Mode mode);
  void request_native_mode(bool heat);
  void receive_relay_payload(const std::string &payload);
  bool set_room_target(float temperature_c);
  bool set_fallback_target(float temperature_c);

  void observe_raw_stove_temperature(float temperature_c) {
    this->controller_.observe_raw_stove_temperature(temperature_c);
  }
  void observe_raw_stove_phase(uint16_t phase);
  void observe_stove_alarm(bool active) { this->controller_.observe_stove_alarm(active); }
  void observe_native_target(float target_c) { this->controller_.observe_native_target(target_c); }
  void observe_manual_native_target(float target_c) { this->controller_.observe_manual_native_target(target_c); }
  void observe_bridge_connected(bool connected) { this->controller_.observe_bridge_connected(connected); }
  void observe_uart_availability(bool available) { this->controller_.observe_uart_availability(available); }

  bool authorize_rf2_emission(int target) const { return this->controller_.authorize_rf2_emission(target); }
  bool on_rf2_emitted(int target);
  void on_rf2_not_emitted();

  void add_rf2_target_trigger(Trigger<int> *trigger) { this->rf2_target_triggers_.push_back(trigger); }
  void add_native_mode_trigger(Trigger<bool> *trigger) { this->native_mode_triggers_.push_back(trigger); }

  int native_min() const { return this->controller_.config().native_min; }
  int native_max() const { return this->controller_.config().native_max; }

 protected:
  void publish_decision_(const Decision &decision);

  static DuepiRoomComponent *instance_;

  Controller controller_;
  Decision last_decision_{};
  DuepiRoomClimate *room_climate_{nullptr};
  DuepiRoomModeSelect *mode_select_{nullptr};
  DuepiRoomFallbackNumber *fallback_number_{nullptr};
  DuepiRoomRelayText *relay_text_{nullptr};
  DuepiRoomStatusSensor *status_sensor_{nullptr};
  std::vector<DuepiRoomDiagnosticSensor *> diagnostic_sensors_;
  std::vector<Trigger<int> *> rf2_target_triggers_;
  std::vector<Trigger<bool> *> native_mode_triggers_;
};

class DuepiRoomClimate : public climate::Climate, public Component {
 public:
  void set_parent(DuepiRoomComponent *parent) { this->parent_ = parent; }
  void setup() override;
  void publish_decision(const Decision &decision, float room_target);
  void publish_observed_mode(climate::ClimateMode mode);

 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;

  DuepiRoomComponent *parent_{nullptr};
};

class DuepiRoomModeSelect : public select::Select, public Component {
 public:
  void set_parent(DuepiRoomComponent *parent) { this->parent_ = parent; }
  void setup() override;
  void publish_mode(Mode mode, bool persist = false);

 protected:
  void control(const std::string &value) override;
  DuepiRoomComponent *parent_{nullptr};
  ESPPreferenceObject preference_;
};

class DuepiRoomFallbackNumber : public number::Number, public Component {
 public:
  void set_parent(DuepiRoomComponent *parent) { this->parent_ = parent; }
  void setup() override;

 protected:
  void control(float value) override;
  DuepiRoomComponent *parent_{nullptr};
};

class DuepiRoomRelayText : public text::Text, public Component {
 public:
  void set_parent(DuepiRoomComponent *parent) { this->parent_ = parent; }
  void setup() override;

 protected:
  void control(const std::string &value) override;
  DuepiRoomComponent *parent_{nullptr};
};

class DuepiRoomDiagnosticSensor : public sensor::Sensor {
 public:
  void set_parent(DuepiRoomComponent *parent) { this->parent_ = parent; }
  void set_metric(DiagnosticMetric metric) { this->metric_ = metric; }
  void publish_decision(const Decision &decision);

 protected:
  DuepiRoomComponent *parent_{nullptr};
  DiagnosticMetric metric_{DiagnosticMetric::SOURCE_AGE};
  float last_value_{NAN};
  bool has_last_value_{false};
};

class DuepiRoomStatusSensor : public text_sensor::TextSensor {
 public:
  void set_parent(DuepiRoomComponent *parent) { this->parent_ = parent; }
  void publish_decision(const Decision &decision);

 protected:
  DuepiRoomComponent *parent_{nullptr};
  std::string last_value_;
};

// Marker is intentionally stable so HA can identify the writable v1 relay by original name/id.
static constexpr const char *DUEPI_ROOM_RELAY_NAME = "Duepi Room Relay v1";
static constexpr const char *DUEPI_ROOM_RELAY_OBJECT_ID = "duepi_room_relay_v1";

// Null-safe boundary helpers for legacy C++ call sites guarded by USE_DUEPI_ROOM.
inline void duepi_room_observe_raw_stove_temperature(DuepiRoomComponent *component, float temperature_c) {
  if (component != nullptr)
    component->observe_raw_stove_temperature(temperature_c);
}
inline void duepi_room_observe_raw_stove_phase(DuepiRoomComponent *component, uint16_t phase) {
  if (component != nullptr)
    component->observe_raw_stove_phase(phase);
}
inline void duepi_room_observe_native_target(DuepiRoomComponent *component, float target_c) {
  if (component != nullptr)
    component->observe_native_target(target_c);
}
inline void duepi_room_observe_manual_native_target(DuepiRoomComponent *component, float target_c) {
  if (component != nullptr)
    component->observe_manual_native_target(target_c);
}
inline void duepi_room_observe_bridge_connected(DuepiRoomComponent *component, bool connected) {
  if (component != nullptr)
    component->observe_bridge_connected(connected);
}
inline void duepi_room_observe_uart_availability(DuepiRoomComponent *component, bool available) {
  if (component != nullptr)
    component->observe_uart_availability(available);
}
inline void duepi_room_receive_relay_payload(DuepiRoomComponent *component, const std::string &payload) {
  if (component != nullptr)
    component->receive_relay_payload(payload);
}
inline bool duepi_room_authorize_rf2_emission(DuepiRoomComponent *component, int target) {
  return component != nullptr && component->authorize_rf2_emission(target);
}
inline bool duepi_room_on_rf2_emitted(DuepiRoomComponent *component, int target) {
  return component != nullptr && component->on_rf2_emitted(target);
}
inline void duepi_room_on_rf2_not_emitted(DuepiRoomComponent *component) {
  if (component != nullptr)
    component->on_rf2_not_emitted();
}

class RF2TargetTrigger : public Trigger<int> {
 public:
  explicit RF2TargetTrigger(DuepiRoomComponent *parent) { parent->add_rf2_target_trigger(this); }
};

class NativeModeCommandTrigger : public Trigger<bool> {
 public:
  explicit NativeModeCommandTrigger(DuepiRoomComponent *parent) { parent->add_native_mode_trigger(this); }
};

}  // namespace duepi_room
}  // namespace esphome
