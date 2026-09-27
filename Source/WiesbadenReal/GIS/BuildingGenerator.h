// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/WiesbadenRegion.h"
#include "GIS/IHeightSampler.h"
#include "GIS/OSMTypes.h"
#include "ProceduralMeshComponent.h"
#include "BuildingGenerator.generated.h"

class UGeoCoordinateConverter;

/** Materialkanal eines Gebaeude-Meshes. */
UENUM(BlueprintType)
enum class EBuildingMeshChannel : uint8
{
	Wall		UMETA(DisplayName = "Fassade"),
	Roof		UMETA(DisplayName = "Dach"),
	GroundFloor	UMETA(DisplayName = "Erdgeschoss / Schaufenster"),
	MAX			UMETA(Hidden)
};

/** Ein erzeugtes Gebaeude mit Metadaten fuer Gameplay und Streaming. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FGeneratedBuilding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int64 SourceId = 0;

	/** True, wenn die Quelle eine Multipolygon-Relation war. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	bool bFromRelation = false;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	EOSMBuildingType BuildingType = EOSMBuildingType::Generic;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	EOSMRoofShape RoofShape = EOSMRoofShape::Flat;

	/** Name aus name=*, z. B. "Kurhaus Wiesbaden". */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FString BuildingName;

	/** Adresse aus addr:street + addr:housenumber. Fuer GPS-Ziele. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FString Address;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	double HeightCm = 1000.0;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 LevelCount = 3;

	/** Grundflaeche in Quadratmetern. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	double FootprintAreaSqm = 0.0;

	/** Schwerpunkt der Grundflaeche in Weltkoordinaten (cm), auf Terrainhoehe. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FVector Centroid = FVector::ZeroVector;

	/** Umschliessende Box in Weltkoordinaten - fuer World-Partition-Zuordnung. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FBox Bounds = FBox(ForceInit);

	/**
	 * Flaechenminimale GEDREHTE Grundriss-Box (Mitte, Halbmasse, Drehung).
	 *
	 * Fuer die Kollision: die achsparallele `Bounds` deckt bei gedrehten
	 * Gebaeuden im Mittel das 2,1-fache des Grundrisses ab und ragt damit auf
	 * die Fahrbahn. Diese drei Werte beschreiben den Grundriss dagegen fuer
	 * rechteckige Bauten exakt und kosten nur wenige Bytes je Gebaeude - der
	 * volle Umriss bleibt bewusst draussen (siehe Hinweis am Ende der Struktur).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FVector2D FootprintCenterCm = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FVector2D FootprintExtentCm = FVector2D::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	float FootprintYawDegrees = 0.0f;

	/**
	 * True, wenn das Gebaeude als Landmarke gilt und statt der prozeduralen
	 * Geometrie ein handmodelliertes Asset gesetzt werden soll. Die
	 * prozedurale Geometrie wird trotzdem erzeugt und dient als Platzhalter,
	 * solange das Asset fehlt.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	bool bIsLandmark = false;

	/**
	 * Stadt-Region, der das Gebaeude zugeordnet ist (WorldClaw-Schritt 3:
	 * Objekte logisch platzieren). Wird vom Regionen-Pass der Pipeline
	 * gesetzt (Wasser/Gruen/Wohnen/Gewerbe/Industrie).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	ECityRegionType RegionType = ECityRegionType::Other;

	/** Name der zugeordneten Region (leer, wenn keine Region trifft). */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FString RegionName;

	// Die Geometrie liegt bewusst NICHT hier, sondern gesammelt in
	// FBuildingMeshData: mehrere Zehntausend Gebaeude mit je eigenen
	// Vertex-Arrays waeren ebenso viele Draw-Calls. Die Zuordnung Gebaeude ->
	// Geometrie ist fuer das Rendering nicht noetig; wo sie gebraucht wird
	// (Zerstoerung einzelner Gebaeude), erfolgt sie ueber Bounds und SourceId.
};

/** Mesh-Daten eines Gebaeudes, gruppiert nach Materialkanal. */
USTRUCT()
struct WIESBADENREAL_API FBuildingMeshSection
{
	GENERATED_BODY()

	EBuildingMeshChannel Channel = EBuildingMeshChannel::Wall;

	/** Fassadenmaterial-Index, abgeleitet aus Gebaeudetyp und Baualter. */
	int32 MaterialVariant = 0;

