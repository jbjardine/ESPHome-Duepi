#include "components/duepi_room/controller.h"
#include "components/duepi_room/relay_parser.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace esphome::duepi_room;

namespace {

struct TestCase {
  const char *name;
  std::function<void()> run;
};

std::vector<TestCase> &tests() {
  static std::vector<TestCase> cases;
  return cases;
}

struct RegisterTest {
  RegisterTest(const char *name, std::function<void()> run) { tests().push_back({name, std::move(run)}); }
};

#define TEST(name)                         \
  void name();                             \
  RegisterTest register_##name(#name, name); \
  void name()

void check(bool condition, const char *expression, const char *file, int line) {
  if (!condition) {
    std::ostringstream message;
    message << file << ':' << line << ": check failed: " << expression;
    throw std::runtime_error(message.str());
  }
}

#define CHECK(expression) check(static_cast<bool>(expression), #expression, __FILE__, __LINE__)
#define CHECK_NEAR(actual, expected, tolerance) CHECK(std::fabs((actual) - (expected)) <= (tolerance))

Config default_config() {
  Config config;
  config.native_min = 10;
  config.native_max = 35;
  config.fallback_target = 20.0F;
  config.max_source_age_seconds = 900;
  config.max_stove_age_ms = 24U * 60U * 60U * 1000U;
  config.relay_silence_ms = 180000;
  config.minimum_write_interval_ms = 300000;
  config.confirmation_timeout_ms = 30000;
  config.kp = 0.10F;
  config.ki_per_hour = 0.025F;
  config.integral_limit = 0.5F;
  config.correction_limit = 1.0F;
  return config;
}

RelayMessage message(uint32_t sequence, float temperature, const std::string &sample_id, uint32_t age = 0,
                     const std::string &generation = "0123456789abcdef0123456789abcdef") {
  RelayMessage result;
  result.version = 1;
  result.generation = generation;
  result.sequence = sequence;
  result.valid = true;
  result.temperature_c = temperature;
  result.has_temperature = true;
  result.age_seconds = age;
  result.sample_id = sample_id;
  return result;
}

struct Fixture {
  uint32_t now{1000};
  Config config{default_config()};
  Controller controller{config, [this]() { return now; }};

  void prepare(Mode mode = Mode::COMPENSATION, float room_target = 20.0F, float room_temperature = 20.0F,
               float stove_temperature = 23.0F, float native_target = 20.0F, uint32_t sequence = 1,
               const std::string &sample_id = "1792485000123456") {
    controller = Controller(config, [this]() { return now; });
    controller.set_room_target(room_target);
    controller.set_mode(mode);
    controller.observe_raw_stove_temperature(stove_temperature);
    controller.observe_raw_stove_phase(0x0201);
    controller.observe_native_target(native_target);
    controller.observe_uart_availability(true);
    CHECK(controller.observe_relay(message(sequence, room_temperature, sample_id)));
  }

  void send_sample(uint32_t sequence, float temperature, const std::string &sample_id, uint32_t age = 0,
                   const std::string &generation = "0123456789abcdef0123456789abcdef") {
    CHECK(controller.observe_relay(message(sequence, temperature, sample_id, age, generation)));
  }
};

TEST(relay_parser_accepts_the_frozen_v1_message) {
  RelayMessage parsed;
  ParseError error;
  const std::string json =
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19.4,"age_s":4,"sample_id":"1792485000123456"})";

  CHECK(parse_relay_message(json, &parsed, &error));
  CHECK(parsed.version == 1);
  CHECK(parsed.sequence == 1);
  CHECK(parsed.valid);
  CHECK(parsed.has_temperature);
  CHECK_NEAR(parsed.temperature_c, 19.4, 0.0001);
  CHECK(parsed.age_seconds == 4);
  CHECK(parsed.sample_id == "1792485000123456");
}

