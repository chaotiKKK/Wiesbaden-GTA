# Arcade-Mobilitaet, Nordfriedhof-Haendler und Nerobergbahn

## Ziel

Das Spiel erhaelt einen sichtbaren, spielbaren Wirtschaftskreislauf fuer den
Helikopterhangar, ein zugaengliches Arcade-Fahrgefuehl fuer Auto und
Helikopter sowie eine voll nutzbare Nerobergbahn als Panorama-Erlebnis.

## Umfang und Reihenfolge

Die Umsetzung erfolgt in fuenf abgeschlossenen vertikalen Ausbaustufen:

1. Nordfriedhof-Haendler, Shop und Helikopterhangar.
2. Arcade-Abstimmung der Pkw-Fahrphysik.
3. Xbox-Gamepad-Steuerung und Arcade-Flugverhalten des Helikopters.
4. Begeh- und befahrbare Nerobergbahn mit eingepasster Szenerie.
5. Gepruefte Standalone-Auslieferung mit Desktop-Verknuepfung.

Jede Stufe bleibt fuer sich baubar, testbar und im Spiel nutzbar.

## Haendler und Hangar

Ein interagierbarer Haendler-NPC wird an der Endhaltestelle Nordfriedhof
platziert: vom Friedhofseingang aus links, hinter der Mauer. Die genaue
Position wird an den realen Kartendaten, der begehbaren Flaeche und der
Mauergeometrie ausgerichtet.

Die Szene besteht aus NPC, kleinem Verkaufsstand, Wegfuehrung,
Beschilderung, zur Umgebung passender Beleuchtung und wenigen passenden
Friedhofsrequisiten. Vorhandene lokale Assets werden bevorzugt; einfache,
fehlende Requisiten entstehen in Blender oder direkt im Unreal-Editor.

Beim Annähern erscheint ein Interaktionshinweis. Die Interaktion oeffnet das
vorhandene Store-System als Haendlerfenster. Es zeigt Guthaben, Preis,
Beschreibung und Besitzstatus. Der Kauf wird ausschliesslich durch
`UWiesbadenStoreSubsystem::TryPurchase` abgewickelt und bleibt damit
persistent. Erfolgs-, Besitz- und Nicht-genug-Guthaben-Faelle erhalten
sichtbares und akustisches Feedback.

Der Helikopterhangar ist wieder eine echte Freischaltung: Vor dem Kauf ist
das Einsteigen gesperrt, danach dauerhaft erlaubt. Konsolenbefehle bleiben
Entwicklerhilfen, sind aber nicht mehr der Spielerweg. Der widerspruechliche
Zwischenstand mit offenem Helikopter und Kaufhinweis wird entfernt.

## Pkw-Fahrgefuehl

Die bestehende datenreine `FWiesbadenVehiclePhysics` bleibt die Quelle der
Wahrheit. Sie wird nicht ersetzt, sondern als Arcade-Modell abgestimmt:

- Beschleunigen, Ausrollen und Bremsen vermitteln sichtbare Masse und
  Traegheit.
- Der Lenkeinschlag baut sich weich auf und ist bei Geschwindigkeit begrenzt.
- Normaler Grip ist stabil und einsteigerfreundlich.
- Handbremse oder zu hohes Tempo erlauben kontrollierbaren Schlupf und
  Gegenlenken; der Wagen faengt sich wieder vorhersehbar.

Xbox-Belegung: linker Stick lenkt, RT beschleunigt, LT bremst bzw. fordert im
Stand Rueckwaerts an, A aktiviert die Handbremse, B steigt aus.

## Helikopter

Der Helikopter bekommt ein Xbox-zentriertes Arcade-Profil. Der rechte Stick
steuert Nick und Roll, der linke Stick steuert Gieren, RT steigt und LT sinkt.
Eingaben werden geglaettet. Bei zentriertem rechten Stick richtet sich der
Helikopter automatisch auf; bei aktiver Eingabe nimmt die Assistenz zurueck.
So ist Schweben, Vorwaertsflug und Landen leicht erlernbar, ohne dass der
Helikopter starr auf Schienen wirkt. Das HUD zeigt die Belegung beim
Einsteigen.

## Nerobergbahn

Die vorhandenen Bahn- und Ride-State-Systeme werden erweitert, nicht
dupliziert. Tal- und Bergstation erhalten Bahnsteig, Wartebereich,
Interaktionszone und einen sichtbar haltenden Zug. Der Einstieg ist nur im
Halt moeglich. Der Spieler wird an einen Sitzplatz gebunden, kann die Kamera
waehrend der Fahrt frei drehen und steigt an der naechsten Station wieder aus.
Ein Notausstieg verhindert festhaengende Zustände.

Schienenprofil, Stützen, Bahnsteige und Zug werden gegen das reale Terrain
ausgerichtet. Wege, Begrenzungen, Vegetation und Stationsdetails binden die
Bahn in die Landschaft ein, ohne grossflaechige Terrain-Aenderungen.

## Fehlerbehandlung und Tests

- Haendler: Testfaelle fuer Annäherung/Interaktion, Kauf, zu wenig Guthaben,
  bereits gekauften Hangar und persistente Freischaltung.
- Pkw: Tests fuer Ausrollen, Bremsweg, Lenkaufbau, Gripgrenze, Handbremsdrift
  und Wiederfangen.
- Helikopter: Tests fuer Gamepad-Achsen, Totzonen, automatische Stabilisierung,
  Auf- und Abstieg sowie Eingabevorrang.
- Bahn: Tests fuer Halt -> Einsteigen -> Fahrt -> Aussteigen, unzulaessige
  Zustandsuebergaenge und sichere Terrain-/Stationsendpunkte.

Nach jeder Stufe laufen die passenden Automation-Tests; zum Abschluss folgen
ein Unreal-Editor-Build und eine Spielpruefung mit Xbox-Gamepad.

## Nicht im Umfang
## Standalone-Auslieferung

Nach der Spielpruefung wird die vorhandene Release-Pipeline
`Tools/build_release.cmd` verwendet. Sie baut zuerst Editor und Automation-
Tests, fuehrt den Rauchtest aus und paketiert nur bei vollstaendig gruenem
Ergebnis die Windows-Standalone nach `Saved/Package/Windows`.

Die Pipeline erneuert danach die Desktop-Verknuepfung
`Wiesbaden Real (Paket).lnk`. Sie zeigt auf die paketierte
`WiesbadenReal.exe`, nicht auf den Unreal Editor. Das vorherige Paket wird
vorher unter `Saved/Package_previous` gesichert; ein fehlgeschlagener oder
unerwuenschter Release kann mit `Tools/build_release.cmd -Rollback`
zurueckgesetzt werden.


Keine neue allgemeine Wirtschaft, kein vollstaendiger NPC-Dialogbaum, keine
Simulationsphysik und keine grosse Landschafts-Neugenerierung.
