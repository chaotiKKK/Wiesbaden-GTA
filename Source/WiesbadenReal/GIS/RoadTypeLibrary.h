// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/OSMTypes.h"
#include "RoadTypeLibrary.generated.h"

/**
 * Bauliche Kennwerte einer Strassenklasse.
 *
 * Die Defaults werden nur verwendet, wenn OSM keine explizite Angabe liefert.
 * Sie stammen aus den deutschen Regelwerken RASt 06 (Richtlinien fuer die
 * Anlage von Stadtstrassen) und RAA (Richtlinien fuer die Anlage von
 * Autobahnen) - dadurch stimmen die Strassenbreiten auch dort, wo OSM keine
 * width- oder lanes-Tags hat, mit der realen Bebauung ueberein.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadTypeDefinition
{
	GENERATED_BODY()

	/** Breite einer einzelnen Fahrspur in Metern. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double LaneWidthMeters = 3.25;

	/** Spuren je Richtung, wenn lanes=* fehlt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	int32 DefaultLanesPerDirection = 1;

	/** Regeltempo in km/h, wenn maxspeed=* fehlt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double DefaultMaxSpeedKmh = 50.0;

	/** Gehwegbreite in Metern. RASt 06 fordert mindestens 2,50 m Regelbreite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double SidewalkWidthMeters = 2.5;

	/** Bordsteinhoehe in Metern. Regelhoehe in Deutschland: 10-12 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double KerbHeightMeters = 0.12;

	/** Radwegbreite in Metern, wenn cycleway vorhanden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double CyclewayWidthMeters = 1.6;

	/** True, wenn dieser Typ standardmaessig Gehwege hat (auch ohne sidewalk-Tag). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	bool bSidewalkByDefault = true;

	/** True, wenn Mittelstreifen-Markierung gezeichnet wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	bool bHasCenterLineMarking = true;

	/**
	 * Vorfahrtsprioritaet. Kleiner = wichtiger. Bei Kreuzungen ohne Ampel
	 * bestimmt die Differenz, wer wartet; bei gleicher Prioritaet gilt
	 * "rechts vor links".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	int32 Priority = 5;

	/** Materialpfad der Fahrbahndecke. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FString RoadMaterialPath;

	/** Materialpfad des Gehwegs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	FString SidewalkMaterialPath;

	/** Standardoberflaeche, wenn surface=* fehlt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	EOSMSurfaceType DefaultSurface = EOSMSurfaceType::Asphalt;

	/** Ob KI-Fahrzeuge diesen Typ befahren duerfen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	bool bTrafficEnabled = true;

	/**
	 * Relative Verkehrsdichte, 0..1. Steuert, wie viele KI-Fahrzeuge das
	 * Verkehrssystem auf diesem Strassentyp einsetzt. Die Rheinstrasse
	 * (primary) traegt in der Realitaet ein Vielfaches des Verkehrs einer
	 * Wohnstrasse.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Road")
	double TrafficDensityFactor = 0.5;
};

/**
 * Registry der Strassenklassen, geladen aus
 * Content/Config/WiesbadenRoadTypes.json (gemeinsamer Config-Pfad wie der
 * Verkehrszeichen-Katalog, siehe WiesbadenConfigPaths).
 *
 * Die Werte liegen in JSON statt im Code, damit Level-Design und
 * Verkehrsbalancing ohne Neukompilierung angepasst werden koennen. Fehlt die
 * Datei oder ist sie fehlerhaft, greifen die im Code hinterlegten Defaults -
 * der Import darf daran nicht scheitern.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API URoadTypeLibrary : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Laedt die Definitionen aus einer JSON-Datei.
	 * @return false, wenn die Datei fehlt oder unlesbar ist. Die Defaults sind
	 *         danach trotzdem gesetzt und die Library nutzbar.
	 */
	UFUNCTION(BlueprintCallable, Category = "GIS|Roads")
	bool LoadFromJsonFile(const FString& FilePath);

	/** Setzt die im Code hinterlegten Defaults nach RASt 06 / RAA. */
	UFUNCTION(BlueprintCallable, Category = "GIS|Roads")
	void ApplyBuiltInDefaults();

	/** Standardpfad der Konfigurationsdatei (Content/Config/WiesbadenRoadTypes.json). */
	static FString GetDefaultConfigPath();

	/**
	 * Definition zu einem Strassentyp. Liefert immer einen gueltigen
	 * Datensatz - fuer unbekannte Typen den Residential-Datensatz.
	 */
	const FRoadTypeDefinition& GetDefinition(EOSMHighwayType Type) const;

	/**
	 * Ermittelt die Fahrspurzahl je Richtung aus den Tags eines Ways.
	 *
	 * Auswertungsreihenfolge:
	 *   1. lanes:forward / lanes:backward (asymmetrische Aufteilung, z. B.
	 *      Mainzer Strasse mit 3 Spuren stadteinwaerts und 2 stadtauswaerts)
	 *   2. lanes (gleichmaessig aufgeteilt; bei Einbahnstrassen alle in eine
	 *      Richtung, bei ungerader Zahl geht die zusaetzliche Spur an die
	 *      Vorwaertsrichtung - das entspricht der ueblichen Praxis, dass die
	 *      Mittelspur eine Abbiegespur der Hauptrichtung ist)
	 *   3. Default der Strassenklasse
	 */
	void ResolveLaneCounts(const FOSMWay& Way, EOSMHighwayType Type, EOSMOnewayType Oneway,
		int32& OutForwardLanes, int32& OutBackwardLanes) const;

	/**
	 * Fahrbahnbreite in Metern.
	 * Prioritaet: width=* -> Spurzahl * Spurbreite.
	 * Die Spurbreite wird bei explizitem width-Tag NICHT verwendet, weil das
	 * Tag die tatsaechlich vermessene Breite ist und damit verlaesslicher als
	 * jede Rechnung.
	 */
	double ResolveCarriagewayWidthMeters(const FOSMWay& Way, EOSMHighwayType Type,
		int32 ForwardLanes, int32 BackwardLanes) const;

	/** Tempolimit in km/h. Faellt auf den Klassendefault zurueck. */
	double ResolveMaxSpeedKmh(const FOSMWay& Way, EOSMHighwayType Type) const;

	/** Gehwegkonfiguration inkl. Klassendefault. */
	EOSMSidewalkType ResolveSidewalk(const FOSMWay& Way, EOSMHighwayType Type) const;

	/** Oberflaeche inkl. Klassendefault. */
	EOSMSurfaceType ResolveSurface(const FOSMWay& Way, EOSMHighwayType Type) const;

private:
	UPROPERTY()
	TMap<EOSMHighwayType, FRoadTypeDefinition> Definitions;

	/** Rueckfallwert, falls ein Typ nicht in der Map steht. */
	UPROPERTY()
	FRoadTypeDefinition FallbackDefinition;
};
