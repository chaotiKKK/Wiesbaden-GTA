// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadNetworkTypes.h"

#include "Vehicles/WiesbadenVehiclePhysics.h"
#include "WiesbadenTrafficSimulation.generated.h"

struct FWiesbadenTrafficLightSystem;

/**
 * Sichtbare Fahrzeug-Platzierung (fuer den ISM-Spawner): Position/Rotation
 * aus der Simulation plus deterministischer Farb-Index.
 */
/** Blinker eines Fahrzeugs. */
UENUM(BlueprintType)
enum class EVehicleIndicator : uint8
{
	None  UMETA(DisplayName = "Aus"),
	Left  UMETA(DisplayName = "Links"),
	Right UMETA(DisplayName = "Rechts")
};

USTRUCT(BlueprintType)
struct WIESBADENREAL_API FPlacedTrafficVehicle
{
	GENERATED_BODY()

	/** Fahrzeug-Id aus der Simulation (stabil ueber Ticks). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 VehicleId = INDEX_NONE;

	/** Welt-Transform (Location + Yaw aus der Fahrtrichtung). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FTransform Transform = FTransform::Identity;

	/** Deterministischer Farb-Index (0..PaletteSize-1) aus der Fahrzeug-Id. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 ColorIndex = 0;

	/** Einschlag der Vorderraeder in Radiant (positiv = links). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float SteerAngleRad = 0.0f;

	/** Bremslicht an. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	bool bBraking = false;

	/** Gesetzter Blinker (Dauerzustand; das Blinken macht die Darstellung). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	EVehicleIndicator Indicator = EVehicleIndicator::None;
};

/** Ein Fahrzeug der Verkehrs-Simulation auf dem Spur-Graph. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FTrafficVehicle
{
	GENERATED_BODY()

	/** Eindeutige, aufsteigend vergebene Fahrzeug-Id (auch Spawn-Zaehler). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 VehicleId = INDEX_NONE;

	/** True, wenn das Fahrzeug auf einer Spur faehrt (sonst auf einer Verbindung). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	bool bOnLane = true;

	/** Aktuelle Spur (Index in FRoadNetwork::Lanes), wenn bOnLane. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneId = INDEX_NONE;

	/** Aktuelle Verbindung (Index in FRoadNetwork::Connections), wenn !bOnLane. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 ConnectionIndex = INDEX_NONE;

	/** Entfernung entlang der aktuellen Bahn (Spur-Mittellinie bzw. Verbindungskurve). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double DistanceCm = 0.0;

	/** Aktuelle Geschwindigkeit in cm/s (durch Kopf-zu-Schwanz begrenzt). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double SpeedCmS = 0.0;

	/** Wunschgeschwindigkeit in cm/s (Tempolimit der Spur * Fahrzeug-Charakter). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double DesiredSpeedCmS = 0.0;

	/** Weltposition auf der Sollbahn (pro Tick berechnet). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FVector Location = FVector::ZeroVector;

	/** Normierte Fahrtrichtung (fuer spaetere Fahrzeug-Meshes). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FVector Forward = FVector::ForwardVector;

	/** Bremslicht an: deutliche Verzoegerung oder Halt trotz Fahrwunsch. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	bool bBraking = false;

	/** Gesetzter Blinker vor dem Abbiegen. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	EVehicleIndicator Indicator = EVehicleIndicator::None;

	/**
	 * Restliche Sperrzeit bis zum naechsten Spurwechsel in Sekunden.
	 *
	 * Ohne Sperre wechselt ein Fahrzeug, das zwischen zwei gleich vollen
	 * Spuren steht, in jedem Tick hin und her und zappelt sichtbar.
	 */
	float LaneChangeCooldown = 0.0f;

	/**
	 * Weltposition der KAROSSERIE - das, was man sieht.
	 *
	 * Location oben ist die Sollposition auf der Bahn. Sie traegt Abstaende,
	 * Stau und Kreuzungslogik und muss exakt auf dem Graphen bleiben. Die
	 * Karosserie folgt ihr nur nach, mit Lenkeinschlag und Wendekreis. Genau
	 * dieser Unterschied nimmt dem Verkehr das "wie auf Schienen".
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FVector BodyLocation = FVector::ZeroVector;

	/** Gierwinkel der Karosserie in Radiant (integriert, nicht abgelesen). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float BodyYawRad = 0.0f;

	/**
	 * Einschlag der Vorderraeder in Radiant (positiv = links).
	 *
	 * Wird gebraucht, um die Raeder mitzudrehen: ein Auto, das um die Ecke
	 * faehrt und dabei geradeaus stehende Raeder hat, faellt sofort auf.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	float SteerAngleRad = 0.0f;

	/** Fahrzeugtyp (Index in WiesbadenTrafficCars::Types()), beim Einsetzen aus der Id. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 TypeIndex = 0;

	/** Tempo der Karosserie aus der Fahrphysik (cm/s) - das, was man sieht. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double BodySpeedCmS = 0.0;

	/** Gerollter Winkel der Raeder (rad, 0..2 pi): Physik-Tempo durch Radradius. */
	float WheelSpinRad = 0.0f;

	/** Nicken und Wanken der Karosserie aus den Beschleunigungen (Grad) - wie
	 *  beim Spielerauto (AWiesbadenCar::ComputeBodyTilt), nur an der Karosserie. */
	float BodyPitchDeg = 0.0f;
	float BodyRollDeg = 0.0f;

	/** Steigung der Fahrbahn unter dem Fahrzeug (Grad, + = bergauf). */
	float SlopePitchDeg = 0.0f;

	/** Solltempo des Vorticks (fuer die Sollbeschleunigung des Fahrers). */
	double PrevSollSpeedCmS = 0.0;

	/**
	 * Die Fahrphysik des Spielerautos (FWiesbadenVehiclePhysics) mit den Werten
	 * des Vorbilds - ein Fahrer am Steuer folgt der Sollbahn
	 * (WiesbadenTrafficCars::ComputeDriverInput).
	 */
	FWiesbadenVehiclePhysics Physics;
	bool bPhysicsInitialized = false;

	/**
	 * Steht am Ende einer Sackgasse und wartet, bis der Spieler nicht mehr
	 * hinsieht - erst dann verschwindet es (vorher: mitten im Bild).
	 */
	bool bWaitingAtDeadEnd = false;

	/** False bis zum ersten Tick; dann wird die Karosserie auf die Bahn gesetzt. */
	bool bBodyInitialized = false;

	/** Intern: zum Entfernen markiert (am Bahnende ohne Folgebahn). */
	bool bRemoved = false;

	/**
	 * Intern: war das Fahrzeug im letzten Tick im Anfahr-Fenster einer
	 * signalisierten Verbindung bzw. dort an Rot gehalten?
	 *
	 * Fuer die Ampel-Diagnose werden DISTINKTE Ereignisse gezaehlt (die
	 * FALSE->TRUE-Flanke), nicht pro Tick: sonst inflationiert ein einziges
	 * wartendes Fahrzeug die "Anfahrten" in unter einer Sekunde auf beliebige
	 * Hoehe, und die Kennzahl luegt (20 "Anfahrten" = ein Fahrzeug, 20 Frames).
	 */
	bool bWasApproachingSignal = false;
	bool bWasHeldAtRed = false;

	/** Fahrbild-Messung: letzte Lenkrichtung (-1/0/+1) und seitliches Nachziehen
	 *  des Sicherheitsnetzes im letzten Tick (cm). */
	int8 SteerSign = 0;
	float LastRecoverCm = 0.0f;

	/** Weicher Spurwechsel: Querversatz der Sollposition zur neuen Spur beim
	 *  Wechsel (cm, klingt ueber LaneChangeSeconds ab) und die Zeit seither. */
	float LaneShiftStartCm = 0.0f;
	float LaneShiftElapsed = 0.0f;

	/** Karosserie auf der Bahn (bBodyOnPath): um so viel liegt sie HINTER der
	 *  Sollposition (cm, entlang der Bahn; negativ = davor), und die zuletzt
	 *  verlassene Bahn - ihre Hinterachse steht dort oft noch. */
	float BodyLagCm = 0.0f;
	bool bPrevOnLane = true;
	int32 PrevEdgeIndex = INDEX_NONE;
};

