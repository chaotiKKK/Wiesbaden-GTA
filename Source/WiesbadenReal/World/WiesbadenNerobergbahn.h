// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "World/WiesbadenRailTransport.h"

#include "WiesbadenNerobergbahn.generated.h"

class UInstancedStaticMeshComponent;
class UProceduralMeshComponent;
class UWiesbadenVehicleCameraComponent;
class USceneComponent;
class UStaticMeshComponent;

/**
 * Die Nerobergbahn - Wiesbadens Wasserballast-Standseilbahn von 1888.
 *
 * Strecke und Stationen kommen aus OpenStreetMap (railway=funicular,
 * Betreiber ESWE, eroeffnet 25.09.1888): 438 m vom Nerotal hinauf zum
 * Neroberg, mit dem Viadukt im unteren Drittel. Die Streckenpunkte sind hier
 * fest einkodiert - die Bahn ist ein Einzelstueck, ein Datenpfad durch den
 * OSM-Parser waere Aufwand ohne zweiten Nutzer.
 *
 * Die beiden OSM-Linien sind die WAGENMITTEN (sie liegen auf 385 von 434 m
 * genau eine Spurweite, also 1,00 m auseinander). Daraus baut
 * BuildTrackInstances() den vorbildgemaessen Querschnitt: DREI Laufschienen
 * mit gemeinsamer Mittelschiene (+-1,00 m und 0,00 m), in der Ausweiche vier,
 * dazu zwei Riggenbach-Zahnstangen in den Wagenmitten, einen Seilkanal mit
 * Rostabdeckung in der Mitte und das Schotterbett - alles Wiederholteile aus
 * Tools/Blender/make_nerobergbahn.py, die als Instanzen gelegt werden.
 *
 * Zwei Wagen laufen im Gegenlauf am Seil, wie beim Vorbild: faehrt der eine
 * bergauf, rollt der andere talwaerts. 7,8 km/h Hoechstgeschwindigkeit
 * (maxspeed aus OSM), Haltezeit in den Stationen.
 *
 * Der Spieler kann MITFAHREN: E-Taste neben einem haltenden Wagen steigt
 * ein, E-Taste unterwegs oder in der Station steigt aus.
 *
 * Die Gleishoehen werden zur Laufzeit vom Gelaende abgetastet. Beim Start
 * ist der Neroberg meist noch nicht gestreamt - bis dahin traegt eine
 * lineare Rampe (83 m Steigung wie beim Vorbild) die Bahn, danach wird auf
 * die echten Hoehen umgebaut.
 */
UCLASS()
class WIESBADENREAL_API AWiesbadenNerobergbahn : public AActor
{
	GENERATED_BODY()

public:
	AWiesbadenNerobergbahn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Fahrgeschwindigkeit in km/h (OSM: maxspeed 7.8). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "1.0"))
	float SpeedKmh = 7.8f;

	/** Haltezeit in den Stationen, Sekunden. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "0.0"))
	float DwellSeconds = 12.0f;

	/** Einstiegsreichweite um einen haltenden Wagen, cm. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "100.0"))
	float BoardRangeCm = 450.0f;

	/** Reale Talstations-Hoehe (ESWE/Vorbild, ~160 m ue. NN) - Anker fuers NN-Datum. */
	static constexpr double RealTalstationNNMeters = 160.0;

	/**
	 * Welt-Z (cm) -> reale Hoehe ueber NN (m). Das Gelaende-Datum liegt rund
	 * 70 m unter NN; ResolveHeights kalibriert den Versatz an der abgetasteten
	 * Talstations-Hoehe. Gedacht fuer Beschilderung/Hoehenmesser - die
	 * Geometrie bleibt am Gelaende (kein Verschieben ins Absolute).
	 */
	double WorldZToNNMeters(double WorldZCm) const
	{
		return WorldZCm / 100.0 + NNDatumOffsetMeters;
	}

	/** Reale NN-Hoehen der Stationen (m), nach der Hoehenaufloesung gueltig. */
	double GetTalstationNNMeters() const { return TalstationNNMeters; }
	double GetBergstationNNMeters() const { return BergstationNNMeters; }

	/**
	 * Fahrgast an Bord? Das HUD braucht das fuer Kurbelhinweis und
	 * Wasserstandsanzeige: der Wagen hat keine Tueren und keinen Motor -
	 * bedient wird er allein ueber die Handkurbel (Wasserschieber).
	 */
	bool IsPlayerRiding() const { return RideSession.IsRiding(); }

	/** Wagen mit dem Fahrgast (INDEX_NONE, wenn niemand mitfaehrt). */
	int32 GetRiddenCarIndex() const
	{
		return RideSession.IsRiding() ? RideSession.CarIndex : INDEX_NONE;
	}

	/**
	 * Wasserballast eines Wagens: Fuellstand 0..1 und Schieberstellung.
	 * Ausserhalb 0..1 (noch keine Wagen gebaut) liefert die Funktion false.
	 */
	bool GetCarWater(int32 Index, float& OutFuellstand, bool& bOutSchieberOffen) const;