	/**
	 * Per-Adress-Override-Schluessel (Adresse aus addr:street +
	 * addr:housenumber). Leer = Standard (Sections sind nach Kanal+Variante
	 * gruppiert); nicht leer = nur Gebaeude dieser Adresse landen in diesem
	 * Abschnitt und bekommen ueber AddressFacadeMaterials ein eigenes Material.
	 */
	FString FacadeOverrideKey;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	bool IsEmpty() const { return Vertices.Num() == 0 || Triangles.Num() < 3; }
};

/** Alle Gebaeudegeometrie eines Generierungslaufs. */
USTRUCT()
struct WIESBADENREAL_API FBuildingMeshData
{
	GENERATED_BODY()

	TArray<FBuildingMeshSection> Sections;

	int32 GetTotalVertexCount() const
	{
		int32 Count = 0;
		for (const FBuildingMeshSection& Section : Sections)
		{
			Count += Section.Vertices.Num();
		}
		return Count;
	}

	int32 GetTotalTriangleCount() const
	{
		int32 Count = 0;
		for (const FBuildingMeshSection& Section : Sections)
		{
			Count += Section.Triangles.Num() / 3;
		}
		return Count;
	}

	void Reset() { Sections.Reset(); }
};

/** Parameter der Gebaeudegenerierung. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FBuildingGenerationSettings
{
	GENERATED_BODY()

	/** Geschosshoehe in Metern, wenn nur building:levels vorliegt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	double MetersPerLevel = 3.2;

	/**
	 * Skaliert alle Gebaeudehoehen (0.6 = flach/niedrig, 2.0 = Hochhaus).
	 * Wird vom City-Prompt gesetzt (BuildingHeightScale); ohne Prompt 1.0.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	double HeightScale = 1.0;

	/**
	 * Gebaeude unterhalb dieser Grundflaeche (m^2) werden verworfen. In OSM
	 * sind Muelltonnenhaeuschen und Trafostationen als building=yes erfasst;
	 * bei mehreren Zehntausend Gebaeuden kosten sie mehr Performance als sie
	 * zur Stadtwahrnehmung beitragen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	double MinFootprintAreaSqm = 8.0;

	/** Gebaeude oberhalb dieser Grundflaeche gelten als Grossbau (eigener Actor). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	double LargeBuildingAreaSqm = 2000.0;

	/**
	 * Dachueberstand in Metern. Ohne Ueberstand wirken Satteldaecher wie
	 * aufgesetzte Kartons; 40 cm entspricht der ueblichen Bauweise.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	double RoofOverhangMeters = 0.4;

	/** Neigung von Satteldaechern in Grad. In Hessen ueblich: 38-45 Grad. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	double GabledRoofAngleDegrees = 42.0;

	/**
	 * Wie tief das Gebaeude in den Boden gesetzt wird (cm). Verhindert
	 * sichtbare Spalten am Hang, wo die Grundflaeche nicht eben ist.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	double FoundationDepthCm = 150.0;

	/** Wenn true, wird das Erdgeschoss als eigener Materialkanal ausgegeben. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	bool bSeparateGroundFloor = true;

	/** Wenn true, werden Daecher erzeugt (bei Draufsicht-Tests abschaltbar). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	bool bGenerateRoofs = true;

	/** Wenn true, werden building:part-Ways als eigene Volumen erzeugt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	bool bGenerateBuildingParts = true;

	/**
	 * Per-Adress-Override: Adressen (addr:street + addr:housenumber, z. B.
	 * "Mainzer Strasse 129"; Abgleich case-insensitive), deren Gebaeude einen
	 * eigenen Mesh-Abschnitt mit eigenem Fassaden-Material bekommen. Das
	 * Material wird auf der Render-Seite ueber AddressFacadeMaterials (gleiche
	 * Adresse) zugewiesen - die Varianten-Pipeline (0-5) bleibt unberuehrt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	TArray<FString> FacadeOverrideAddresses;

	/**
	 * City-Prompt-Overrides (bewusst getrennt von der Varianten-Pipeline):
	 * Stil-Gewichte (Materialvariante 0-5 -> Gewicht) aus der FCityPromptSpec.
	 * Der Generator vergibt pro Gebaeude deterministisch (Seed = OSM-Id) eine
	 * gewichtete Variante als FacadeOverrideKey "PromptStyle:<Name>"
	 * (Namen aus CityPromptParser::GetFacadeStyleNames). Die Render-Seite
	 * mappt den Key ueber PromptFacadeMaterials auf ein Material.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	TMap<int32, float> PromptFacadeVariantWeights;

	/**
	 * City-Prompt-Overrides: kanonische Namen der erkannten Landmarken (z. B.
	 * "Marktkirche"). Gebaeude, deren name=* dagegen matcht (case- und
	 * sz/umlaut-insensitiv, Wortgrenzen), werden als Landmarke markiert und
	 * bekommen den FacadeOverrideKey "PromptLandmark:<Name>".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	TArray<FString> PromptLandmarkNames;

	/** False deaktiviert beide Prompt-Overrides (Stil + Landmarke). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	bool bApplyPromptOverrides = true;

	/**
	 * Stadt-Regionen aus der Pipeline (WorldClaw-Schritt 3: regionale Planung
	 * VOR der Objekt-Erzeugung). Gebaeude werden beim Bauen ihrer Region
	 * zugeordnet (RegionType/RegionName); Wasser-Gebaeude werden bei
	 * bRemoveWaterBuildings verworfen - BEVOR Mesh entsteht (kein Haus im
	 * See, auch nicht im gerenderten Mesh). Regionales Feintuning: Hoehen-
	 * und Fassaden-Faktoren je Regionstyp (GetRegionalHeightScale/Key).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	TArray<FWiesbadenRegion> RegionMap;

	/** False deaktiviert die Regionen-Regeln (Zuordnung + Wasser-Filter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	bool bApplyRegionRules = false;

	/** Gebaeude in Wasser-Regionen verwerfen (kein Haus im See). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Buildings")
	bool bRemoveWaterBuildings = true;
};

/** Diagnose eines Gebaeude-Generierungslaufs. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FBuildingGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	bool bSuccess = false;

	/**
	 * True, wenn der Lauf ueber den Abbruch-Callback abgebrochen wurde
	 * (kein Fehler - die erzeugten Gebaeude sind ein brauchbares Teilresultat).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	bool bCancelled = false;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 BuildingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 RelationBuildingCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 LandmarkCount = 0;

	/** Wegen zu kleiner Grundflaeche verworfen. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 TooSmallCount = 0;

	/** Wegen fehlgeschlagener Triangulierung verworfen. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 DegenerateCount = 0;

	/** Wegen unaufloesbarer Node-Referenzen verworfen. */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 IncompleteCount = 0;

	/** Gebaeude in Wasser-Regionen verworfen (WorldClaw: kein Haus im See). */
	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 WaterRemovedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 VertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Buildings")
	double DurationSeconds = 0.0;

	FString ToString() const;
};

