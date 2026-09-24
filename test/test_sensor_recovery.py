"""Run the actual SensorManager against replaceable fake I2C devices."""
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class SensorRecoveryTests(unittest.TestCase):
    @unittest.skipUnless(sys.platform == "win32" and shutil.which("clang++"),
                         "Requires Windows with Clang and LLD")
    def test_sensor_replacement_and_bus_fallback(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / "sensor_recovery_test.exe"
            command = [
                "clang++", "-std=c++17", "-ffreestanding", "-fno-exceptions",
                "-fno-rtti", "-fno-stack-protector", "-fuse-ld=lld", "-nostdlib",
                "-Itest/fakes", "-Iinclude", "test/test_sensor_recovery.cpp",
                "src/firmware/sensor_manager.cpp", "src/firmware/sht45_sensor.cpp",
                "-Wl,/entry:main,/subsystem:console", "-o", str(executable),
            ]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True, text=True)
            result = subprocess.run([str(executable)], timeout=10)
            self.assertEqual(result.returncode, 0,
                             f"Sensor recovery check failed at C++ line {result.returncode}")