/** Parameter der Verkehrs-Simulation. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficSettings
{
	GENERATED_BODY()

	/**
	 * Verkehrsdichte 0..1 (aus dem City-Prompt: "leere Strassen" 0.2, "Stau"
	 * 0.95). Steuert die Spawn-Rate linear.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrafficDensity = 0.5f;

	/** Fahrzeuge pro Sekunde bei voller Dichte (1.0). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float MaxSpawnRatePerSecond = 2.0f;

	/** Anteil des Tempolimits als Basis-Wunschgeschwindigkeit (0.75 = 75 %). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float TargetSpeedFraction = 0.75f;

	/**
	 * Mindestabstand zwischen zwei Fahrzeugen (Spitze an Spitze) in cm.
	 * Der Folger bremst so, dass diese Luecke nie unterschritten wird.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double MinGapCm = 700.0;

	/**
	 * Zeitluecke, die ein wartepflichtiges Fahrzeug in der Vorfahrtstrasse
	 * abwartet, in Sekunden.
	 *
	 * Ohne Vorfahrt haelt an jeder ampellosen Kreuzung AUCH die Hauptstrasse,
	 * sobald aus einer Wohnstrasse jemand im Knoten steht - gemessen brach der
	 * Fluss damit von 36 auf 56 % Steher ein. Mit ihr wartet der
	 * Wartepflichtige auf eine Luecke, und die Hauptachse laeuft durch.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	double JunctionYieldSeconds = 3.0;

	/**
	 * Blockierfreihaltung: nicht in die Kreuzung fahren, wenn dahinter kein
	 * Platz ist.
	 *
	 * Sie ist der teuerste Teil der Kreuzungsregel und zugleich der, der die
	 * letzten ineinander steckenden Paare beseitigt. Gemessen am Bahnhofsplatz
	 * (je 200 s): mit ihr 0,5 Paare je Diagnose bei 51,5 % Stehern, ohne sie
	 * 3,6 Paare bei 46,2 %. Abschaltbar, damit diese Abwaegung nachrechenbar
	 * bleibt statt in einer Zahl zu verschwinden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bKeepJunctionsClear = true;

	/**
	 * Haltelinie je Zufahrt aus der Knotengeometrie statt pauschal 350 cm vor
	 * dem Spurende (FWiesbadenTrafficSimulation::ComputeStopSetbackCm). Aus =
	 * alter Stand, fuer den A/B-Vergleich (-WbHaltelinienAlt).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bGeometricStopLines = true;

	/**
	 * Kreuzungskonflikte ueberhaupt beachten.
	 *
	 * Abschaltbar, damit die Wirkung der Regel MESSBAR bleibt - so wie die
	 * gruene Welle abschaltbar ist. Ohne diesen Schalter liesse sich nur gegen
	 * einen alten Messlauf vergleichen, in dem auch alles andere anders war
	 * (andere Karte, andere Signalprogramme), und die Zuordnung der Wirkung
	 * waere Behauptung statt Messung.
	 *
	 * AUS heisst: Fahrzeuge fahren wieder durcheinander - kein Spielzustand,
	 * nur ein Messwerkzeug.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bJunctionConflicts = true;

	/**
	 * Platz, den es hinter der Kreuzung geben muss, in cm.
	 *
	 * Gemessen war dies der teuerste Posten der ganzen Kreuzungsregel: 27 bis
	 * 32 der rund 55 wartenden Fahrzeuge standen NICHT wegen eines belegten
	 * Weges, sondern weil hinter der Kreuzung angeblich kein Platz war.
	 *
	 * Verlangt wurde der volle Folgeabstand (700 cm). Der ist fuer die Luecke
	 * zwischen zwei FAHRENDEN Fahrzeugen gedacht; zum Einfaedeln braucht es
	 * nur den Platz, den das Fahrzeug einnimmt - 414 cm Laenge plus etwas
	 * Luft. Alles darueber haelt Fahrzeuge vor einer Kreuzung fest, hinter der
	 * sie problemlos Platz faenden, und genau das staut sich zurueck.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double JunctionExitSpaceCm = 480.0;

	/**
	 * KREUZUNG FREI HALTEN, ernst gemeint (an): eingefahren wird erst, wenn
	 * hinter der Kreuzung mindestens MinGapCm + VehicleHalfLengthCm frei sind
	 * (sonst gilt JunctionExitSpaceCm), und die Haltelinie einer Zufahrt haelt
	 * auch zu ihrer EIGENEN Zielspur Abstand.
	 *
	 * GEMESSEN am 26.09.2026: 91 von 92 "Fahrzeuge ineinander"-Paaren lagen an
	 * Kreuzungen, 51 davon zwischen einem Wartenden vor der Ecke und einem
	 * Fahrzeug, das 1,6-4 m weit in der Querstrasse stand. Der Folgeabstand
	 * gilt Mitte zu Mitte (700 cm): wer bei 480 cm freier Zielspur einfuhr,
	 * musste 2 m VOR deren Anfang halten - mitten in der Kreuzung. Und die
	 * Haltelinie nahm die eigene Zielspur ausdruecklich aus.
	 * Gilt nur an echten Kreuzungen (mindestens drei Arme), nicht an den
	 * Stossstellen zerteilter Strassen, und ein Fahrzeug auf der Zielspur
	 * zaehlt mit seinem Bremsweg: streng ueberall kostete an der
	 * Albrecht-Duerer-Strasse (8,9-m-Stuecke) so viel Fluss, dass der Anteil
	 * Stehender von 29 auf 40 % stieg.
	 * Nur zum Messen abschaltbar: -WbKreuzungAlt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bStrictJunctionClearance = true;

	/**
	 * Halbe Laenge und halbe Breite eines Verkehrsfahrzeugs in cm.
	 *
	 * Beschreibt DASSELBE Auto wie die Kollisionsbox des Spawners
	 * (UTrafficVehicleSpawnerComponent::VehicleCollisionExtent, 207/77/77) -
	 * wer eine der beiden Zahlen aendert, muss die andere mitziehen. Gebraucht
	 * werden sie, wo es um den Platz geht, den ein Fahrzeug WIRKLICH einnimmt:
	 * die Konfliktpruefung an Kreuzungen und die Ueberlappungs-Diagnose. Der
	 * Folgeabstand MinGapCm ist davon unabhaengig - er misst von Mitte zu
	 * Mitte und ist absichtlich groesser als ein Fahrzeug lang ist.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "50.0"))
	double VehicleHalfLengthCm = 207.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "20.0"))
	double VehicleHalfWidthCm = 77.0;

	/** Untergrenze der Wunschgeschwindigkeit in km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "5.0"))
	double MinSpeedKmh = 20.0;

	/** Obergrenze der gleichzeitig aktiven Fahrzeuge (Performance-Guard). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "1"))
	int32 MaxVehicles = 3000;

	/**
	 * Umkreis um den Spieler, in dem neue Fahrzeuge einsetzen, in Metern.
	 *
	 * Ohne diese Begrenzung verteilen sich die Fahrzeuge ueber das gesamte
	 * Netz - in Wiesbaden 2.700 km. Selbst 3.000 Fahrzeuge ergaeben dann rund
	 * eines je Kilometer, und in Sichtweite waere so gut wie nie eines. Ein
	 * Verkehr, den niemand sieht, kostet nur Rechenzeit.
	 *
	 * 600 m liegt knapp ausserhalb der Sichtweite: Fahrzeuge tauchen nicht vor
	 * den Augen des Spielers auf, sind aber schnell erreichbar.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "50.0"))
	double SpawnRadiusMeters = 600.0;

	/**
	 * Entfernung, ab der Fahrzeuge wieder entfernt werden, in Metern.
	 *
	 * Muss deutlich groesser als SpawnRadiusMeters sein, sonst entsteht am
	 * Rand ein Flackern aus staendigem Einsetzen und Entfernen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double DespawnRadiusMeters = 900.0;

	/**
	 * Fahrzeuge je SPURKILOMETER im Umkreis bei voller Dichte (1.0).
	 *
	 * Frueher stand hier eine feste Stueckzahl je Umkreis (110). Die ist die
	 * falsche Groesse: wie belebt eine Strasse WIRKT, haengt nicht daran, wie
	 * viele Fahrzeuge irgendwo im 600-m-Radius sind, sondern wie dicht sie auf
	 * der Fahrbahn stehen. Am Stadtrand liegen rund 37 km Spur im Umkreis, in
	 * der Innenstadt ein Vielfaches - dieselbe Stueckzahl ergab dort also eine
	 * noch leerere Strasse. Mit einer Dichte passt sich die Zahl der Umgebung
	 * an: 37 km * 12 /km * Dichte 0.5 = rund 220 Fahrzeuge (vorher 55).
	 *
	 * Bezug: 6 Fahrzeuge je km bei Standard-Dichte heisst eines alle 165 m -
	 * belebt, aber kein Stau. Gemessen am Startplatz: 55 Fahrzeuge kosteten
	 * 132 Bilder/s, 147 kosteten 128, 368 noch 120 - die Zahl ist nicht das
	 * Bildraten-Nadeloehr, die Obergrenze setzt die Glaubwuerdigkeit.
	 *
	 * MaxVehicles bleibt die harte Obergrenze.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	double VehiclesPerLaneKm = 12.0;

	/**
	 * Deterministischer Streu-Startwert: beeinflusst die individuelle
	 * Wunschgeschwindigkeit je Fahrzeug (0.8..1.0 des TargetSpeedFraction).
	 * Gleicher Seed + gleiche Eingaben -> identisches Verhalten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	int32 RandomSeed = 20260814;

	/**
	 * Groesste Beschleunigung in cm/s^2.
	 *
	 * Die Simulation hat die Geschwindigkeit frueher in einem Tick auf den
	 * Zielwert gesetzt - ein Fahrzeug sprang damit zwischen Stillstand und
	 * Vollgas. Daraus entstanden Stop-and-Go-Wellen, die sich rueckwaerts
	 * durch die Kolonne fortpflanzten und wie ein Stau aus dem Nichts aussahen.
	 *
	 * 250 cm/s^2 (2,5 m/s^2) entspricht zuegigem Anfahren eines Pkw.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "10.0"))
	double MaxAccelerationCmS2 = 250.0;

	/**
	 * Angenommene Verzoegerung fuer die Vorausschau in cm/s^2.
	 *
	 * Bestimmt NICHT, wie stark ein Fahrzeug bremsen darf - das legt die
	 * Abstandsregel fest, und sie muss dafuer frei bleiben, sonst faehrt der
	 * Verkehr auf. Dieser Wert beantwortet nur die Frage, ab welcher Entfernung
	 * vor einem Bahnende ein Fahrzeug ueber die Bahngrenze hinausschauen muss:
	 * Mindestluecke plus Bremsweg aus der aktuellen Geschwindigkeit.
	 *
	 * 800 cm/s^2 sind 0,8 g - eine kraeftige Bremsung.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "10.0"))
	double MaxDecelerationCmS2 = 800.0;

	/**
	 * VORAUSSCHAUENDES Folgen (Gipps): mit dieser Verzoegerung (cm/s^2) plant
	 * ein Fahrer, hinter seinem Vordermann zum Stehen zu kommen, und mit dieser
	 * Reaktionszeit (s) haelt er Abstand.
	 *
	 * Frueher loeste die Abstandsregel nur den NAECHSTEN Tick exakt: der
	 * Folger fuhr mit 7 m Mittenabstand hinterher (bei 50 km/h eine halbe
	 * Sekunde) und bremste, wenn der Vordermann stand, in EINEM Tick von 50 auf
	 * 0. Die Physik-Karosserie kann das nicht - sie schwang nach, das
	 * Sicherheitsnetz zog sie quer zurueck, und genau das war im Spiel als
	 * Schwanken, Rutschen und Ineinanderfahren zu sehen. Die exakte Regel
	 * bleibt als Notbremse darunter.
	 *
	 * 350 cm/s^2 ist ein zuegiges, aber alltaegliches Bremsen; 0,6 s ergeben im
	 * Gleichgewicht rund 0,9 s Zeitluecke plus MinGapCm.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild", meta = (ClampMin = "50.0"))
	double ComfortDecelerationCmS2 = 350.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild", meta = (ClampMin = "0.0"))
	double FollowReactionSeconds = 0.6;

	/**
	 * Dauer eines Spurwechsels (s): die Sollposition zieht in einem weichen
	 * S-Bogen hinueber statt in einem Tick 3,5 m quer zu springen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild", meta = (ClampMin = "0.5"))
	double LaneChangeSeconds = 3.5;

	/** Unter diesem Tempo (cm/s) wechselt niemand die Spur - kein Hopsen im stehenden Stau. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild", meta = (ClampMin = "0.0"))
	double LaneChangeMinSpeedCmS = 280.0;

	/** A/B: altes Fahrbild (exakte Abstandsregel, Spurwechsel als Sprung, Rot sofort), -WbFahrbildAlt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild")
	bool bSmoothDriving = true;

	/**
	 * KAROSSERIE AUF DER BAHN: Hinter- und Vorderachse liegen auf der
	 * Fahrlinie, die Ausrichtung ist ihre Verbindung, der Radeinschlag folgt
	 * aus der Kruemmung. Die Fahrphysik bleibt fuer das LAENGS-Fahren (Motor,
	 * Gaenge, Bremse, Gewichtsverlagerung) - die Karosserie laeuft mit ihrem
	 * physikalischen Tempo entlang der Bahn hinter der Sollposition her.
	 *
	 * Vorher lenkte die Karosserie frei einem Zielpunkt nach, schnitt in den
	 * engen Kreuzungen Ecken, das Sicherheitsnetz zog sie QUER zurueck, und
	 * wer so zum Stehen kam, stand schraeg. Gemessen: 26-31 cm seitliches
	 * Nachziehen je Fahrzeug-Sekunde, Gier-Abweichung RMS 25-28 Grad, 10-17 %
	 * schraeg im Stand, stehende Autos bis 64 Grad quer zur Spur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild")
	bool bBodyOnPath = true;

	/** Groesster Nachlauf der Karosserie hinter der Sollposition (cm) - mehr wird
	 *  unsichtbar mitgezogen, damit Kolonnen nicht ineinander rutschen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrbild", meta = (ClampMin = "0.0"))
	double MaxBodyLagCm = 200.0;

	// (Die Regel dazu steht als RequiredLaneChangeGapCm weiter unten.)

	/** True: Fahrzeuge duerfen zum Ueberholen die Spur wechseln. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	bool bAllowLaneChange = true;

	/**
	 * Anteil der Wunschgeschwindigkeit, unter dem ein Fahrzeug als behindert
	 * gilt und einen Spurwechsel erwaegt (0.7 = unter 70 %).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	double LaneChangeSpeedDeficit = 0.7;

	/**
	 * UNTERGRENZE der Luecke nach vorn UND nach hinten auf der Zielspur in cm.
	 *
	 * Nach hinten ist genauso wichtig wie nach vorn: Wer vor einen schnelleren
	 * Nachfolger zieht, loest dort dieselbe Bremswelle aus, der er selbst
	 * entkommen wollte.
	 *
	 * DIESER WERT WAR DER GRUND, WARUM NIE UEBERHOLT WURDE. Er stand auf
	 * 1400 cm - doppelt so viel wie der Folgeabstand MinGapCm (700 cm), den
	 * die Simulation im fliessenden Verkehr selbst herstellt. Damit verlangte
	 * die Regel eine Luecke, die doppelt so gross ist wie jede Luecke, die
	 * ueberhaupt entsteht: in einer Kolonne existierte sie nirgends. Gemessen
	 * scheiterten 393.957 von 594.580 Anlaeufen (66 %) genau hier, waehrend in
	 * 95 Sekunden ganze 59 Spurwechsel zustande kamen.
	 *
	 * Jetzt ist es die Untergrenze im Stand; darueber zaehlt die Zeitluecke
	 * (siehe LaneChangeGapSeconds).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "100.0"))
	double LaneChangeMinGapCm = 700.0;

	/**
	 * Geforderte Zeitluecke zur Zielspur in Sekunden.
	 *
	 * Eine feste Strecke ist an beiden Enden falsch: 14 m sind bei 15 km/h
	 * dreieinhalb Sekunden (viel zu zaghaft, man kommt nie hinein) und bei
	 * 80 km/h eine halbe Sekunde (viel zu dreist). Die Zeitluecke stimmt bei
	 * beiden Tempi - genau so schaetzen Fahrer eine Luecke auch ab.
	 *
	 * Nach vorn zaehlt das eigene Tempo, nach hinten das des Nachfolgers: er
	 * ist derjenige, der bremsen muss.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.1"))
	double LaneChangeGapSeconds = 1.2;

	/**
	 * Vorausschau in Sekunden: ab wann ein Fahrzeug einen Wechsel ERWAEGT.
	 *
	 * Bisher zog ein Fahrzeug erst hinaus, wenn es bereits unter 70 % seines
	 * Wunschtempos war - also wenn es schon eingeklemmt ist und die Luecken
	 * ringsum laengst zu sind. Gemessen scheiterten dann 92 % der Anlaeufe
	 * daran, dass auch die Nebenspur vorn dicht war. Ein Fahrer wechselt
	 * frueher: sobald er sieht, dass er auf einen Langsameren auflaeuft, und
	 * solange die Luecke noch da ist.
	 *
	 * 0 schaltet die Vorausschau ab (dann gilt nur das Tempodefizit).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	double LaneChangeLookAheadSeconds = 4.0;

	/** Sperrzeit zwischen zwei Spurwechseln desselben Fahrzeugs in Sekunden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic", meta = (ClampMin = "0.0"))
	float LaneChangeCooldownSeconds = 4.0f;

	// -- Einspurmodell der Karosserie ----------------------------------------
	//
	// "KI-Autos sollen nicht wie auf Schienen fahren, sondern echte
	// Fahrphysik, Wendezirkel und Radstellungen beachten."
	//
	// Die LAENGSbewegung bleibt auf dem Spur-Graphen - sie traegt Abstand,
	// Stau und Ampeln, und dort ist der Graph unschlagbar guenstig. Nur die
	// QUERbewegung war das Problem: Position und Gierwinkel wurden direkt von
	// der Polylinie abgelesen. Damit sprang die Ausrichtung an jedem
	// Stuetzpunkt um, jede Kurve wurde auf den Zentimeter genau getroffen, und
	// kein Fahrzeug hatte je einen Wendekreis.
	//
	// Darum faehrt die Karosserie jetzt ein eigenes Einspurmodell: Sie zielt
	// auf einen Punkt weiter vorn auf der Bahn (reine Verfolgung), daraus
	// folgt ein Radeinschlag; der Einschlag ist begrenzt (Wendekreis) und
	// aendert sich nur mit endlicher Geschwindigkeit (Lenkraddrehung); erst
	// daraus ergeben sich Gierrate und Position.

	/** Radstand in cm (Mittelklasse-Pkw). Bestimmt zusammen mit dem groessten
	 *  Einschlag den Wendekreis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "100.0"))
	double WheelbaseCm = 265.0;

	/**
	 * Groesster Radeinschlag in Grad.
	 *
	 * 33 Grad bei 265 cm Radstand ergeben einen Wendekreisradius von
	 * 265/tan(33 Grad) = 408 cm, also gut 8 m Durchmesser - der Wert eines
	 * ueblichen Pkw. Groesser laesst die Autos um die eigene Achse drehen,
	 * kleiner laesst sie enge Kreuzungen ueberfahren.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "5.0", ClampMax = "60.0"))
	double MaxSteerAngleDeg = 33.0;

	/** Wie schnell sich der Einschlag aendern darf, in Grad je Sekunde. Ohne
	 *  diese Grenze zuckt das Rad in einem Tick von Anschlag zu Anschlag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "10.0"))
	double MaxSteerRateDegS = 220.0;

	/** Grundvorausschau der reinen Verfolgung in cm (bei Stillstand). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "50.0"))
	double LookaheadBaseCm = 320.0;

	/**
	 * Zusaetzliche Vorausschau je Sekunde Fahrzeit.
	 *
	 * Schnelle Fahrzeuge muessen weiter vorausschauen, sonst pendeln sie: Sie
	 * korrigieren auf einen Punkt, den sie laengst passiert haben. 0,55 s sind
	 * bei 50 km/h rund 7,6 m Vorausschau.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "0.0"))
	double LookaheadSeconds = 0.55;

	/**
	 * Groesster geduldeter Abstand der Karosserie zur Sollbahn in cm.
	 *
	 * Das Einspurmodell kann die Bahn nicht immer halten - in sehr engen
	 * Kurven schneidet oder weitet es. Das ist erwuenscht. Laeuft der Fehler
	 * aber davon (Bahnwechsel, kaputtes Netz), landet das Auto im Gruenen. Ab
	 * dieser Grenze wird es weich zur Bahn zurueckgezogen.
	 *
	 * 400 cm liegt bewusst UEBER einer Spurbreite: Ein Spurwechsel versetzt
	 * die Sollbahn schlagartig um rund 350 cm, und die Karosserie soll in
	 * diesem Moment nicht am Sicherheitsnetz haengen, sondern gemaechlich
	 * herueberziehen - genau so, wie ein Spurwechsel aussehen muss.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "20.0"))
	double MaxBodyDeviationCm = 400.0;

	/**
	 * Groesster geduldeter Abstand IM STAND in cm.
	 *
	 * Die grosszuegigen 400 cm oben sind mit dem Spurwechsel begruendet - und
	 * der passiert im FAHREN. Ein stehendes Fahrzeug hat keinen Grund, vier
	 * Meter neben seiner Spur zu stehen, und genau das war im Probespiel zu
	 * sehen: gemessen 23 von 27 ineinander steckenden Paaren betrafen
	 * ausschliesslich die Karosserien, bei Seitenversaetzen bis 5 m, waehrend
	 * die Sollpositionen sauber auseinander lagen.
	 *
	 * Dass es im Stand nie von allein besser wird, liegt am Modell: die
	 * Karosserie bewegt sich mit `Step = Tempo * Dt` - bei Tempo null bewegt
	 * sie sich gar nicht. Wer mit Versatz zum Stehen kommt, bleibt dort.
	 *
	 * 60 cm ist kein gegriffener Wert: die schmalste Spur im Netz misst
	 * 275 cm, ein Fahrzeug 154 cm - es bleiben (275 - 154) / 2 = 60 cm Spiel
	 * je Seite. Damit steht das Auto im Stand immer noch in SEINER Spur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "0.0"))
	double StandingBodyDeviationCm = 60.0;

	/**
	 * Tempo in cm/s, ab dem die volle Abweichung erlaubt ist.
	 *
	 * Dazwischen wird linear geblendet. 500 cm/s sind 18 km/h - darunter
	 * wechselt in dichtem Verkehr kaum jemand die Spur, darueber soll das
	 * Herueberziehen ungestoert aussehen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "1.0"))
	double BodyDeviationFullSpeedCmS = 500.0;

	/**
	 * Groesster geduldeter Abstand im normalen Fahren in cm.
	 *
	 * Die 400 cm oben gelten NUR fuer das Herueberziehen nach einem
	 * Spurwechsel - dafuer sind sie begruendet. Wer geradeaus faehrt, hat
	 * dafuer keinen Anlass: in engen Kurven schneidet oder weitet das
	 * Einspurmodell um Dezimeter, nicht um Meter. 120 cm laesst dieses Leben
	 * zu und haelt das Auto trotzdem in seiner Spur - bei 325 cm Spurbreite
	 * und 154 cm Fahrzeugbreite reicht es gerade bis an die Nachbarspur.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik", meta = (ClampMin = "0.0"))
	double DrivingBodyDeviationCm = 120.0;

	/**
	 * Karosserie mit der Fahrphysik des Spielerautos: Gaenge, Reifen-
	 * Seitenkraefte, Radlastverlagerung - gelenkt und gefahren von einem
	 * Fahrer, der der Sollbahn folgt (WiesbadenTrafficCars). false = das
	 * alte kinematische Einspurmodell (StepBicycleModel).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Fahrphysik")
	bool bPhysicsBodies = true;

	// -- Nicht vor den Augen des Spielers ----------------------------------
	//
	// Fahrzeuge setzten an jedem Spuranfang im 600-m-Umkreis ein und
	// verschwanden am Ende jeder Sackgasse - auch mitten im Bild. Jetzt gilt:
	// was der Spieler sehen koennte, entsteht und verschwindet nicht.

	/**
	 * Sichtweite des Verkehrs in Metern: so weit zeichnet ihn
	 * UTrafficVehicleSpawnerComponent. Im Blickkegel und naeher als das setzt
	 * kein Fahrzeug ein und verschwindet keines.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Sicht", meta = (ClampMin = "50.0"))
	double DrawDistanceMeters = 550.0;

	/** Zuschlag auf das halbe Blickfeld (Grad): Bildrand, Umsehen, Kurven. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Sicht", meta = (ClampMin = "0.0", ClampMax = "90.0"))
	double ViewConeMarginDeg = 20.0;

	/** So nah gilt alles als sichtbar, egal wohin die Kamera schaut (m). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic|Sicht", meta = (ClampMin = "0.0"))
	double AlwaysVisibleMeters = 15.0;
};

/** Momentaufnahme der Simulation (pro Tick, fuer HUD/Blueprint). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficReport
{
	GENERATED_BODY()

	/** Aktive Fahrzeuge (nach dem letzten Tick). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 ActiveVehicleCount = 0;

	/** Kumulativ gespawnte Fahrzeuge. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int64 TotalSpawnedCount = 0;

	/** Kumulativ entfernte Fahrzeuge (Sackgassen-Ende, Netzende). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int64 TotalRemovedCount = 0;

	/** Kumulativ zurueckgelegte Strecke aller Fahrzeuge in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double TotalDistanceCm = 0.0;

	/** Mittlere Geschwindigkeit der aktiven Fahrzeuge in km/h. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double MeanSpeedKmh = 0.0;

	/** Aktive Fahrzeuge je km befahrbares Netz (Dichte-Kennzahl). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	double ActiveVehiclesPerKm = 0.0;

	/**
	 * Fahrzeuge, die im letzten Tick praktisch standen (< 5 km/h), obwohl sie
	 * schneller fahren wollten.
	 *
	 * Ohne diese Zahl laesst sich "die Autos stauen sich" nicht pruefen. Dass
	 * die Simulation laeuft, heisst nicht, dass der Verkehr fliesst.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 StalledVehicleCount = 0;

	/** Spurwechsel (Ueberholvorgaenge) im letzten Tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangesThisTick = 0;

	// -- Warum NICHT gewechselt wurde (letzter Tick) --------------------------
	//
	// Ein einzelner Tick-Zaehler beantwortet die Frage nicht, um die es geht.
	// Bei 4 s Sperrzeit je Fahrzeug ist "0 Spurwechsel in diesem Tick" auch
	// dann der Normalfall, wenn das Ueberholen tadellos laeuft - die Zahl kann
	// "passiert nie" und "passiert selten" gar nicht unterscheiden. Die
	// folgenden Zaehler zerlegen das Nein in seine Gruende, und erst damit
	// laesst sich sagen, WO es haengt.

	/** Fahrzeuge, die langsam genug und nicht gesperrt waren. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeCandidates = 0;

	/** Davon: keine Nachbarspur in derselben Richtung vorhanden. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeNoNeighbour = 0;

	/** Davon: Nachbarspur vorhanden, aber Luecke vorn oder hinten zu klein. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeBlockedByGap = 0;

	/** Davon: Luecke gross genug, aber kein spuerbarer Gewinn. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeNoGain = 0;

	/** Von den Luecken-Ablehnungen: nur vorn zu eng / nur hinten / beides. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeTightAheadOnly = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeTightBehindOnly = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 LaneChangeTightBoth = 0;

	/** Einsatzorte, die im letzten Tick verworfen wurden, weil der Spieler sie sehen koennte. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 SpawnsSkippedInView = 0;

	/** Fahrzeuge, die am Sackgassen-Ende warten, bis niemand hinsieht. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	int32 WaitingAtDeadEnd = 0;
};

/**
 * Datenreine Verkehrs-Simulation auf dem Fahrspur-Graphen (kein Welt-/Actor-
 * Zugriff, deterministisch, testbar).
 *
 * - Fahrzeuge folgen dem Graph: Spur-Mittellinie -> Kreuzungs-Verbindung
 *   (FLaneConnection::ConnectionPath) -> Folgespur. An einer Kreuzung waehlt
 *   ein Fahrzeug deterministisch (FNV-1a-Hash aus Fahrzeug-Id + Knoten-Id)
 *   eine erlaubte Verbindung; Sackgassen entfernen das Fahrzeug.
 * - TrafficDensity (aus dem City-Prompt) steuert die Spawn-Rate linear:
 *   Rate = MaxSpawnRatePerSecond * Density. Der Spawn-Lane wird per
 *   Round-Robin ueber alle befahrbaren Spuren gewaehlt (deterministisch);
 *   ein blockierter Spur-Anfang verschiebt den Spawn (kein Stapeln).
 * - Kopf-zu-Schwanz: je Bahn wird der Folger so begrenzt, dass die MinGapCm-
 *   Luecke zum Vordermann nie unterschritten wird - hohe Dichte erzeugt
 *   dadurch natuerlich Stau.
 *
 * Initialisierung: Initialize(Network, Settings) einmal nach dem Laden der
 * Stadt (die Simulation haelt nur einen schwach interpretierten Verweis auf
 * das Netz; der Aufrufer stellt die Lebensdauer sicher).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficSimulation
{
	GENERATED_BODY()

	/** Parameter der Simulation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Traffic")
	FWiesbadenTrafficSettings Settings;

	/** Aktive Fahrzeuge (Determinismus: Reihenfolge = Spawn-Reihenfolge). */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	TArray<FTrafficVehicle> Vehicles;

	/** Momentaufnahme nach dem letzten Tick. */
	UPROPERTY(BlueprintReadOnly, Category = "Traffic")
	FWiesbadenTrafficReport Report;

	/** Initialisiert die Simulation auf einem Strassennetz (idempotent). */
	void Initialize(const FRoadNetwork& InNetwork, const FWiesbadenTrafficSettings& InSettings);

	/**
	 * Ein Temporary als Netz ist immer ein Fehler - deshalb hier geloescht.
	 *
	 * Initialize merkt sich nur einen Zeiger auf das Netz; der Aufrufer stellt
	 * die Lebensdauer sicher. `Sim.Initialize(MakeNetwork(), ...)` uebergibt
	 * ein Temporary, das am Ende des Ausdrucks stirbt - der Zeiger haengt
	 * danach in der Luft. Elf Testaufrufe machten genau das und liefen
	 * jahrelang unauffaellig durch, weil der freigegebene Speicher zufaellig
	 * unangetastet blieb. Erst eine zusaetzliche Allokation in Initialize hat
	 * ihn neu belegt - und die Simulation stuerzte mit ungueltigen Spur-Indizes
	 * ab, an einer Stelle, die mit der Ursache nichts zu tun hatte.
	 *
	 * Als geloeschte Ueberladung faengt der Compiler das ab, statt es dem
	 * Zufall zu ueberlassen.
	 */
	void Initialize(FRoadNetwork&& InNetwork, const FWiesbadenTrafficSettings& InSettings) = delete;

	/**
	 * Aktiviert die Stopp-Regel: Fahrzeuge, die sich einer roten Ampel
	 * (FWiesbadenTrafficLightSystem) naehern, halten an der Haltelinie. Der
	 * Zeiger ist schwach (der Aufrufer stellt die Lebensdauer sicher, analog
	 * zum Netz-Zeiger). nullptr = keine Ampel-Steuerung.
	 */
	void SetTrafficLightSystem(const FWiesbadenTrafficLightSystem* InTrafficLights);

	/**
	 * Fahrzeuge, die im letzten Tick von einer roten Ampel gehalten wurden.
	 *
	 * Wirksamkeits-Kennzahl: dass das Ampelsystem laeuft, heisst nicht, dass es
	 * den Verkehr beeinflusst - der Zeiger darauf war lange gar nicht gesetzt.
	 */
	int32 GetVehiclesHeldAtRed() const { return LastVehiclesHeldAtRed; }

	/** Seit Initialize aufsummiert: wie oft ein Fahrzeug an einer signalisierten
	 *  Verbindung angehalten wurde bzw. eine solche ueberhaupt anfuhr. Erlaubt der
	 *  Diagnose, "Kopplung defekt" von "keine Ampel auf den befahrenen Spuren" zu
	 *  unterscheiden (nur ~5% der Kreuzungen sind Ampeln). */
	// -- Stau-Karte: Fluss je Strasse ueber den ganzen Lauf -------------------
	//
	// Eine Momentaufnahme ("diese zehn Fahrzeuge stehen gerade") sagt nicht,
	// WO die Stadt klemmt - das naechste Bild zeigt zehn andere. Erst die
	// Summe ueber viele Sekunden trennt den Ort, an dem es immer steht, von
	// dem, an dem gerade zufaellig jemand bremst.

	/** Fluss-Messung einer Spur ueber den ganzen Lauf. */
	struct FLaneFlowSample
	{
		/** Fahrzeug-Ticks auf dieser Spur (nicht Fahrzeuge - dieselben zaehlen mehrfach). */
		int32 Samples = 0;
		/** Davon unter 5 km/h trotz Fahrwunsch. */
		int32 Stalled = 0;
		/** Summe der Geschwindigkeiten, fuer den Mittelwert. */
		double SpeedSumCmS = 0.0;
		/** Tempolimit dieser Spur (cm/s) - der Massstab fuer "fliesst". */
		double LimitCmS = 0.0;
	};

	/**
	 * Fluss je Spur mitschreiben. Kostet eine Map-Suche je Fahrzeug und Tick,
	 * deshalb nur auf Anforderung (-WbStauKarte).
	 */
	bool bCollectLaneFlow = false;

	/** Die gesammelten Messwerte, LaneId -> Fluss. */
	const TMap<int32, FLaneFlowSample>& GetLaneFlow() const { return LaneFlow; }

	/**
	 * Schreibt Netz und Stau-Messwerte als Textdatei fuer die Karte.
	 *
	 * Zwei Abschnitte: NETZ (alle Spuren als Linie, der graue Stadtplan) und
	 * STAU (je gemessener Spur Ort, Mitteltempo, Limit, Steher-Anteil).
	 * Gibt die Zahl der geschriebenen Stau-Zeilen zurueck, -1 bei Fehler.
	 */
	int32 WriteCongestionMap(const FString& Path, int32 MinSamples = 20) const;

	int32 GetLifetimeVehiclesHeldAtRed() const { return LifetimeVehiclesHeldAtRed; }

	/**
	 * Spurwechsel seit dem Start der Simulation.
	 *
	 * Die Lebenszeit-Summe, nicht der Tick: nur sie beantwortet "wechselt der
	 * Verkehr je die Spur?".
	 */
	int32 GetLifetimeLaneChanges() const { return LifetimeLaneChanges; }

	/** Kandidaten und Ablehnungsgruende seit dem Start (fuer die Diagnose). */
	void GetLifetimeLaneChangeReasons(int32& OutCandidates, int32& OutNoNeighbour,
		int32& OutBlockedByGap, int32& OutNoGain) const
	{
		OutCandidates = LifetimeLaneChangeCandidates;
		OutNoNeighbour = LifetimeLaneChangeNoNeighbour;
		OutBlockedByGap = LifetimeLaneChangeBlockedByGap;
		OutNoGain = LifetimeLaneChangeNoGain;
	}

	/** Aufschluesselung der zu engen Luecken: nur vorn / nur hinten / beides. */
	void GetLifetimeTightSides(int32& OutAheadOnly, int32& OutBehindOnly, int32& OutBoth) const
	{
		OutAheadOnly = LifetimeTightAheadOnly;
		OutBehindOnly = LifetimeTightBehindOnly;
		OutBoth = LifetimeTightBoth;
	}
	int32 GetLifetimeVehiclesApproachingSignal() const { return LifetimeVehiclesApproachingSignal; }

	/** Fahrzeuge, die in diesem Tick vor einem belegten Kreuzungsweg warten. */
	int32 GetVehiclesHeldAtJunction() const { return LastVehiclesHeldAtJunction; }

	/**
	 * WARUM sie warten - aufgeschluesselt nach den drei Sperren.
	 *
	 * Die blosse Zahl der Wartenden sagt nicht, welche Regel den Fluss kostet.
	 * Ohne die Aufschluesselung aendert man auf Verdacht: frueheres Bremsen
	 * statt Vollhalt brachte gemessen NICHTS, weil die Ursache woanders lag.
	 */
	void GetJunctionBlockReasons(int32& OutNoRoomAhead, int32& OutConflictBusy,
		int32& OutYielding) const
	{
		OutNoRoomAhead = LastBlockedNoRoomAhead;
		OutConflictBusy = LastBlockedConflictBusy;
		OutYielding = LastBlockedYielding;
	}

	/** Wie viele der Wartenden wollen LINKS abbiegen? */
	int32 GetBlockedLeftTurners() const { return LastBlockedLeftTurners; }

	/**
	 * Fahrzeuge, die INEINANDER stecken - aufgeschluesselt nach Bahn-Beziehung.
	 *
	 * Die Abstandsregeln arbeiten je Bahn (Spur bzw. Verbindung). Wo zwei
	 * Fahrzeuge auf VERSCHIEDENEN Bahnen denselben Platz beanspruchen, greift
	 * keine von ihnen - im Spiel sieht man das als Kaefer, die zur Haelfte
	 * ineinander stehen. Ohne diese Aufschluesselung laesst sich nicht sagen,
	 * WELCHE Grenze verletzt wird, und man baut auf Verdacht.
	 */
	struct FOverlapReport
	{
		/** Ineinander steckende Paare insgesamt. */
		int32 Pairs = 0;

		/** Davon: beide auf DERSELBEN Bahn (waere ein Fehler der Folgeregel). */
		int32 SameEdge = 0;

		/** Davon: beide auf Verbindungen DESSELBEN Knotens (Kreuzungskonflikt). */
		int32 SameJunction = 0;

		/** Davon: eines auf einer Spur, eines auf einer Verbindung. */
		int32 LaneAndConnection = 0;

		/** Davon: alles Uebrige (verschiedene Spuren, verschiedene Knoten). */
		int32 Other = 0;

		/**
		 * Davon: nur die KAROSSERIEN ueberlappen, die Sollpositionen auf den
		 * Bahnen nicht.
		 *
		 * Das trennt die beiden moeglichen Ursachen: liegen schon die Bahnen
		 * ineinander, ist die Spur zu schmal; liegen nur die Karosserien
		 * ineinander, schwingt das Nachlaufmodell zu weit aus
		 * (MaxBodyDeviationCm).
		 */
		int32 OnlyBodies = 0;

		/** Schmalste Spur, die an einer Ueberlappung beteiligt war (cm). */
		double NarrowestLaneCm = 0.0;

		/**
		 * Verschiedene Spuren DESSELBEN Strassenabschnitts (Parallel- oder
		 * Gegenspur) - hier waere die Spuraufteilung schuld.
		 */
		int32 SameSegmentLanes = 0;

		/**
		 * Verschiedene Spuren VERSCHIEDENER Abschnitte - hier liegen zwei
		 * Strassen im Netz zu dicht nebeneinander.
		 */
		int32 CrossSegmentLanes = 0;

		/** Kleinster Abstand zweier Sollpositionen auf verschiedenen Spuren (cm). */
		double MinLaneRailDistanceCm = 0.0;

		/** Groesster Seitenversatz Karosserie gegen Sollposition (cm). */
		double MaxBodyOffsetCm = 0.0;

		/** Fahrzeuge, die an mindestens einem Paar beteiligt sind. */
		int32 VehiclesInvolved = 0;

		/**
		 * Davon: an einer Kreuzung - mindestens eines auf einer Verbindung
		 * oder naeher als JunctionZoneCm an Anfang/Ende seiner Spur.
		 */
		int32 AtJunction = 0;

		/**
		 * Davon: nur mit den ECHTEN Massen des Typs ineinander (Tripo-Modell,
		 * Ursprung in der Radstandmitte), mit der Einheitsbox
		 * VehicleHalfLengthCm/VehicleHalfWidthCm nicht.
		 */
		int32 OnlyRealSize = 0;

		/** Davon: mehr als 2,5 m Hoehenunterschied - Bruecke ueber Strasse, kein
		 *  echtes Ineinander (die Pruefung selbst bleibt in der Ebene). */
		int32 DifferentLevels = 0;

		/** Die ersten Paare im Einzelnen (Zustand beider Fahrzeuge), fuer das Log. */
		TArray<FString> Samples;
	};

	/** Bis hierher (cm) vor dem Ende bzw. nach dem Anfang einer Spur zaehlt ein Paar als "an der Kreuzung". */
	static constexpr double JunctionZoneCm = 1500.0;

	/**
	 * Die sichtbare Grundflaeche eines Fahrzeugs: Mitte, Fahrtrichtung, halbe
	 * Laenge und Breite. Mit bBody die Karosserie aus der Fahrphysik und die
	 * echten Masse des Typs (WiesbadenTrafficCars::Types(): Ursprung in der
	 * Radstandmitte, vorn FrontCm, hinten RearCm), sonst die Sollposition.
	 * Datenrein (Test Traffic.JunctionConflict).
	 */
	static void GetVehicleFootprint(const FTrafficVehicle& Vehicle, bool bBody,
		FVector& OutCenter, FVector& OutForward, double& OutHalfLengthCm, double& OutHalfWidthCm);

	/** Wie AreVehiclesOverlapping, aber mit eigener Groesse je Fahrzeug. */
	static bool AreBoxesOverlapping(
		const FVector& CenterA, const FVector& ForwardA, double HalfLengthA, double HalfWidthA,
		const FVector& CenterB, const FVector& ForwardB, double HalfLengthB, double HalfWidthB);

	/**
	 * Zaehlt die ineinander steckenden Paare (O(n^2), nur fuer die Diagnose).
	 *
	 * Bewusst NICHT je Tick: bei 330 Fahrzeugen sind das 54.000 Paarpruefungen.
	 * Die Diagnose ruft es alle 15 s.
	 */
	void CountVehicleOverlaps(FOverlapReport& Out) const;

	/**
	 * FAHRBILD: Wie natuerlich bewegen sich die Karosserien? Summen seit dem
	 * letzten Abholen (TakeMotionQuality). Jede Zahl steht fuer ein Symptom,
	 * das im Spiel zu sehen war: Lenkzappeln, seitliches Rutschen (das
	 * Sicherheitsnetz zieht die Karosserie quer), schraeg stehende oder
	 * schwankende Karosserien, Bremsungen der Sollposition, die kein Auto
	 * fahren kann, und Spurwechsel mitten im Stau.
	 */
	struct FMotionQuality
	{
		double AllSeconds = 0.0;         // Fahrzeug-Sekunden gesamt
		double DrivingSeconds = 0.0;     // davon Karosserie schneller als 3 m/s
		int32 SteerReversals = 0;        // Lenk-Richtungswechsel (|Einschlag| > 1 Grad) in Fahrt
		double SlideCm = 0.0;            // seitliches Nachziehen durch das Sicherheitsnetz
		double YawErrSqDegS = 0.0;       // (Karosserie- minus Bahnrichtung)^2 * dt, alle Fahrzeuge
		double StandYawBad = 0.0;        // Fahrzeug-Sekunden im Stand mit mehr als 10 Grad Schraeglage
		double OffsetSqCmS = 0.0;        // Seitenversatz^2 * dt
		double RollSqDegS = 0.0;         // Wanken^2 * dt (in Fahrt)
		double PitchSqDegS = 0.0;        // Nicken^2 * dt (in Fahrt)
		int32 HardSollBrakes = 0;        // Ticks mit Soll-Verzoegerung ueber 8 m/s^2
		double MaxSollDecelCmS2 = 0.0;
		int32 LaneChanges = 0;
		int32 LaneChangesSlow = 0;       // davon unter einem Drittel des Wunschtempos
	};
	FMotionQuality TakeMotionQuality()
	{
		const FMotionQuality Out = Motion;
		Motion = FMotionQuality();
		return Out;
	}

	/**
	 * Stecken zwei Fahrzeuge ineinander? Datenrein, ohne Netz und ohne Welt.
	 *
	 * Rechteck gegen Rechteck in der EBENE (Separating Axis Theorem ueber die
	 * vier Kantennormalen). Ein blosser Mittenabstand genuegt nicht: zwei
	 * Fahrzeuge auf Nachbarspuren stehen voellig zu Recht 3 m nebeneinander,
	 * zwei hintereinander duerfen sich bei 3 m schon beruehren.
	 *
	 * Hoehe bleibt aussen vor - wie ueberall in dieser Simulation: sonst gilt
	 * die Bruecke ueber der Strasse als Konflikt.
	 */
	static bool AreVehiclesOverlapping(
		const FVector& LocationA, const FVector& ForwardA,
		const FVector& LocationB, const FVector& ForwardB,
		double HalfLengthCm, double HalfWidthCm);

	/**
	 * Wo genau liegen sich zwei Verbindungen im Weg?
	 *
	 * Liefert zusaetzlich die Bogenlaenge, ab der ein Fahrzeug den Konfliktpunkt
	 * HINTER sich hat - erst damit wird die Regel brauchbar: sperrt jedes
	 * Fahrzeug seine ganze Verbindung, bis es sie verlassen hat, steht die
	 * Stadt (gemessen 56 % Steher statt 36 %). Wer den Kreuzungspunkt passiert
	 * hat, ist aus dem Weg.
	 *
	 * @param OutClearOnA Bogenlaenge auf A, ab der A den Punkt passiert hat.
	 * @param OutClearOnB Dasselbe auf B.
	 */
	/**
	 * Kommen sich zwei Wege durch einen Knoten naeher als MinDistanceCm (eine
	 * Autobreite), OHNE sich zu schneiden? Dann passen zwei Autos dort nicht
	 * nebeneinander - FindConnectionConflict sieht nur Schnittpunkte. Gemessen am
	 * Bahnhofsplatz: zwei Autos standen auf Wegen 1,3 m nebeneinander im Knoten.
	 * OutClear* = Bogenlaenge, ab der der jeweils andere den engen Abschnitt
	 * verlassen hat. Datenrein (Test Traffic.Fahrbild).
	 */
	static bool FindPathProximity(const FLaneConnection& A, const FLaneConnection& B,
		double MinDistanceCm, double& OutClearOnA, double& OutClearOnB);

	static bool FindConnectionConflict(const FLaneConnection& A, const FLaneConnection& B,
		double& OutClearOnA, double& OutClearOnB);

	/** Bogenlaenge einer Verbindung - geteilt, damit es nur EINE gibt. */
	static double ConnectionPathLength(const FLaneConnection& C);

	/** Nur die Weg-Geometrie: schneiden sich die beiden Bahnen? */
	static bool FindPathCrossing(const FLaneConnection& A, const FLaneConnection& B,
		double& OutClearOnA, double& OutClearOnB);

	/**
	 * Duerfen diese beiden gleichzeitig Gruen bekommen?
	 *
	 * Andere Frage als DoConnectionsConflict, gleiche Geometrie. Die
	 * gemeinsame Zielspur ist fuer die Laufzeitregel ein Konflikt, fuer eine
	 * Freigabegruppe nur dann, wenn bSameTargetLaneBlocks gesetzt ist.
	 */
	static bool DoConnectionsConflictForGroup(const FLaneConnection& A,
		const FLaneConnection& B, bool bSameTargetLaneBlocks);

	/**
	 * Geduldeter Abstand der Karosserie zur Sollbahn bei diesem Tempo (cm).
	 *
	 * Datenrein: die grosszuegige Grenze gilt dem Spurwechsel, und der
	 * passiert im Fahren. Im Stand zaehlt nur noch, dass das Auto in seiner
	 * Spur steht.
	 */
	static double BodyDeviationLimitCm(const FWiesbadenTrafficSettings& InSettings,
		double SpeedCmS, bool bChangingLane);

	/** Schneiden sich zwei Strecken in der Ebene? (Hoehe bleibt aussen vor.) */
	static bool SegmentsIntersect2D(const FVector& A0, const FVector& A1,
		const FVector& B0, const FVector& B1);

	/**
	 * Koennen zwei Verbindungen desselben Knotens nicht gleichzeitig befahren
	 * werden? Datenrein, ohne Netz.
	 *
	 * Drei Faelle, und die Reihenfolge ist wichtig:
	 *  - GLEICHE Quellspur: KEIN Konflikt. Die beiden faecheren aus derselben
	 *    Kolonne auf, und dort haelt sie die Folgeregel schon auseinander. Wer
	 *    sie hier sperrt, laesst eine Kreuzung nur noch einzeln abfliessen.
	 *  - GLEICHE Zielspur: Konflikt. Zwei Fahrzeuge, die in dieselbe Spur
	 *    einfaedeln, treffen sich am Ende, auch wenn die Wege sich vorher nicht
	 *    schneiden.
	 *  - Sonst: Konflikt genau dann, wenn sich die Wege kreuzen. Der
	 *    Gegenverkehr derselben Achse faehrt damit weiter gleichzeitig - seine
	 *    Wege liegen parallel nebeneinander.
	 */
	static bool DoConnectionsConflict(const FLaneConnection& A, const FLaneConnection& B);

	/**
	 * Zulaessiges Tempo beim Anfahren einer BELEGTEN Kreuzung.
	 *
	 * Bremswegmodell v = sqrt(2*a*s), dieselbe Formel wie bei der
	 * Hindernisregel und aus demselben Grund: die belegte Kreuzung ist ein
	 * STEHENDES Hindernis. Vorher wurde erst an der Haltelinie hart auf null
	 * gesetzt - jedes Anfahren danach kostet Zeit, und die Kolonne dahinter
	 * muss mitbremsen. Genau daraus entstehen die Stop-and-Go-Wellen, die den
	 * Fluss fressen.
	 *
	 * @param DistanceToLineCm Reststrecke bis zur Haltelinie.
	 * @param StopBufferCm     Abstand VOR der Linie, an dem gestanden wird.
	 */
	static double ApproachSpeedForBlockedJunctionCmS(
		double CurrentSpeedCmS, double DistanceToLineCm,
		double StopBufferCm, double DecelerationCmS2);

	/** Ein Wegstueck, auf dem an einer Kreuzung ein anderes Fahrzeug steht oder faehrt. */
	struct FStopObstacle
	{
		FVector From = FVector::ZeroVector;
		FVector To = FVector::ZeroVector;
	};

	/**
	 * HALTELINIE einer Zufahrt: Wie weit vor dem Spurende (Fahrzeugmitte) muss
	 * ein wartendes Fahrzeug stehen, damit es NICHTS von dem beruehrt, was an
	 * diesem Knoten sonst faehrt oder wartet?
	 *
	 * Frueher hielt jede Zufahrt pauschal 350 cm vor ihrem Spurende. Im Netz
	 * enden die Spuren aber verschieden nah am Knoten: an spitz zulaufenden
	 * Armen stand der Wartende der einen Zufahrt im Wartebereich der anderen,
	 * und wo die Spur dicht an den Querweg reicht, im Weg der Abbieger. Genau
	 * das war das "Verkeilen" an Kreuzungen (gemessen am Bahnhofsplatz: alle
	 * 13-19 Paare je Diagnose an Kreuzungen, meist beide stehend).
	 *
	 * Geschoben wird in StepCm-Schritten von BaseCm bis hoechstens MaxCm (und
	 * nie ueber den Spuranfang); die Grundflaeche ist ein Rechteck halber Laenge
	 * HalfLengthCm und halber Breite HalfWidthCm, jedes Hindernis ein Streifen
	 * halber Breite ObstacleHalfWidthCm um sein Wegstueck. Findet sich keine
	 * freie Stelle (Zusammenfuehrung, die ueber viele Meter eng parallel
	 * laeuft), bleibt es bei BaseCm und bOutResolved ist false.
	 * Datenrein (Test Traffic.StopLines).
	 */
	static double ComputeStopSetbackCm(const TArray<FVector>& Approach, double LengthCm,
		const TArray<FStopObstacle>& Obstacles, double BaseCm, double MaxCm, double StepCm,
		double HalfLengthCm, double HalfWidthCm, double ObstacleHalfWidthCm, bool& bOutResolved);

	/** Haltelinie einer Spur: Abstand der wartenden Fahrzeugmitte vor dem Spurende (cm). */
	double GetStopDistanceCm(int32 LaneId) const;

	/** Ehrliches, verkehrsUNABHAENGIGES Signal: war seit Initialize je eine von
	 *  einer Ampel kontrollierte Verbindung rot? False heisst bei geladener Stadt:
	 *  die Sim ist gar nicht an das Ampelsystem gekoppelt (SetTrafficLightSystem
	 *  nie gerufen - der TrafficLights-Zeiger blieb null, der Rot-Block wird
	 *  uebersprungen). Genau dieser Bug rutschte sonst als "Inconclusive" durch,
	 *  weil am stationaeren Spawn zu wenige Fahrzeuge eine Ampel anfahren. */
	bool HasObservedSignalizedRed() const { return bAnySignalizedConnectionEverRed; }

	/** Setzt die Simulation zurueck (kein Netz, keine Fahrzeuge). */
	void Reset();

	/** True, wenn ein Netz initialisiert wurde. */
	bool IsInitialized() const { return Network != nullptr; }

	/** Treibt die Simulation einen Schritt weiter (Spawn, Folgen, Entfernen). */
	void Tick(float DeltaSeconds);

	/**
	 * Setzt die Bezugsposition, um die herum Verkehr entsteht und wieder
	 * entfernt wird - normalerweise die Kameraposition des Spielers.
	 *
	 * Ohne gesetzte Position faellt die Simulation auf das alte Verhalten
	 * zurueck und verteilt Fahrzeuge ueber das ganze Netz. Das ist fuer
	 * datenreine Tests brauchbar, im Spiel aber nicht: dort saehe man nie ein
	 * Fahrzeug.
	 */
	void SetObserverLocation(const FVector& InLocation);

	/**
	 * Blick des Spielers (Kamera): Ort, Richtung, waagerechtes Blickfeld.
	 * Setzt zugleich den Bezugspunkt (SetObserverLocation). Ohne Blick
	 * (datenreine Tests) gilt nichts als sichtbar.
	 */
	void SetObserverView(const FVector& InLocation, const FVector& InViewDirection, float HorizontalFovDeg);

	/**
	 * Koennte der Spieler diesen Punkt sehen? Im Blickkegel (halbes Blickfeld
	 * plus Zuschlag) und naeher als die Sichtweite - oder ganz nah.
	 * Datenrein (Test Vehicles.Traffic.SpawnOutOfView).
	 */
	static bool IsPointInView(const FVector& Point, const FVector& ViewLocation, const FVector& ViewDirection,
		double CosHalfCone, double DrawDistanceCm, double AlwaysVisibleCm);

	/** IsPointInView mit dem gesetzten Blick; false ohne Blick. */
	bool IsVisibleToObserver(const FVector& Point) const;

	/** Seit Initialize: verworfene Einsatzorte in Sicht / Wartende an Sackgassen. */
	int64 GetLifetimeSpawnsSkippedInView() const { return LifetimeSpawnsSkippedInView; }
	int64 GetLifetimeDeadEndWaits() const { return LifetimeDeadEndWaits; }

	/**
	 * Meldet das Spielerfahrzeug als Hindernis.
	 *
	 * Ohne das faehrt der Verkehr stur seine Spur ab und ignoriert den
	 * Spieler vollstaendig: er wird gerammt, statt dass gebremst wird. Mit
	 * Kollisionskoerpern allein entstuende sogar der umgekehrte Eindruck -
	 * die Fahrzeuge schoeben den Spieler vor sich her.
	 *
	 * @param Location Position des Spielerfahrzeugs (Radaufstandspunkt).
	 * @param HalfLengthCm Halbe Fahrzeuglaenge - Puffer im Bremsabstand.
	 */
	void SetPlayerObstacle(const FVector& Location, double HalfLengthCm);

	/** Hebt die Hindernis-Meldung auf (kein Spielerfahrzeug in der Welt). */
	void ClearPlayerObstacle();

	/**
	 * Zulaessige Geschwindigkeit eines Fahrzeugs angesichts eines Hindernisses
	 * voraus (datenrein, testbar).
	 *
	 * Das Hindernis wird als stehend angenommen - konservativ und damit
	 * sicher: bremst der Verkehr zu frueh, faellt das kaum auf; bremst er zu
	 * spaet, faehrt er in den Spieler.
	 *
	 * @param CorridorHalfWidthCm Seitlicher Abstand, bis zu dem das Hindernis
	 *        als "in der eigenen Spur" gilt.
	 * @param ReactionDistanceCm  Entfernung, ab der ueberhaupt reagiert wird.
	 * @param DecelerationCmS2    Komfortable Verzoegerung in cm/s^2
	 *        (400 = 4 m/s^2, ein deutliches, aber nicht ruppiges Bremsen).
	 * @return Die (ggf. verringerte) Geschwindigkeit in cm/s.
	 */
	/**
	 * Geforderte Luecke zur Zielspur bei diesem Tempo (cm).
	 *
	 * Datenrein und statisch, damit die Regel direkt geprueft werden kann -
	 * die Zahl entscheidet, ob ueberhaupt je ueberholt wird.
	 */
	/**
	 * Vorausschauendes Folgetempo nach Gipps (cm/s): so schnell darf der Folger
	 * fahren, dass er mit DecelCmS2 hinter dem Vordermann (der ebenso bremsen
	 * koennte) zum Stehen kaeme - nach einer Reaktionszeit ReactionSeconds.
	 * Gap und MinGap von Mitte zu Mitte. Im Gleichgewicht haelt das
	 * MinGap + 1,5 * Tempo * Reaktionszeit. Datenrein (Test Traffic.Fahrbild).
	 */
	static double SafeFollowSpeedCmS(double GapCm, double MinGapCm, double SpeedCmS,
		double LeaderSpeedCmS, double DecelCmS2, double ReactionSeconds);

	/** Querversatz eines weichen Spurwechsels nach Elapsed von Duration Sekunden:
	 *  StartCm * (1 - s), s = Glaettungsstufe 5. Ordnung (Tempo und Querbeschleunigung
	 *  beginnen und enden bei null). Datenrein. */
	static double LaneShiftOffsetCm(double StartCm, double Elapsed, double Duration);

	/** Bogenlaenge des naechstgelegenen Punkts einer Polylinie (Ebene). Datenrein. */
	static double ProjectOntoPolylineCm(const TArray<FVector>& Line, const FVector& Point);

	static double RequiredLaneChangeGapCm(const FWiesbadenTrafficSettings& InSettings,
		double SpeedCmS);

	static double ComputeObstacleAwareSpeed(
		double CurrentSpeedCmS,
		const FVector& VehicleLocation,
		const FVector& VehicleForward,
		const FVector& ObstacleLocation,
		double ObstacleHalfLengthCm,
		double CorridorHalfWidthCm,
		double ReactionDistanceCm,
		double MinGapCm,
		double DecelerationCmS2);

	/**
	 * Waehlt Spawn-Spuren im Umkreis einer Position (datenrein, testbar).
	 *
	 * @param Lanes        Spuren des Netzes.
	 * @param Candidates   Zulaessige Spur-Ids (keine Busspuren).
	 * @param Center       Bezugspunkt.
	 * @param RadiusCm     Suchradius; <= 0 liefert alle Kandidaten.
	 * @param OutLaneIds   Spuren im Umkreis, Reihenfolge stabil.
	 */
	static void SelectSpawnLanesNear(
		const TArray<FRoadLane>& Lanes,
		const TArray<int32>& Candidates,
		const FVector& Center,
		double RadiusCm,
		TArray<int32>& OutLaneIds);

	/**
	 * Reine Platzierungsfunktion fuer den Fahrzeug-Spawner (datenrein,
	 * deterministisch, testbar): wandelt die Fahrzeuge in sichtbare
	 * Platzierungen um - 1-km-Culling um ObserverLocation, Yaw aus der
	 * Fahrtrichtung und ein deterministischer Farb-Index (FNV-1a-Hash der
	 * Fahrzeug-Id mod PaletteSize) fuer den ISM-Pool. Radius <= 0 = kein
	 * Culling. PaletteSize wird auf >= 1 geklemmt.
	 */
	static void PlaceTrafficVehicles(
		const TArray<FTrafficVehicle>& Vehicles,
		const FVector& ObserverLocation,
		double CullRadiusCm,
		int32 PaletteSize,
		TArray<FPlacedTrafficVehicle>& OutPlaced);

	/** Liefert die aktuelle Dichte 0..1 (aus den Settings). */
	float GetDensity() const { return Settings.TrafficDensity; }

	/**
	 * Zielbestand im Umkreis nach aktueller Dichte (Spurkilometer *
	 * VehiclesPerLaneKm * TrafficDensity, gekappt auf MaxVehicles). Ohne
	 * Beobachter gilt MaxVehicles - dann gibt es keinen Umkreis.
	 */
	int32 GetTargetVehicleCount() const;

	/**
	 * Befahrbare Spurlaenge im Spawn-Umkreis in Kilometern.
	 *
	 * Eine nackte Fahrzeugzahl sagt nichts darueber, ob die Strassen belebt
	 * wirken: 55 Fahrzeuge sind auf einem Dorfanger viel und im Wiesbadener
	 * Netz fast nichts. Erst Fahrzeuge JE KILOMETER ist die Groesse, die der
	 * Spieler sieht.
	 */
	double GetNearbyLaneKm() const { return NearbySpawnLaneLengthCm / 100000.0; }

	/**
	 * Ein Steher mit dem GRUND, aus dem er steht (Diagnose).
	 *
	 * "Die Autos stauen sich" liess sich bisher nicht nachgehen: die Bilanz
	 * nennt nur eine Anzahl. Ohne den Grund ist nicht zu unterscheiden, ob der
	 * Verkehr vor dem geparkten Spielerauto wartet, hinter einem Vordermann
	 * klemmt, an Rot haelt oder in einer Sackgasse steckt.
	 */
	struct FStalledVehicle
	{
		int32 VehicleId = INDEX_NONE;
		int32 LaneId = INDEX_NONE;
		FVector Location = FVector::ZeroVector;
		double SpeedCmS = 0.0;
		double DesiredSpeedCmS = 0.0;

		/** Abstand zum Spielerfahrzeug in cm (-1, wenn keines gemeldet ist). */
		double PlayerDistanceCm = -1.0;

		/** Steht ein anderes Fahrzeug direkt davor? Abstand in cm, sonst -1. */
		double AheadDistanceCm = -1.0;

		/** Faehrt es auf eine rote Ampel zu? */
		bool bHeldAtRed = false;

		/** Hat die Spur ueberhaupt eine Fortsetzung? */
		bool bHasSuccessor = true;

		/** Strassenklasse der Spur - zeigt, ob der Steher im Durchgangsnetz steht. */
		EOSMHighwayType HighwayType = EOSMHighwayType::None;
	};

	/**
	 * Gehoert die Klasse zum DURCHGANGSNETZ des Verkehrs?
	 *
	 * Service-Wege (OSM highway=service: Parkplatzgassen, Zufahrten, Gassen,
	 * Hofeinfahrten) sind es nicht. Zufahrten und Gassen sind zudem einspurig
	 * in EINER Richtung gebaut (RoadTypeLibrary), Parkplatzgassen bilden
	 * Schleifen: Fahrzeuge, die dort eingesetzt wurden oder hineinbogen,
	 * kreisten auf Parkflaechen und blockierten sich gegenseitig (gemeldet
	 * 26.09.2026). Darum setzt der Verkehr dort nicht ein und biegt nur hinein,
	 * wenn es keine andere Fortsetzung gibt.
	 */
	static bool IsThroughTrafficClass(EOSMHighwayType Type)
	{
		return Type != EOSMHighwayType::Service;
	}

	/**
	 * WENDEN AM SACKGASSENENDE: ergaenzt das Netz, damit Fahrzeuge am Ende
	 * einer Spur ohne Fortsetzung wenden und zurueckfahren, statt dort zu
	 * warten und unbeobachtet zu verschwinden.
	 *
	 *  - Zweispurige Sackgasse: Wendeschleife (ETurnType::UTurn) von der Spur
	 *    auf die Gegenspur desselben Abschnitts.
	 *  - Einspurige Sackgasse (Zufahrten, Gassen: nur EINE Spur gebaut):
	 *    gespiegelte Rueckspur, Wendeschleife darauf, und am Anfang der
	 *    Sackgasse Verbindungen zurueck auf die Spuren, die die Kreuzung dort
	 *    verlassen.
	 *
	 * Neue Spuren/Verbindungen kommen nur ANS ENDE (bestehende Nummern bleiben
	 * gueltig) und tragen bAddedTurnaround. Idempotent: ein Netz, das schon
	 * Wendeschleifen hat, bleibt unveraendert. Vor Initialize aufrufen.
	 * @return Zahl der Wendeschleifen; OutReverseLanes = angelegte Rueckspuren.
	 */
	static int32 AddDeadEndTurnarounds(FRoadNetwork& InOutNetwork, int32* OutReverseLanes = nullptr);

	/** Wendeschleife vom Spurende E (Fahrtrichtung Dir) zum Start S der Gegenrichtung. */
	static TArray<FVector> BuildTurnaroundPath(const FVector& E, const FVector& Dir, const FVector& S);

	/** Fahrzeuge insgesamt und davon auf Service-Wegen (Diagnose -WbStauLog). */
	void CountVehiclesOnServiceRoads(int32& OutOnService, int32& OutTotal) const;

	/**
	 * Ampel-Schlangenprobe (-WbAmpelSpur=<Spur>): je Gruenphase der Zufahrt
	 * eine Logzeile - Dauer, Wartende zu Beginn, ueber die Haltelinie
	 * Abgeflossene, Wartende danach und wohin der Vorderste will. Beantwortet
	 * "baut sich die Schlange pro Gruen ab?" mit Zahlen statt Eindruck.
	 * Nach TrafficSimulation.Tick aufrufen; ohne gesetzte Spur ein No-Op.
	 */
	void SetQueueProbeLane(int32 LaneId) { ProbeLaneId = LaneId; }
	void StepQueueProbe(float DeltaSeconds);