/**
 * Erzeugt Gebaeudegeometrie aus OSM-Grundrissen.
 *
 * VERFAHREN
 *  1. Grundriss aus Way oder Multipolygon-Relation aufloesen
 *  2. In Weltkoordinaten projizieren, Rauschen entfernen
 *  3. Hoehe aus height / building:levels / Typdefault bestimmen
 *  4. Fassaden als Extrusion der Umrisskanten erzeugen
 *  5. Dach nach roof:shape erzeugen
 *
 * Die Fassaden-UVs werden geschossweise vergeben: U laeuft in Metern entlang
 * der Wand, V in Geschossen. Ein Fassadenmaterial mit einer Kachel je
 * Geschoss erzeugt dadurch automatisch die richtige Fensterzahl, ohne dass
 * Fenster als Geometrie modelliert werden muessen - das ist der einzige Weg,
 * mehrere Zehntausend Gebaeude im Speicherbudget darzustellen.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API UBuildingGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Erzeugt alle Gebaeude eines Datensatzes.
	 *
	 * @param HeightSampler Darf nullptr sein - dann steht alles auf Z=0.
	 * @param OutMeshData Darf nullptr sein, wenn nur die Metadaten (Adressen,
	 *        Positionen) gebraucht werden, etwa auf einem dedizierten Server.
	 * @param Cancel Wird zwischen den Gebaeuden gepollt (typischerweise ein
	 *        std::atomic-Flag des Aufrufers); liefert es true, bricht der Lauf
	 *        sofort ab und meldet Report.bCancelled. Der Gebaeude-Pass ist die
	 *        mit Abstand laengste Stufe der Pipeline - ohne den Callback muesste
	 *        der Shutdown bis zu 120 s auf eine laufende Stufe warten.
	 */
	FBuildingGenerationReport Generate(
		const FOSMDataSet& DataSet,
		const UGeoCoordinateConverter* Converter,
		const IHeightSampler* HeightSampler,
		const FBuildingGenerationSettings& Settings,
		TArray<FGeneratedBuilding>& OutBuildings,
		FBuildingMeshData* OutMeshData,
		TFunction<bool()> Cancel = nullptr);

	/**
	 * Prueft, ob ein Gebaeude eine Landmarke ist (Spezifikation 5.3).
	 * Erkennung ueber name=* und Gebaeudetyp, nicht ueber die OSM-ID - IDs
	 * aendern sich, wenn ein Mapper das Gebaeude neu zeichnet.
	 */
	static bool IsLandmark(const FString& BuildingName, EOSMBuildingType Type, double FootprintAreaSqm);

	/** Liste der als Landmarke behandelten Gebaeudenamen in Wiesbaden. */
	static const TArray<FString>& GetLandmarkNames();

