import sqlite3
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

import pandas as pd

from logger import logger, viewer_data


class ViewerCacheTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.path = Path(self.temp.name) / "measurements.db"
        self.csv = self.path.with_suffix(".csv")
        self.db = logger.connect(self.path)
        self.cache = viewer_data.MeasurementCache()

    def tearDown(self):
        self.cache.close()
        self.db.close()
        self.temp.cleanup()

    def insert(self, sequence, timestamp, temperature=25, humidity=94):
        logger.insert(self.db, {"boot_id": 1, "sequence": sequence,
                               "boxtemp": temperature, "humidity": humidity,
                               "humidity_offset_rh": 1})
        self.db.execute("UPDATE measurements SET received_at=? WHERE sequence=?",
                        (timestamp, sequence))
        self.db.commit()

    def test_only_new_rows_are_normalized_and_peak_is_preserved(self):
        self.insert(1, "2026-09-24T12:00:00+02:00")
        first = self.cache.load(self.path, self.csv)
        with patch.object(viewer_data, "read_database", wraps=viewer_data.read_database) as read:
            self.assertIs(first, self.cache.load(self.path, self.csv))
            read.assert_not_called()
        self.insert(2, "2026-09-24T12:00:05+02:00", temperature=62.83)
        with patch.object(viewer_data, "normalize_data", wraps=viewer_data.normalize_data) as normalize:
            data = self.cache.load(self.path, self.csv)
            self.assertEqual(1, len(normalize.call_args.args[0]))
        self.assertEqual([25, 62.83], data["temp_box_c"].tolist())
        self.assertEqual([95, 95], data["humidity_corrected_rh"].tolist())
        pd.testing.assert_frame_equal(data.reset_index(drop=True),
            viewer_data.load_data(self.path, self.csv).reset_index(drop=True))

    def test_backfill_sorts_older_timestamp_without_losing_rows(self):
        self.insert(1, "2026-09-24T12:00:05+02:00")
        self.cache.load(self.path, self.csv)
        self.insert(2, "2026-09-24T12:00:00+02:00", temperature=63)
        data = self.cache.load(self.path, self.csv)
        self.assertEqual([63, 25], data["temp_box_c"].tolist())

    def test_edits_and_deletes_reload_existing_data(self):
        self.insert(1, "2026-09-24T12:00:00+02:00")
        self.cache.load(self.path, self.csv)
        self.db.execute("UPDATE measurements SET humidity_offset_rh=2")
        self.db.commit()
        self.assertEqual(96, self.cache.load(self.path, self.csv).iloc[0]["humidity_corrected_rh"])
        self.db.execute("DELETE FROM measurements")
        self.db.commit()
        self.assertTrue(self.cache.load(self.path, self.csv).empty)

    def test_invalid_timestamp_does_not_duplicate_rows_on_next_append(self):
        self.insert(1, "invalid")
        self.assertTrue(self.cache.load(self.path, self.csv).empty)
        self.insert(2, "2026-09-24T12:00:00+02:00")
        self.assertEqual(1, len(self.cache.load(self.path, self.csv)))
