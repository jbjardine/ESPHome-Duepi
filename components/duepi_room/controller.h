#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace esphome {
namespace duepi_room {

enum class Mode : uint8_t {
  LOCAL,
  COMPENSATION,
  PI,
};

enum class Status : uint8_t {
  LOCAL,
  WAITING_STOVE,
  WAITING_SOURCE,
  REGULATING,
  FALLBACK,
  BRIDGE,
  STOVE_INACTIVE,
  AWAITING_CONFIRMATION,
  COMMAND_UNCONFIRMED,
  UART_BUSY,
};

struct Config {
  int native_min{10};
  int native_max{35};
  float fallback_target{20.0F};
  uint32_t max_source_age_seconds{900};
  uint32_t max_stove_age_ms{45000};
  uint32_t relay_silence_ms{180000};
  uint32_t minimum_write_interval_ms{300000};
  uint32_t confirmation_timeout_ms{30000};
  float kp{0.10F};
  float ki_per_hour{0.025F};
  float integral_limit{0.5F};
  float correction_limit{1.0F};
};

struct RelayMessage {
  uint8_t version{0};
  std::string generation;
  uint32_t sequence{0};
  bool valid{false};
  bool has_temperature{false};
  float temperature_c{0.0F};
  uint32_t age_seconds{0};
  std::string sample_id;
};

struct Decision {
  Status status{Status::LOCAL};
  Mode mode{Mode::LOCAL};
  bool target_requested{false};
  int target{0};
  bool source_age_available{false};
  uint32_t source_age_seconds{0};
  bool room_temperature_available{false};
  float room_temperature{0.0F};
  bool native_target_available{false};
  float native_target{0.0F};
  bool computed_target_available{false};
  float unclamped_target{0.0F};
  int computed_target{0};
  float correction{0.0F};
  float integral{0.0F};
  float error{0.0F};
};

using MonotonicClock = std::function<uint32_t()>;

class Controller {
 public:
  explicit Controller(Config config, MonotonicClock clock);

  // User-facing control. External modes remain inert until fresh postboot inputs arrive.
  void set_mode(Mode mode);
  bool set_room_target(float temperature_c);
  bool set_fallback_target(float temperature_c);

  // Observation hooks. Each accepted observation is timestamped by the injected monotonic clock.
  void observe_raw_stove_temperature(float temperature_c);
  void observe_raw_stove_phase(uint16_t phase);
  void observe_native_target(float target_c);
  bool observe_relay(const RelayMessage &message);
  void invalidate_relay_source();
  void observe_manual_native_target(float target_c);
  void on_explicit_mode_command(bool heat);
  void observe_bridge_connected(bool connected);
  void observe_uart_availability(bool available);

  // A target becomes an unconfirmed command only after the shared RF2 path emits it.
  Decision poll();
  bool authorize_rf2_emission(int target) const;
  bool on_rf2_emitted(int target);
  void on_rf2_not_emitted();

  Mode mode() const { return this->mode_; }
  float room_target() const { return this->room_target_; }
  const Config &config() const { return this->config_; }

 private:
  static uint32_t elapsed_ms(uint32_t now, uint32_t then) { return now - then; }
  static int round_ties_up(float value);

  uint32_t now_() const;
  bool fresh_(bool valid, uint32_t observed_at, uint32_t now) const;
  bool stove_inputs_fresh_(uint32_t now) const;
  bool source_usable_(uint32_t now) const;
  uint64_t source_age_ms_(uint32_t now) const;
  int clamp_native_(int target) const;
  void reset_integral_();
  void cancel_request_();
  void reset_after_input_loss_();
  void clear_source_(bool mark_seen);
  void refresh_confirmation_timeout_(uint32_t now);
  bool active_stove_(uint32_t now) const;
  Decision make_decision_(Status status) const;
  bool request_allowed_(bool fallback, int target, uint32_t now) const;
  float calculate_correction_(float error) const;
  void integrate_new_sample_(uint32_t now, float error, float stove_temperature, float base);

  Config config_;
  MonotonicClock clock_;
  uint32_t boot_ms_{0};
  Mode mode_{Mode::LOCAL};
  float room_target_{20.0F};
  bool room_target_valid_{true};

  float stove_temperature_{0.0F};
  uint32_t stove_temperature_ms_{0};
  bool stove_temperature_valid_{false};
  uint16_t stove_phase_{0};
  uint32_t stove_phase_ms_{0};
  uint32_t stove_phase_revision_{0};
  uint32_t required_phase_revision_{0};
  bool phase_after_heat_required_{false};
  bool stove_phase_valid_{false};
  float native_target_{0.0F};
  uint32_t native_target_ms_{0};
  bool native_target_valid_{false};
  float last_readback_target_{0.0F};
  bool last_readback_valid_{false};
  float divergent_target_{0.0F};
  uint8_t divergent_target_count_{0};

  RelayMessage source_;
  uint32_t source_packet_ms_{0};
  bool source_packet_seen_{false};
  bool source_present_{false};
  bool source_invalidated_{false};
  std::string generation_;
  uint32_t last_sequence_{0};
  bool has_sequence_{false};

  bool bridge_connected_{false};
  bool bridge_baseline_valid_{false};
  float bridge_baseline_target_{0.0F};
  bool bridge_latest_target_valid_{false};
  float bridge_latest_target_{0.0F};
  bool uart_available_{false};

  float integral_{0.0F};
  float correction_{0.0F};
  std::string last_integrated_sample_id_;
  uint32_t last_integrated_sample_ms_{0};
  bool has_integrated_sample_{false};

  bool request_offered_{false};
  int offered_target_{0};
  bool offered_fallback_{false};
  bool fallback_done_{false};
  bool pending_confirmation_{false};
  int emitted_target_{0};
  uint32_t emitted_ms_{0};
  bool command_unconfirmed_{false};
  uint32_t last_write_ms_{0};
  bool last_write_valid_{false};
};

}  // namespace duepi_room
}  // namespace esphome
