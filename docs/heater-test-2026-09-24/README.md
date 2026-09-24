# SHT45-Abkuehltest vom 24.09.2026

Firmware 2.0.11, Boot-ID 1971548083. Sechs Heizimpulse mit nominal 200 mW fuer 1 s im Abstand von 120 s. Heizimpulse von 01:19:21 bis 01:29:22 (Europe/Zurich). Anschliessend automatisch keine weitere Heizung; Nachbeobachtung bis etwa 01:45 Uhr. Alle Heiz- und Abkuehlwerte bleiben aufgezeichnet.

## Ergebnis

Die Temperatur ist nach etwa 90 s fuer diesen Aufbau weitgehend zurueckgekehrt. Die Feuchte zeigt einen deutlich laengeren Nachlauf. Eine vollstaendige Feuchtestabilisierung nach 60 oder 90 s ist nicht nachgewiesen. Die kurze Differenz zwischen 90 und 110 s unterschaetzt den laengeren Nachlauf.

### Vergleich innerhalb der sechs Zyklen

Differenz zum jeweiligen Wert bei etwa 110 s nach dem gespeicherten Heizpeak; Median ueber sechs Zyklen. Das ist ein relativer Vergleich, keine absolute Kalibrierung.

| Wartezeit | Temperaturdifferenz | Feuchtedifferenz |
|---|---:|---:|
| 30 s | +0.11 Grad C | -1.01 Prozentpunkte |
| 45 s | +0.06 Grad C | -0.52 Prozentpunkte |
| 60 s | +0.04 Grad C | -0.31 Prozentpunkte |
| 90 s | +0.01 Grad C | -0.12 Prozentpunkte |

### Nach dem letzten Heizimpuls

| Abstand zum Heizpeak | SHT45 Temperatur | SHT45 Feuchte | SCD41 Feuchte |
|---|---:|---:|---:|
| 60 s | 24.98 Grad C | 84.95 % rF | 81.43 % rF |
| 90 s | 24.93 Grad C | 85.12 % rF | 81.40 % rF |
| 110 s | 24.95 Grad C | 85.25 % rF | 81.42 % rF |
| 180 s | 24.95 Grad C | 85.50 % rF | 81.41 % rF |
| 300 s | 24.90 Grad C | 85.81 % rF | 81.43 % rF |
| 600 s | 24.87 Grad C | 86.14 % rF | 81.55 % rF |
| 900 s | 24.87 Grad C | 86.51 % rF | 81.59 % rF |

## Einordnung und Empfehlung

- Temperatur: vorlaeufig 90 s Wartezeit fuer den getesteten Aufbau.
- Feuchte: 60/90 s nicht als vollstaendig stabil freigeben. Vorlaeufig mindestens 5 min und zusaetzlich die zeitliche Aenderung beobachten, beispielsweise weniger als 0.1 Prozentpunkte pro Minute ueber zwei Minuten. Dieses Kriterium ist ein Vorschlag, keine Herstellergarantie. Bei den beobachteten Kurven kann dafuer etwa 10-15 min erforderlich sein.
- Nach 5 min stieg die Feuchte bis 15 min noch um 0.70 Prozentpunkte. Selbst 5 min allein garantieren deshalb kein Gleichgewicht. Auch eine langsame reale Umgebungsaenderung oder eine Aenderung des Feuchte-Offsets laesst sich ohne unabhaengige Referenz nicht ausschliessen.
- Die SCD41-Feuchte blieb in dieser Nachbeobachtung vergleichsweise ruhig (etwa 81.4-81.6 % rF); sie ist keine kalibrierte Referenz. Der Inbox-PT100 wurde mit aufgezeichnet.
- Ergebnis gilt fuer diesen Einbau bei ungefaehr 25 Grad C und 85 % rF, nicht automatisch bei anderen Temperaturen, Luftstroemungen oder Kondensation.

## Daten und Betriebszustand

Alle sechs Heizpeaks sowie die Vergleichspunkte 60/90/110 s sind gespeichert. Ein fehlender Logger-Datensatz (Sequenz 212) liegt nach der fuenfminuetigen Vergleichsphase; er beeinflusst diese Vergleiche nicht. Sequenz 1 fehlt ebenfalls in der Logger-Aufzeichnung vor dem ersten Impuls. Zeitangaben beziehen sich auf den gespeicherten Heizmesspunkt; durch den 5-s-Erfassungstakt sind es ungefaehre Wartezeiten, keine millisekundengenauen Zeitpunkte nach Heizungsende.

Der Heizer ist nach sechs Impulsen automatisch aus. Die Test-Firmware startet bei einem Geraeteneustart erneut einen auf sechs Impulse begrenzten Test. Die regulaere Aufzeichnung aller Sensorwerte laeuft weiter. Es werden weiterhin keine gueltigen Messwerte wegen Heizung verworfen.

- [Messdaten CSV](measurements.csv)
- [Vollstaendige Auswertung JSON](summary.json)
- [Diagramm](cooldown.png)

Herstellergrundlage: [Sensirion: Using the Integrated Heater of SHT4x in High-Humidity Environments](https://sensirion.com/media/documents/A88858C9/629626D4/Application_Note_Creep_Mitigation_SHT4x.pdf). Die dort genannte Minute ist ein Beispiel fuer einen bestimmten Aufbau; die thermische und Feuchte-Erholung muss am eigenen Geraet validiert werden.
