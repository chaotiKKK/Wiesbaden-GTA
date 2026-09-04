// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenHealthReport.generated.h"

/**
 * Evidenz-gewichtetes Ampel-Kopplungs-Verdikt.
 *
 * EINE Stelle entscheidet, ob die Ampel-Kopplung wirkt - der Health-Report.
 * Sowohl die Inline-Diagnose (Prosa-Log) als auch Warnings() lesen dieses
 * Verdikt, statt die Drei-Wege-Logik je fuer sich nachzubilden (genau diese
 * Doppelung liess die Diagnosen frueher auseinanderdriften).
 */
enum class EWiesbadenTrafficLightVerdict : uint8
{
	Effective,    // Halte-Ereignisse an Rot belegt -> Kopplung wirkt.
	Broken,       // genug Anfahrten, aber NIE gehalten -> Kopplung defekt.
	Inconclusive  // zu wenig Anfahrten (oder keine Ampeln) fuer ein Urteil.
};

/**
 * Evidenz-gewichtetes Perf-Verdikt (Spiel-Strang-Last).
 *
 * Wie beim Ampel-Verdikt entscheidet EINE Stelle - der Report. Bewusst auf die
 * DETERMINISTISCHEN Primaerzahlen (Primitive-Komponenten, Instanzen) gestuetzt:
 * die sind lauf-zu-lauf bit-identisch und eine Ueberschreitung ist eine echte
 * Regression. Die Bildzeit (Spiel-Strang-ms) ist last-sensibel und wird nur als
 * Kontext gemeldet, NICHT ins Verdikt gezogen - sonst risse Maschinenlast das
 * Urteil faelschlich (dieselbe Falle wie die 60-ms-Rauchtest-Backup-Schranke).
 */
enum class EWiesbadenPerfVerdict : uint8
{
	Ok,          // deterministische Zaehler im Rahmen.
	Overloaded,  // Komponenten/Instanzen ueber der harten Schwelle -> Regression.
	Unknown      // noch kein Snapshot erhoben (vor dem 8-s-Diagnoseblock).
};

