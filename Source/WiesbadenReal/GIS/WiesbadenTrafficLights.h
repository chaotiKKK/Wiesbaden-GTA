// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "GIS/RoadNetworkTypes.h"

#include "WiesbadenTrafficLights.generated.h"

/** Signalbegriff einer Ampelgruppe (deutsche Reihenfolge). */
UENUM(BlueprintType)
enum class ESignalAspect : uint8
{
    Red      UMETA(DisplayName = "Rot"),
    RedAmber UMETA(DisplayName = "Rot-Gelb"),
    Green    UMETA(DisplayName = "Gruen"),
    Amber    UMETA(DisplayName = "Gelb")
};

/** Parameter der Ampel-Steuerung. */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficLightSettings
{
	GENERATED_BODY()

	/**
	 * Gruenzeit der Hauptrichtung je Umlauf in Sekunden.
	 *
	 * Die Gruenzeit ist die EINGABE, der Umlauf das Ergebnis - so wie ein
	 * Signalprogramm wirklich entworfen wird. Andersherum (fester Umlauf,
	 * Gruen als Rest) frisst jede zusaetzliche Phase die Hauptrichtung auf:
	 * gemessen sank das Geradeaus-Gruen mit einer Abbiegephase in 30 s Umlauf
	 * von 9 auf 3,5 Sekunden, und der Verkehr stand (42 % Steher statt 26 %,
	 * Tempo 15 statt 19 km/h).
	 *
	 * Der Umlauf einer Kreuzung steht in FWiesbadenTrafficLight::CycleSeconds.
	 *
	 * Dieser Wert gilt fuer die GROESSTE Kreuzung. Kleine Knoten bekommen
	 * weniger (bis herunter zu MinGreenSecondsPerCycle) - eine Wohnstrassen-
	 * kreuzung mit demselben Takt wie eine sechsspurige Hauptkreuzung laesst
	 * Fahrer vor leeren Querstrassen warten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "1.0"))
	double GreenSecondsPerCycle = 15.0;

	/**
	 * Gruenzeit der Hauptrichtung an der KLEINSTEN Kreuzung (s).
	 *
	 * Untergrenze, nicht Richtwert: darunter raeumt eine Zufahrt nicht mehr
	 * zuverlaessig, und der Anteil fester Zeiten (Rot-Gelb, Gelb, Raeumzeit)
	 * am Umlauf waechst ins Absurde.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "1.0"))
	double MinGreenSecondsPerCycle = 7.0;

	/**
	 * Raster, auf das der Umlauf gerundet wird (s). 0 schaltet es ab.
	 *
	 * DIESES RASTER IST DER PREIS FUER DIE GRUENE WELLE. Eine Welle setzt
	 * voraus, dass die Ampeln einer Achse im GLEICHEN Takt laufen - sonst
	 * laeuft der Versatz binnen weniger Umlaeufe davon und die Welle zerfaellt.
	 * Wuerde jede Kreuzung ihren genauen Wunschumlauf bekommen (38,4 s hier,
	 * 41,1 s dort), waere genau das die Folge. Mit dem Raster landen
	 * aehnlich grosse Kreuzungen auf demselben Umlauf; die Groesse wirkt sich
	 * dann in der Gruen-AUFTEILUNG aus. Genau so wird es auch real gemacht:
	 * gemeinsamer Umlauf im Zug, eigene Aufteilung je Knoten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
	double CycleQuantumSeconds = 10.0;

	/**
	 * Raeumgeschwindigkeit fuer die Allrot-Zeit (km/h).
	 *
	 * Wer bei Gelb noch in der Kreuzung ist, braucht laenger, um eine breite
	 * Kreuzung zu verlassen als eine schmale. AllRedSeconds ist die
	 * Untergrenze, die Breite bestimmt den Rest.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "1.0"))
	double ClearanceSpeedKmh = 25.0;

	/** Distanz vor der Haltelinie, ab der ein Fahrzeug bei Rot stoppt (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
	double StopDistanceCm = 300.0;

	/**
	 * Deterministischer Startwert: versetzt die Phasen je Kreuzung (aus
	 * Node-Id + Seed), damit nicht alle Ampeln der Stadt synchron schalten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	int32 RandomSeed = 20260814;

    /** Rot-Gelb-Dauer vor Gruen (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
    double RedAmberSeconds = 1.0;

    /** Gelb-Dauer am Ende der Gruenzeit (s). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
    double AmberSeconds = 3.0;

    /** MINDEST-Allrotzeit nach Gelb (s); breite Kreuzungen bekommen mehr
     *  (siehe ClearanceSpeedKmh). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
    double AllRedSeconds = 2.0;

	/**
	 * Eigene Phase fuer Linksabbieger.
	 *
	 * Ohne sie teilen sich Linksabbieger die Freigabe mit dem Geradeausverkehr
	 * der GEGENRICHTUNG - im Spiel fuhren beide gleichzeitig durcheinander,
	 * weil niemand Gegenverkehr beachtet. Eine geschuetzte Phase loest das im
	 * Signalprogramm statt im Fahrverhalten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bProtectedLeftTurns = true;

	/**
	 * Konfliktfreie Freigabegruppen (an). Nur zum MESSEN abschaltbar.
	 *
	 * Aus bleibt die Faustregel Achse x Abbiegeart stehen, und zwei
	 * gleichzeitig freigegebene Verbindungen koennen sich wieder kreuzen oder
	 * in dieselbe Spur einfaedeln. Das ist kein Spielmodus, sondern der
	 * Vergleichspunkt: ohne ihn liesse sich der Preis der Konfliktfreiheit
	 * (laengere Umlaeufe) nicht gegen ihren Nutzen halten. Startschalter:
	 * -WbOhneKonfliktgruppen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bConflictFreeGroups = true;

	/**
	 * Sperrt eine gemeinsame ZIELSPUR zwei Bewegungen gegeneinander?
	 *
	 * GEMESSEN am 20.09.2026: die konfliktfreien Gruppen kosteten am
	 * Bahnhofsplatz 37 Prozent Tempo (7,8 -> 4,9 km/h), weil 6357
	 * Verbindungen ihre Wunschgruppe verlassen mussten und der Umlauf von
	 * 36 auf 51 s stieg. Ein Teil davon geht auf diese Frage.
	 *
	 * Fuer die LAUFZEITREGEL ist die gemeinsame Zielspur ein Konflikt - der
	 * Hintere wartet, bis der Vordere die Verbindung verlassen hat. Fuer eine
	 * FREIGABEGRUPPE ist sie es nicht: ein Verkehrsplaner gibt zwei
	 * einfaedelnde Stroeme gemeinsam frei, sie sortieren sich ueber Luecken,
	 * und genau dafuer gibt es die Laufzeitregel. Wer sie auch hier trennt,
	 * kauft Konfliktfreiheit mit zusaetzlichen Phasen - und jede Phase
	 * verlaengert den Umlauf fuer ALLE Zufahrten.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bSameTargetLaneBlocksGroup = false;

	/**
	 * Die am staerksten gebundenen Bewegungen zuerst einsortieren.
	 *
	 * Die erste Fassung lief in aufsteigender Verbindungs-Nummer. Das ist
	 * reproduzierbar, aber blind: wer viele Konflikte hat, findet spaet keinen
	 * Platz mehr und bekommt eine eigene Gruppe. Wer zuerst die am staerksten
	 * gebundenen setzt, laesst den leichten Rest hinterher in die vorhandenen
	 * Gruppen fallen - weniger Gruppen, weniger Phasen, kuerzerer Umlauf.
	 * Bei gleichem Grad entscheidet weiter die Nummer, damit dieselbe Stadt
	 * dasselbe Programm bekommt.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bOrderGroupsByConflictDegree = true;

	/** Gruenzeit der Abbiegephase (s). Kurz - es sind wenige Fahrzeuge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "0.0"))
	double LeftTurnGreenSeconds = 5.0;

	/**
	 * Gruene Welle: der Phasenversatz folgt dem Ort statt einem Hash.
	 *
	 * Der Versatz war bisher ein Hash aus der Knoten-Id - gut, damit nicht die
	 * ganze Stadt gleichzeitig schaltet, aber fuer den Fahrer bedeutungslos.
	 * Entlang derselben Achse ergibt der Ort geteilt durch die Auslegungs-
	 * geschwindigkeit genau die Welle: wer mit dieser Geschwindigkeit faehrt,
	 * trifft die naechste Ampel wieder bei Gruen.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights")
	bool bGreenWave = true;

	/** Auslegungsgeschwindigkeit der gruenen Welle in km/h. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TrafficLights", meta = (ClampMin = "5.0"))
	double GreenWaveSpeedKmh = 50.0;
};

/**
 * Ein Zeitfenster im Signalprogramm einer Kreuzung.
 *
 * Gleich lange Fenster genuegen nicht, sobald es Abbiegephasen gibt: bei vier
 * Phasen in 30 s Zyklus blieben je 7,5 s, davon 6 s fuer Rot-Gelb, Gelb und
 * Raeumzeit - eineinhalb Sekunden Gruen. Deshalb traegt jede Phase ihre eigene
 * Dauer.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenSignalPhase
{
	GENERATED_BODY()

	/** Richtungsgruppe, die in diesem Fenster freigegeben wird. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 Group = 0;

	/** Laenge des Fensters inklusive Rot-Gelb, Gelb und Raeumzeit (s). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	float DurationSeconds = 0.0f;

	/**
	 * Enthaelt diese Gruppe AUSSCHLIESSLICH Linksabbieger?
	 *
	 * Wird dort gesetzt, wo es aus den Bewegungen berechnet wird, und von
	 * der Statistik gelesen - statt an zwei Stellen aus der Gruppennummer
	 * geraten zu werden. Die Nummer taugt dafuer seit der Konfliktfaerbung
	 * nicht mehr: sie vergibt aufsteigend, Geradeausverkehr landet auf
	 * ungeraden Nummern, und ungerade Nummern ueber 3 gibt es ueberhaupt
	 * erst seit der Faerbung.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	bool bLeftTurnOnly = false;
};

/** Eine einzelne Ampel an einer Kreuzung (datenrein). */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficLight
{
	GENERATED_BODY()

	/** OSM-Kreuzungsknoten (NodeId aus FRoadIntersection). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int64 NodeId = 0;

	/** Position der Kreuzung in Weltkoordinaten (cm). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	FVector Location = FVector::ZeroVector;

	/**
	 * Anzahl der Richtungsgruppen dieser Kreuzung.
	 *
	 * Zwei Achsen mal zwei Freigaben: Gruppe = Achse * 2 + (Linksabbieger ?
	 * 1 : 0). Geradeaus und rechts teilen sich eine Gruppe - beide kreuzen
	 * keinen Gegenverkehr.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 GroupCount = 4;

	/**
	 * Das Signalprogramm dieser Kreuzung, der Reihe nach.
	 *
	 * Kreuzungen OHNE Linksabbieger bekommen gar keine Abbiegephase - sonst
	 * stuenden 22 der 30 Sekunden fuer eine Bewegung, die es dort nicht gibt.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	TArray<FWiesbadenSignalPhase> Phases;

	/** Summe der Phasendauern (s) - der tatsaechliche Umlauf dieser Kreuzung. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double CycleSeconds = 0.0;

	/**
	 * Groesse dieser Kreuzung, 0 (Wohnstrasse) bis 1 (Hauptknoten).
	 *
	 * Steht hier, damit im Spiel nachvollziehbar ist, WARUM eine Kreuzung
	 * ihren Takt hat - ohne das Mass ist ein Umlauf von 30 s gegen 60 s nur
	 * eine Zahl, die man nicht pruefen kann.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	float SizeScore = 0.5f;

	/** Gruenzeit der Hauptrichtung dieser Kreuzung (s). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double GreenSeconds = 0.0;

	/** Deterministischer Phasen-Offset in Sekunden (aus NodeId + Seed). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double PhaseOffsetSeconds = 0.0;

	/** Connections dieser Kreuzung -> Richtungsgruppen-Id (Netz-Indizes). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	TMap<int32, int32> ConnectionGroups;
};

/**
 * Datenreine Ampel-Steuerung (kein Welt-/Actor-Zugriff, deterministisch,
 * testbar).
 *
 *  - Uebernimmt OSM-Kreuzungen mit EIntersectionControl::TrafficSignals
 *    (vom RoadNetworkGenerator aus highway=traffic_signals gesetzt).
 *  - Jede Ampel-Kreuzung teilt ihre Connections in Richtungsgruppen (je
 *    Gruppe ein Zeitfenster im Zyklus); die Gruppen schalten deterministisch
 *    aus ElapsedSeconds + per-Kreuzung-Offset - nie zwei Achsen gleichzeitig.
 *  - IsConnectionGreen(Index) fragt den aktuellen Zustand einer Verbindung
 *    ab; die Verkehrs-Simulation stoppt damit Fahrzeuge vor der Haltelinie.
 *
 * Initialisierung: Initialize(Network, Settings) nach dem Laden der Stadt.
 * Die Ampel-Simulation wird pro Tick fortgeschritten (Tick(DeltaSeconds)).
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenTrafficLightSystem
{
	GENERATED_BODY()

	/** Baut die Ampeln aus den TrafficSignals-Kreuzungen des Netzes. */
	void Initialize(const FRoadNetwork& InNetwork, const FWiesbadenTrafficLightSettings& InSettings);

	/**
	 * Temporaries ablehnen: Initialize merkt sich nur einen Zeiger auf das
	 * Netz. Siehe die ausfuehrliche Begruendung an
	 * FWiesbadenTrafficSimulation::Initialize - dort hat genau dieser Fehler
	 * einen Absturz an voellig anderer Stelle erzeugt.
	 */
	void Initialize(FRoadNetwork&& InNetwork, const FWiesbadenTrafficLightSettings& InSettings) = delete;

	/** Setzt das System zurueck (keine Ampeln, Zeit 0). */
	void Reset();

	/** Schreitet die Ampel-Zeit fort (deterministisch). */
	void Tick(float DeltaSeconds);

	/**
	 * STRASSENACHSE einer Anfahrt aus ihrer Peilung (datenrein, testbar).
	 *
	 * Die Peilung kommt aus Atan2(dY, dX): 0 Grad ist Ostfahrt, 90 Grad
	 * Nordfahrt. Geteilt wird modulo 180, damit die BEIDEN Richtungen
	 * derselben Strasse dieselbe Achse tragen - Ost und West die Achse 0,
	 * Nord und Sued die Achse 1.
	 *
	 * Wer einen Ampelzustand fuer eine Fahrtrichtung braucht, MUSS diese
	 * Fassung benutzen - eine zweite, gleich aussehende Rechnung an anderer
	 * Stelle laeuft frueher oder spaeter auseinander.
	 */
	static int32 AxisForBearing(double BearingDeg);

	/**
	 * Richtungsgruppe aus Achse und Abbiegerichtung (datenrein, testbar).
	 *
	 * Geradeaus und rechts teilen sich die Gruppe der Achse; Linksabbieger
	 * bekommen eine eigene.
	 */
	static int32 GroupForApproach(int32 Axis, bool bLeftTurn);

	/** True, wenn diese Abbiegeart eine eigene Linksphase braucht. */
	static bool IsLeftTurn(ETurnType Turn);

	/**
	 * Macht die Richtungsgruppen einer Kreuzung KONFLIKTFREI.
	 *
	 * Achse und Abbiegeart sind nur eine Faustregel. Sie trifft die
	 * Regelkreuzung gut, aber nicht den funfarmigen Knoten, die schiefe
	 * Einmuendung oder die Stelle, an der zwei Zufahrten in dieselbe Spur
	 * einfaedeln. Weil je Phase genau EINE Gruppe freigegeben wird, ist jeder
	 * Konflikt INNERHALB einer Gruppe ein gleichzeitig freigegebenes
	 * Begegnungspaar - und das ist genau der Fall, den eine Ampel verhindern
	 * soll.
	 *
	 * Geprueft wird mit FWiesbadenTrafficSimulation::DoConnectionsConflict -
	 * derselben Rechnung, mit der die Simulation ihre Kreuzungsregel baut.
	 * Zwei Rechnungen fuer dieselbe Frage laufen auseinander; diese eine
	 * kennt beide Faelle: sich schneidende Wege UND gemeinsame Zielspur.
	 *
	 * Verfahren: eine gierige Faerbung in aufsteigender Verbindungsnummer
	 * (deterministisch). Jede Verbindung behaelt ihre Wunschgruppe, wenn dort
	 * kein Konfliktpartner sitzt; sonst nimmt sie die naechste freie, notfalls
	 * eine neue. Die Regelkreuzung bleibt damit bei ihren vier Gruppen.
	 *
	 * Rueckgabe: die Zahl der benutzten Gruppen (also das neue GroupCount).
	 */
	static int32 MakeGroupsConflictFree(
		const FRoadNetwork& InNetwork, TMap<int32, int32>& InOutGroups,
		bool bSameTargetLaneBlocksGroup = false,
		bool bOrderGroupsByConflictDegree = true);

	/**
	 * Wie viele Verbindungen die Faustregel verlassen mussten (Diagnose).
	 *
	 * Ohne diese Zahl liesse sich nicht beurteilen, ob die Konfliktfreiheit
	 * ein paar Sonderfaelle kostet oder den halben Stadtplan umbaut.
	 */
	void GetConflictStatistics(int32& OutMovedConnections, int32& OutExtraGroups,
		int32& OutMaxGroupsAtOneLight) const;

	/**
	 * Wie viele Kreuzungen eine eigene Abbiegephase bekommen haben, und wie
	 * lang der Umlauf im Mittel ist (Diagnose).
	 *
	 * Eine Abbiegephase kostet Umlaufzeit, und die zahlen ALLE Richtungen.
	 * Ohne diese beiden Zahlen laesst sich nicht beurteilen, ob der laengere
	 * Umlauf die Konfliktfreiheit wert ist.
	 */
	/**
	 * Kennzahlen der Signalprogramme.
	 *
	 * Die SPANNE steht mit drin, weil der Mittelwert allein die Frage nicht
	 * beantwortet, um die es hier geht: solange jede Kreuzung denselben Takt
	 * hatte, sah ein Mittelwert von 42 s genauso aus wie 26..68 s. Erst
	 * Minimum und Maximum zeigen, ob sich der Umlauf wirklich nach der
	 * Groesse richtet.
	 */
	void GetProgramStatistics(int32& OutWithLeftPhase, double& OutMeanCycleSeconds,
		double& OutMinCycleSeconds, double& OutMaxCycleSeconds) const;

	/**
	 * Koennen zwei Verbindungen GLEICHZEITIG gruen sein?
	 *
	 * Das Signalprogramm gibt je Phase genau eine Richtungsgruppe frei. Zwei
	 * Verbindungen verschiedener Gruppen DERSELBEN Ampel treffen sich deshalb
	 * nie - dort braucht es keine zweite Absicherung durch die
	 * Kreuzungskonflikt-Regel, und eine zweite Absicherung kostet dort nur
	 * Fluss.
	 *
	 * True (also "koennte zusammentreffen") liefert die Funktion bewusst auch
	 * im Zweifel: ohne Ampel, bei verschiedenen Ampeln oder wenn einer
	 * Verbindung die Gruppe fehlt (die gilt als dauerhaft gruen).
	 */
	bool CanBeGreenTogether(int32 ConnectionA, int32 ConnectionB) const;

	/** Anzahl der Ampeln (TrafficSignals-Kreuzungen). */
	int32 GetTrafficLightCount() const { return Lights.Num(); }

	/** True, wenn die Kreuzung eine Ampel besitzt. */
	bool HasTrafficLightAt(int64 NodeId) const;

	/**
	 * True, wenn die Verbindung (Index in FRoadNetwork::Connections) gerade
	 * gruen ist. Connections ohne zugehoerige Ampel sind immer gruen (keine
	 * Einschraenkung).
	 */
	bool IsConnectionGreen(int32 ConnectionIndex) const;

    /** Signalbegriff einer Verbindung (Rot/RotGelb/Gruen/Gelb). */
    ESignalAspect GetConnectionAspect(int32 ConnectionIndex) const;

    /** Signalbegriff einer Richtungsgruppe (0..GroupCount-1). */
    ESignalAspect GetGroupAspect(int32 LightIndex, int32 Group) const;

	/** True, wenn diese Verbindung ueberhaupt von einer Ampel kontrolliert wird
	 *  (Kreuzung mit TrafficSignals). Diagnose: unterscheidet "keine Ampel an der
	 *  Verbindung" von "Ampel steht auf gruen". */
	bool IsConnectionControlled(int32 ConnectionIndex) const { return ConnectionToLight.Contains(ConnectionIndex); }

	/**
	 * Index der Ampel (in Lights), die diese Verbindung steuert, sonst
	 * INDEX_NONE. Der Wert IST der Index in Lights - nicht in
	 * FRoadNetwork::Intersections. Genau diese Verwechslung liess in der
	 * echten Stadt jede Verbindung gruen erscheinen, weil nur ~1073 der
	 * ~20213 Kreuzungen Ampeln sind und der Intersections-Index daher meist
	 * ausserhalb von Lights lag.
	 */
	int32 GetLightIndexForConnection(int32 ConnectionIndex) const
	{
		const int32* Found = ConnectionToLight.Find(ConnectionIndex);
		return Found ? *Found : INDEX_NONE;
	}

	/**
	 * True, wenn AKTUELL mindestens eine kontrollierte Verbindung rot ist
	 * (Frueh-Ausstieg beim ersten Rot). VerkehrsUNABHAENGIGE Diagnose-Sonde: die
	 * Verkehrs-Simulation beobachtet damit ueber ihren eigenen Ampel-Zeiger, ob
	 * das System ueberhaupt Rot-Phasen erzeugt und an sie durchreicht - so faellt
	 * der "SetTrafficLightSystem nie gerufen"-Bug auf, auch wenn am stationaeren
	 * Spawn zu wenige Fahrzeuge eine Ampel anfahren. Bei staffelphasigen Kreuzungen
	 * (Gruen < Zyklus) ist fast immer irgendeine kontrollierte Verbindung rot.
	 */
	bool AnyControlledConnectionRed() const;

	/** Einstellungen (fuer Diagnose/HUD). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	FWiesbadenTrafficLightSettings Settings;

	/** Aktive Ampeln (je TrafficSignals-Kreuzung eine). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	TArray<FWiesbadenTrafficLight> Lights;

	/** Verbindungen, die wegen eines Konflikts ihre Wunschgruppe verlassen haben. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 ConflictMovedConnections = 0;

	/** Gruppen ueber die vier der Faustregel hinaus (Summe ueber alle Ampeln). */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 ConflictExtraGroups = 0;

	/** Groesste Gruppenzahl an einer einzelnen Kreuzung. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	int32 MaxGroupsAtOneLight = 0;

	/** Fortgeschrittene Ampel-Zeit in Sekunden. */
	UPROPERTY(BlueprintReadOnly, Category = "TrafficLights")
	double ElapsedSeconds = 0.0;

private:
	/** Richtungsgruppe einer Connection aus Spur-Richtung und Abbiegeart. */
	int32 ComputeGroupIndex(const FRoadLane& Lane, ETurnType Turn) const;

	/** Baut das Signalprogramm einer Kreuzung aus ihren Richtungsgruppen. */
	void BuildSignalProgram(FWiesbadenTrafficLight& Light,
		const FRoadIntersection& Intersection) const;

	/** Phasenversatz: gruene Welle entlang der Hauptachse, sonst Hash. */
	double ComputePhaseOffset(const FRoadIntersection& Intersection,
		const FRoadNetwork& InNetwork, double CycleSeconds) const;

	/** Rangfolge der Strassenklassen - nur zum Finden der Hauptachse. */
	static double RoadClassRank(EOSMHighwayType Type);

	// -- Groesse einer Kreuzung -> Zeiten des Signalprogramms -----------------
	//
	// Alles statisch und datenrein: die Zuordnung Groesse -> Zeit ist die
	// eigentliche Entscheidung und wird direkt geprueft (Traffic.UmlaufGroesse),
	// nicht ueber ein aufgebautes Netz hinweg erraten.

public:
	/**
	 * Volle Fahrbahnbreite der breitesten Zufahrt in Metern.
	 *
	 * Das ist der Weg, den ein Fahrzeug quer durch die Kreuzung zuruecklegt,
	 * und zugleich ein gutes Mass fuer die Zahl der Fahrstreifen, die je
	 * Freigabe abfliessen. Liefert 0, wenn die Kreuzung keine Armdaten hat.
	 */
	static double WidestApproachMeters(const FRoadIntersection& Intersection);

	/**
	 * Groesse einer Kreuzung als Zahl 0..1.
	 *
	 * Zwei Dinge machen eine Kreuzung gross: die Fahrbahnbreite (wie viel
	 * abfliesst und wie weit zu raeumen ist) und die Zahl der Arme. Die Breite
	 * wiegt schwerer - ein fuenfarmiger Knoten aus Wohnstrassen bleibt klein.
	 *
	 * Ohne Armdaten (synthetische Netze) wird 0,5 geliefert: eine mittlere
	 * Kreuzung, statt aus fehlenden Daten eine Groesse zu erfinden.
	 */
	static double JunctionSize01(const FRoadIntersection& Intersection);

	/** Gruenzeit der Hauptrichtung fuer eine Kreuzung dieser Groesse (s). */
	static double GreenSecondsFor(const FWiesbadenTrafficLightSettings& InSettings,
		double Size01);

	/** Gruenzeit der Abbiegephase fuer eine Kreuzung dieser Groesse (s). */
	static double LeftGreenSecondsFor(const FWiesbadenTrafficLightSettings& InSettings,
		double Size01);

	/** Allrot-Raeumzeit: Querungsweg durch Raeumgeschwindigkeit, mind. AllRedSeconds. */
	static double ClearanceSecondsFor(const FWiesbadenTrafficLightSettings& InSettings,
		double WidthMeters);

	/**
	 * Rundet einen Wunschumlauf auf das gemeinsame Raster (CycleQuantumSeconds).
	 *
	 * @param RequiredCycleSeconds Was der Umlauf mindestens tragen muss (feste
	 *        Zeiten, Abbiegephasen, Mindestgruen). Darunter wird AUFgerundet
	 *        statt kaufmaennisch - sonst liegt der tatsaechliche Umlauf
	 *        zwischen zwei Rasterstufen, und genau diese Ampel laeuft der
	 *        gruenen Welle davon.
	 */
	static double QuantiseCycle(const FWiesbadenTrafficLightSettings& InSettings,
		double DesiredCycleSeconds, double RequiredCycleSeconds = 0.0);

private:

	/** Deterministischer FNV-1a-Hash ueber zwei uint32. */
	static uint32 Hash2(uint32 A, uint32 B);

	/** 0..0.999 aus einem Hash - fuer den Phasen-Offset. */
	static float HashFraction(uint32 Hash);

	// Nicht-reflektierte Laufzeit-Daten.
	const FRoadNetwork* Network = nullptr;
	TMap<int32, int32> ConnectionToLight; // ConnectionIndex -> Lights-Index
};
