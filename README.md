# fastnchip-sensors

Messsystem fuer eine Klimakammer auf dem Arduino Portenta Machine Control:

- SHT45 fuer Inbox-Temperatur und relative Feuchte
- SCD41 fuer CO2 und eine gepunktete Feuchte-Vergleichskurve
- PT100 Kanal 0 fuer die Aussentemperatur
- lokale Webseite und QSPI-Rueckpuffer
- Python-Logger mit SQLite, CSV-Export, Viewer und Backup

## Installation

1. `include/wifi-credentials.example.h` nach
   `../wifi-credentials.h` kopieren und dort `WIFI_SSID` und
   `WIFI_PASSWORD` eintragen. Die echte Datei liegt damit ausserhalb des
   Repositorys.
2. Sensorparameter und Kanalbelegung in include/config.h kontrollieren.
3. Firmware bauen: pio run
4. Erstinstallation per USB/DFU: pio run -t upload
5. Python-Pakete: python -m pip install -r logger/requirements.txt

## Betrieb

Logger: python logger/logger.py

Andere IP: python logger/logger.py --url http://192.168.31.168

Eine vorhandene measurements.csv wird einmalig nach measurements.db importiert.
Danach wird der QSPI-Puffer nachgeholt und anhand von boot_id plus sequence
duplikatfrei gespeichert.
Die Firmware erzeugt pro Start eine Hardware-Zufallskennung, damit ein
zurueckgesetzter Flash-Zaehler keine alten Messschluessel wiederverwendet.
Falls die Zufallsquelle ausfaellt, dient die gespeicherte Kennung als Fallback.

SCD41 koennen einzeln am selben Anschluss ausgetauscht werden (feste Adresse
0x62). Nach drei Messintervallen ohne gueltige Messung initialisiert die Firmware erneut
und durchsucht Wire sowie Wire1. Ohne Sensor wird alle 5 Sekunden gesucht.
Die Seriennummer steht als scd_serial in API und SQLite zur Zuordnung der
Messwerte zum jeweiligen Exemplar. Beide erhalten denselben festen Offset
aus config.h; eine automatische Temperaturanpassung findet nicht statt.
Zwei SCD41 duerfen nicht gleichzeitig am selben I2C-Bus angeschlossen werden.

