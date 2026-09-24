"""Read and incrementally cache append-only sensor measurements."""
import sqlite3
from contextlib import closing

import pandas as pd


def read_database(db, after_id=0):
    columns = {row[1] for row in db.execute("PRAGMA table_info(measurements)")}
    optional = ",".join(name if name in columns else f"NULL AS {name}"
                        for name in ("humidity_scd_rh", "humidity_offset_rh"))
    return pd.read_sql_query(
        f"""SELECT COALESCE(device_time,received_at) AS timestamp,
        co2_ppm,temp_box_c,humidity_rh,temp_outer_c,{optional}
        FROM measurements WHERE id > ? ORDER BY id""", db, params=(after_id,))


def read_csv(path):
    return pd.read_csv(path, usecols=lambda name: name in {
        "timestamp", "co2_ppm", "temp_box_c", "humidity_rh", "temp_outer_c",
        "humidity_scd_rh", "humidity_offset_rh"}, on_bad_lines="skip")


def normalize_data(df):
    # Legacy CSV rows contain local timestamps without an offset. New rows are
    # ISO-8601 timestamps with an offset. Preserve the legacy wall-clock time
    # and convert offset timestamps to Zurich wall-clock time before sorting.
    raw_timestamp = df["timestamp"].astype("string")
    has_offset = raw_timestamp.str.contains(r"(?:Z|[+-]\d{2}:\d{2})$", na=False)
    timestamp = pd.Series(pd.NaT, index=df.index, dtype="datetime64[ns]")
    timestamp.loc[~has_offset] = pd.to_datetime(
        raw_timestamp.loc[~has_offset], format="mixed", errors="coerce"
    )
    timestamp.loc[has_offset] = (
        pd.to_datetime(raw_timestamp.loc[has_offset], format="mixed", errors="coerce", utc=True)
        .dt.tz_convert("Europe/Zurich")
        .dt.tz_localize(None)
    )
    df["timestamp"] = timestamp
    df.dropna(subset=["timestamp"], inplace=True)
    if "temp_outer_c" not in df.columns:
        df["temp_outer_c"] = float("nan")
    df["temp_outer_c"] = pd.to_numeric(df["temp_outer_c"], errors="coerce")
    for column in ("humidity_scd_rh", "humidity_offset_rh"):
        if column not in df.columns:
            df[column] = float("nan")
        df[column] = pd.to_numeric(df[column], errors="coerce")
    df["humidity_corrected_rh"] = (
        pd.to_numeric(df["humidity_rh"], errors="coerce")
        + df["humidity_offset_rh"].fillna(0)
    ).clip(0, 100)
    df.sort_values("timestamp", inplace=True)
    return df


def load_data(database, csv):
    if database.exists():
        with closing(sqlite3.connect(database, timeout=10)) as db:
            return normalize_data(read_database(db))
    return normalize_data(read_csv(csv))


class MeasurementCache:
    """Keep all points; normalize only newly appended rows during normal logging.

    Edits/deletions without a simultaneous append trigger a full reload.
    Historical editing concurrent with appending requires restarting the viewer.
    """
    def __init__(self):
        self._db = None
        self._identity = None
        self._version = None
        self._last_id = 0
        self._row_count = 0
        self._frame = None
        self.revision = 0

    def close(self):
        if self._db is not None:
            self._db.close()
        self._db = None
        self._identity = None
        self._version = None
        self._frame = None
        self._last_id = 0
        self._row_count = 0

    def load(self, database, csv):
        path = database if database.exists() else csv
        stat = path.stat()
        identity = (path.resolve(), stat.st_dev, stat.st_ino, database.exists())
        if identity != self._identity:
            self.close()
            self._identity = identity
            if database.exists():
                self._db = sqlite3.connect(database.resolve().as_uri() + "?mode=ro",
                                           uri=True, timeout=10)
        if self._db is None:
            version = (stat.st_mtime_ns, stat.st_size)
            if version != self._version:
                self._frame = normalize_data(read_csv(csv))
                self._version = version
                self.revision += 1
            return self._frame
        return self._load_database()

    def _load_database(self):
        self._db.execute("BEGIN")
        try:
            version = self._db.execute("PRAGMA data_version").fetchone()[0]
            if self._frame is not None and version == self._version:
                return self._frame
            last_id, count = self._db.execute(
                "SELECT COALESCE(MAX(id),0),COUNT(*) FROM measurements").fetchone()
            appended = (self._frame is not None and last_id > self._last_id
                        and count > self._row_count)
            rows = read_database(self._db, self._last_id if appended else 0)
            if appended and len(rows) != count - self._row_count:
                appended = False
                rows = read_database(self._db)
            incoming = normalize_data(rows)
            frame = pd.concat([self._frame, incoming], ignore_index=True) if appended else incoming
            if appended and not frame["timestamp"].is_monotonic_increasing:
                frame.sort_values("timestamp", inplace=True)
            self._frame = frame
            self._last_id, self._row_count, self._version = last_id, count, version
            self.revision += 1
            return self._frame
        finally:
            self._db.rollback()
