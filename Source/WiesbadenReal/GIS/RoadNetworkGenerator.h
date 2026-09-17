// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GIS/IHeightSampler.h"
#include "GIS/RoadNetworkTypes.h"
#include "ProceduralMeshComponent.h"
#include "RoadNetworkGenerator.generated.h"

class UGeoCoordinateConverter;
class URoadTypeLibrary;

/** Materialkanal eines erzeugten Mesh-Abschnitts. */
UENUM(BlueprintType)
enum class ERoadMeshChannel : uint8
{
	Carriageway		UMETA(DisplayName = "Fahrbahn"),
	Sidewalk		UMETA(DisplayName = "Gehweg"),
	Kerb			UMETA(DisplayName = "Bordstein"),
	Intersection	UMETA(DisplayName = "Kreuzungsflaeche"),
	LaneMarking		UMETA(DisplayName = "Fahrbahnmarkierung"),
	Crossing		UMETA(DisplayName = "Zebrastreifen"),
	Cycleway		UMETA(DisplayName = "Radweg"),

	/**
	 * Boeschung: der Rock vom Fahrbahnrand hinunter aufs Gelaende.
	 *
	 * Bewusst ein EIGENER Kanal. Die Boeschungen lagen zuvor im Kanal
	 * "Fahrbahn", und das hat eine Messung verdorben: Die Verdeckungs-
	 * Statistik tastet Fahrbahn-Vertices ab und vergleicht sie mit der
	 * Gelaendehoehe - Boeschungs-Vertices laufen aber bauartbedingt bis auf
	 * Gelaendehoehe hinunter und zaehlten deshalb ausnahmslos als "verdeckt".
	 * Gemeldet wurden so 8 bis 14 Prozent verdeckte Fahrbahn, die es in dieser
	 * Form nie gab.
	 */
	Embankment		UMETA(DisplayName = "Boeschung"),
	MAX				UMETA(Hidden)
};

/** Ein Mesh-Abschnitt, direkt an UProceduralMeshComponent::CreateMeshSection uebergebbar. */
USTRUCT()
struct WIESBADENREAL_API FRoadMeshSection
{
	GENERATED_BODY()

	ERoadMeshChannel Channel = ERoadMeshChannel::Carriageway;

	/** Oberflaechenart - bestimmt die Materialinstanz innerhalb des Kanals. */
	EOSMSurfaceType Surface = EOSMSurfaceType::Asphalt;

	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FColor> VertexColors;
	TArray<FProcMeshTangent> Tangents;

	bool IsEmpty() const { return Vertices.Num() == 0 || Triangles.Num() < 3; }

	/**
	 * Haengt einen anderen Abschnitt an und verschiebt dessen Indizes.
	 * Wird zum Zusammenfassen vieler Strassensegmente in wenige Draw-Calls
	 * verwendet - bei 40.000 Segmenten waere ein Mesh je Segment nicht
	 * darstellbar.
	 */
	void Append(const FRoadMeshSection& Other);
};

/** Alle erzeugten Mesh-Abschnitte, gruppiert nach Kanal und Oberflaeche. */
USTRUCT()
struct WIESBADENREAL_API FRoadMeshData
{
	GENERATED_BODY()

	TArray<FRoadMeshSection> Sections;

	int32 GetTotalVertexCount() const
	{
		int32 Count = 0;
		for (const FRoadMeshSection& Section : Sections)
		{
			Count += Section.Vertices.Num();
		}
		return Count;
	}

	int32 GetTotalTriangleCount() const
	{
		int32 Count = 0;
		for (const FRoadMeshSection& Section : Sections)
		{
			Count += Section.Triangles.Num() / 3;
		}
		return Count;
	}

	void Reset() { Sections.Reset(); }
};

