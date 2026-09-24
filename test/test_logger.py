import importlib.util
import sqlite3
import tempfile
import unittest
from unittest.mock import patch
from pathlib import Path
SPEC = importlib.util.spec_from_file_location("sensor_logger", Path(__file__).parents[1] / "logger" / "logger.py")
logger = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(logger)
class LoggerTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.db = logger.connect(Path(self.temp.name) / "test.db")
    def tearDown(self):
        self.db.close(); self.temp.cleanup()
    def test_live_logging_uses_current_without_fetching_history(self):
        payload = {"measurement": {"boot_id": 30, "sequence": 1, "humidity": 94}}
        with patch.object(logger, "connect", return_value=self.db), \
                patch.object(logger, "import_csv", return_value=0), \
                patch.object(logger, "fetch_backlog", return_value=iter(())), \
                patch.object(logger, "fetch_json", side_effect=[payload, KeyboardInterrupt]) as fetch, \
                patch.object(logger.time, "sleep"), \
                patch.object(logger.sys, "argv", ["logger", "--url", "http://device"]), \
                patch("builtins.print"):
            with self.assertRaises(KeyboardInterrupt):
                logger.main()
        self.assertEqual(["http://device/api/current"] * 2,
                         [call.args[0] for call in fetch.call_args_list])
        self.assertEqual((94.0,), self.db.execute(
            "SELECT humidity_rh FROM measurements WHERE boot_id=30").fetchone())

    def test_sequence_deduplication(self):
        row = {"boot_id": 7, "sequence": 1, "uptime_ms": 5000, "co2": 900,
               "boxtemp": 24.0, "humidity": 80.0, "outertemp": 20.0}
        self.assertTrue(logger.insert(self.db, row))
        self.assertFalse(logger.insert(self.db, row))
        self.assertTrue(logger.insert(self.db, {**row, "sequence": 2, "humidity": 81.0}))
        self.assertEqual(2, self.db.execute("SELECT COUNT(*) FROM measurements").fetchone()[0])
    def test_invalid_values_stay_null(self):
        row = {"boot_id": 1, "sequence": 1, "uptime_ms": 5, "co2": None,
               "boxtemp": None, "humidity": None, "outertemp": 20,
               "valid": {"co2": False, "boxtemp": False, "humidity": False, "outertemp": True},
               "faults": {"rtd_box": 4}}
        logger.insert(self.db, row)
        stored = self.db.execute("SELECT co2_ppm,temp_box_c,valid_co2 FROM measurements").fetchone()
        self.assertEqual((None, None, 0), stored)

    def test_scd_temperature_does_not_replace_box_temperature(self):
        logger.insert(self.db, {"boot_id": 12, "sequence": 1,
                               "boxtemp": 26.5, "scdtemp": 28.25,
                               "scd_offset": 4.0})
        stored = self.db.execute(
            "SELECT temp_box_c,temp_scd_c,scd_temperature_offset_c FROM measurements"
        ).fetchone()
        self.assertEqual((26.5, 28.25, 4.0), stored)

    def test_scd_humidity_is_separate_and_survives_csv_export(self):
        logger.insert(self.db, {"boot_id": 15, "sequence": 1,
                               "humidity": 94.0, "scd_humidity": 90.0})
        logger.insert(self.db, {"boot_id": 15, "sequence": 2,
                               "humidity": None, "scd_humidity": 91.0})
        stored = self.db.execute(
            "SELECT humidity_rh,humidity_scd_rh,valid_humidity,valid_scd_humidity "
            "FROM measurements ORDER BY sequence").fetchall()
        self.assertEqual([(94.0, 90.0, 1, 1), (None, 91.0, 0, 1)], stored)
        path = Path(self.temp.name) / "export.csv"
        logger.export_csv(self.db, path)
        with path.open(newline="") as file:
            rows = list(logger.csv.DictReader(file))
        self.assertEqual(["90.0", "91.0"], [row["humidity_scd_rh"] for row in rows])
        imported = logger.connect(Path(self.temp.name) / "imported.db")
        try:
            self.assertEqual(2, logger.import_csv(imported, path))
            self.assertEqual([(94.0, 90.0), (None, 91.0)], imported.execute(
                "SELECT humidity_rh,humidity_scd_rh FROM measurements ORDER BY sequence"
            ).fetchall())
        finally:
            imported.close()

    def test_inbox_pt100_is_recorded_separately(self):
        logger.insert(self.db, {"boot_id": 16, "sequence": 1,
                               "boxtemp": 62.8, "box_rtd_temp": 25.2,
                               "sht_heated": True, "rtd_box_raw": 8990})
        logger.insert(self.db, {"boot_id": 16, "sequence": 2,
                               "boxtemp": 25.5, "box_rtd_temp": None,
                               "faults": {"rtd_box": 4}})
        self.assertEqual([(62.8, 25.2, 1, 0), (25.5, None, 0, 4)],
                         self.db.execute("SELECT temp_box_c,temp_box_rtd_c,valid_box_rtd,"
                                         "rtd_box_fault FROM measurements ORDER BY sequence").fetchall())

    def test_heated_and_cooling_values_are_retained_and_exported(self):
        for sequence, (temperature, heated, cooling) in enumerate(
                [(78.0, True, False), (35.0, False, True)], 1):
            logger.insert(self.db, {"boot_id": 17, "sequence": sequence,
                                   "boxtemp": temperature, "humidity": 12.5,
                                   "sht_heated": heated, "sht_cooling": cooling})
        self.assertEqual([(78.0, 12.5, 1, 1, 1, 0), (35.0, 12.5, 1, 1, 0, 1)],
                         self.db.execute("SELECT temp_box_c,humidity_rh,valid_box,"
                                         "valid_humidity,sht_heated,sht_cooling "
                                         "FROM measurements ORDER BY sequence").fetchall())
        path = Path(self.temp.name) / "heater.csv"
        logger.export_csv(self.db, path)
        with path.open(newline="") as file:
            rows = list(logger.csv.DictReader(file))
        self.assertEqual(["1", "0"], [row["sht_heated"] for row in rows])
        self.assertEqual(["0", "1"], [row["sht_cooling"] for row in rows])

    def test_manual_offset_and_cooldown_survive_csv_roundtrip(self):
        logger.insert(self.db, {"boot_id": 20, "sequence": 1, "humidity": 94.2,
                               "humidity_offset_rh": 1.0, "sht_heater_elapsed_ms": 60010, "sht_read_uptime_ms": 132000})
        path = Path(self.temp.name) / "correction.csv"
        logger.export_csv(self.db, path)
        imported = logger.connect(Path(self.temp.name) / "imported.db")
        try:
            logger.import_csv(imported, path)
            self.assertEqual((94.2, 1.0, 60010, 132000), imported.execute(
                "SELECT humidity_rh,humidity_offset_rh,sht_heater_elapsed_ms,sht_read_uptime_ms FROM measurements"
            ).fetchone())
        finally:
            imported.close()
        self.assertEqual((None, None, 0), self.db.execute(
            "SELECT temp_box_rtd_c,valid_box_rtd,rtd_box_fault FROM measurements"
        ).fetchone())

    def test_sensor_identity_survives_a_swap(self):
        for sequence, serial in enumerate(("000000000001", "000000000002"), 1):
            logger.insert(self.db, {"boot_id": 12, "sequence": sequence,
                                   "scd_serial": serial})
        self.assertEqual(
            [("000000000001",), ("000000000002",)],
            self.db.execute("SELECT scd_serial FROM measurements ORDER BY sequence").fetchall())

    def test_rtd_configuration_recovery_is_recorded(self):
        diagnostics = dict(zip(logger.RTD_DIAGNOSTIC_COLUMNS,
                               (8990, 17, 17, 8978, 17, 17, 1)))
        logger.insert(self.db, {"boot_id": 13, "sequence": 1, **diagnostics})
        stored = self.db.execute(
            "SELECT " + ",".join(logger.RTD_DIAGNOSTIC_COLUMNS) + " FROM measurements"
        ).fetchone()
        self.assertEqual(tuple(diagnostics.values()), stored)

    def test_migrate_existing_database_preserves_old_rows(self):
        path = Path(self.temp.name) / "legacy.db"
        with sqlite3.connect(path) as legacy:
            legacy.executescript(logger.SCHEMA)
            legacy.execute("ALTER TABLE measurements ADD COLUMN rtd_box_fault INTEGER NOT NULL DEFAULT 0")
            legacy.execute("""INSERT INTO measurements
                (received_at,boot_id,sequence,uptime_ms,temp_box_c,
                 valid_co2,valid_box,valid_humidity,valid_outer)
                VALUES ('2026-09-08',1,1,5000,25.5,0,1,0,0)""")
        legacy.close()
        for _ in range(2):
            migrated = logger.connect(path)
            try:
                stored = migrated.execute(
                    "SELECT temp_box_c,temp_scd_c,scd_temperature_offset_c,humidity_scd_rh FROM measurements"
                ).fetchone()
                self.assertEqual((25.5, None, None, None), stored)
                logger.insert(migrated, {"boot_id": 2, "sequence": 1,
                                         "humidity": 94, "humidity_offset_rh": 1})
                self.assertEqual((94.0, 1.0), migrated.execute(
                    "SELECT humidity_rh,humidity_offset_rh FROM measurements WHERE boot_id=2"
                ).fetchone())
            finally:
                migrated.close()
if __name__ == "__main__":
    unittest.main()
