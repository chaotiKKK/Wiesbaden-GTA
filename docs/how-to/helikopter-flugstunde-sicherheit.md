# How-to: Die Flugstunde nachweisen — Feuersperre, Anzeige, Abbau

Diese Anleitung beschreibt, woran man erkennt, dass die geführte
Hubschrauber-Flugstunde wirklich feuersicher ist, was der Spieler davon sieht
und was beim Abbau passiert. Sie setzt voraus, dass du das Editor-Target gebaut
hast (`Tools\build_gate1.cmd`).

Die aktuelle Regression umfasst drei Automationstests. Sie prueft Regeln,
Actor-Reset sowie Controller-Eingaben mit echten Geschuetzticks. Eine
visuelle HUD-Abnahme und der neue Paketlauf bleiben separate Schritte.

```
Tools\run_automation_test.cmd WiesbadenReal.Vehicles.Lesson ausbau_heli_gruen
```

Der Lauf `Saved/Logs/wb_test_ausbau_heli_gruen.log` vom 01.10.2026 war
3/3 gruen (`LessonSafety`, `LessonFire`, `LessonHeldFire`). Das gilt fuer den
dort gebauten Stand; nach weiteren Lifecycle-Aenderungen erneut pruefen.
Der vorausgehende Lauf `wb_test_ausbau_heli_rot.log` schlug an der fehlenden
Loslass-Sperre und am nicht angeschlossenen Absturz-Tick fehl.

## Die eine Regel: scharfes Feuern braucht zwei Freigaben

`AWiesbadenHelicopter::AllowsLiveFire(bLessonDryFireActive, bReleasePending)` ist
der ganze Vertrag:

| Trockenmodus | wartender Schuss | scharfes Feuern |
|---|---|---|
| aus | nein | **erlaubt** |
| an | nein | gesperrt |
| aus | ja | gesperrt |
| an | ja | gesperrt |

Die *Loslass-Sperre* (`bDryFireReleasePending`) wird bei jedem Wechsel des
Trockenmodus gesetzt. So ist auch Gamepad-A im Annahme-/Abschlussframe
abgesichert, bevor der Pawn die Taste gelesen hat. Ausschliesslich eine
ungehaltene Feuer-Eingabe hebt sie auf; erst ein neuer Druck darf feuern.

## Wo die Sperre sitzt und wer sie löst

Die Sperre sitzt am **Hubschrauber**, nicht am HUD. Fünf Stellen bauen die
Lektion ab, und alle fünf laufen über **eine** Funktion,
`AWiesbadenVehicleHUD::EndHelicopterLesson(Hinweis, ZusaetzlichHeli)`:

| Auslöser | Regel / Ort |
|---|---|
| Aussteigen (`Exit`) | `UpdateHelicopterLesson` |
| Pausenmenue „Flugstunde abbrechen“ | Menüeintrag 9 |
| Pawnwechsel | `ShouldLessonBreakOnPawnChange` |
| **Absturz** | `ShouldLessonBreakOnHeliLoss` |
| HUD-Ende | `EndPlay` |

Der Absturz war die eine Stelle, die fehlte: der Spieler sitzt im selben Actor
weiter (`bDestroyed`, nicht `UnPossessed`), also greift der Pawnwechsel nicht.
Die Lektion wartete auf Schritte, die nicht mehr erfüllbar sind, und die
Feuersperre blieb bis zum Aussteigen stehen.

Zusaetzlich beendet der HUD-Tick eine laufende Lektion auch bei verborgenem
HUD, Pawnwechsel und Absturz. `AWiesbadenHelicopter::EndPlay` raeumt den
lokalen Feuerzustand vollstaendig, weil der Actor dann nicht weiterlebt.
Aussteigen und Wiederaufsetzen behalten dagegen die Loslass-Sperre.

## Nach einem Absturz

`RespawnOnTowerHelipad` setzt den **Flug**zustand zurück und räumt über
`ReleaseFireForRespawn()` den **Feuer**zustand: Geschuetzabzug aus,
Loslass-Sperre **an**. Die Feuersperre der Lektion bleibt dabei **zu** — der aufgesetzte
Hubschrauber ist flugfähig, und die Lektion hält ihn bis zu ihrem Ende fest.
Wer sie beendet, ist der HUD (Absturzzeile der Tabelle). Ein Aufheben der
Sperre im Rumpf selbst würde scharfes Feuern *während* der Lektion erlauben —
das ist genau der Grund für diese Aufteilung.

## Was der Spieler sieht

Das Feld **GESCHUETZ** auf der Hubschrauber-Instrumententafel, Text aus
`AWiesbadenVehicleHUD::GetHelicopterGunStatus(...)`:

| Lage | Anzeige |
|---|---|
| Lektion oder Einladung läuft | `TROCKEN` |
| Abzug nach Lektionsende noch gehalten | `ABZUG LOS` |
| Geschütz überhitzt | `UEBERHITZT` |
| Kette leer | `LEER` |
| sonst | `BEREIT <Munition>` |

Die sperrenden Zustände kommen **vor** der Munition: solange die Lektion läuft,
ist die Zahl Nebensache, der Grund nicht.

Vor dem 01.10.2026 hatte die Tafel überhaupt kein Geschützfeld — Vario, Fahrt,
Fluglage, Höhe, Kurs, MOT, Kollektiv, Rotor, sonst nichts. In der Flugstunde
konnte man den Abzug halten, es passierte sichtbar nichts, und die Anzeige sagte
nichts dazu.

## Zwei Anzeigeregeln, die man beim Lesen prüfen sollte

- Die Lektionstafel wird **nicht** gezeichnet, solange das Pausenmenü offen ist
  (`PauseView != Aus`). Sonst lag sie über dem Menü, das selbst „Unterricht
  pausiert — bestätigen beendet die Flugstunde“ verspricht. Die Einladung war
  schon richtig behandelt.
- Der Abschlussschritt nennt `Enter / A`, kein `F / Y`, und kein
  `Tab / B überspringen`. `GetHelicopterLessonConfirmKeys` bzw.
  `GetHelicopterLessonSkipHint` sind die Quellen.

## Belegstellen im Log

- `Helikopter <Name> EndPlay (Grund <n>) — Abzug und Flugstunden-Feuersperre
  zurückgesetzt.`
- `Ka52 wiederaufgesetzt auf dem Landeplatz (…).` — danach ist der Abzug nach
  dem Aufsetzen frei.
- `Ka52-Reset: kein Sebbotower in der Ebene — bleibe, wo ich bin.` — erscheint
  in einem Testfeld ohne Turm; im Spiel kommt der Wiederaufsetzpfad nicht davon
  ab.