private:
	int32 ProbeLaneId = INDEX_NONE;
	double ProbeTime = 0.0;
	double ProbePhaseStart = 0.0;
	bool bProbeGreen = false;
	int32 ProbeWaitingAtStart = 0;
	int32 ProbeDeparted = 0;
	int32 ProbeHeadConnection = INDEX_NONE;
	bool bProbeHeadGreenSeen = false;
	TSet<int32> ProbeOnLane;

public:

	/**
	 * Die aktuellen Steher mit Grund, hoechstens MaxCount, die naechsten zuerst.
	 * Fuer die Diagnose gedacht und deshalb nicht je Tick gerufen.
	 */
	void CollectStalledVehicles(int32 MaxCount, TArray<FStalledVehicle>& Out) const;

	/** Anzahl der Spuren im Spawn-Umkreis (Bezugsgroesse zu GetNearbyLaneKm). */
	int32 GetNearbyLaneCount() const { return NearbySpawnLaneIds.Num(); }

private:
	/** Wunschgeschwindigkeit eines Fahrzeugs auf seiner aktuellen Spur. */
	double ComputeDesiredSpeed(const FTrafficVehicle& Vehicle) const;

	/** Laenge der aktuellen Bahn (Spur oder Verbindung) in cm. */
	double GetEdgeLengthCm(const FTrafficVehicle& Vehicle) const;

	/**
	 * Wechselt an ein Bahnende auf die Folgebahn. Rueckgabe false = keine
	 * Folgebahn (Sackgasse / kaputtes Netz) - der Aufrufer entfernt.
	 */
	bool AdvanceEdge(FTrafficVehicle& Vehicle);

	/**
	 * Die naechste Bahn, OHNE den Wechsel auszufuehren.
	 *
	 * Noetig, um vor dem Wechsel zu fragen, ob dort schon jemand steht. Ohne
	 * diese Frage setzte AdvanceEdge Fahrzeuge aus verschiedenen Zufluessen im
	 * selben Tick auf denselben Punkt - gemessen drei Stueck auf 1 cm genau
	 * uebereinander, und die Abstandsregel bekam sie nicht mehr auseinander:
	 * sie haelt nur den Abstand, sie kann keinen herstellen.
	 *
	 * @return false bei Sackgasse oder kaputter Folgebahn (Aufrufer entfernt).
	 */
	bool PeekNextEdge(const FTrafficVehicle& Vehicle, bool& bOutOnLane, int32& OutIndex) const;

	/** Eindeutiger Schluessel einer Bahn (Spur oder Verbindung). */
	static int64 EdgeKey(bool bOnLane, int32 Index)
	{
		return (static_cast<int64>(Index) << 1) | (bOnLane ? 0 : 1);
	}

	/**
	 * Deterministisch gewaehlte Folgespur-Verbindung einer Spur (wie
	 * AdvanceEdge sie nutzt) - fuer die Ampel-Stopp-Regel, damit das Fahrzeug
	 * vor seiner gewaehlten Route haelt. INDEX_NONE = keine Verbindung.
	 */
	int32 PickSuccessorConnection(const FTrafficVehicle& Vehicle) const;

	/** Gewicht der Zielspur einer Verbindung (Strassenklasse). */
	double GetSuccessorWeight(int32 ConnectionIndex) const;

	/** Strassenklasse der Zielspur einer Verbindung (None, wenn unbekannt). */
	EOSMHighwayType GetSuccessorClass(int32 ConnectionIndex) const;