/** Parameter der Strassennetz-Erzeugung. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadGenerationSettings
{
	GENERATED_BODY()

	/**
	 * Maximale Segmentlaenge in cm nach dem Resampling. Bestimmt, wie genau
	 * die Fahrbahn dem Terrain folgt. 500 cm ist der Kompromiss, bei dem am
	 * Neroberg-Anstieg keine Durchdringung mehr sichtbar ist.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double MaxSegmentLengthCm = 500.0;

	/** Douglas-Peucker-Toleranz in cm vor dem Resampling. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double SimplificationToleranceCm = 15.0;

	/** Chaikin-Glaettungsdurchlaeufe fuer Kurven. 0 deaktiviert die Glaettung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	int32 SmoothingIterations = 2;

	/**
	 * Hoehe der Fahrbahndecke ueber dem Terrain in cm.
	 *
	 * Hier standen 8 cm - zu wenig. Das Landscape loest mit 7,81 m je Quad
	 * auf und kann eine 6,5 m breite Fahrbahn-Rinne gar nicht abbilden;
	 * zwischen zwei Gitterpunkten zieht die lineare Interpolation die
	 * Oberflaeche ueber die Fahrbahn. Die Strasse war dadurch im Spiel
	 * praktisch unsichtbar.
	 *
	 * Empirisch gemessen an der Platter Strasse: Gelaende 11300, Fahrbahn
	 * 11308 (unsichtbar), Bordstein 11315 (sichtbar). Die Schwelle liegt also
	 * zwischen 8 und 15 cm. 20 cm halten Abstand davon, ohne dass die Fahrbahn
	 * als Damm auffaellt - Gehweg und Bordstein sitzen relativ dazu und wandern
	 * mit.
	 *
	 * Hier standen zuvor 30 cm. Zusammen mit 45 cm Gelaende-Aushub und der
	 * Minimum-Regel der Einebnung schwebte die Fahrbahn gemessen 91,6 cm ueber
	 * dem Boden. Ein Bordstein ist 12 cm hoch; bei einem knappen Meter Luft
	 * schaut man unter die Strasse.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double RoadSurfaceOffsetCm = 20.0;

	/** Hoehe der Markierungen ueber der Fahrbahn in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double MarkingOffsetCm = 1.5;

	/** Zusaetzlicher Kreuzungsradius ueber die Armbreiten hinaus, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double IntersectionMarginCm = 100.0;

	/** Hoehenversatz je OSM-Layer bei Bruecken und Tunneln, in cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double LayerHeightCm = 550.0;

	/** Wenn true, werden Fusswege und Radwege als eigene Geometrie erzeugt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	bool bGenerateFootways = true;

	/** Wenn true, werden Fahrbahnmarkierungen erzeugt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	bool bGenerateLaneMarkings = true;

	/**
	 * Rand- und StVO-Grenzlinien aus den aufgeloesten Spur-Attributen
	 * (FRoadSegment::LaneAttributes) erzeugen: Fahrbahnbegrenzung am Aussenrand
	 * klassifizierter Strassen plus je Grenze der korrekte Stil (durchgezogen/
	 * gestrichelt, Breit-/Schmalstrich). Ist dies aus, faellt BuildLaneMarkings
	 * auf die alte Heuristik zurueck (nur Innengrenzen, Richtungstrennung
	 * durchgezogen) - so laesst sich das Modul einzeln backen (Spec Abschnitt 9).
	 * Greift nur zusammen mit bGenerateLaneMarkings.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	bool bGenerateEdgeLines = true;

	/** Wenn true, werden Gehwege mit Bordstein erzeugt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	bool bGenerateSidewalks = true;

	/**
	 * Boeschung an der Fahrbahnkante erzeugen.
	 *
	 * Das Gelaende hat 7,81 m Rasterweite, die Fahrbahn ist rund 7 m breit.
	 * Am Hang traegt die Einebnung deshalb nur ein bis zwei Rasterpunkte; das
	 * Gelaende daneben faellt weg, und die Fahrbahn steht als Damm mit freier
	 * Kante darueber. Gemessen liegen 90 % der Fahrbahn unter 37 cm ueber
	 * Grund, das oberste Prozent aber bis 28 m - im Spiel als abgeschnittene,
	 * schwebende Strasse zu sehen.
	 *
	 * Die Boeschung schliesst diese Kante zum Gelaende hin.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	bool bGenerateEmbankments = true;

	/**
	 * Nahe beieinanderliegende Strassenenden zu einem Knoten zusammenfassen.
	 *
	 * In OSM verbinden sich Wege ueber einen GEMEINSAMEN Knoten. Fehlt der -
	 * weil zwei Wege getrennt erfasst wurden -, enden beide Strassen im Nichts,
	 * obwohl sie sich beruehren. Gemessen liegen 1.175 von 4.000 freien Enden
	 * unter 15 m an einem fremden Ende, im Median 7,8 m, im Minimum 53 cm.
	 * Im Spiel sieht das aus, als fehle die Kreuzung.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	bool bSnapLooseRoadEnds = true;

	/** Groesster Abstand, ueber den zwei freie Enden verbunden werden (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads", meta = (ClampMin = "0.0"))
	double LooseEndSnapRadiusCm = 1200.0;

	/** Ab dieser Hoehendifferenz wird eine Boeschung erzeugt (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads", meta = (ClampMin = "1.0"))
	double EmbankmentMinHeightCm = 25.0;

	/**
	 * Groesste Boeschungshoehe (cm).
	 *
	 * Bruecken stehen konstruktionsbedingt weit ueber dem Gelaende; dort waere
	 * eine durchgehende Erdwand falsch. Sie brauchen ein eigenes Bauwerk mit
	 * Pfeilern - das ist hier NICHT geloest, die Boeschung wird nur begrenzt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads", meta = (ClampMin = "50.0"))
	double EmbankmentMaxHeightCm = 400.0;

	/** Breite einer Laengsmarkierung in cm. StVO-Regelbreite: 12 cm. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Roads")
	double MarkingWidthCm = 12.0;
};

/** Diagnose eines Generierungslaufs. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FRoadGenerationReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	bool bSuccess = false;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	FString ErrorMessage;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 ProcessedWayCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 SkippedWayCount = 0;

	/** Ways, deren Node-Referenzen ausserhalb des Extrakts lagen. */
	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 IncompleteWayCount = 0;

	/**
	 * Gruende, aus denen ein Way verworfen wurde - einzeln.
	 *
	 * SkippedWayCount allein sagt nur, DASS etwas fehlt. Der Abgleich des
	 * gebauten Netzes mit den Quelldaten ergab 1.990 fehlende Fahrbahnwege
	 * (118,6 km), darunter Stuecke von Rheinallee, Mainzer Strasse, Platter
	 * Strasse und dem Kaiser-Friedrich-Ring - und keine der vier Verwerfungs-
	 * stellen im Code liess sich ohne diese Aufteilung ausschliessen.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 SkippedUnknownTypeCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 SkippedNotDrivableCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 SkippedUnresolvedCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 SkippedTooFewPointsCount = 0;

	/**
	 * Ways, die alle Pruefungen bestanden haben und TROTZDEM kein einziges
	 * Segment ergaben.
	 *
	 * Das ist der stille Fall: Der Way wird als verarbeitet gezaehlt, seine
	 * Teilstuecke fallen aber alle in der Zerlegung durch (entartete
	 * Teilbereiche oder zu wenige Punkte nach dem Neuabtasten). Im Ergebnis
	 * fehlt die Strasse, ohne dass irgendein Zaehler anschlaegt.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 WaysWithoutSegmentCount = 0;

	/** Teilstuecke, die beim Neuabtasten unter zwei Punkte fielen. */
	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 DroppedSubSegmentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 SegmentCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 LaneCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 IntersectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 ConnectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 RestrictedConnectionCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 VertexCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	int32 TriangleCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Roads")
	double DurationSeconds = 0.0;

	FString ToString() const;
};

