"""Reliable Portenta logger: QSPI backfill -> SQLite -> optional CSV export."""
import argparse
import csv
import json
import os
import sqlite3
import sys
import time
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

BASE = Path(__file__).parent
DB_FILE = BASE / "data" / "measurements.db"
CSV_FILE = BASE / "data" / "measurements.csv"
DEFAULT_URL = os.getenv("SENSOR_URL", "http://192.168.31.168")
POLL_INTERVAL = float(os.getenv("POLL_INTERVAL", "5"))
RTD_DIAGNOSTIC_COLUMNS = (
    "rtd_box_raw", "rtd_box_config_before", "rtd_box_config_after", "rtd_outer_raw",
    "rtd_outer_config_before", "rtd_outer_config_after", "rtd_config_recoveries",
)

SCHEMA = """
CREATE TABLE IF NOT EXISTS measurements(
 id INTEGER PRIMARY KEY, received_at TEXT NOT NULL, device_time TEXT,
 boot_id INTEGER NOT NULL, sequence INTEGER NOT NULL, uptime_ms INTEGER NOT NULL,
 co2_ppm INTEGER, temp_box_c REAL, humidity_rh REAL, temp_outer_c REAL,
 valid_co2 INTEGER NOT NULL, valid_box INTEGER NOT NULL,
 valid_humidity INTEGER NOT NULL, valid_outer INTEGER NOT NULL,
 rtd_outer_fault INTEGER NOT NULL DEFAULT 0,
 UNIQUE(boot_id, sequence));
CREATE INDEX IF NOT EXISTS ix_measurements_received ON measurements(received_at);
"""

def now() -> str:
    return datetime.now(timezone.utc).astimezone().isoformat(timespec="seconds")

def connect(path: Path = DB_FILE) -> sqlite3.Connection:
    path.parent.mkdir(parents=True, exist_ok=True)
    db = sqlite3.connect(path, timeout=10)
    db.executescript(SCHEMA)
    _add_sensor_columns(db)
    db.execute("PRAGMA journal_mode=WAL")
    db.execute("PRAGMA synchronous=NORMAL")
    return db


def _add_sensor_columns(db: sqlite3.Connection) -> None:
    columns = {row[1] for row in db.execute("PRAGMA table_info(measurements)")}
    additions = {"temp_scd_c": "REAL", "scd_temperature_offset_c": "REAL",
                 "scd_serial": "TEXT", "humidity_scd_rh": "REAL",
                 "valid_scd_humidity": "INTEGER",
                 "temp_box_rtd_c": "REAL", "valid_box_rtd": "INTEGER",
                 "rtd_box_fault": "INTEGER",
                 "sht_heated": "INTEGER", "sht_cooling": "INTEGER",
                 "humidity_offset_rh": "REAL", "sht_heater_elapsed_ms": "INTEGER", "sht_read_uptime_ms": "INTEGER"}
    additions.update({name: "INTEGER" for name in RTD_DIAGNOSTIC_COLUMNS})
    for name, column_type in additions.items():
        if name not in columns:
            db.execute(f"ALTER TABLE measurements ADD COLUMN {name} {column_type}")
    db.commit()

def fetch_json(url: str) -> dict:
    with urllib.request.urlopen(url, timeout=8) as response:
        return json.load(response)

def fetch_backlog(url: str):
    with urllib.request.urlopen(url, timeout=30) as response:
        for raw in response:
            try:
                yield json.loads(raw)
            except (json.JSONDecodeError, UnicodeDecodeError):
                continue

def normalize(payload: dict) -> dict:
    record = payload.get("measurement", payload)
    valid = record.get("valid", {})
    faults = record.get("faults", {})
    return {
        "received_at": now(), "device_time": None,
        "boot_id": int(record["boot_id"]), "sequence": int(record["sequence"]),
        "uptime_ms": int(record.get("uptime_ms", 0)),
        "co2_ppm": record.get("co2"), "temp_box_c": record.get("boxtemp"),
        "humidity_rh": record.get("humidity"), "temp_outer_c": record.get("outertemp"),
        "humidity_scd_rh": record.get("scd_humidity"),
        "valid_scd_humidity": int(valid.get("scd_humidity", record.get("scd_humidity") is not None)),
        "humidity_offset_rh": record.get("humidity_offset_rh", 0.0),
        "sht_heater_elapsed_ms": record.get("sht_heater_elapsed_ms"),
        "sht_read_uptime_ms": record.get("sht_read_uptime_ms"),
        "sht_heated": record.get("sht_heated"),
        "sht_cooling": record.get("sht_cooling"),
        "temp_box_rtd_c": record.get("box_rtd_temp"),
        "valid_box_rtd": None if "box_rtd_temp" not in record else int(valid.get("box_rtd_temp", record.get("box_rtd_temp") is not None)),
        "rtd_box_fault": int(faults.get("rtd_box", 0)),
        "temp_scd_c": record.get("scdtemp"),
        "scd_temperature_offset_c": record.get("scd_offset"),
        "scd_serial": record.get("scd_serial"),
        **{name: record.get(name) for name in RTD_DIAGNOSTIC_COLUMNS},
        "valid_co2": int(valid.get("co2", record.get("co2") is not None)),
        "valid_box": int(valid.get("boxtemp", record.get("boxtemp") is not None)),
        "valid_humidity": int(valid.get("humidity", record.get("humidity") is not None)),
        "valid_outer": int(valid.get("outertemp", record.get("outertemp") is not None)),
        "rtd_outer_fault": int(faults.get("rtd_outer", 0)),
    }