private:
	struct FTrackPoint
	{
		FVector Position = FVector::ZeroVector;   // Welt, cm
		double ArcLength = 0.0;                    // ab Talstation, cm
		bool bHeightResolved = false;
	};

	struct FTrack
	{
		TArray<FTrackPoint> Points;
		double TotalLength = 0.0;
	};

	/** Baut die Weltpunkte beider Gleise aus den OSM-Koordinaten. */
	void BuildTracks();

	/** Tastet fehlende Gleishoehen vom Gelaende ab; true, wenn neu geloest. */
	bool ResolveHeights();

	/**
	 * Baut die Erdbauwerke (Damm, Viaduktwaende, Gelaender) als Bandgeometrie
	 * und legt danach die Gleisbauteile als Instanzen auf die Trasse.
	 */
	void BuildTrackMeshes();

	/**
	 * Legt Schienen, Zahnstangen, Seilkanal, Schwellen und Schotterbett als
	 * Instanzen entlang der Trassenmitte.
	 *
	 * Die Querlage folgt dem gemessenen Abstand der beiden Wagenmitten: bei
	 * einer Spurweite (Normalfall) fallen die beiden inneren Schienen auf
	 * dieselbe Lage bei 0 und werden EINMAL gelegt - das ist die gemeinsame
	 * Mittelschiene des Vorbilds. In der Ausweiche laufen sie auseinander.
	 */
	void BuildTrackInstances();

	/** Punkt und Richtung bei Bogenlaenge s auf einem Gleis. */
	void SampleTrack(const FTrack& Track, double S, FVector& OutPos, FVector& OutTangent) const;

	/** Legt einen Wagen als StaticMesh-Komponente an (SM_WbNbWagen). */
	USceneComponent* BuildCar(const TCHAR* Name, int32 Index);

	/**
	 * Bewegliche Teile und Wasserballast eines Wagens.
	 *
	 * Zeiger, Schwimmer und Kurbelarm sind EIGENE Meshes: der Actor setzt sie
	 * (Anker und Neigung wie im Blender-Mesh, siehe CarTiltGrade/CarTachoAnchor
	 * in WiesbadenNerobergbahn.cpp) und bewegt sie im Betrieb - die Nadel mit
	 * der Geschwindigkeit, der Schwimmer mit dem Fuellstand, der Arm beim
	 * Bedienen des Wasserschiebers.
	 */
	struct FWbCarDetail
	{
		UStaticMeshComponent* Zeiger = nullptr;
		UStaticMeshComponent* Schwimmer = nullptr;
		UStaticMeshComponent* Kurbel = nullptr;

		/** Wasserballast: 0 = leer (Skalenmarke 10), 1 = voll (Marke 40). */
		float Fuellstand = 0.75f;

		/** Wasserschieber offen? Wird mit der Kurbel umgeschaltet. */
		bool bSchieberOffen = false;

		/** Aktuelle Stellung des Kurbelarms im Bau-System (Grad, 0 = senkrecht). */
		float Kurbelwinkel = -26.0f;

		/**
		 * Stellung der Nadel auf der Scheibe, Bildwinkel in Grad: 225 = Skala
		 * Null (links oben), -45 = Skalenende 10 km/h. Wird in
		 * UpdateCarDetails gesetzt und ist das Gegenstueck zum Zeiger im Bild
		 * (Pruefung Nadelstellung gegen Log, siehe -WbWagenlog).
		 */
		float Zeigerwinkel = 225.0f;

		/** Position der Wagenmitte auf dem Gleis und Gleislaenge (cm). */
		double Bahnposition = 0.0;
		double Bahnlaenge = 0.0;
	};

	/** Zustand beider Wagen (Index 0 = Wagen A). */
	FWbCarDetail CarDetail[2];

	/** Setzt Nadel, Schwimmer und Kurbelarm nach Zustand und Geschwindigkeit. */
	void UpdateCarDetails(int32 Index, double S, double Laenge, float Kmh,
		float DeltaSeconds);

	/** Kurbel bedienen: Wasserschieber auf/zu (TON 13:13). */
	void ToggleWaterValve(int32 Index);

	/**
	 * Setzt Tal-/Bergstation und Viadukt auf die aufgeloeste Trasse.
	 *
	 * Erst nach der Gelaendeabtastung: vorher stuenden die Bauwerke auf der
	 * Rueckfallrampe weit unter dem Hang. Laeuft genau einmal.
	 */
	void PlaceStructures();

	/** Kamera des mitfahrenden Spielers ueber das gemeinsame Fahrzeug-Rig. */
	void CreatePassengerCamera();
	void DestroyPassengerCamera();

	/** Ein- oder Aussteigen des Spielers. */
	void ToggleBoarding();

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	USceneComponent* Root = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UProceduralMeshComponent* TrackMesh = nullptr;

	/**
	 * Gleisbauteile als Instanzen (Wiederholteile je 2 m bzw. 6 m).
	 * Reihenfolge: Schienen, Zahnstangen, Seilkanal, Schwellen, Schotterbett.
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UInstancedStaticMeshComponent* GleisSchienen = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UInstancedStaticMeshComponent* GleisZahnstangen = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UInstancedStaticMeshComponent* GleisSeilkanal = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UInstancedStaticMeshComponent* GleisSchwellen = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UInstancedStaticMeshComponent* GleisBett = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	USceneComponent* CarA = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	USceneComponent* CarB = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UStaticMeshComponent* Talstation = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UStaticMeshComponent* Bergstation = nullptr;

	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	UStaticMeshComponent* Viadukt = nullptr;

	/**
	 * Stuetzpfeiler unter der Trasse. Zur Laufzeit erzeugt (Anzahl haengt von
	 * Streckenlaenge und Gelaende ab), wo das Gleis mehr als SupportGapMinCm ueber
	 * dem Terrain liegt - sonst "haengen die Schienen in der Luft".
	 */
	UPROPERTY(VisibleAnywhere, Category = "Wiesbaden|Bahn")
	TArray<TObjectPtr<UStaticMeshComponent>> SupportPillars;

	/** Ab dieser Trasse-ueber-Terrain-Hoehe (cm) wird ein Pfeiler gesetzt. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "0.0"))
	double SupportGapMinCm = 150.0;

	/** Abstand der Pfeiler entlang der Trasse (cm). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn", meta = (ClampMin = "200.0"))
	double SupportSpacingCm = 900.0;

	/** Bauwerke bereits gesetzt? */
	bool bStructuresPlaced = false;

	FTrack TrackA;
	FTrack TrackB;

	/** Fahrposition des Wagens A ab Talstation, cm. B laeuft gegenlaeufig. */
	double CablePosition = 0.0;

	/** +1 bergauf (Wagen A), -1 talwaerts, 0 Haltezeit. */
	int32 Direction = +1;

	/** Restliche Haltezeit. */
	float DwellRemaining = 0.0f;

	/** Alle Hoehen aus dem Gelaende geloest und Geometrie neu gebaut? */
	bool bHeightsFinal = false;

	/** NN-Datum: Versatz Welt-Z(m) -> reale Hoehe ue. NN, in ResolveHeights aus
	 *  der Talfuss-Hoehe kalibriert (Gelaende-Datum liegt ~70 m unter NN). */
	double NNDatumOffsetMeters = 0.0;
	double TalstationNNMeters = RealTalstationNNMeters;
	double BergstationNNMeters = 0.0;

	/** Naechster Abtastversuch fuer die Hoehen. */
	float HeightRetryRemaining = 0.0f;

	/** Besitz- und Zustandsdaten der aktuellen Fahrt. */
	WiesbadenRailTransport::FWiesbadenRideSession RideSession;

	/** Flanke der Einstiegstaste. */
	bool bBoardKeyHeld = false;

	/** Flanke der Kurbeltaste (Wasserschieber). */
	bool bCrankKeyHeld = false;


	/** Gemeinsames Kamera-Rig fuer Follow/Orbit/Cockpit und Mausradzoom. */
	UPROPERTY(Transient)
	UWiesbadenVehicleCameraComponent* PassengerCamera = nullptr;

	/**
	 * Entwicklungshilfe -WbMitfahr=<Sekunden>: laesst den Spieler nach so
	 * vielen Sekunden selbsttaetig in Wagen A einsteigen. Negativ = aus.
	 *
	 * Damit laesst sich die Mitfahrt ohne Tastendruck ausloesen - noetig, um
	 * den eingerichteten Innenraum aufzunehmen (der Spielstart erfolgt am
	 * Steuer eines Autos, siehe -WbZuFuss).
	 */
	float DevRideAfterSeconds = -1.0f;

	/** True, sobald der Entwicklungs-Einstieg ausgefuehrt wurde. */
	bool bDevRideDone = false;

	/**
	 * Entwicklungshilfe -WbKurbel=<Sekunden>: dreht nach so vielen Sekunden
	 * die Kurbel (Wasserschieber auf). Negativ = aus. Damit lassen sich
	 * Schwimmerstellung und Kurbelarm ohne Tastendruck aufnehmen.
	 */
	float DevCrankAfterSeconds = -1.0f;

	/** True, sobald die Entwicklungs-Kurbel gedreht wurde. */
	bool bDevCrankDone = false;

	/**
	 * Entwicklungshilfe -WbWagenlog=<Sekunden>: schreibt den Zustand der
	 * beweglichen Teile (Nadel, Schwimmer, Kurbel) im angegebenen Abstand in
	 * das Log. Die Zahlen sind das Gegenstueck zu den Pixeln einer Aufnahme -
	 * Nadelstellung und Kurbelwinkel lassen sich ohne sie nicht pruefen.
	 * 0 oder negativ = aus.
	 */
	float DevCarLogInterval = 0.0f;

	/** Naechster Zeitpunkt fuer die Zustandszeile. */
	float NextDevCarLogTime = 0.0f;

	/** Entwicklungshilfe: Profil, Clearance und Segmentgrenzen anzeigen. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Bahn|Debug")
	bool bDebugRailway = false;

#if !UE_BUILD_SHIPPING
	void DrawRailwayDebug();
#endif
};
