// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenHeightAudit.generated.h"

struct FRoadNetwork;
class ALandscapeProxy;

/** Eine Stelle, an der Fahrbahn und Gelaende auseinanderliegen. */
USTRUCT()
struct WIESBADENREAL_API FHeightMismatch
{
	GENERATED_BODY()

	/** Weltposition (cm). */
	UPROPERTY() FVector Location = FVector::ZeroVector;

	/** Hoehe der Fahrbahn und des Gelaendes an derselben XY (cm). */
	UPROPERTY() double RoadZ = 0.0;
	UPROPERTY() double TerrainZ = 0.0;

	/** RoadZ - TerrainZ. Positiv = Fahrbahn schwebt, negativ = begraben. */
	UPROPERTY() double DeltaCm = 0.0;

	UPROPERTY() FString StreetName;
};

/** Ergebnis der Hoehenpruefung ueber das gesamte Stadtgebiet. */
USTRUCT()
struct WIESBADENREAL_API FHeightAuditReport
{
	GENERATED_BODY()

	UPROPERTY() bool bSuccess = false;
	UPROPERTY() FString ErrorMessage;

	/** Wie viele Kreuzungen geprueft wurden und wie viele Gelaendedaten hatten. */
	UPROPERTY() int32 CheckedCount = 0;
	UPROPERTY() int32 WithoutTerrainCount = 0;

	/** Wie viele ueber der Toleranz liegen, getrennt nach Richtung. */
	UPROPERTY() int32 FloatingCount = 0;
	UPROPERTY() int32 BuriedCount = 0;

	UPROPERTY() double MeanAbsDeltaCm = 0.0;
	UPROPERTY() double WorstFloatingCm = 0.0;
	UPROPERTY() double WorstBuriedCm = 0.0;

	/** Die groessten Abweichungen, absteigend. */
	UPROPERTY() TArray<FHeightMismatch> Worst;

	/**
	 * Kreuzungen AUSSERHALB der Gelaendeflaeche.
	 *
	 * GetHeightAtLocation liefert auch weit ausserhalb noch einen Wert - den
	 * geklemmten Randwert. Der erste Durchlauf hat genau das gezeigt: die
	 * vierzig schlimmsten Faelle meldeten alle exakt 25.599 cm Gelaendehoehe,
	 * an Stellen elf Kilometer auseinander. Das war kein Gelaende, das war
	 * ein Anschlag - die Strassen liegen dort schlicht ausserhalb des
	 * Hoehenmodells.
	 *
	 * Ohne diese Trennung ertraenken solche Faelle jede Statistik, und der
	 * eigentliche Fehler im abgedeckten Gebiet bleibt unsichtbar.
	 */
	UPROPERTY() int32 OutsideTerrainCount = 0;

	/** Wie CheckedCount/FloatingCount/MeanAbsDeltaCm, aber NUR innerhalb der
	 *  Gelaendeflaeche - das ist die Zahl, die den Spieler betrifft. */
	UPROPERTY() int32 CheckedInsideCount = 0;
	UPROPERTY() int32 FloatingInsideCount = 0;
	UPROPERTY() int32 BuriedInsideCount = 0;
	UPROPERTY() double MeanAbsDeltaInsideCm = 0.0;

	/**
	 * Verteilung der Betraege innerhalb der Gelaendeflaeche, in Stufen von
	 * 25/50/100/200/500/1000 cm und darueber.
	 *
	 * Ein Mittelwert allein sagt nicht, ob alle Kreuzungen ein bisschen
	 * daneben liegen oder wenige sehr weit. Fuer die Entscheidung, ob ein
	 * systematischer Versatz oder einzelne Ausreisser vorliegen, ist genau
	 * das der Unterschied.
	 */
	UPROPERTY() TArray<int32> HistogramInside;

	/** Umschliessende Flaeche aller Landscape-Proxies (Welt, cm). */
	UPROPERTY() FBox TerrainBounds = FBox(ForceInit);

	FString ToString() const;
};

/**
 * Prueft im GESAMTEN Stadtgebiet, ob die Fahrbahn auf dem Gelaende liegt.
 *
 * Anlass: "Auf der Platter Strasse Richtung Taunusstein sitzen Fahrbahn und
 * Gehwege nicht im Terrain, sondern in der Luft." Die bisherige Diagnose mass
 * nur am Spielerpunkt - was an den anderen 22.226 Kreuzungen los ist, stand
 * nirgends.
 *
 * Der Verdacht, den die Zahlen bestaetigen oder widerlegen sollen: Die
 * Strassen entstehen aus dem HOEHENMODELL (SRTM, feine Aufloesung), das
 * Landscape dagegen hat 781 cm je Feld. Auf einer steilen, gewoelbten Flanke
 * schneidet die lineare Interpolation des Landscape die Kuppe ab - die
 * Fahrbahn steht dann ueber dem Gelaende, ohne dass an einem der beiden
 * Datensaetze etwas falsch waere.
 *
 * Gemessen wird an den KREUZUNGEN, nicht an allen Fahrbahn-Vertices: Sie
 * liegen ueber das ganze Netz verteilt, ihre Zahl ist beherrschbar, und ein
 * Hoehenfehler zeigt sich dort genauso wie mitten auf der Strecke.
 */
struct WIESBADENREAL_API FWiesbadenHeightAudit
{
	/**
	 * @param Network      Fertiges Strassennetz.
	 * @param Proxies      Alle Landscape-Proxies der Welt.
	 * @param ToleranceCm  Ab welcher Abweichung eine Stelle gezaehlt wird.
	 * @param MaxWorst     Wie viele Einzelfaelle der Bericht auflistet.
	 */
	static FHeightAuditReport Run(
		const FRoadNetwork& Network,
		const TArray<ALandscapeProxy*>& Proxies,
		double ToleranceCm,
		int32 MaxWorst);

	/** Schreibt den Bericht als JSON. */
	static bool WriteJson(const FHeightAuditReport& Report, const FString& Path);
};