private:
	/**
	 * Baut ein einzelnes Gebaeude aus Aussenring und Loechern.
	 * @return false, wenn der Grundriss degeneriert ist.
	 */
	bool BuildSingleBuilding(
		int64 SourceId,
		bool bFromRelation,
		const TMap<FName, FString>& Tags,
		const TArray<FVector2D>& OuterRing,
		const TArray<TArray<FVector2D>>& Holes,
		const IHeightSampler* HeightSampler,
		const FBuildingGenerationSettings& Settings,
		FGeneratedBuilding& OutBuilding,
		FBuildingMeshData* OutMeshData) const;

	/** Erzeugt die Fassaden eines Rings. */
	void BuildWalls(
		const TArray<FVector2D>& Ring,
		double BaseZ,
		double TopZ,
		int32 LevelCount,
		int32 MaterialVariant,
		const FString& FacadeOverrideKey,
		const FBuildingGenerationSettings& Settings,
		FBuildingMeshData& OutMeshData) const;

	/** Erzeugt das Dach nach der angegebenen Form. */
	void BuildRoof(
		const TArray<FVector2D>& OuterRing,
		const TArray<TArray<FVector2D>>& Holes,
		double EavesZ,
		EOSMRoofShape Shape,
		double RoofHeightCm,
		int32 MaterialVariant,
		EOSMBuildingType BuildingType,
		bool bIsLandmark,
		int64 SourceId,
		const FString& FacadeOverrideKey,
		double RoofOverhangMeters,
		FBuildingMeshData& OutMeshData) const;

	/**
	 * Loest eine Multipolygon-Relation in Aussen- und Innenringe auf.
	 * OSM-Multipolygone bestehen aus Way-Fragmenten, die erst zu geschlossenen
	 * Ringen zusammengesetzt werden muessen - ein Innenhof kann aus fuenf
	 * einzelnen Ways bestehen.
	 * @return false, wenn kein geschlossener Aussenring gebildet werden konnte.
	 */
	bool AssembleMultipolygon(
		const FOSMRelation& Relation,
		const FOSMDataSet& DataSet,
		const UGeoCoordinateConverter& Converter,
		TArray<TArray<FVector2D>>& OutOuterRings,
		TArray<TArray<FVector2D>>& OutInnerRings) const;

	/**
	 * Sucht oder erzeugt den Mesh-Abschnitt fuer Kanal, Variante und Override-Key
	 * und liefert seinen INDEX in MeshData.Sections.
	 *
	 * Bewusst KEINE Referenz: Ein spaeteres FindOrAddSection kann MeshData.Sections
	 * neu allozieren und eine gehaltene Referenz entwerten (Use-after-Realloc). Der
	 * Index bleibt stabil; der Aufrufer bindet die Referenz erst NACH allen
	 * Section-Anforderungen.
	 */
	static int32 FindOrAddSection(
		FBuildingMeshData& MeshData,
		EBuildingMeshChannel Channel,
		int32 MaterialVariant,
		const FString& FacadeOverrideKey);

