from pathlib import Path
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


if __name__ == "__main__":
    unittest.main(verbosity=2)