TEST(relay_parser_accepts_an_explicit_invalid_source_without_temperature) {
  RelayMessage parsed;
  const std::string json =
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":2,"valid":false,"age_s":0,"sample_id":""})";

  CHECK(parse_relay_message(json, &parsed));
  CHECK(!parsed.valid);
  CHECK(!parsed.has_temperature);
  CHECK(parsed.sample_id.empty());
}

TEST(relay_parser_ignores_unknown_fields_but_rejects_malformed_json) {
  RelayMessage parsed;
  CHECK(parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":3,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1","extra":"ok"})",
      &parsed));
  CHECK(!parse_relay_message("{bad json", &parsed));
}

TEST(relay_parser_fails_closed_for_unsupported_versions_and_wrong_types) {
  RelayMessage parsed;
  CHECK(!parse_relay_message(
      R"({"v":2,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":"1","generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1.5,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":"true","temp_c":19,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":"19","age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":1e999,"age_s":0,"sample_id":"1"})",
      &parsed));
}

TEST(relay_parser_rejects_invalid_generation_sequence_age_sample_and_temperature) {
  RelayMessage parsed;
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789ABCDEF0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":0,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":-1,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":0,"sample_id":"x"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"age_s":0,"sample_id":"1"})",
      &parsed));
  CHECK(!parse_relay_message(
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":false,"temp_c":19,"age_s":0,"sample_id":""})",
      &parsed));
}

TEST(relay_parser_rejects_messages_over_254_bytes) {
  RelayMessage parsed;
  std::string boundary =
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1","pad":")";
  boundary.append(254 - boundary.size() - 2, 'x');
  boundary += "\"}";
  CHECK(boundary.size() == 254);
  CHECK(parse_relay_message(boundary, &parsed));

  std::string json =
      R"({"v":1,"generation":"0123456789abcdef0123456789abcdef","seq":1,"valid":true,"temp_c":19,"age_s":0,"sample_id":"1","pad":" )";
  json.append(220, 'x');
  json += R"("})";
  CHECK(json.size() > 254);
  CHECK(!parse_relay_message(json, &parsed));
}

TEST(compensation_uses_raw_stove_measurement_and_room_error) {
  for (const auto &[room, expected] : std::vector<std::pair<float, int>>{{18.0F, 25}, {20.0F, 23}, {21.0F, 22}}) {
    Fixture fixture;
    fixture.prepare(Mode::COMPENSATION, 20.0F, room, 23.0F, 23.0F);
    const Decision decision = fixture.controller.poll();
    CHECK(decision.status == Status::REGULATING);
    CHECK(decision.target_requested == (expected != 23));
    CHECK(decision.computed_target == expected);
  }
}

TEST(compensation_rounds_positive_half_ties_up_then_clamps_to_native_limits) {
  Fixture fixture;
  fixture.config.native_max = 25;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 17.5F, 23.0F, 23.0F);
  const Decision decision = fixture.controller.poll();
  CHECK(decision.target_requested);
  CHECK(decision.target == 25);
  CHECK_NEAR(decision.unclamped_target, 25.5, 0.0001);

  Fixture low;
  low.prepare(Mode::COMPENSATION, 20.0F, 25.0F, 15.0F, 15.0F);
  const Decision low_decision = low.controller.poll();
  CHECK(low_decision.target_requested);
  CHECK(low_decision.target == 10);
}

TEST(pi_integrates_only_on_distinct_samples_and_the_first_sample_has_zero_elapsed_time) {
  Fixture fixture;
  fixture.config.kp = 0.0F;
  fixture.config.ki_per_hour = 0.5F;
  fixture.config.native_max = 35;
  fixture.prepare(Mode::PI, 20.0F, 19.0F, 23.0F, 24.0F, 1, "1792485000123456");
  Decision first = fixture.controller.poll();
  CHECK_NEAR(first.integral, 0.0, 0.0001);

  fixture.now += 60U * 60U * 1000U;
  fixture.send_sample(2, 19.0F, "1792488600123456");
  Decision second = fixture.controller.poll();
  CHECK_NEAR(second.integral, 0.5, 0.0001);
  CHECK_NEAR(second.correction, 0.5, 0.0001);

  fixture.now += 60000;
  fixture.send_sample(3, 19.0F, "1792488600123456", 60);
  const Decision heartbeat = fixture.controller.poll();
  CHECK_NEAR(heartbeat.integral, 0.5, 0.0001);
}