public:
	/**
	 * Wie attraktiv eine Strassenklasse fuer den Durchgangsverkehr ist
	 * (datenrein, testbar).
	 *
	 * Die Wahl an Kreuzungen war zuvor GLEICHVERTEILT - eine Wohnstrasse
	 * wurde so oft genommen wie die Bundesstrasse daneben, und der Verkehr
	 * irrte durch Wohngebiete, waehrend die Hauptachsen leer blieben.
	 */
	static double GetRoadClassWeight(EOSMHighwayType Type);

	/**
	 * Bremslicht an? (datenrein, testbar)
	 *
	 * Zwei Faelle, beide noetig: eine deutliche Verzoegerung (der Fahrer tritt
	 * auf die Bremse) UND Stillstand trotz Fahrwunsch (er steht in der
	 * Schlange und haelt den Fuss auf dem Pedal). Ohne den zweiten Fall bliebe
	 * eine wartende Kolonne dunkel - gerade dort, wo Bremslichter im Bild am
	 * meisten ausmachen.
	 *
	 * Die Schwelle ist eine VERZOEGERUNG, keine Geschwindigkeitsdifferenz:
	 * sonst haengt das Ergebnis an der Bildrate.
	 */
	static bool ShouldShowBrakeLight(double PrevSpeedCmS, double SpeedCmS,
		double DesiredSpeedCmS, double DeltaSeconds);

	/** Blinker aus der Abbiegerichtung der gewaehlten Verbindung (datenrein). */
	static EVehicleIndicator IndicatorForTurn(ETurnType Turn);

	/**
	 * Leuchtet der Blinker in diesem Augenblick? (datenrein, testbar)
	 *
	 * Die Phase kommt aus der Fahrzeug-Id: blinkten alle im Gleichtakt, saehe
	 * eine Kreuzung aus wie eine Lichterkette. 1,5 Hz ist der uebliche Takt.
	 */
	static bool IsIndicatorLit(int32 VehicleId, double TimeSeconds);

	/**
	 * Waehlt aus KUMULIERTEN Gewichten deterministisch einen Index
	 * (datenrein, testbar).
	 *
	 * Roll ist ein Hash, kein Zufallszahlengenerator: gleiche Eingaben
	 * ergeben dieselbe Wahl, damit die Simulation reproduzierbar bleibt.
	 * Cumulative[i] ist die Summe der Gewichte 0..i; ein leeres oder
	 * gewichtsloses Feld liefert INDEX_NONE.
	 */
	static int32 PickWeightedIndex(const TArray<double>& Cumulative, uint32 Roll);

	/**
	 * Anteil der Fahrzeuge je Strassenklasse (Diagnose).
	 *
	 * "Hauptstrassen tragen mehr Verkehr" ist eine Behauptung, solange sie
	 * niemand nachzaehlt. Die Karte ordnet jeder Klasse die Zahl der gerade
	 * darauf fahrenden Fahrzeuge zu.
	 */
	void CollectClassDistribution(TMap<EOSMHighwayType, int32>& Out) const;

	/**
	 * Ein Schritt des Einspurmodells (datenrein, testbar).
	 *
	 * Reine Verfolgung: Aus dem Zielpunkt vor dem Fahrzeug folgt der noetige
	 * Radeinschlag delta = atan(2*L*sin(alpha)/Ld), wobei alpha der Winkel
	 * zwischen Fahrzeuglaengsachse und der Sichtlinie zum Ziel ist. Der
	 * Einschlag wird auf MaxSteerRad begrenzt - das IST der Wendekreis - und
	 * darf sich je Sekunde nur um MaxSteerRateRadS aendern. Erst daraus
	 * ergibt sich die Gierrate omega = v/L * tan(delta).
	 *
	 * Nur X und Y werden integriert; die Hoehe setzt der Aufrufer aus der
	 * Bahn, damit die Fahrzeuge auf der Strasse bleiben.
	 */
	static void StepBicycleModel(
		const FVector2D& TargetXY,
		double SpeedCmS,
		double WheelbaseCm,
		double MaxSteerRad,
		double MaxSteerRateRadS,
		double Dt,
		FVector2D& InOutBodyXY,
		float& InOutYawRad,
		float& InOutSteerRad);

