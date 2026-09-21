// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "SebboHqShape.generated.h"

/** Werkstoff eines Bauteils am Hauptsitz. */
UENUM(BlueprintType)
enum class EHqMaterial : uint8
{
	/** Sichtbeton der Kerne, Decken und Bruestungsbaender. */
	Concrete UMETA(DisplayName = "Beton"),
	/** Glasfassade der Regelgeschosse. */
	Glass    UMETA(DisplayName = "Glas"),
	/** Dunkles Metall: Attika, Krone, Landeplatzrand. */
	Metal    UMETA(DisplayName = "Metall"),
	/** Markierungen auf dem Landeplatz. */
	Marking  UMETA(DisplayName = "Markierung"),
	MAX      UMETA(Hidden)
};

/** Grundform eines Bauteils. */
UENUM(BlueprintType)
enum class EHqPrimitive : uint8
{
	Box      UMETA(DisplayName = "Quader"),
	Cylinder UMETA(DisplayName = "Zylinder"),
	MAX      UMETA(Hidden)
};

/**
 * Ein Bauteil des Hauptsitzes in OERTLICHEN Koordinaten (Zentimeter).
 *
 * Ursprung ist der Fusspunkt in der Mitte des Grundrisses, +X zur Strasse
 * (Platter Strasse), +Y quer, +Z nach oben. Der Actor setzt das Ganze an die
 * echte Weltkoordinate und dreht es einmal.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FHqPart
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "HQ")
	EHqPrimitive Primitive = EHqPrimitive::Box;

	UPROPERTY(BlueprintReadOnly, Category = "HQ")
	EHqMaterial Material = EHqMaterial::Concrete;

	/** Mittelpunkt des Bauteils (cm, oertlich). */
	UPROPERTY(BlueprintReadOnly, Category = "HQ")
	FVector CenterCm = FVector::ZeroVector;

	/** Kantenlaengen bzw. (Durchmesser, Durchmesser, Hoehe) in cm. */
	UPROPERTY(BlueprintReadOnly, Category = "HQ")
	FVector SizeCm = FVector::ZeroVector;

	/** Geschoss, zu dem das Teil gehoert (-1 = Sockel/Krone/Dach). */
	UPROPERTY(BlueprintReadOnly, Category = "HQ")
	int32 Floor = -1;
};

/** Ein physisches Zielvolumen der Tower-Ankunft in lokalen Zentimetern. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FHqArrivalTarget
{
	GENERATED_BODY()

	/** Mittelpunkt des Ziels relativ zum Tower-Fusspunkt. */
	UPROPERTY(BlueprintReadOnly, Category = "HQ|Arrival")
	FVector CenterCm = FVector::ZeroVector;

	/** Halbe Ausdehnung der akzeptierten Ankunftszone. */
	UPROPERTY(BlueprintReadOnly, Category = "HQ|Arrival")
	FVector ExtentCm = FVector::ZeroVector;
};

/** Private Tower-Bauteile und die drei getrennten Ankunftsziele. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FSebboHqArrivalLayout
{
	GENERATED_BODY()

	/** Garage, Eingangsbereich und deren konstruktive Umfassung. */
	UPROPERTY(BlueprintReadOnly, Category = "HQ|Arrival")
	TArray<FHqPart> Parts;

	UPROPERTY(BlueprintReadOnly, Category = "HQ|Arrival")
	FHqArrivalTarget GarageTarget;

	UPROPERTY(BlueprintReadOnly, Category = "HQ|Arrival")
	FHqArrivalTarget PedestrianTarget;

	UPROPERTY(BlueprintReadOnly, Category = "HQ|Arrival")
	FHqArrivalTarget HelicopterTarget;
};

/**
 * Die Masse des Hauptsitzes.
 *
 * ENTSCHIEDEN am 2026-09-20: 60 m hoch, 15 Geschosse, 30 x 30 m Grundflaeche,
 * auf der freien Flaeche westlich der Galileistrasse 35 (Kandidat A der
 * Standortkarte). Die Nachbarschaft ist vier- bis fuenfgeschossig - der Turm
 * ist ein bewusster Fremdkoerper, aber ein gerechneter.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FSebboHqDimensions
{
	GENERATED_BODY()

	/** Kantenlaenge des Regelgrundrisses (cm). */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "500.0"))
	double FootprintCm = 3000.0;

	/** Lichte Geschosshoehe plus Deckenstaerke (cm). */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "200.0"))
	double FloorHeightCm = 400.0;

	/** Regelgeschosse ueber dem Sockel. */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "1"))
	int32 FloorCount = 15;

	/** Wieviele der unteren Geschosse den breiteren Sockel bilden. */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "0"))
	int32 PodiumFloors = 2;

	/** Ueberstand des Sockels ueber den Regelgrundriss je Seite (cm). */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "0.0"))
	double PodiumOversizeCm = 200.0;

	/** Deckenstaerke - das sichtbare Bruestungsband je Geschoss (cm). */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "10.0"))
	double SlabCm = 45.0;

	/** Seitenlaenge des Aufzugs-/Treppenkerns (cm). */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "100.0"))
	double CoreCm = 900.0;

	/** Hoehe der Krone ueber dem obersten Geschoss (cm). */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "0.0"))
	double CrownHeightCm = 600.0;

	/**
	 * Durchmesser des Landeplatzes auf dem Dach (cm).
	 *
	 * 11 m, nicht 18: auf ein 30-m-Dach passt neben der Krone kein groesserer
	 * Platz, dessen Anflug frei bleibt. Mit 18 m lag die Aufsetzflaeche zur
	 * Haelfte unter der Krone - gross gezeichnet und unbenutzbar.
	 */
	UPROPERTY(EditAnywhere, Category = "HQ", meta = (ClampMin = "500.0"))
	double HelipadDiameterCm = 1100.0;

	/** Gesamthoehe bis Oberkante Attika (cm) - abgeleitet, nicht eingestellt. */
	double TotalHeightCm() const { return FloorCount * FloorHeightCm; }
};