Der SHT45 wird an I2C (SDA, SCL, GND und passende Versorgung des Moduls)
angeschlossen, Adresse `0x44` in `config.h`. Die Firmware sucht unabhaengig vom
SCD41 auf Wire und Wire1. Beide Sensoren koennen wegen ihrer unterschiedlichen
Adressen denselben Bus verwenden. Ausserhalb der Heiz-/Abkuehlphase laufen interne High-Precision-Messungen alle 5 Sekunden; die 10-ms-Konversion wird im Hauptloop abgewartet. CRC-Fehler,
fehlende und veraltete Werte werden ungueltig, ohne Ersatz durch SCD41 oder PT100.
Der erste Messdatensatz kann vor Ende der ersten Konversion noch NULL enthalten.
Protokoll und Umrechnung: [Sensirion SHT4x-Datenblatt](https://sensirion.com/resource/datasheet/sht4x).

## Firmware-Struktur

Die Firmware ist entlang ihrer Verantwortlichkeiten unterteilt:

- `SensorManager`: SCD41- und Aussen-RTD-Hardwarezugriff
- `Sht45Sensor`: unabhaengige, nicht blockierende SHT45-Abfrage mit CRC-Pruefung
- `MeasurementController`: Messtakt, Verlauf und Datenfluss
- `QspiStorage`: persistenter Messpuffer und Partitionen
- `FirmwareUpdater`: OTA-Datei, Pruefung und Aktivierung
- `WebServer`: WLAN, HTTP-API und UI-Auslieferung
- `Application`: explizite Verdrahtung und Ablaufsteuerung

Sensorinitialisierung, HTTP-Empfang, OTA-Upload und Backlog-Auslieferung werden
schrittweise im Hauptloop verarbeitet. Nur notwendige Start-/Reset-Wartezeiten
sind blockierend.

Viewer: python logger/viewer.py

<img src="screenshot_viewer.jpg" alt="FastNChip sensor data viewer" width="800">

CSV fuer Excel: python logger/logger.py --export-csv

## Datenqualitaet

Sensorfehler werden als SQL NULL plus Validitaetsflag und RTD-Fehlercode
gespeichert. Geraete-Uptime und Sequenznummer machen Neustarts und Luecken sichtbar.

Der SCD41-ASC ist standardmaessig deaktiviert, da eine geschlossene Kammer nicht
regelmaessig 400-ppm-Frischluft sieht. Der SCD41-Temperaturoffset wird bei jeder
Initialisierung aus config.h gesetzt und vom Sensor zur Kontrolle zurueckgelesen.
Der bestehende Offset stammt vom frueheren Abgleich mit dem Box-PT100 und bleibt
unveraendert. Ein spaeterer Abgleich zum SHT45 setzt stabile, vergleichbare
Temperaturen an beiden Messorten voraus. Der Hoehenwert steht in config.h.
Betrieb nur ohne Kondensation.

SQLite speichert die korrigierte SCD41-Temperatur separat als temp_scd_c und den
zurueckgelesenen Offset als scd_temperature_offset_c. Alte Zeilen bleiben dort
NULL. Inbox-Temperatur (`boxtemp` / `temp_box_c`) und Feuchte
(`humidity` / `humidity_rh`) stammen neu vom SHT45. Die SCD41-Feuchte steht separat
als `scd_humidity` in API, Verlauf und QSPI sowie als `humidity_scd_rh` mit
`valid_scd_humidity` in SQLite und CSV. Webansicht und Viewer zeichnen sie
gepunktet. Bestehende Datenbanken werden beim Loggerstart erweitert; alte
Messwerte behalten ihre urspruengliche Quelle (Box-PT100 bzw. SCD41-Feuchte),
die neue Vergleichsspalte bleibt fuer alte Zeilen NULL. Alte CSV bleiben lesbar.

Die PT100-Messung verwendet Dreileiterkompensation und einen 50-Hz-Netzfilter.
configure_rtd_driver.py passt beim Build den fest auf Version 1.0.5 gesetzten
Arduino-MAX31865-Treiber an: 50 Hz und 70 ms Wartezeit fuer Einzelmessungen
(laut Datenblatt bis zu 66 ms). Die heruntergeladene Bibliothek bleibt unveraendert.
Vor jeder Messung werden Dreileitermodus und Filter neu gesetzt und zurueckgelesen;
nach der Konversion wird die Konfiguration nochmals geprueft. So bleibt ein
alleiniger MAX31865-Reset nicht dauerhaft als fehlende Leitungswiderstands-
kompensation unentdeckt. Bei fehlgeschlagener Pruefung ist der Messwert ungueltig.
API, QSPI und SQLite speichern ADC-Rohwerte, die Konfiguration vor/nach der
Messung (`rtd_outer_config_before/after`) sowie
`rtd_config_recoveries`, den kumulativen Zaehler der Korrekturversuche seit Start.
Im Normalbetrieb ist die Konfiguration 17 (0x11: Dreileiter, 50 Hz, Bias aus).
Die Pruefung stellt feste Hardwareeinstellungen wieder her; sie kalibriert
keine Temperatur und veraendert den festen SCD41-Offset nicht.

Ein begrenzter Vergleich kann ueber RTD_DIAGNOSTIC_SAMPLES aktiviert werden
(0 = aus). RTD_DIAGNOSTIC_TWO_WIRE waehlt fehlende Dreileiterkompensation statt
60 Hz als Vergleich. Regulaere Messwerte bleiben bei Dreileiter/50 Hz;
Vergleichswerte stehen separat in API und QSPI. Absichtliche Moduswechsel
erhoehen ebenfalls den Korrekturzaehler. Im normalen Build ist der Vergleich aus.
Zum reproduzierten Temperatursprung siehe [Untersuchung vom 09.09.2026](docs/pt100-offset-2026-09-09.md).

## Persistenz und Backup

Der Portenta schreibt NDJSON auf QSPI und rotiert bei 4 MiB. SQLite ist die
fuehrende Langzeitspeicherung. backup.py sichert eine laufende Datenbank konsistent.

## OTA und Tests

pio run erzeugt firmware.ota. upload_firmware.ps1 akzeptiert kein rohes BIN.

Tests: python -m unittest discover -s test -p "test_*.py"

## Regulaerer Betrieb ab 2.0.13

Der Inbox-PT100 (Kanal 1) wird nicht mehr abgefragt, aufgezeichnet oder angezeigt.
Alte Datenbankspalten und historische Werte bleiben erhalten. Der Aussen-PT100
(Kanal 0) bleibt aktiv.

Die Aufzeichnung von CO2, SCD41-Feuchte und Aussen-PT100 erfolgt alle 5 Sekunden.
Der SHT45 wird unabhaengig davon alle 65 Sekunden uebernommen. Zwischen diesen
Terminen halten Anzeige und Aufzeichnung seinen letzten Wert einschliesslich
Offset. `sht_read_uptime_ms` kennzeichnet in API, QSPI, SQLite und CSV den
urspruenglichen Uebernahmezeitpunkt; wiederholte Werte sind keine neuen SHT45-
Messungen. Ein Fehler am naechsten Termin ersetzt den alten Wert durch ungueltig.
Nach einer erfolgreichen
SHT45-Ablesung wird ein Heizimpuls angefordert: nominal 200 mW bei 3.3 V fuer
1 Sekunde, nur mit einem frischen Wert unter 65 Grad C. Nach der maximalen
Heizkonversion (1.11 s) folgen mindestens 60 Sekunden Abkuehlzeit, bevor eine
neue normale Messung beginnt. Heizwerte werden nicht als Umgebungsmessung
veroeffentlicht. Der Hauptloop bleibt waehrenddessen frei. Nach einem Neustart
ist die erste gueltige Messung noch ungeheizt und ohne Korrektur.

`humidity` / `humidity_rh` bleiben Rohwerte. Pro Messung werden der manuelle
Offset `humidity_offset_rh` und `sht_heater_elapsed_ms` (Zeit der SHT45-Messung
seit Ende der maximalen Heizkonversion) in QSPI, API, SQLite und CSV gespeichert.
Im Zeitfenster von 60 bis unter 65 Sekunden nach der Heizkonversion gilt
+1.00 Prozentpunkt rF; ausserhalb dieses Fensters und bei Fehlern gilt kein
Offset. Das ist eine vorlaeufige manuelle Korrektur, keine absolute Kalibrierung.

Webansicht (`humidity_corrected`) und Viewer (`humidity_corrected_rh`) zeigen
nur Rohwert plus gespeicherten Offset, begrenzt auf 0 bis 100 %rF. Es gibt keine
zusaetzliche Rohwertkurve. SCD41 bleibt gepunktet, SHT45 als Volllinie.
Historische Werte ohne Offset bleiben unveraendert; feste Skalen und adaptive
Skalierung bleiben erhalten.

Der abgeschlossene Abkuehltest mit Firmware 2.0.11 (sechs Impulse im Abstand
von 120 Sekunden, danach Heizer aus) ist unter
[Heiztest vom 24.09.2026](docs/heater-test-2026-09-24/README.md) dokumentiert.