/**
 * Maschinenlesbarer Zustandsbericht der Laufzeit-Selbstdiagnosen.
 *
 * Buendelt die bisher verstreuten Kennzahlen (Streaming, Verkehr, Ampeln,
 * Fussgaenger, Gebaeude-Kollision) in EINE Struktur. Die reinen Rohzahlen fuellt
 * das CitySubsystem (BuildHealthReport); die INTERPRETATION (Warnungen) und die
 * JSON-Ausgabe leben hier - entkoppelt und testbar. So kann jede Sitzung, der
 * Rauchtest oder ein externes System den Ist-Zustand parsen, statt Prosa-Logs zu
 * greppen. Wichtig (Lehre dieser Codebasis): Warnungen sind EVIDENZ-gewichtet -
 * z. B. "Ampel-Kopplung greift nicht" nur bei genug Anfahrten ohne Halten, nicht
 * schon bei geometrischer Ampel-Naehe (sonst Fehlalarm).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenHealthReport
{
	GENERATED_BODY()

	/** World Partition: alle Zellen um den Spieler geladen. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bStreamingComplete = false;

	/** Ampeln im Netz (0 = keine gebacken). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 TrafficLightCount = 0;

	/** Aktuell simulierte Verkehrsfahrzeuge (== Vehicles.Num()). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 ActiveVehicles = 0;

	/** Davon tatsaechlich gezeichnete (im Cull-Radius platzierte) Fahrzeuge.
	 *  Getrennt von ActiveVehicles, damit "simuliert aber nicht gezeichnet"
	 *  evidenzbasiert erkannt wird - wie bei den Fussgaengern. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 TrafficVehiclesVisible = 0;

	/** Seit Stadt-Spawn: Anfahrten auf signalisierte Verbindungen / Halte an Rot.
	 *  Erlauben das evidenz-gewichtete Ampel-Verdikt. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 VehiclesApproachingSignal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 VehiclesHeldAtRed = 0;

	/** Ehrliches, verkehrsUNABHAENGIGES Signal fuer die Ampel-Kopplung: war seit
	 *  Stadt-Spawn je eine von einer Ampel kontrollierte Verbindung rot? Anders als
	 *  VehiclesApproachingSignal/HeldAtRed braucht es KEINEN Verkehr - es liest das
	 *  Ampelsystem ueber den Kopplungs-Zeiger der Sim direkt. False bei fertiger
	 *  Stadt mit Ampeln = die Sim ist nicht ans Ampelsystem gekoppelt
	 *  (SetTrafficLightSystem nie gerufen) - der historische Bug, der sonst als
	 *  Inconclusive durchrutschte. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bSignalizedConnectionEverRed = false;

	/** Simulierte bzw. tatsaechlich gezeichnete Fussgaenger. Beide getrennt, damit
	 *  "simuliert aber nicht gezeichnet" evidenzbasiert erkannt wird. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PedestriansSimulated = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PedestriansDrawn = 0;

	/** Aktive Gebaeude-Kollisionskoerper um den Spieler (0 bei fertiger Stadt =
	 *  verdaechtig). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 BuildingCollisionBodies = 0;

	// -- Beleuchtung: Sonnenstand + Himmelslicht der geladenen Karte -----------
	// Der abgestimmte Streiflicht-Stand ist Sonnen-Pitch ~-24 Grad mit SkyLight
	// ~1.3 (EnsureLightingActors, dauerhaft verankert). Ein Re-Bake OHNE diese
	// Verankerung faellt auf den flachen Zenit-Stand (~-88 Grad) zurueck und hebt
	// das Ambient - das fuellt die langen Schatten und nimmt der Stadt die Tiefe.
	// Genau diesen Rueckfall soll die Diagnose fangen.

	/** True, wenn eine Sonne (DirectionalLight) gefunden wurde - trennt "kein
	 *  Licht geladen" von einem echten Pitch von 0. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bLightingPresent = false;

	/** Pitch der Sonne in Grad, normalisiert auf (-180,180]. Erwartet ~-24
	 *  (streifend); ~-88 heisst flach im Zenit (Re-Bake-Rueckfall). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	float SunPitchDegrees = 0.0f;

	/** Intensitaet des Himmelslichts (SkyLight). Erwartet ~1.3; deutlich hoeher =
	 *  Ambient nicht reduziert (Schatten werden aufgefuellt). WeatherFX taste das
	 *  SkyLight NICHT an, der Wert ist also stabil ablesbar. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	float SkyLightIntensity = 0.0f;

	/** True, wenn ueberhaupt Stadtdaten geladen sind (sonst sind 0-Werte normal). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bCityLoaded = false;

	// -- Perf-Snapshot (Spiel-Strang-Last) ----------------------------------
	// Einmal beim 8-s-Diagnoseblock erhoben; vorher bPerfValid == false.

	/** Mittlere Spiel-Strang-Zeit je Bild in ms. LAST-SENSIBEL -> Kontext, kein
	 *  Verdikt-Kriterium (siehe EWiesbadenPerfVerdict). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	float PerfGameThreadMs = 0.0f;

	/** Verwaltete Primitive-Komponenten (Sichtbarkeit/Bounds je Bild). DETERMINISTISCH. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfPrimitiveComponents = 0;

	/** Davon beweglich bzw. mit Kollision (Kontext der Last-Verteilung). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfMovableComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfCollisionComponents = 0;

	/** Instanz-Komponenten (HISM/ISM) und ihre Instanzen gesamt. DETERMINISTISCH. */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfInstanceComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfInstances = 0;

	/** Mesh-Abschnitte gesamt bzw. OHNE Material (Zeichnen-Defekt = Schachbrett). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfMeshSectionsTotal = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	int32 PerfMeshSectionsWithoutMaterial = 0;

	/** True, sobald der Perf-Snapshot erhoben ist (8-s-Block gelaufen). */
	UPROPERTY(BlueprintReadOnly, Category = "Wiesbaden|Health")
	bool bPerfValid = false;

	// Harte, deterministische Schwellen (identisch zum Rauchtest-Primaersignal:
	// ein Rueckfall zum 6000-m-Streaming-Regime riss ~19.700 / ~1,07 Mio.).
	static constexpr int32 PerfMaxPrimitiveComponents = 13000;
	static constexpr int32 PerfMaxInstances = 800000;

	// Steiler (negativer) als das gilt als "flach im Zenit": der abgestimmte
	// Streiflicht-Stand ist -24 Grad, der Re-Bake-Rueckfall ~-88. -60 trennt
	// beide mit reichlich Rand und schlaegt bei -24 nicht faelschlich an.
	static constexpr float FlatZenithSunPitch = -60.0f;

	/** Verkehr simuliert, aber KEINES gezeichnet (Traeger/Mesh/Cull-Defekt). Reines
	 *  Zahlen-Praedikat; die Aufrufer setzen ihre eigenen Vorbedingungen (Stadt
	 *  geladen / Streaming fertig) davor. */
	bool HasTrafficDrawDefect() const { return ActiveVehicles > 0 && TrafficVehiclesVisible == 0; }

	/** Fussgaenger simuliert, aber KEINER gezeichnet. Reines Zahlen-Praedikat. */
	bool HasPedestrianDrawDefect() const { return PedestriansSimulated > 0 && PedestriansDrawn == 0; }

	/** Evidenz-gewichtetes Ampel-Kopplungs-Verdikt (siehe Enum). Einzige Stelle,
	 *  die die Drei-Wege-Entscheidung trifft. */
	EWiesbadenTrafficLightVerdict TrafficLightVerdict() const;

	/** Evidenz-gewichtetes Perf-Verdikt (siehe Enum). Einziger Owner der
	 *  Ueberlast-Entscheidung; stuetzt sich NUR auf die deterministischen Zaehler. */
	EWiesbadenPerfVerdict PerfVerdict() const;

	/** Mesh-Abschnitte ohne Material -> Zeichnen-Defekt (Schachbrett). Reines
	 *  Zahlen-Praedikat; Aufrufer setzen ihre Vorbedingungen davor. */
	bool HasMaterialDrawDefect() const { return PerfMeshSectionsWithoutMaterial > 0; }

	/** Sonne steht flach im Zenit (Streiflicht-Beleuchtung durch Re-Bake verloren)?
	 *  Reines Zahlen-Praedikat; setzt bLightingPresent voraus, damit ein Pitch von
	 *  0 ohne geladenes Licht nicht faelschlich anschlaegt. */
	bool HasFlatZenithLighting() const { return bLightingPresent && SunPitchDegrees <= FlatZenithSunPitch; }

	/** Evidenz-gewichtete Warnungen (leer = gesund). */
	TArray<FString> Warnings() const;

	/** Kompakte, parsebare JSON-Zeile (inkl. "warnings"-Array). */
	FString ToJson() const;
};