private:

	/**
	 * Punkt auf der Sollbahn, AheadCm vor dem Fahrzeug - ueber Bahngrenzen
	 * hinweg.
	 *
	 * Ohne den Blick auf die Folgebahn zielt ein Fahrzeug am Spurende auf das
	 * Spurende selbst und beginnt erst einzulenken, wenn es schon in der
	 * Kreuzung steht. Es wuerde jede Abbiegung anschneiden.
	 */
	FVector GetPathPointAhead(const FTrafficVehicle& Vehicle, double AheadCm) const;

	/** Fuehrt die Karosserie eines Fahrzeugs der Sollbahn nach. */
	void UpdateBodyPose(FTrafficVehicle& Vehicle, double Dt) const;

	/** Karosserie mit Fahrphysik + Fahrer einen Tick weiter (setzt BodyLocation XY, Gier, Lenkung). */
	void StepPhysicsBody(FTrafficVehicle& Vehicle, const FVector& Target, double Dt) const;

	/** Karosserie auf der Bahn (bBodyOnPath): Physik laengs, Achsen auf der Fahrlinie. */
	void StepBodyOnPath(FTrafficVehicle& Vehicle, double Dt) const;

	/**
	 * Punkt und Richtung auf dem Fahrweg, OffsetCm von der Sollposition
	 * (negativ = dahinter, auch auf der zuletzt verlassenen Bahn; positiv =
	 * davor, auch auf der Folgebahn), mit dem Querversatz eines laufenden
	 * weichen Spurwechsels zu dem Zeitpunkt, an dem das Fahrzeug dort ist.
	 */
	void SamplePathAt(const FTrafficVehicle& Vehicle, double OffsetCm, FVector& OutLocation, FVector& OutForward) const;