/**
 * Erzeugt aus OSM-Daten das befahrbare Strassennetz: Geometrie, Kreuzungen,
 * Markierungen und den Fahrspur-Graphen fuer die Verkehrs-KI.
 *
 * ABLAUF
 *  1. Ways filtern und in Weltkoordinaten projizieren
 *  2. Ways an Kreuzungsknoten in Segmente zerlegen
 *  3. Bauliche Kennwerte je Segment aufloesen (Spuren, Breite, Gehwege)
 *  4. Kreuzungen erfassen, Arme sortieren, Segmentenden kuerzen
 *  5. Fahrspur-Mittellinien erzeugen
 *  6. Fahrspur-Graph verknuepfen, Abbiegevorschriften anwenden
 *  7. Mesh-Geometrie erzeugen
 *
 * Die Schritte 1-6 sind rein datenverarbeitend und ohne Engine-Kontext
 * ausfuehrbar - dadurch sind sie im Automation-Test ohne geladenes Level
 * pruefbar. Nur Schritt 7 erzeugt Renderdaten.
 */
UCLASS(BlueprintType)
class WIESBADENREAL_API URoadNetworkGenerator : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * Erzeugt das Strassennetz.
	 *
	 * @param DataSet       Geparste OSM-Daten.
	 * @param Converter     Initialisierte Georeferenzierung.
	 * @param TypeLibrary   Strassenklassen-Definitionen.
	 * @param HeightSampler Hoehenmodell. Darf nullptr sein - dann liegt alles
	 *                      auf Z=0 (fuer Tests und Flachland-Prototypen).
	 * @param Settings      Generierungsparameter.
	 * @param OutNetwork    Ergebnis-Netzwerk.
	 * @param OutMeshData   Erzeugte Geometrie. Darf nullptr sein, wenn nur der
	 *                      Graph gebraucht wird (Server ohne Rendering).
	 */
	FRoadGenerationReport Generate(
		const FOSMDataSet& DataSet,
		const UGeoCoordinateConverter* Converter,
		const URoadTypeLibrary* TypeLibrary,
		const IHeightSampler* HeightSampler,
		const FRoadGenerationSettings& Settings,
		FRoadNetwork& OutNetwork,
		FRoadMeshData* OutMeshData);

	/** Klassifiziert eine Abbiegebeziehung anhand des Richtungswinkels. */
	static ETurnType ClassifyTurn(const FVector& IncomingDirection, const FVector& OutgoingDirection);

	/** Wandelt turn:lanes-Werte in eine ETurnIndication-Bitmaske. */
	static uint8 ParseTurnIndication(const FString& Value);

