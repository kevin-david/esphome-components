"""Exercise metadata through the ESPHome schema, before code generation."""
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "components"))
from ups_hid.sensor import CONFIG_SCHEMA
from esphome.core import CORE


class SensorMetadataTest(unittest.TestCase):
    def config(self, kind, **overrides):
        CORE.reset()
        return CONFIG_SCHEMA(dict(id="metric", name="Metric", ups_hid_id="ups", type=kind, **overrides))

    def test_metadata_for_home_assistant(self):
        for kind, unit, device_class, decimals in [
            ("battery_level", "%", "battery", 0),
            ("input_voltage", "V", "voltage", 1),
            ("output_voltage", "V", "voltage", 1),
            ("runtime", "min", "duration", 0),
        ]:
            with self.subTest(kind=kind):
                config = self.config(kind)
                self.assertEqual(config["unit_of_measurement"], unit)
                self.assertEqual(config["device_class"], device_class)
                self.assertEqual(config["accuracy_decimals"], decimals)

    def test_user_metadata_takes_precedence(self):
        config = self.config("battery_level", unit_of_measurement="custom", device_class="power", accuracy_decimals=3)
        self.assertEqual(config["unit_of_measurement"], "custom")
        self.assertEqual(config["device_class"], "power")
        self.assertEqual(config["accuracy_decimals"], 3)


if __name__ == "__main__":
    unittest.main()