TEST(pi_anti_windup_blocks_integral_growth_at_correction_and_native_output_limits) {
  Fixture correction_limit;
  correction_limit.config.kp = 1.0F;
  correction_limit.config.ki_per_hour = 0.5F;
  correction_limit.config.correction_limit = 0.5F;
  correction_limit.prepare(Mode::PI, 20.0F, 19.0F, 23.0F, 23.5F);
  correction_limit.controller.poll();
  correction_limit.now += 60U * 60U * 1000U;
  correction_limit.send_sample(2, 19.0F, "1792488600123456");
  CHECK_NEAR(correction_limit.controller.poll().integral, 0.0, 0.0001);

  Fixture native_limit;
  native_limit.config.kp = 0.0F;
  native_limit.config.ki_per_hour = 0.5F;
  native_limit.config.native_max = 25;
  native_limit.prepare(Mode::PI, 20.0F, 19.0F, 24.0F, 25.0F);
  native_limit.controller.poll();
  native_limit.now += 60U * 60U * 1000U;
  native_limit.send_sample(2, 19.0F, "1792488600123456");
  const Decision saturated = native_limit.controller.poll();
  CHECK_NEAR(saturated.integral, 0.0, 0.0001);
  CHECK(saturated.computed_target == 25);
}

TEST(pi_allows_integral_unwinding_when_error_reverses) {
  Fixture fixture;
  fixture.config.kp = 0.0F;
  fixture.config.ki_per_hour = 0.5F;
  fixture.prepare(Mode::PI, 20.0F, 19.0F, 23.0F, 24.0F);
  fixture.controller.poll();
  fixture.now += 60U * 60U * 1000U;
  fixture.send_sample(2, 19.0F, "1792488600123456");
  const Decision positive = fixture.controller.poll();
  CHECK_NEAR(positive.integral, 0.5, 0.0001);
  CHECK(positive.target_requested);
  CHECK(fixture.controller.authorize_rf2_emission(positive.target));
  CHECK(fixture.controller.on_rf2_emitted(positive.target));
  fixture.controller.observe_native_target(static_cast<float>(positive.target));

  fixture.now += 60U * 60U * 1000U;
  fixture.send_sample(3, 21.0F, "1792492200123456");
  const Decision unwound = fixture.controller.poll();
  CHECK_NEAR(unwound.integral, 0.0, 0.0001);
}

TEST(repeated_and_out_of_order_source_packets_do_not_extend_the_relay_lease) {
  Fixture fixture;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 20.0F, 23.0F, 23.0F);
  fixture.controller.poll();
  fixture.now += 179000;
  CHECK(!fixture.controller.observe_relay(message(1, 20.0F, "1792485000123456")));
  fixture.now += 2000;
  const Decision expired = fixture.controller.poll();
  CHECK(expired.status == Status::FALLBACK);
  CHECK(expired.target_requested);
  CHECK(expired.target == 20);
}

TEST(stale_or_explicitly_invalid_source_selects_fallback_once) {
  Fixture fixture;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  fixture.controller.poll();
  fixture.now += 901000;
  fixture.send_sample(2, 18.0F, "1792485000123456", 901);
  const Decision fallback = fixture.controller.poll();
  CHECK(fallback.status == Status::FALLBACK);
  CHECK(fallback.target_requested);
  CHECK(fallback.target == 20);
  CHECK(fixture.controller.on_rf2_emitted(fallback.target));
  fixture.controller.observe_native_target(20.0F);
  CHECK(fixture.controller.poll().status == Status::FALLBACK);
  CHECK(!fixture.controller.poll().target_requested);

  Fixture invalid;
  invalid.prepare(Mode::COMPENSATION, 20.0F, 20.0F, 23.0F, 23.0F);
  RelayMessage bad = message(2, 0.0F, "");
  bad.valid = false;
  bad.has_temperature = false;
  bad.temperature_c = std::numeric_limits<float>::quiet_NaN();
  CHECK(invalid.controller.observe_relay(bad));
  const Decision invalid_fallback = invalid.controller.poll();
  CHECK(invalid_fallback.status == Status::FALLBACK);
  CHECK(invalid_fallback.target_requested);
  CHECK(invalid_fallback.target == 20);
}