private:
	// -- Schritt 2: Zerlegung ------------------------------------------------

	/**
	 * Zaehlt, wie oft jeder Node von befahrbaren Ways referenziert wird.
	 * Nodes mit Zaehler >= 2 sind Verzweigungspunkte.
	 */
	void CountNodeReferences(const FOSMDataSet& DataSet, TMap<FOSMId, int32>& OutCounts) const;

	// -- Schritt 4: Kreuzungen ----------------------------------------------

	/**
	 * Verbindet freie Strassenenden, die dicht beieinanderliegen und
	 * aufeinander zulaufen. @return Zahl der zusammengefassten Paare.
	 */
	int32 SnapLooseRoadEnds(
		const FRoadGenerationSettings& Settings,
		FRoadNetwork& Network) const;

	void BuildIntersections(
		const FOSMDataSet& DataSet,
		const UGeoCoordinateConverter& Converter,
		const FRoadGenerationSettings& Settings,
		FRoadNetwork& Network) const;

	void TrimSegmentsAtIntersections(
		const FRoadGenerationSettings& Settings,
		FRoadNetwork& Network) const;

	// -- Schritt 5/6: Spuren und Graph --------------------------------------

	void BuildLanes(const FRoadGenerationSettings& Settings, FRoadNetwork& Network) const;

	void ConnectLanes(
		const FOSMDataSet& DataSet,
		const FRoadGenerationSettings& Settings,
		FRoadNetwork& Network,
		int32& OutRestrictedCount) const;

	/** Wertet type=restriction-Relationen aus (Abbiegeverbote). */
	void CollectTurnRestrictions(
		const FOSMDataSet& DataSet,
		TSet<TPair<int64, int64>>& OutForbiddenWayPairs) const;

	// -- Schritt 7: Geometrie ------------------------------------------------

	/**
	 * Baut eine FLAECHE (Platz, Fussgaengerzone) statt eines Bandes.
	 *
	 * Wiesbaden hat 297 solcher Flaechen in den OSM-Daten, 101 davon benannt.
	 * Als Band gebaut ergaben sie einen Pfad um sich selbst herum.
	 */
	void BuildAreaMesh(
		const FRoadSegment& Segment,
		const FRoadGenerationSettings& Settings,
		FRoadMeshData& OutMeshData) const;

	void BuildSegmentMesh(
		const FRoadNetwork& Network,
		const FRoadSegment& Segment,
		const URoadTypeLibrary& TypeLibrary,
		const IHeightSampler* HeightSampler,
		const FRoadGenerationSettings& Settings,
		FRoadMeshData& OutMeshData) const;

	/**
	 * Schliesst die Fugen an Knoten, an denen ZWEI Strassen ohne Kreuzung
	 * aufeinandertreffen.
	 *
	 * Dort wird nicht gekuerzt und keine Kreuzungsflaeche gebaut - die beiden
	 * Fahrbahnbaender stossen einfach aneinander. Ihre Endquerschnitte stehen
	 * aber jeweils senkrecht zur EIGENEN Fahrtrichtung. Knickt die Strasse am
	 * Knoten ab, bilden die beiden Enden aussen am Bogen einen Keil, und dort
	 * liegt blanker Boden.
	 *
	 * In Wiesbaden gemessen: 6.998 solcher Knoten, 1.508 davon mit einem Knick
	 * ueber 15 Grad, Keilbreite im Median 136,9 cm und bis zu 33,7 m; dazu 425
	 * Breitensprunge von im Median 3,0 m. Beim Fahren ist das der haeufigste
	 * sichtbare Riss im Strassennetz.
	 */
	void BuildBendFillers(const FRoadNetwork& Network, FRoadMeshData& OutMeshData) const;

	/** Boeschung von der Fahrbahnkante hinunter auf das Gelaende. */
	void BuildEmbankmentMesh(
		const FRoadSegment& Segment,
		const URoadTypeLibrary& TypeLibrary,
		const IHeightSampler* HeightSampler,
		const FRoadGenerationSettings& Settings,
		FRoadMeshData& OutMeshData) const;

	void BuildIntersectionMesh(
		const FRoadIntersection& Intersection,
		const FRoadGenerationSettings& Settings,
		FRoadMeshData& OutMeshData) const;

	void BuildLaneMarkings(
		const FRoadSegment& Segment,
		const URoadTypeLibrary& TypeLibrary,
		const FRoadGenerationSettings& Settings,
		FRoadMeshData& OutMeshData) const;

	/** Sucht oder erzeugt den Mesh-Abschnitt fuer Kanal und Oberflaeche. */
	static FRoadMeshSection& FindOrAddSection(
		FRoadMeshData& MeshData,
		ERoadMeshChannel Channel,
		EOSMSurfaceType Surface);

	/**
	 * Projiziert 2D-Punkte auf Terrainhoehe und addiert den Kanal-Offset.
	 * Der Layer-Offset fuer Bruecken wird hier ebenfalls angewendet.
	 */
	void ProjectToTerrain(
		const TArray<FVector2D>& Points2D,
		const IHeightSampler* HeightSampler,
		double AdditionalOffsetCm,
		int32 Layer,
		const FRoadGenerationSettings& Settings,
		TArray<FVector>& OutPoints) const;

	// -- Arbeitsdaten der laufenden Generierung -----------------------------
	//
	// Die gesamte Grundrisslogik (Zerlegen, Kuerzen, Spurversatz) rechnet in
	// 2D; erst zum Schluss wird auf Terrainhoehe projiziert. Diese
	// Zwischenergebnisse liegen als Member vor, weil sie von mehreren
	// Generierungsschritten gelesen werden und ein Durchreichen durch alle
	// Signaturen die Schnittstellen unleserlich machen wuerde.
	//
	// Generate() ist dadurch nicht wiedereintrittsfaehig. Das ist bewusst:
	// die Generierung laeuft einmal pro Level-Ladevorgang auf genau einem
	// Thread. Gleichzeitige Aufrufe werden in Generate() abgewiesen.

	// mutable, weil die Generierungsschritte als const deklariert sind: sie
	// veraendern das Ergebnis-Netzwerk (Ausgabeparameter), nicht den Zustand
	// des Generators. Diese Arrays sind reine Zwischenablage.

	/** Grundriss je Segment, Index = SegmentId. */
	mutable TArray<TArray<FVector2D>> WorkingCenterlines2D;

	/** An Kreuzungen gekuerzter Grundriss je Segment. */
	mutable TArray<TArray<FVector2D>> WorkingTrimmed2D;

	/** Wird waehrend eines laufenden Generate()-Aufrufs gesetzt. */
	bool bGenerationInProgress = false;
};