/**
 * Baut die Bauteile des Hauptsitzes - datenrein, ohne Welt und ohne Mesh.
 *
 * Gleiche Aufteilung wie bei den Strassenmoebeln und dem Bus-Innenraum: die
 * Form ist eine Rechnung und laesst sich ohne Engine pruefen; der Actor macht
 * daraus nur noch Komponenten. Die spaeteren Stufen (Treppenhaus, Schacht,
 * Halle, Tiefgarage) haengen sich hier an, statt den Actor wachsen zu lassen.
 */
namespace SebboHq
{
	/** Turmhuelle: Sockel, Regelgeschosse, Kern, Krone, Landeplatz. */
	WIESBADENREAL_API void BuildShell(
		const FSebboHqDimensions& Dimensions, TArray<FHqPart>& OutParts);

	/** Oberkante der Attika ueber dem Fusspunkt (cm). */
	WIESBADENREAL_API double GetRoofHeightCm(const FSebboHqDimensions& Dimensions);

	/**
	 * Hoehe des privaten Bodens ueber dem Bauplateau (cm).
	 *
	 * Deckenstaerke plus Belag. Das Bauplateau wird beim Bake um genau diesen
	 * Betrag UNTER die Fahrbahn gelegt, damit der fertige Boden die Strasse
	 * trifft (ConfigureSebboHqPad). Der Wert steht hier, damit Turm und
	 * Plateau nicht zwei Zahlen fuehren.
	 */
	WIESBADENREAL_API double GetAccessFloorCm(const FSebboHqDimensions& Dimensions);

	/** Hoehe der Landeplatzflaeche ueber dem Fusspunkt (cm). */
	WIESBADENREAL_API double GetHelipadHeightCm(const FSebboHqDimensions& Dimensions);

	/**
	 * Versatz der Landeplatzmitte aus der Dachmitte (cm, +X).
	 *
	 * EINE Wahrheit fuer Huelle und Ankunftsziel. Beide rechneten den Versatz
	 * vorher getrennt aus demselben Faktor - der Landeplatz lag darum zwar
	 * unter dem Ziel, aber beide gemeinsam unter der Krone, und das fiel
	 * keiner der beiden Rechnungen auf.
	 */
	WIESBADENREAL_API double GetHelipadOffsetCm(const FSebboHqDimensions& Dimensions);

	/**
	 * Halbe Kantenlaenge der Krone (cm).
	 *
	 * Die Krone sitzt auf dem Erschliessungskern und darf den Anflug auf den
	 * Landeplatz nicht ueberdecken: sie war 16,5 m breit und haengte damit
	 * ueber den inneren 8 m des Landeplatzes - von oben war das Aufsetzfeld
	 * nicht erreichbar (Laufzeit-Sonde, 21.09.2026).
	 */
	WIESBADENREAL_API double GetCrownHalfWidthCm(const FSebboHqDimensions& Dimensions);

	/**
	 * Oberkante des Erschliessungskerns (cm).
	 *
	 * Der Kern ragt ein Geschoss ueber die Attika - Dachaufbau mit Ausstieg.
	 * Huelle und Kern teilen sich diese Hoehe: die Krone sitzt darauf, der
	 * Dachaufbau endet dort.
	 */
	WIESBADENREAL_API double GetCoreTopHeightCm(const FSebboHqDimensions& Dimensions);

	/**
	 * Vertikaler Kern: Treppenhaus und Aufzugsschacht ueber alle Geschosse.
	 *
	 * HOHL gebaut - Waende um eine Leere, wie beim Bus-Innenraum. Die
	 * Darstellung ist rein additiv: ein Vollkoerper waere ein Betonklotz, kein
	 * Raum. Je Geschoss vier Aussenwaende, eine Mittelwand, zwei
	 * Tueroeffnungen als LUECKEN zwischen Wandstuecken, ein Treppenpodest und
	 * ein Lauf mit 25-cm-Stufen.
	 *
	 * Die +Y-Haelfte ist der Aufzugsschacht und bleibt bewusst ohne Boeden -
	 * Kabine, Tueren und Antrieb kommen mit Stufe 7. Die -Y-Haelfte ist das
	 * begehbare Treppenhaus.
	 *
	 * Der Kern steht MITTIG im Grundriss; die Huelle (BuildShell) baut ihn
	 * nicht mehr mit, sie kennt nur seine Oberkante fuer Krone und Mast.
	 */
	WIESBADENREAL_API void BuildVerticalCore(
		const FSebboHqDimensions& Dimensions, TArray<FHqPart>& OutParts);

	/**
	 * Private Garage und Personeneingang an der +X-Seite (Platter Strasse).
	 * Die oeffentliche Fahrbahn, Gehweg und Bordstein gehoeren bewusst NICHT
	 * hierher; dieser reine Builder beschreibt nur die Tower-Seite der Grenze.
	 */
	WIESBADENREAL_API FSebboHqArrivalLayout BuildArrivalFacilities(
		const FSebboHqDimensions& Dimensions);
}
