from pathlib import Path
import json
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def yaml_script(name: str) -> str:
    scripts = read("components/script.yaml")
    match = re.search(rf"(?ms)^  - id: {re.escape(name)}\n(.*?)(?=^  - id: |\Z)", scripts)
    if match is None:
        raise AssertionError(f"script {name} is missing")
    return match.group(1)


class RoomIntegrationContracts(unittest.TestCase):
    def test_package_uses_native_api_action_round_trip(self) -> None:
        room_package = read("components/packages/room_regulation.yaml")

        self.assertIn("on_source_request:", room_package)
        self.assertIn("data_template:", room_package)
        self.assertIn("generation", room_package)
        self.assertIn("seq", room_package)
        self.assertRegex(room_package, r"(?m)^    - action: duepi_room_receive$")
        self.assertIn("duepi_room_receive", room_package)
        self.assertIn("homeassistant_services: true", room_package)
        self.assertIn('return "esphome." + node_name + "_duepi_room_receive";', room_package)

    def test_native_source_text_is_optional_persistent_and_validated(self) -> None:
        package = read("components/packages/room_regulation.yaml")
        component = read("components/duepi_room/duepi_room.cpp")
        self.assertIn('id: duepi_room_source', package)
        self.assertIn('name: "Room temperature sensor"', package)
        self.assertIn("restore_value: true", package)
        self.assertIn('initial_value: ""', package)
        self.assertIn("pattern: '^$|^sensor\\.[a-z0-9_]+$'", package)
        self.assertIn("bool DuepiRoomComponent::set_source_entity_id", component)
        self.assertIn("valid_source_entity_id(entity_id)", component)
        self.assertIn("request_source_measurement_()", component)

    def test_native_source_takes_priority_without_removing_legacy_relay_support(self) -> None:
        component = read("components/duepi_room/duepi_room.cpp")
        legacy = re.search(
            r"(?s)void DuepiRoomComponent::receive_relay_payload\([^)]*\) \{(.*?)\n\}",
            component,
        )
        native = re.search(
            r"(?s)void DuepiRoomComponent::receive_native_source_payload\([^)]*\) \{(.*?)\n\}",
            component,
        )
        self.assertIsNotNone(legacy)
        self.assertIsNotNone(native)
        self.assertIn("if (!this->source_entity_id_.empty())", legacy.group(1))
        self.assertIn("if (this->source_entity_id_.empty())", native.group(1))
        self.assertIn("controller_.observe_relay(message)", native.group(1))

    def test_legacy_and_automatic_targets_share_one_rf2_writer(self) -> None:
        climate = read("components/climate.yaml")
        room_package = read("components/packages/room_regulation.yaml")
        writer = yaml_script("STOVE_SET_TARGET")

        self.assertIn("id: STOVE_SET_TARGET", climate)
        self.assertIn("id: STOVE_SET_TARGET", room_package)
        self.assertLess(writer.index("uart.write"), writer.index("duepi_room_on_rf2_emitted"))
        self.assertIn("duepi_room_authorize_rf2_emission", writer)
        self.assertIn("duepi_room_on_rf2_not_emitted", writer)
        self.assertNotIn("F00", writer)
        self.assertNotIn("RF000", writer)
        self.assertNotIn("set_target_temperature", room_package)

    def test_target_writer_restarts_for_the_latest_manual_change(self) -> None:
        writer = yaml_script("STOVE_SET_TARGET")
        mode_writer = yaml_script("STOVE_SET_MODE")

        self.assertRegex(writer, r"(?m)^    mode: restart$")
        self.assertNotRegex(writer, r"(?m)^    max_runs:")
        self.assertRegex(mode_writer, r"(?m)^    mode: restart$")
        self.assertNotRegex(mode_writer, r"(?m)^    max_runs:")

    def test_manual_override_cancels_the_pending_automatic_offer_before_execution(self) -> None:
        climate = read("components/climate.yaml")
        manual_path = climate[climate.index("target_temperature_change_action:"):]

        observe = manual_path.index("duepi_room_observe_manual_native_target")
        cancel = manual_path.index("duepi_room_on_rf2_not_emitted")
        execute = manual_path.index("id: STOVE_SET_TARGET")
        self.assertLess(observe, cancel)
        self.assertLess(cancel, execute)

    def test_opt_in_writer_and_rc6000_gate_share_normalized_celsius_limits(self) -> None:
        writer = yaml_script("STOVE_SET_TARGET")
        uart = read("components/uart.yaml")
        component = read("components/duepi_room/duepi_room.cpp")

        for content in (writer, uart):
            with self.subTest(content=content[:40]):
                self.assertIn("duepi_room_native_min()", content)
                self.assertIn("duepi_room_native_max()", content)
        self.assertIn("int duepi_room_native_min()", component)
        self.assertIn("int duepi_room_native_max()", component)

    def test_observations_are_compile_guarded_without_an_optional_yaml_id(self) -> None:
        integrations = {
            "components/climate.yaml": read("components/climate.yaml"),
            "components/uart.yaml": read("components/uart.yaml"),
            "components/globals.yaml": read("components/globals.yaml"),
        }
        for path, content in integrations.items():
            with self.subTest(path=path):
                self.assertIn("#ifdef USE_DUEPI_ROOM", content)
                self.assertNotRegex(content, r"id\(duepi_room(?:_component)?\)")

        uart = integrations["components/uart.yaml"]
        self.assertLess(uart.index("duepi_room_observe_raw_stove_temperature"), uart.index("room_temp_stove).publish_state"))
        self.assertIn("duepi_room_observe_raw_stove_phase", uart)
        self.assertIn("duepi_room_observe_native_target", uart)
        self.assertIn("duepi_room_observe_stove_alarm", uart)
        self.assertIn("duepi_room_observe_manual_native_target", integrations["components/climate.yaml"])
        self.assertIn("duepi_room_observe_bridge_connected", integrations["components/globals.yaml"])

    def test_poll_activity_counter_covers_both_poll_sequences(self) -> None:
        time_yaml = read("components/time.yaml")
        self.assertEqual(time_yaml.count("room_poll_active_count)++"), 2)
        self.assertEqual(time_yaml.count("room_poll_active_count)--"), 2)
        self.assertIn("room_poll_active_count) == 0", read("components/globals.yaml"))
        self.assertIn("room_poll_active_count) == 0", yaml_script("STOVE_SET_TARGET"))

    def test_room_package_forwards_only_explicit_heat_and_off_commands(self) -> None:
        room_package = read("components/packages/room_regulation.yaml")
        native_mode = read("components/climate.yaml")
        self.assertIn("on_native_mode_command", room_package)
        self.assertIn("id: STOVE_SET_MODE", room_package)
        self.assertIn("id: STOVE_SET_MODE", native_mode)
        self.assertIn("boot_restore_in_progress", native_mode)
        self.assertIn("climate_sync_in_progress", native_mode)

    def test_rearm_button_only_selects_compensation(self) -> None:
        package = read("components/packages/room_regulation.yaml")
        match = re.search(r"(?ms)^button:\n(.*?)(?=^[a-z_][a-z0-9_-]*:\n|\Z)", package)
        self.assertIsNotNone(match, "template rearm button is missing")
        button = match.group(1)

        self.assertRegex(button, r'(?m)^  - platform: template$')
        self.assertRegex(button, r'(?m)^    id: duepi_room_rearm$')
        self.assertRegex(button, r'(?m)^    name: "Reactivate room compensation"$')
        self.assertRegex(button, r'(?m)^    icon: mdi:restart$')
        self.assertEqual(re.findall(r"(?m)^      - ([a-z_]+(?:\.[a-z_]+)*):$", button), ["select.set"])
        self.assertRegex(button, r"(?m)^          id: duepi_room_mode$")
        self.assertRegex(button, r"(?m)^          option: Compensation$")

    def test_assistant_descriptions_explain_manual_local_and_reactivation(self) -> None:
        expected = {
            "custom_components/duepi_room/strings.json": (
                "room temperature sensor",
                "mode on the ESPHome device",
                "switches the mode to Local",
                "Reactivate room compensation",
                "does not turn the stove on",
            ),
            "custom_components/duepi_room/translations/en.json": (
                "room temperature sensor",
                "mode on the ESPHome device",
                "switches the mode to Local",
                "Reactivate room compensation",
                "does not turn the stove on",
            ),
            "custom_components/duepi_room/translations/fr.json": (
                "sonde de température de la pièce",
                "mode de régulation sur l’appareil ESPHome",
                "sélectionne Local",
                "Réactiver la compensation",
                "n’allume pas le poêle",
            ),
        }
        for path, phrases in expected.items():
            with self.subTest(path=path):
                strings = json.loads(read(path))
                descriptions = (
                    strings["config"]["step"]["user"].get("description", ""),
                    strings["options"]["step"]["init"].get("description", ""),
                )
                for description in descriptions:
                    for phrase in phrases:
                        self.assertIn(phrase, description)

    def test_script_sdk_guard_is_identical_across_native_translation_units(self) -> None:
        runner = read("tests/native/run_tests.sh")
        compatibility_header = ROOT / "tests/native/esphome_script_test_compat.h"
        test_translation_unit = read("tests/native/test_duepi_room.cpp")

        self.assertIn('-include "$repo_root/tests/native/esphome_script_test_compat.h"', runner)
        self.assertTrue(compatibility_header.is_file())
        self.assertIn("#define ScopedSourceGuard TestSourceGuard", compatibility_header.read_text())
        self.assertNotIn("class TestSourceGuard", test_translation_unit)

    def test_observed_stove_phase_updates_room_climate_display_without_commands(self) -> None:
        header = read("components/duepi_room/duepi_room.h")
        component = read("components/duepi_room/duepi_room.cpp")
        observer = re.search(
            r"(?s)void DuepiRoomComponent::observe_raw_stove_phase\(uint16_t phase\) \{(.*?)\n\}",
            component,
        )
        self.assertIn("map_stove_phase_to_display_mode", header)
        self.assertIsNotNone(observer, "raw phase observer must publish the observed display mode")
        body = observer.group(1)
        self.assertIn("controller_.observe_raw_stove_phase(phase)", body)
        self.assertIn("this->room_climate_->publish_observed_mode", body)
        self.assertNotIn("request_native_mode", body)
        publish_mode = re.search(
            r"(?s)void DuepiRoomClimate::publish_observed_mode\([^)]*\) \{(.*?)\n\}",
            component,
        )
        self.assertIsNotNone(publish_mode)
        self.assertIn("this->mode = mode", publish_mode.group(1))
        self.assertIn("this->publish_state()", publish_mode.group(1))
        self.assertNotIn("make_call", publish_mode.group(1))


if __name__ == "__main__":
    unittest.main(verbosity=2)