TEST(malformed_source_invalidates_current_source_without_accepting_a_replay) {
  Fixture fixture;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  fixture.controller.poll();
  fixture.controller.invalidate_relay_source();
  const Decision fallback = fixture.controller.poll();
  CHECK(fallback.status == Status::FALLBACK);
  CHECK(fallback.target_requested);
  CHECK(!fixture.controller.observe_relay(message(1, 18.0F, "1792485000123456")));
}

TEST(no_initial_source_waits_for_the_relay_deadline_then_falls_back) {
  Fixture fixture;
  fixture.controller.set_mode(Mode::COMPENSATION);
  fixture.controller.observe_raw_stove_temperature(23.0F);
  fixture.controller.observe_raw_stove_phase(0x0201);
  fixture.controller.observe_native_target(23.0F);
  fixture.controller.observe_uart_availability(true);
  CHECK(fixture.controller.poll().status == Status::WAITING_SOURCE);
  fixture.now += 180001;
  const Decision expired = fixture.controller.poll();
  CHECK(expired.status == Status::FALLBACK);
  CHECK(expired.target_requested);
}

TEST(local_mode_never_requests_a_fallback_or_automatic_target) {
  Fixture fixture;
  fixture.prepare(Mode::LOCAL, 20.0F, 18.0F, 23.0F, 23.0F);
  const Decision decision = fixture.controller.poll();
  CHECK(decision.status == Status::LOCAL);
  CHECK(!decision.target_requested);
}

TEST(postboot_inputs_and_a_stable_burning_phase_are_required_before_regulation) {
  Fixture fixture;
  fixture.controller.set_mode(Mode::COMPENSATION);
  CHECK(fixture.controller.poll().status == Status::WAITING_STOVE);
  fixture.controller.observe_raw_stove_temperature(17.0F);
  fixture.controller.observe_raw_stove_phase(0x0101);
  fixture.controller.observe_native_target(23.0F);
  fixture.controller.observe_uart_availability(true);
  fixture.send_sample(1, 18.0F, "1792485000123456");
  CHECK(fixture.controller.poll().status == Status::STOVE_INACTIVE);
  fixture.controller.observe_raw_stove_phase(0x0201);
  const Decision burning = fixture.controller.poll();
  CHECK(burning.status == Status::REGULATING);
  CHECK(burning.target_requested);
}

TEST(stale_raw_stove_temperature_phase_or_native_target_blocks_automatic_writes) {
  Fixture fixture;
  fixture.config.max_stove_age_ms = 45000;
  fixture.prepare();
  fixture.now += 45000;
  const Decision stale = fixture.controller.poll();
  CHECK(stale.status == Status::WAITING_STOVE);
  CHECK(!stale.target_requested);
}

TEST(manual_native_target_and_two_stable_divergent_readbacks_latch_local) {
  Fixture manual;
  manual.prepare();
  manual.controller.observe_manual_native_target(28.0F);
  const Decision after_manual = manual.controller.poll();
  CHECK(after_manual.status == Status::LOCAL);
  CHECK(!after_manual.target_requested);
  CHECK_NEAR(after_manual.native_target, 28.0, 0.001);

  Fixture readback;
  readback.prepare();
  readback.controller.poll();
  readback.controller.on_rf2_not_emitted();
  readback.controller.observe_native_target(24.0F);
  CHECK(readback.controller.mode() == Mode::COMPENSATION);
  readback.controller.observe_native_target(24.0F);
  CHECK(readback.controller.mode() == Mode::LOCAL);
  CHECK_NEAR(readback.controller.poll().native_target, 24.0, 0.001);
}