FIELDS = tuple(normalize({"boot_id": 0, "sequence": 0}).keys())
def insert(db: sqlite3.Connection, payload: dict) -> bool:
    row = normalize(payload)
    sql = f"INSERT OR IGNORE INTO measurements({','.join(FIELDS)}) VALUES({','.join('?' for _ in FIELDS)})"
    before = db.total_changes
    db.execute(sql, tuple(row[key] for key in FIELDS))
    db.commit()
    return db.total_changes > before

def import_csv(db: sqlite3.Connection, path: Path = CSV_FILE) -> int:
    if not path.exists() or db.execute("SELECT COUNT(*) FROM measurements").fetchone()[0]:
        return 0
    count = 0
    with path.open(newline="", encoding="utf-8-sig") as file:
        for sequence, row in enumerate(csv.DictReader(file), 1):
            payload = {"boot_id": 0, "sequence": sequence, "uptime_ms": sequence * 5000,
                       "co2": _number(row.get("co2_ppm")), "boxtemp": _number(row.get("temp_box_c")),
                       "humidity": _number(row.get("humidity_rh")), "outertemp": _number(row.get("temp_outer_c")),
                       "scd_humidity": _number(row.get("humidity_scd_rh")),
                       "humidity_offset_rh": _number(row.get("humidity_offset_rh")),
                       "sht_heater_elapsed_ms": _number(row.get("sht_heater_elapsed_ms")),
                       "sht_read_uptime_ms": _number(row.get("sht_read_uptime_ms")),
                       "sht_heated": _number(row.get("sht_heated")),
                       "sht_cooling": _number(row.get("sht_cooling")),
                       "box_rtd_temp": _number(row.get("temp_box_rtd_c"))}
            if insert(db, payload):
                db.execute("UPDATE measurements SET received_at=?, device_time=? WHERE boot_id=0 AND sequence=?",
                           (row["timestamp"], row["timestamp"], sequence))
                count += 1
    db.commit()
    return count

def _number(value):
    try:
        return float(value) if value not in (None, "") else None
    except ValueError:
        return None

def export_csv(db: sqlite3.Connection, path: Path = CSV_FILE) -> None:
    temp = path.with_suffix(".csv.tmp")
    with temp.open("w", newline="", encoding="utf-8") as file:
        writer = csv.writer(file)
        writer.writerow(["timestamp", "co2_ppm", "temp_box_c", "humidity_rh", "temp_outer_c",
                         "boot_id", "sequence", "valid_co2", "valid_box", "valid_humidity", "valid_outer",
                         "humidity_scd_rh", "valid_scd_humidity", "sht_heated", "sht_cooling", "temp_box_rtd_c", "valid_box_rtd", "humidity_offset_rh", "sht_heater_elapsed_ms", "sht_read_uptime_ms"])
        writer.writerows(db.execute("""SELECT COALESCE(device_time,received_at),co2_ppm,temp_box_c,humidity_rh,temp_outer_c,
            boot_id,sequence,valid_co2,valid_box,valid_humidity,valid_outer,
            humidity_scd_rh,valid_scd_humidity,sht_heated,sht_cooling,temp_box_rtd_c,valid_box_rtd,humidity_offset_rh,sht_heater_elapsed_ms,sht_read_uptime_ms FROM measurements ORDER BY id"""))
    temp.replace(path)

def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--url", default=DEFAULT_URL)
    parser.add_argument("--export-csv", action="store_true")
    args = parser.parse_args()
    with connect() as db:
        imported = import_csv(db)
        if imported: print(f"Imported {imported} existing CSV rows.")
        if args.export_csv:
            export_csv(db); print(f"Exported {CSV_FILE}"); return
        try:
            recovered = sum(insert(db, row) for row in fetch_backlog(args.url + "/api/backlog"))
            print(f"Recovered {recovered} buffered rows from Portenta.")
        except Exception as exc:
            print(f"Backfill unavailable: {exc}", file=sys.stderr)
        print(f"Logging {args.url} to {DB_FILE}")
        last_export = time.monotonic()
        while True:
            started = time.monotonic()
            try:
                payload = fetch_json(args.url + "/api/current")
                if insert(db, payload):
                    row = normalize(payload)
                    print(f"{row['received_at']}  #{row['sequence']}  CO2={row['co2_ppm']}  "
                          f"Box={row['temp_box_c']} C  Outer={row['temp_outer_c']} C  RH={row['humidity_rh']}%")
            except Exception as exc:
                print(f"[{now()}] fetch error: {exc}", file=sys.stderr)
            if time.monotonic() - last_export >= 3600:
                export_csv(db); last_export = time.monotonic()
            time.sleep(max(0, POLL_INTERVAL - (time.monotonic() - started)))

if __name__ == "__main__":
    try: main()
    except KeyboardInterrupt: print("\nLogger stopped.")