private:

	/** Position und Fahrtrichtung auf einer Polylinie bei Distanz abtasten. */
	static void SamplePolyline(const TArray<FVector>& Polyline, double DistanceCm,
		FVector& OutLocation, FVector& OutForward);

	/**
	 * Begrenzt die Geschwindigkeit auf das Fahrzeug, das auf der NAECHSTEN
	 * Bahn am weitesten hinten steht.
	 *
	 * Die Kopf-zu-Schwanz-Regel gruppiert nur je Spur bzw. je Verbindung. Ein
	 * Fahrzeug am Spurende sah damit das Fahrzeug nicht, das auf der
	 * Kreuzungsverbindung davor stand - es fuhr hinein, und erst im naechsten
	 * Tick, mit bereits negativem Abstand, wurde es auf 0 geklemmt. Genau so
	 * entstanden die ineinander steckenden Fahrzeuge an den Kreuzungen.
	 */
	void ApplyCrossEdgeHeadway(double Dt);

	/**
	 * Freie Strecke vor der Position AtDistanceCm auf einer Spur, ohne das
	 * Fahrzeug IgnoreVehicleId. Rueckgabe TNumericLimits<double>::Max(), wenn
	 * die Spur frei ist.
	 */
	double ComputeGapAheadOnLane(int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId) const;

	/** Freie Strecke HINTER der Position - fuer den Spurwechsel. Liefert
	 *  zusaetzlich das Tempo des Nachfolgers: er muss bremsen, also gibt
	 *  SEIN Tempo die noetige Zeitluecke vor, nicht das eigene. */
	double ComputeGapBehindOnLane(int32 LaneId, double AtDistanceCm, int32 IgnoreVehicleId,
		double* OutFollowerSpeedCmS = nullptr) const;

	/** Ueberholen: behinderte Fahrzeuge auf eine freie Nachbarspur setzen. */
	void ApplyLaneChanges(float DeltaSeconds);

	/** Deterministischer FNV-1a-Hash ueber zwei uint32. */
	static uint32 Hash2(uint32 A, uint32 B);

	/** 0..0.999 aus einem Hash - fuer die individuelle Geschwindigkeitsstreuung. */
	static float HashFraction(uint32 Hash);

	// Nicht-reflektierte Laufzeit-Daten (kein UPROPERTY - bewusst).
	const FRoadNetwork* Network = nullptr;
	const FWiesbadenTrafficLightSystem* TrafficLights = nullptr;

	/** Zaehler des letzten Ticks - siehe GetVehiclesHeldAtRed(). */
	int32 LastVehiclesHeldAtRed = 0;

	/** Fahrzeuge, die dieser Tick vor einem belegten Kreuzungsweg gehalten hat. */
	int32 LastVehiclesHeldAtJunction = 0;

	/** Davon: kein Platz hinter der Kreuzung / Konfliktpunkt belegt / Vorfahrt. */
	int32 LastBlockedNoRoomAhead = 0;
	int32 LastBlockedConflictBusy = 0;
	int32 LastBlockedYielding = 0;

	/** Davon: Linksabbieger (zeigt, ob Abbiegespuren etwas braechten). */
	int32 LastBlockedLeftTurners = 0;

	/** Seit Initialize aufsummierte Kennzahlen fuer die ehrliche Ampel-Diagnose. */
	int32 LifetimeVehiclesHeldAtRed = 0;
	int32 LifetimeLaneChanges = 0;

	/** Fahrbild-Summen seit dem letzten TakeMotionQuality. */
	FMotionQuality Motion;
	int32 LifetimeLaneChangeCandidates = 0;
	int32 LifetimeLaneChangeNoNeighbour = 0;
	int32 LifetimeLaneChangeBlockedByGap = 0;
	int32 LifetimeLaneChangeNoGain = 0;
	int32 LifetimeTightAheadOnly = 0;
	int32 LifetimeTightBehindOnly = 0;
	int32 LifetimeTightBoth = 0;

	/** Fluss je Spur - nicht reflektiert, reine Laufzeit-Messung. */
	TMap<int32, FLaneFlowSample> LaneFlow;
	int32 LifetimeVehiclesApproachingSignal = 0;

	/** Wurde seit Initialize je eine kontrollierte Verbindung rot beobachtet?
	 *  Verkehrsunabhaengig ueber den TrafficLights-Zeiger gesetzt; siehe
	 *  HasObservedSignalizedRed(). Bleibt false, wenn der Zeiger nie gesetzt wurde. */
	bool bAnySignalizedConnectionEverRed = false;
	TArray<int32> SpawnLaneIds;
	TMap<int32, double> ConnectionLengthCm;
	TMap<int32, TArray<int32>> LaneSuccessorIndices; // LaneId -> Verbindungs-Indizes

	/**
	 * Nachbarspuren gleicher Fahrtrichtung: LaneId -> benachbarte LaneIds.
	 *
	 * Zwei Spuren sind benachbart, wenn sie zum selben Strassenabschnitt
	 * gehoeren, dieselbe Richtung haben und ihr Spurindex sich um genau eins
	 * unterscheidet. Gegenspuren sind damit ausgeschlossen - ein Ueberholen
	 * ueber die Gegenfahrbahn will hier niemand.
	 */
	TMap<int32, TArray<int32>> LaneNeighbours;

	/**
	 * Verbindungen, die einander im Weg liegen: Index -> Indizes am SELBEN
	 * Knoten, deren Wege sich kreuzen oder in dieselbe Spur muenden.
	 *
	 * Einmal beim Initialisieren gerechnet - das Netz aendert sich nicht, und
	 * je Tick waeren das Zehntausende Strecken-Schnitte.
	 */
	struct FConnectionConflict
	{
		/** Die kreuzende Verbindung. */
		int32 OtherConnection = INDEX_NONE;

		/**
		 * Bogenlaenge auf der ANDEREN Verbindung, ab der sie mich nicht mehr
		 * stoert - der Konfliktpunkt liegt dann hinter dem Fahrzeug.
		 */
		double ClearDistanceOnOtherCm = 0.0;
	};

	TMap<int32, TArray<FConnectionConflict>> ConnectionConflicts;

	/**
	 * Verbindungen an ECHTEN Kreuzungen: am Knoten treffen mindestens drei
	 * Abschnitte zusammen. OSM zerteilt Strassen in Stuecke von wenigen
	 * Metern; an diesen Stossstellen kreuzen sich nur Spurwechsel derselben
	 * Strasse - dort gilt die strenge Blockierfreihaltung nicht.
	 */
	TSet<int32> MultiArmConnections;

	/** Verbindungen je Kreuzungsknoten (fuer die Konflikt-Vorberechnung). */
	void BuildConnectionConflicts();

	/** Haltelinie je Zufahrt aus der Knotengeometrie (ComputeStopSetbackCm); leer = ueberall Grundwert. */
	TArray<double> LaneStopDistanceCm;
	void BuildStopLines();

	/**
	 * Haelt Fahrzeuge vor der Kreuzung, solange ein kreuzender Weg belegt ist.
	 *
	 * Ohne diese Regel fahren zwei Fahrzeuge aus verschiedenen Zufahrten
	 * gleichzeitig in denselben Knoten - jedes haelt auf seiner eigenen Bahn
	 * brav Abstand, aber die beiden Bahnen kreuzen sich. Gemessen waren das
	 * bis zu 105 ineinander steckende Paare je Diagnose.
	 */
	void ApplyJunctionConflicts();

	/** Fahrzeug-Indizes je Spur, absteigend nach Distanz. Pro Tick neu. */
	TMap<int32, TArray<int32>> VehiclesByLaneCache;
	double SpawnAccumulator = 0.0;
	int64 TotalSpawned = 0;
	int64 TotalRemoved = 0;
	double TotalDistanceCm = 0.0;

	/** Bezugspunkt fuer Spawn und Entfernen (Spielerposition). */
	FVector ObserverLocation = FVector::ZeroVector;
	bool bHasObserver = false;

	/** Blick des Spielers (SetObserverView). */
	bool bHasView = false;
	FVector ViewDirection = FVector::ForwardVector;
	double ViewCosHalfCone = 0.0;

	/** Zaehler der Einsatzversuche - streut die Ortswahl, wenn ein Ort verworfen wird. */
	int64 SpawnAttempts = 0;
	int64 LifetimeSpawnsSkippedInView = 0;
	int64 LifetimeDeadEndWaits = 0;

	/** Spielerfahrzeug als Hindernis, auf das der Verkehr reagiert. */
	FVector PlayerObstacleLocation = FVector::ZeroVector;
	double PlayerObstacleHalfLengthCm = 0.0;
	bool bHasPlayerObstacle = false;

	/**
	 * Spuren im aktuellen Umkreis. Wird nur neu bestimmt, wenn sich der
	 * Beobachter merklich bewegt hat - die Suche laeuft ueber alle 111.000
	 * Spuren und waere je Frame zu teuer.
	 */
	TArray<int32> NearbySpawnLaneIds;

	/** Summierte Laenge von NearbySpawnLaneIds in cm - mit der Auswahl gepflegt. */
	double NearbySpawnLaneLengthCm = 0.0;

	/**
	 * Kumulierte Strassenklassen-Gewichte zu NearbySpawnLaneIds.
	 *
	 * Der Einsatzort wurde frueher REIHUM ueber alle Spuren im Umkreis
	 * vergeben. Weil Wohn- und Servicestrassen die Hauptstrassen zahlenmaessig
	 * weit uebertreffen, landete der Verkehr ueberwiegend in Seitenstrassen -
	 * also gerade nicht dort, wo man faehrt. Mit den Gewichten entscheidet
	 * die KLASSE, nicht die Anzahl der Spuren.
	 *
	 * Faellt bei der ohnehin noetigen Umkreissuche ab (alle paar hundert
	 * Meter Fahrt), nicht je Bild.
	 */
	TArray<double> NearbySpawnCumulativeWeights;

	FVector LastSpawnSearchLocation = FVector::ZeroVector;
	bool bNearbyLanesValid = false;
};