TEST(bridge_blocks_writes_and_disconnect_requires_fresh_observations) {
  Fixture fixture;
  fixture.prepare();
  fixture.controller.observe_bridge_connected(true);
  CHECK(fixture.controller.poll().status == Status::BRIDGE);
  CHECK(!fixture.controller.poll().target_requested);
  fixture.controller.observe_bridge_connected(false);
  CHECK(fixture.controller.poll().status == Status::WAITING_STOVE);

  fixture.controller.observe_raw_stove_temperature(23.0F);
  fixture.controller.observe_raw_stove_phase(0x0201);
  fixture.controller.observe_native_target(23.0F);
  fixture.send_sample(2, 20.0F, "1792485060123456");
  CHECK(fixture.controller.poll().status == Status::REGULATING);
}

TEST(bridge_target_change_latches_local_on_disconnect) {
  Fixture fixture;
  fixture.prepare();
  fixture.controller.observe_bridge_connected(true);
  fixture.controller.observe_native_target(24.0F);
  fixture.controller.observe_bridge_connected(false);
  CHECK(fixture.controller.mode() == Mode::LOCAL);
  CHECK(fixture.controller.poll().status == Status::LOCAL);
}

TEST(explicit_off_latches_local_but_heat_needs_a_new_postcommand_phase_observation) {
  Fixture off;
  off.prepare();
  off.controller.on_explicit_mode_command(false);
  CHECK(off.controller.mode() == Mode::LOCAL);
  CHECK(off.controller.poll().status == Status::LOCAL);

  Fixture heat;
  heat.prepare();
  heat.controller.on_explicit_mode_command(true);
  CHECK(heat.controller.mode() == Mode::COMPENSATION);
  CHECK(heat.controller.poll().status == Status::WAITING_STOVE);
  CHECK(!heat.controller.poll().target_requested);
  heat.controller.observe_raw_stove_phase(0x0101);
  CHECK(heat.controller.poll().status == Status::STOVE_INACTIVE);
  heat.controller.observe_raw_stove_phase(0x0201);
  const Decision heating = heat.controller.poll();
  CHECK(heating.status == Status::REGULATING);
  CHECK(heating.target_requested);

  Fixture local;
  local.prepare(Mode::LOCAL);
  local.controller.on_explicit_mode_command(true);
  CHECK(local.controller.mode() == Mode::LOCAL);
}

TEST(uart_busy_prevents_requests_and_reported_emission_starts_confirmation) {
  Fixture fixture;
  fixture.prepare();
  fixture.controller.observe_uart_availability(false);
  const Decision busy = fixture.controller.poll();
  CHECK(busy.status == Status::UART_BUSY);
  CHECK(!busy.target_requested);

  fixture.controller.observe_uart_availability(true);
  const Decision requested = fixture.controller.poll();
  CHECK(requested.target_requested);
  CHECK(requested.status == Status::REGULATING);
  CHECK(fixture.controller.authorize_rf2_emission(requested.target));
  CHECK(fixture.controller.on_rf2_emitted(requested.target));
  CHECK(fixture.controller.poll().status == Status::AWAITING_CONFIRMATION);
}

TEST(rf2_request_cancellation_never_starts_confirmation) {
  Fixture fixture;
  fixture.prepare();
  const Decision requested = fixture.controller.poll();
  CHECK(requested.target_requested);
  fixture.controller.on_rf2_not_emitted();
  CHECK(fixture.controller.poll().status == Status::REGULATING);
}

TEST(minimum_write_interval_is_measured_from_actual_emission) {
  Fixture fixture;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  Decision first = fixture.controller.poll();
  CHECK(first.target_requested);
  CHECK(first.target == 25);
  CHECK(fixture.controller.on_rf2_emitted(first.target));
  fixture.controller.observe_native_target(25.0F);

  fixture.now += 100000;
  fixture.controller.set_room_target(21.0F);
  fixture.send_sample(2, 18.0F, "1792485100123456", 100);
  const Decision early = fixture.controller.poll();
  CHECK(early.status == Status::REGULATING);
  CHECK(!early.target_requested);

  fixture.now += 200000;
  fixture.send_sample(3, 18.0F, "1792485300123456", 300);
  const Decision due = fixture.controller.poll();
  CHECK(due.target_requested);
  CHECK(due.target == 26);
}