public:
	/**
	 * Waehlt die Fassaden-Materialvariante (0-5).
	 * Beruecksichtigt Gebaeudetyp und, falls vorhanden, start_date /
	 * building:material - dadurch bekommt die Wilhelmstrasse
	 * Gruenderzeit-Putzfassaden und das Industriegebiet in Biebrich
	 * Sichtbeton, ohne dass beides einzeln getaggt sein muss.
	 *
	 * SeedId (OSM-Id) streut Wohnbauten OHNE eindeutiges Material/Typ
	 * deterministisch ueber eine epochengerechte Palette - so wirken
	 * benachbarte Wohnbloecke nicht uniform. Oeffentlich fuer den Unit-Test.
	 */
	static int32 SelectMaterialVariant(const TMap<FName, FString>& Tags, EOSMBuildingType Type,
		int64 SeedId);

	/**
	 * Dachdeckung aus Fassaden-Variante, Dachform, Gebaeudetyp und Landmarke -
	 * EINE Deckung je Gebaeude.
	 *
	 * 0 = Terrakotta-Pfanne (Wohnbau: Putz/Backstein/Fachwerk),
	 * 1 = Schiefer (Gruenderzeit/Civic/Uni: Sandstein-Fassade),
	 * 2 = Zink/Blech (Moderne/Buero/Industrie ODER jedes Flachdach),
	 * 3 = Kupfergruen/Patina (buergerliche Wahrzeichen + jede Kuppel/Turmhelm),
	 * 4 = dunkler Schiefer (Kirchen).
	 *
	 * Kirchen (EOSMBuildingType::Church) und Wahrzeichen (bIsLandmark) tragen
	 * eine EIGENE, markante Deckung statt der allgemeinen Sandstein->Schiefer-
	 * Regel: Kirchen dunklen Schiefer (ortsgerecht - Marktkirche/Bergkirche/
	 * Ringkirche), buergerliche Wahrzeichen (Kurhaus, Rathaus, Theater ...) die
	 * kupfergruene Patina, und jede Kuppel/jedes Zeltdach ebenfalls Kupfergruen.
	 * Alle uebrigen Sandstein-Bauten (Civic/Uni/Gruenderzeit) bleiben beim
	 * normalen Schiefer.
	 *
	 * Loest die alte Regionswuerfelung im Dachmaterial ab: die Deckung folgt
	 * dem Gebaeudetyp, nicht einer 14-m-Weltzelle. Datenrein pruefbar (Test
	 * GIS.RoofCovering).
	 */
	static int32 RoofCoveringIndex(int32 MaterialVariant, EOSMRoofShape Shape,
		EOSMBuildingType BuildingType, bool bIsLandmark);

	/**
	 * Deterministische Tonstufe (0-255) je Gebaeude aus der OSM-Id.
	 *
	 * BuildRoof legt den Wert in den G-Kanal der Dach-Vertexfarbe; das Material
	 * verschiebt damit die Deckungsfarbe leicht (+-~9 %), sodass eine Reihe
	 * gleichtypiger Haeuser nicht identisch wirkt - die DeckungsART (R-Kanal,
	 * RoofCoveringIndex) bleibt davon unberuehrt. Eigene Mischkonstante, damit
	 * der Ton NICHT mit der Fassaden-/Deckungswahl korreliert. Datenrein
	 * pruefbar (Test GIS.RoofTone): gleiche Id -> gleicher Ton, benachbarte Ids
	 * -> unterschiedliche Toene, gleichverteilt.
	 */
	static uint8 RoofToneByte(int64 SourceId);

