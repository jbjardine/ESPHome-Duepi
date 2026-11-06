import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).resolve().parent
ESPHOME = Path(
    os.environ.get("ESPHOME_BIN", str(ROOT / ".venv-esphome/bin/esphome"))
)
CELSIUS_CONFIG = FIXTURES / "room_esp01_1m.yaml"


def run_config(minimum: int, maximum: int, unit: str) -> subprocess.CompletedProcess[str]:
    source = CELSIUS_CONFIG.read_text(encoding="utf-8")
    source = source.replace('min_temp: "10"', f'min_temp: "{minimum}"', 1)
    source = source.replace('max_temp: "35"', f'max_temp: "{maximum}"', 1)
    source = source.replace('temp_unit: "°C"', f'temp_unit: "{unit}"', 1)
    config_path = None
    try:
        with tempfile.NamedTemporaryFile(
            mode="w",
            encoding="utf-8",
            suffix=".yaml",
            prefix=".room-unit-",
            dir=FIXTURES,
            delete=False,
        ) as stream:
            stream.write(source)
            config_path = Path(stream.name)
        return subprocess.run(
            [str(ESPHOME), "config", str(config_path)],
            cwd=ROOT,
            text=True,
            capture_output=True,
        )
    finally:
        if config_path is not None:
            config_path.unlink(missing_ok=True)


def sensor_block(output: str, name: str) -> str:
    marker = f"name: {name}"
    index = output.index(marker)
    start = output.rfind("  - platform: duepi_room", 0, index)
    end = output.find("\n  - platform:", index)
    return output[start:] if end < 0 else output[start:end]


class NativeUnitConfigTests(unittest.TestCase):
    def test_celsius_limits_are_unchanged(self):
        result = run_config(10, 35, "°C")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("  native_min: 10\n", result.stdout)
        self.assertIn("  native_max: 35\n", result.stdout)
        self.assertIn("  limit_unit: °C\n", result.stdout)

    def test_fahrenheit_50_to_95_normalizes_to_10_to_35_celsius(self):
        result = run_config(50, 95, "°F")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("  native_min: 10\n", result.stdout)
        self.assertIn("  native_max: 35\n", result.stdout)
        self.assertIn("  limit_unit: °C\n", result.stdout)

    def test_fahrenheit_values_are_range_checked_after_conversion(self):
        result = run_config(50, 104, "°F")
        self.assertEqual(result.returncode, 0, (result.stdout + result.stderr)[-1500:])
        self.assertIn("  native_min: 10\n", result.stdout)
        self.assertIn("  native_max: 40\n", result.stdout)
        self.assertIn("  limit_unit: °C\n", result.stdout)

    def test_nonintegral_fahrenheit_bounds_round_inward(self):
        result = run_config(51, 94, "F")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("  native_min: 11\n", result.stdout)
        self.assertIn("  native_max: 34\n", result.stdout)

    def test_fahrenheit_limits_with_no_integer_celsius_value_are_rejected(self):
        result = run_config(54, 55, "°F")
        output = result.stdout + result.stderr
        self.assertNotEqual(result.returncode, 0, output)
        self.assertIn("empty after conversion to integer Celsius", output)

    def test_pi_correction_is_a_celsius_delta_not_absolute_temperature(self):
        result = run_config(10, 35, "°C")
        self.assertEqual(result.returncode, 0, result.stderr)
        pi_correction = sensor_block(result.stdout, "Room PI correction")
        computed_target = sensor_block(result.stdout, "Room computed native target")
        self.assertIn("unit_of_measurement: Δ°C", pi_correction)
        self.assertNotIn("device_class: temperature", pi_correction)
        self.assertIn("unit_of_measurement: °C", computed_target)
        self.assertIn("device_class: temperature", computed_target)


if __name__ == "__main__":
    unittest.main(verbosity=2)