TEST(fallback_can_bypass_the_write_interval_once_when_uart_is_free) {
  Fixture fixture;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  const Decision first = fixture.controller.poll();
  CHECK(first.target_requested);
  CHECK(fixture.controller.on_rf2_emitted(first.target));
  fixture.controller.observe_native_target(static_cast<float>(first.target));

  fixture.now += 1000;
  fixture.send_sample(2, 18.0F, "1792485001123456", 901);
  const Decision fallback = fixture.controller.poll();
  CHECK(fallback.status == Status::FALLBACK);
  CHECK(fallback.target_requested);
  CHECK(fallback.target == 20);
}

TEST(matching_rc6000_readback_confirms_and_mismatch_timeout_stops_further_writes) {
  Fixture confirmed;
  confirmed.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  const Decision request = confirmed.controller.poll();
  CHECK(request.target_requested);
  CHECK(confirmed.controller.on_rf2_emitted(request.target));
  confirmed.controller.observe_native_target(static_cast<float>(request.target));
  CHECK(confirmed.controller.poll().status == Status::REGULATING);

  Fixture unconfirmed;
  unconfirmed.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  const Decision unconfirmed_request = unconfirmed.controller.poll();
  CHECK(unconfirmed.controller.on_rf2_emitted(unconfirmed_request.target));
  unconfirmed.now += 30001;
  const Decision failure = unconfirmed.controller.poll();
  CHECK(failure.status == Status::COMMAND_UNCONFIRMED);
  CHECK(!failure.target_requested);
  unconfirmed.controller.set_mode(Mode::COMPENSATION);
  CHECK(unconfirmed.controller.poll().status == Status::REGULATING);
}

TEST(monotonic_elapsed_time_and_write_interval_survive_uint32_wraparound) {
  Fixture fixture;
  fixture.now = std::numeric_limits<uint32_t>::max() - 10000U;
  fixture.prepare(Mode::COMPENSATION, 20.0F, 18.0F, 23.0F, 23.0F);
  const Decision first = fixture.controller.poll();
  CHECK(first.target_requested);
  CHECK(fixture.controller.on_rf2_emitted(first.target));
  fixture.controller.observe_native_target(static_cast<float>(first.target));

  fixture.now += 299999U;
  fixture.controller.set_room_target(21.0F);
  fixture.send_sample(2, 18.0F, "1792485300123456", 299);
  CHECK(!fixture.controller.poll().target_requested);
  fixture.now += 1U;
  fixture.send_sample(3, 18.0F, "1792485301123456", 300);
  CHECK(fixture.controller.poll().target_requested);
}

TEST(new_generation_resets_sequence_and_pi_history) {
  Fixture fixture;
  fixture.config.kp = 0.0F;
  fixture.config.ki_per_hour = 0.5F;
  fixture.prepare(Mode::PI, 20.0F, 19.0F, 23.0F, 24.0F);
  fixture.controller.poll();
  fixture.now += 60000;
  const std::string generation = "fedcba9876543210fedcba9876543210";
  fixture.send_sample(1, 19.0F, "1792485060123456", 0, generation);
  const Decision reset = fixture.controller.poll();
  CHECK_NEAR(reset.integral, 0.0, 0.0001);
}

}  // namespace

int main() {
  int failed = 0;
  for (const auto &test : tests()) {
    try {
      test.run();
      std::cout << "PASS " << test.name << '\n';
    } catch (const std::exception &error) {
      ++failed;
      std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
    }
  }
  std::cout << tests().size() - failed << '/' << tests().size() << " tests passed\n";
  return failed == 0 ? 0 : 1;
}