public:
	/**
	 * Liefert den Per-Adress-Override-Schluessel (die kanonische Schreibweise
	 * aus FacadeOverrideAddresses) fuer eine Adresse oder leer. Case- und
	 * sz/umlaut-insensitiv ueber NormalizeAddressForMatch; rein datenbasiert
	 * und damit direkt testbar.
	 */
	static FString ResolveFacadeOverrideKey(
		const FString& Address,
		const TArray<FString>& OverrideAddresses);

	/**
	 * Normalisiert eine Adresse fuer den Vergleich: lowercase, sz->ss und
	 * ae/oe/ue-Transliteration der Umlaute, damit OSM-Schreibweisen (mit
	 * sz/Umlauten) und ASCII-Eingaben ("Strasse", "Mueller") dieselbe
	 * Vergleichsform ergeben. Rein datenbasiert und direkt testbar.
	 */
	static FString NormalizeAddressForMatch(const FString& Address);

	/**
	 * Adress-Override fuer die realen Gebaeude an der Platter Strasse (per Adresse
	 * identifiziert, sz/umlaut-insensitiv ueber NormalizeAddressForMatch):
	 *  - 140      -> 12 Geschosse (markanter Wohnblock)
	 *  - 142      -> eingeschossige Garage/Flachdach (Spawn + Heli davor)
	 *  - 144/146  -> 6 Geschosse (Wohnblock)
	 * Setzt LevelCount/HeightCm (und fuer 142 BuildingType/RoofShape) auf OutBuilding
	 * und gibt true zurueck, wenn ein Zweig griff. MetersPerLevel skaliert die Hoehe
	 * konsistent zur restlichen Generierung. Rein datenbasiert -> direkt testbar.
	 *
	 * bHasReliableHeight: liegt eine verlaessliche, amtliche Hoehe vor (height/
	 * building:height-Tag, wie ihn der LoD2-Injektor aus den Hessen-Daten setzt),
	 * weicht der Override komplett zurueck (return false, keine Aenderung) - die
	 * echten Gebaeudehoehen gewinnen und ersetzen so das hartcodierte Raten. Der
	 * Override bleibt nur noch Fallback fuer Footprints ohne amtliche Hoehe.
	 */
	static bool ApplyPlatterAddressOverride(FGeneratedBuilding& OutBuilding, double MetersPerLevel,
		bool bHasReliableHeight = false);

	/**
	 * Deterministische gewichtete Stil-Auswahl aus den Prompt-Gewichten
	 * (Materialvariante -> Gewicht). Seed = OSM-Id als String; gleicher Seed
	 * ergibt immer dieselbe Variante. Gibt -1 zurueck, wenn keine Gewichte
	 * gesetzt sind.
	 */
	static int32 SelectPromptVariant(const TMap<int32, float>& VariantWeights, const FString& SeedString);

	/**
	 * Stil-Override-Key "PromptStyle:<Name>" (Name aus
	 * CityPromptParser::GetFacadeStyleNames) oder leer, wenn keine Gewichte
	 * gesetzt sind. Adress- und Landmarken-Overrides haben Vorrang.
	 */
	static FString ResolvePromptStyleKey(const TMap<int32, float>& VariantWeights, const FString& SeedString);

	/**
	 * Landmarken-Override-Key "PromptLandmark:<KanonischerName>" (Name aus
	 * der Prompt-Liste), wenn der Gebaeude-Name dagegen matcht (case- und
	 * sz/umlaut-insensitiv wie NormalizeAddressForMatch, Wortgrenzen). Leer,
	 * wenn kein Treffer oder keine Liste gesetzt.
	 */
	static FString ResolvePromptLandmarkKey(const FString& BuildingName, const TArray<FString>& PromptLandmarkNames);

	/**
	 * Regionaler Hoehen-Faktor je Regionstyp (WorldClaw: Objekt-Hoehen je
	 * Region): Industrie 1.1 (grosszuegige Hallen), Gruen 0.85 (im Park
	 * stehen keine Hochhaeuser), sonst 1.0.
	 */
	/**
	 * Streut eine Standard-Gebaeudehoehe anhand echter Signale.
	 *
	 * Notwendig, weil 85 % der Wiesbadener ALKIS-Grundrisse als
	 * building=residential ohne Geschosszahl vorliegen (99.284 von 117.425;
	 * nur 210 tragen eine explizite Hoehe). Sie alle bekamen denselben
	 * Typ-Default und die Stadt wirkte dadurch wie eine gleichfoermige Mauer.
	 *
	 * Verwendet ausschliesslich vorhandene Groessen statt erfundener Daten:
	 * Lage zum Zentrum (der Weltursprung IST das Wiesbadener Zentrum),
	 * Grundrissflaeche und eine aus der SourceId abgeleitete, reproduzierbare
	 * Streuung.
	 *
	 * Nur fuer den Default-Fall gedacht - getaggte Hoehen bleiben unangetastet.
	 */
	static double VaryDefaultHeightMeters(
		double BaseHeightMeters,
		double FootprintAreaSqm,
		double DistanceToCentreCm,
		int64 SourceId,
		double MetersPerLevel);

	static float GetRegionalHeightScale(ECityRegionType Type);

	/**
	 * Regionaler Fassaden-Key je Regionstyp ("Region:Industrie" fuer
	 * Industrie, "Region:Gewerbe" fuer Commercial), sonst leer. Der Key
	 * trennt einen eigenen Abschnitt; die Render-Seite kann ihn ueber die
	 * Key->Material-Maps (z. B. PromptFacadeMaterials) mit einem Material
	 * belegen. Greift nur, wenn kein Adress-/Landmarken-/Stil-Key aktiv ist.
	 */
	static FString GetRegionalFacadeKey(ECityRegionType Type);
};
