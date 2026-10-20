#include "controller.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace esphome {
namespace duepi_room {

namespace {

constexpr uint16_t ACTIVE_BURNING_PHASE = 0x0201;
constexpr float TARGET_EPSILON = 0.49F;
constexpr float STABLE_TARGET_EPSILON = 0.01F;

float finite_nonnegative(float value) { return std::isfinite(value) && value > 0.0F ? value : 0.0F; }

bool close_enough(float left, float right, float tolerance = STABLE_TARGET_EPSILON) {
  return std::fabs(left - right) <= tolerance;
}

}  // namespace

Controller::Controller(Config config, MonotonicClock clock) : config_(config), clock_(std::move(clock)) {
  if (this->config_.native_max < this->config_.native_min)
    this->config_.native_max = this->config_.native_min;
  this->config_.fallback_target = std::isfinite(this->config_.fallback_target)
                                      ? std::clamp(this->config_.fallback_target,
                                                   static_cast<float>(this->config_.native_min),
                                                   static_cast<float>(this->config_.native_max))
                                      : static_cast<float>(this->config_.native_min);
  this->config_.kp = finite_nonnegative(this->config_.kp);
  this->config_.ki_per_hour = finite_nonnegative(this->config_.ki_per_hour);
  this->config_.integral_limit = finite_nonnegative(this->config_.integral_limit);
  this->config_.correction_limit = finite_nonnegative(this->config_.correction_limit);
  this->room_target_ = std::clamp(20.0F, static_cast<float>(this->config_.native_min),
                                 static_cast<float>(this->config_.native_max));
  this->boot_ms_ = this->now_();
}

void Controller::set_mode(Mode mode) {
  this->mode_ = mode;
  this->reset_integral_();
  this->cancel_request_();
  this->pending_confirmation_ = false;
  this->command_unconfirmed_ = false;
  this->divergent_target_count_ = 0;
  if (mode == Mode::LOCAL) {
    this->fallback_done_ = false;
    return;
  }
  this->fallback_done_ = false;
}

bool Controller::set_room_target(float temperature_c) {
  if (!std::isfinite(temperature_c) || temperature_c < this->config_.native_min ||
      temperature_c > this->config_.native_max)
    return false;
  if (close_enough(this->room_target_, temperature_c))
    return true;
  this->room_target_ = temperature_c;
  this->room_target_valid_ = true;
  this->reset_integral_();
  this->cancel_request_();
  return true;
}

bool Controller::set_fallback_target(float temperature_c) {
  if (!std::isfinite(temperature_c))
    return false;
  const float bounded = std::clamp(temperature_c, static_cast<float>(this->config_.native_min),
                                  static_cast<float>(this->config_.native_max));
  if (close_enough(this->config_.fallback_target, bounded))
    return true;
  this->config_.fallback_target = bounded;
  this->fallback_done_ = false;
  this->cancel_request_();
  return true;
}

void Controller::observe_raw_stove_temperature(float temperature_c) {
  const bool was_valid = this->stove_temperature_valid_;
  const float previous = this->stove_temperature_;
  if (!std::isfinite(temperature_c)) {
    this->stove_temperature_valid_ = false;
    this->reset_after_input_loss_();
    return;
  }
  this->stove_temperature_ = temperature_c;
  this->stove_temperature_ms_ = this->now_();
  this->stove_temperature_valid_ = true;
  if (!was_valid || !close_enough(previous, temperature_c))
    this->cancel_request_();
}

void Controller::observe_raw_stove_phase(uint16_t phase) {
  this->stove_phase_ = phase;
  this->stove_phase_ms_ = this->now_();
  this->stove_phase_valid_ = true;
  this->stove_phase_revision_++;
  if (this->phase_after_heat_required_ && this->stove_phase_revision_ == this->required_phase_revision_)
    this->phase_after_heat_required_ = false;
  if (phase != ACTIVE_BURNING_PHASE) {
    this->reset_integral_();
    this->cancel_request_();
  }
}

void Controller::observe_native_target(float target_c) {
  const uint32_t now = this->now_();
  this->refresh_confirmation_timeout_(now);
  if (!std::isfinite(target_c)) {
    this->native_target_valid_ = false;
    this->reset_after_input_loss_();
    return;
  }

  const float previous_target = this->native_target_;
  const bool previous_valid = this->native_target_valid_;
  this->native_target_ = target_c;
  this->native_target_ms_ = now;
  this->native_target_valid_ = true;

  if (this->bridge_connected_) {
    this->bridge_latest_target_ = target_c;
    this->bridge_latest_target_valid_ = true;
    return;
  }

  if (this->pending_confirmation_) {
    if (close_enough(target_c, static_cast<float>(this->emitted_target_))) {
      this->pending_confirmation_ = false;
      this->last_readback_target_ = target_c;
      this->last_readback_valid_ = true;
      this->divergent_target_count_ = 0;
    }
    return;
  }

  if (this->mode_ != Mode::LOCAL && !this->command_unconfirmed_) {
    if (!this->last_readback_valid_) {
      this->last_readback_target_ = target_c;
      this->last_readback_valid_ = true;
      this->divergent_target_count_ = 0;
    } else if (std::fabs(target_c - this->last_readback_target_) >= TARGET_EPSILON) {
      this->cancel_request_();
      if (this->divergent_target_count_ != 0 &&
          close_enough(this->divergent_target_, target_c, TARGET_EPSILON)) {
        this->divergent_target_count_++;
      } else {
        this->divergent_target_ = target_c;
        this->divergent_target_count_ = 1;
      }
      if (this->divergent_target_count_ >= 2) {
        this->set_mode(Mode::LOCAL);
        this->native_target_ = target_c;
        this->native_target_ms_ = now;
        this->native_target_valid_ = true;
        this->last_readback_target_ = target_c;
        this->last_readback_valid_ = true;
      }
      return;
    } else {
      this->divergent_target_count_ = 0;
      this->divergent_target_ = target_c;
      this->last_readback_target_ = target_c;
    }
  }

  if (previous_valid && !close_enough(previous_target, target_c))
    this->cancel_request_();
}

bool Controller::observe_relay(const RelayMessage &message) {
  bool valid_message = message.version == 1 && message.generation.size() == 32 && message.sequence != 0;
  for (const char character : message.generation) {
    if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f')))
      valid_message = false;
  }
  if (message.valid) {
    if (!message.has_temperature || !std::isfinite(message.temperature_c) || message.sample_id.empty() ||
        message.sample_id.size() > 20)
      valid_message = false;
    for (const char character : message.sample_id) {
      if (character < '0' || character > '9')
        valid_message = false;
    }
  } else if (message.has_temperature || !message.sample_id.empty()) {
    valid_message = false;
  }

  if (!valid_message) {
    this->invalidate_relay_source();
    return false;
  }

  const bool new_generation = this->generation_.empty() || this->generation_ != message.generation;
  if (new_generation) {
    this->generation_ = message.generation;
    this->has_sequence_ = false;
    this->source_present_ = false;
    this->source_invalidated_ = false;
    this->reset_integral_();
    this->cancel_request_();
    this->fallback_done_ = false;
  }
  if (this->has_sequence_ && message.sequence <= this->last_sequence_)
    return false;

  const bool had_usable_source = this->source_present_ && this->source_.valid;
  const bool new_sample = message.valid &&
                          (!this->source_present_ || this->source_.sample_id != message.sample_id || new_generation);
  this->last_sequence_ = message.sequence;
  this->has_sequence_ = true;
  this->source_packet_ms_ = this->now_();
  this->source_packet_seen_ = true;
  this->source_ = message;
  if (!message.valid) {
    this->source_present_ = false;
    this->source_invalidated_ = true;
    this->reset_integral_();
    this->cancel_request_();
    if (had_usable_source)
      this->fallback_done_ = false;
    return true;
  }

  this->source_present_ = true;
  this->source_invalidated_ = false;
  this->fallback_done_ = false;
  if (this->request_offered_ && this->offered_fallback_)
    this->cancel_request_();
  if (new_sample)
    this->cancel_request_();
  return true;
}

void Controller::invalidate_relay_source() {
  this->clear_source_(true);
}

void Controller::observe_manual_native_target(float target_c) {
  if (std::isfinite(target_c)) {
    this->native_target_ = target_c;
    this->native_target_ms_ = this->now_();
    this->native_target_valid_ = true;
    this->last_readback_target_ = target_c;
    this->last_readback_valid_ = true;
  }
  this->set_mode(Mode::LOCAL);
}

void Controller::on_explicit_mode_command(bool heat) {
  if (!heat) {
    this->set_mode(Mode::LOCAL);
    return;
  }
  // HEAT is routed to the existing native mode path; it must not activate external regulation.
  if (this->mode_ == Mode::LOCAL)
    return;
  this->reset_integral_();
  this->cancel_request_();
  this->phase_after_heat_required_ = true;
  this->required_phase_revision_ = this->stove_phase_revision_ + 1;
}

void Controller::observe_bridge_connected(bool connected) {
  if (connected == this->bridge_connected_)
    return;
  if (connected) {
    this->bridge_connected_ = true;
    this->bridge_baseline_valid_ = this->native_target_valid_ &&
                                   this->fresh_(this->native_target_valid_, this->native_target_ms_, this->now_());
    this->bridge_baseline_target_ = this->native_target_;
    this->bridge_latest_target_valid_ = this->bridge_baseline_valid_;
    this->bridge_latest_target_ = this->native_target_;
    this->pending_confirmation_ = false;
    this->command_unconfirmed_ = false;
    this->reset_integral_();
    this->cancel_request_();
    return;
  }

  const bool changed_target = this->bridge_baseline_valid_ && this->bridge_latest_target_valid_ &&
                              std::fabs(this->bridge_latest_target_ - this->bridge_baseline_target_) >=
                                  TARGET_EPSILON;
  this->bridge_connected_ = false;
  if (changed_target)
    this->set_mode(Mode::LOCAL);
  this->stove_temperature_valid_ = false;
  this->stove_phase_valid_ = false;
  this->native_target_valid_ = false;
  this->last_readback_valid_ = false;
  this->bridge_baseline_valid_ = false;
  this->bridge_latest_target_valid_ = false;
  this->clear_source_(true);
  this->reset_integral_();
  this->cancel_request_();
}

void Controller::observe_uart_availability(bool available) {
  this->uart_available_ = available;
  if (!available)
    this->cancel_request_();
}

Decision Controller::poll() {
  const uint32_t now = this->now_();
  this->refresh_confirmation_timeout_(now);

  if (this->mode_ == Mode::LOCAL)
    return this->make_decision_(Status::LOCAL);
  if (this->bridge_connected_)
    return this->make_decision_(Status::BRIDGE);
  if (this->command_unconfirmed_)
    return this->make_decision_(Status::COMMAND_UNCONFIRMED);
  if (this->pending_confirmation_)
    return this->make_decision_(Status::AWAITING_CONFIRMATION);

  if (this->stove_phase_valid_ && this->fresh_(true, this->stove_phase_ms_, now) &&
      this->stove_phase_ != ACTIVE_BURNING_PHASE) {
    this->reset_integral_();
    this->cancel_request_();
    return this->make_decision_(Status::STOVE_INACTIVE);
  }

  if (!this->stove_inputs_fresh_(now)) {
    this->reset_after_input_loss_();
    return this->make_decision_(Status::WAITING_STOVE);
  }

  if (!this->source_usable_(now)) {
    this->reset_integral_();
    this->cancel_request_();
    if (!this->source_packet_seen_ && !this->source_invalidated_ &&
        elapsed_ms(now, this->boot_ms_) < this->config_.relay_silence_ms)
      return this->make_decision_(Status::WAITING_SOURCE);

    Decision decision = this->make_decision_(Status::FALLBACK);
    const int fallback = this->clamp_native_(this->round_ties_up(this->config_.fallback_target));
    decision.computed_target_available = true;
    decision.unclamped_target = this->config_.fallback_target;
    decision.computed_target = fallback;
    decision.correction = 0.0F;
    decision.integral = 0.0F;
    if (this->fallback_done_ || this->round_ties_up(this->native_target_) == fallback) {
      this->fallback_done_ = true;
      this->cancel_request_();
      return decision;
    }
    if (!this->uart_available_) {
      decision.status = Status::UART_BUSY;
      return decision;
    }
    if (!this->request_offered_) {
      this->request_offered_ = true;
      this->offered_target_ = fallback;
      this->offered_fallback_ = true;
      decision.target_requested = true;
      decision.target = fallback;
    }
    return decision;
  }

  const float error = this->room_target_ - this->source_.temperature_c;
  const float base = this->stove_temperature_ + error;
  if (this->mode_ == Mode::PI) {
    this->integrate_new_sample_(now, error, this->stove_temperature_, base);
  } else {
    this->integral_ = 0.0F;
    this->correction_ = 0.0F;
  }

  const float unclamped_target = base + this->correction_;
  const int computed_target = this->clamp_native_(this->round_ties_up(unclamped_target));
  Decision decision = this->make_decision_(Status::REGULATING);
  decision.error = error;
  decision.correction = this->correction_;
  decision.integral = this->integral_;
  decision.computed_target_available = true;
  decision.unclamped_target = unclamped_target;
  decision.computed_target = computed_target;

  if (this->round_ties_up(this->native_target_) == computed_target) {
    this->cancel_request_();
    return decision;
  }
  if (!this->uart_available_) {
    decision.status = Status::UART_BUSY;
    return decision;
  }
  if (this->last_write_valid_ && elapsed_ms(now, this->last_write_ms_) < this->config_.minimum_write_interval_ms)
    return decision;
  if (!this->request_offered_) {
    this->request_offered_ = true;
    this->offered_target_ = computed_target;
    this->offered_fallback_ = false;
    decision.target_requested = true;
    decision.target = computed_target;
  }
  return decision;
}

bool Controller::authorize_rf2_emission(int target) const {
  if (!this->request_offered_ || this->offered_target_ != target || this->mode_ == Mode::LOCAL ||
      this->bridge_connected_ || this->command_unconfirmed_ || this->pending_confirmation_ ||
      !this->uart_available_)
    return false;
  const uint32_t now = this->now_();
  if (!this->active_stove_(now) || !this->stove_inputs_fresh_(now))
    return false;
  const bool source_usable = this->source_usable_(now);
  if (this->offered_fallback_) {
    if (source_usable || this->fallback_done_)
      return false;
  } else {
    if (!source_usable)
      return false;
    if (this->last_write_valid_ &&
        elapsed_ms(now, this->last_write_ms_) < this->config_.minimum_write_interval_ms)
      return false;
  }
  return true;
}

bool Controller::on_rf2_emitted(int target) {
  if (!this->authorize_rf2_emission(target))
    return false;
  const uint32_t now = this->now_();
  this->pending_confirmation_ = true;
  this->emitted_target_ = target;
  this->emitted_ms_ = now;
  this->last_write_ms_ = now;
  this->last_write_valid_ = true;
  if (this->offered_fallback_)
    this->fallback_done_ = true;
  this->cancel_request_();
  return true;
}

void Controller::on_rf2_not_emitted() {
  this->cancel_request_();
}

uint32_t Controller::now_() const {
  return this->clock_ ? this->clock_() : 0;
}

bool Controller::fresh_(bool valid, uint32_t observed_at, uint32_t now) const {
  return valid && elapsed_ms(now, observed_at) < this->config_.max_stove_age_ms;
}

bool Controller::stove_inputs_fresh_(uint32_t now) const {
  return this->active_stove_(now) && this->fresh_(this->stove_temperature_valid_, this->stove_temperature_ms_, now) &&
         this->fresh_(this->native_target_valid_, this->native_target_ms_, now);
}

bool Controller::source_usable_(uint32_t now) const {
  if (!this->source_present_ || !this->source_.valid || !this->source_.has_temperature ||
      elapsed_ms(now, this->source_packet_ms_) >= this->config_.relay_silence_ms)
    return false;
  return this->source_age_ms_(now) <= static_cast<uint64_t>(this->config_.max_source_age_seconds) * 1000ULL;
}

uint64_t Controller::source_age_ms_(uint32_t now) const {
  return static_cast<uint64_t>(this->source_.age_seconds) * 1000ULL +
         static_cast<uint64_t>(elapsed_ms(now, this->source_packet_ms_));
}

int Controller::clamp_native_(int target) const {
  return std::clamp(target, this->config_.native_min, this->config_.native_max);
}

void Controller::reset_integral_() {
  this->integral_ = 0.0F;
  this->correction_ = 0.0F;
  this->last_integrated_sample_id_.clear();
  this->last_integrated_sample_ms_ = 0;
  this->has_integrated_sample_ = false;
}

void Controller::cancel_request_() {
  this->request_offered_ = false;
  this->offered_target_ = 0;
  this->offered_fallback_ = false;
}

void Controller::reset_after_input_loss_() {
  this->reset_integral_();
  this->cancel_request_();
}

void Controller::clear_source_(bool mark_seen) {
  const bool had_source = this->source_present_;
  this->source_present_ = false;
  this->source_invalidated_ = mark_seen;
  this->source_packet_seen_ = this->source_packet_seen_ || mark_seen;
  this->reset_integral_();
  this->cancel_request_();
  if (had_source)
    this->fallback_done_ = false;
}

void Controller::refresh_confirmation_timeout_(uint32_t now) {
  if (this->pending_confirmation_ && elapsed_ms(now, this->emitted_ms_) >= this->config_.confirmation_timeout_ms) {
    this->pending_confirmation_ = false;
    this->command_unconfirmed_ = true;
    this->reset_integral_();
    this->cancel_request_();
  }
}

bool Controller::active_stove_(uint32_t now) const {
  return this->fresh_(this->stove_phase_valid_, this->stove_phase_ms_, now) &&
         this->stove_phase_ == ACTIVE_BURNING_PHASE && !this->phase_after_heat_required_;
}

Decision Controller::make_decision_(Status status) const {
  const uint32_t now = this->now_();
  Decision decision;
  decision.status = status;
  decision.mode = this->mode_;
  decision.native_target_available = this->native_target_valid_;
  decision.native_target = this->native_target_;
  decision.room_temperature_available = this->source_present_ && this->source_.valid;
  decision.room_temperature = this->source_.temperature_c;
  decision.source_age_available = decision.room_temperature_available;
  if (decision.source_age_available) {
    const uint64_t age_seconds = this->source_age_ms_(now) / 1000ULL;
    decision.source_age_seconds = age_seconds > std::numeric_limits<uint32_t>::max()
                                      ? std::numeric_limits<uint32_t>::max()
                                      : static_cast<uint32_t>(age_seconds);
  }
  decision.correction = this->correction_;
  decision.integral = this->integral_;
  return decision;
}

float Controller::calculate_correction_(float error) const {
  if (this->mode_ != Mode::PI)
    return 0.0F;
  return std::clamp(this->config_.kp * error + this->integral_, -this->config_.correction_limit,
                    this->config_.correction_limit);
}

void Controller::integrate_new_sample_(uint32_t now, float error, float stove_temperature, float base) {
  if (!this->source_present_ || !this->source_.valid ||
      this->source_.sample_id == this->last_integrated_sample_id_)
    return;

  if (this->has_integrated_sample_) {
    const uint32_t elapsed = elapsed_ms(now, this->last_integrated_sample_ms_);
    if (elapsed > 0) {
      const float elapsed_hours = static_cast<float>(elapsed) / 3600000.0F;
      float candidate = this->integral_ + this->config_.ki_per_hour * error * elapsed_hours;
      candidate = std::clamp(candidate, -this->config_.integral_limit, this->config_.integral_limit);
      const float candidate_raw_correction = this->config_.kp * error + candidate;
      const float candidate_correction = std::clamp(candidate_raw_correction, -this->config_.correction_limit,
                                                    this->config_.correction_limit);
      const bool correction_saturated_outward =
          (candidate_raw_correction > this->config_.correction_limit && candidate > this->integral_) ||
          (candidate_raw_correction < -this->config_.correction_limit && candidate < this->integral_);
      const float native_output = base + candidate_correction;
      const bool native_saturated_outward =
          (native_output > this->config_.native_max && candidate > this->integral_) ||
          (native_output < this->config_.native_min && candidate < this->integral_);
      if (!correction_saturated_outward && !native_saturated_outward)
        this->integral_ = candidate;
    }
  }

  this->last_integrated_sample_id_ = this->source_.sample_id;
  this->last_integrated_sample_ms_ = this->source_packet_ms_;
  this->has_integrated_sample_ = true;
  this->correction_ = this->calculate_correction_(error);
  (void) stove_temperature;
}

int Controller::round_ties_up(float value) {
  if (!std::isfinite(value))
    return 0;
  // These early exits preserve round-then-clamp results without overflowing an int cast.
  if (value >= static_cast<float>(std::numeric_limits<int>::max()) - 1.0F)
    return std::numeric_limits<int>::max();
  if (value <= static_cast<float>(std::numeric_limits<int>::min()) + 1.0F)
    return std::numeric_limits<int>::min();
  return static_cast<int>(std::floor(static_cast<double>(value) + 0.5));
}

}  // namespace duepi_room
}  // namespace esphome
