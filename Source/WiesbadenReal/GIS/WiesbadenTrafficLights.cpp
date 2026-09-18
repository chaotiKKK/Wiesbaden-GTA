// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenTrafficLights.h"

namespace
{
	// Schutz vor degenerierten Einstellungen.
	constexpr double MinCycleSeconds = 2.0;
}

double FWiesbadenTrafficLightSystem::RoadClassRank(EOSMHighwayType Type)
{
	// Nur eine Rangfolge, um den Hauptarm einer Kreuzung zu finden - keine
	// Verkehrsmenge. Die Gewichtung des Verkehrs liegt bewusst woanders
	// (FWiesbadenTrafficSimulation::GetRoadClassWeight); hier zaehlt allein,
	// welche Strasse die Achse der gruenen Welle vorgibt.
	switch (Type)
	{
	case EOSMHighwayType::Motorway:
	case EOSMHighwayType::Trunk:			return 6.0;
	case EOSMHighwayType::Primary:			return 5.0;
	case EOSMHighwayType::Secondary:		return 4.0;
	case EOSMHighwayType::Tertiary:			return 3.0;
	case EOSMHighwayType::Unclassified:		return 2.0;
	case EOSMHighwayType::Residential:		return 1.0;
	default:								return 0.5;
	}
}

uint32 FWiesbadenTrafficLightSystem::Hash2(uint32 A, uint32 B)
{
	uint32 H = 2166136261u;
	const uint8 Bytes[8] = {
		static_cast<uint8>(A), static_cast<uint8>(A >> 8),
		static_cast<uint8>(A >> 16), static_cast<uint8>(A >> 24),
		static_cast<uint8>(B), static_cast<uint8>(B >> 8),
		static_cast<uint8>(B >> 16), static_cast<uint8>(B >> 24) };
	for (const uint8 Byte : Bytes)
	{
		H ^= Byte;
		H *= 16777619u;
	}
	return H;
}

float FWiesbadenTrafficLightSystem::HashFraction(uint32 Hash)
{
	return static_cast<float>(Hash % 1000u) / 1000.0f;
}

int32 FWiesbadenTrafficLightSystem::AxisForBearing(double BearingDeg)
{
	// Auf [0, 360) bringen, sonst faellt eine Peilung von -90 Grad in die
	// falsche Achse - und genau solche Werte liefert Atan2.
	double Normalized = FMath::Fmod(BearingDeg, 360.0);
	if (Normalized < 0.0)
	{
		Normalized += 360.0;
	}
	return (Normalized < 180.0) ? 0 : 1;
}

bool FWiesbadenTrafficLightSystem::IsLeftTurn(ETurnType Turn)
{
	// Wenden zaehlt mit: es kreuzt denselben Gegenverkehr wie das Linksabbiegen.
	return Turn == ETurnType::Left || Turn == ETurnType::UTurn;
}

int32 FWiesbadenTrafficLightSystem::GroupForApproach(int32 Axis, bool bLeftTurn)
{
	return FMath::Clamp(Axis, 0, 1) * 2 + (bLeftTurn ? 1 : 0);
}

int32 FWiesbadenTrafficLightSystem::ComputeGroupIndex(
	const FRoadLane& Lane, ETurnType Turn) const
{
	if (Lane.Centerline.Num() < 2)
	{
		return 0;
	}

	// Richtung der ankommenden Spur am Kreuzungsende (letztes Liniensegment).
	const FVector Last = Lane.Centerline.Last();
	const FVector Prev = Lane.Centerline[Lane.Centerline.Num() - 2];
	const double BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Last.Y - Prev.Y, Last.X - Prev.X));

	return GroupForApproach(AxisForBearing(BearingDeg), IsLeftTurn(Turn));
}

void FWiesbadenTrafficLightSystem::Initialize(
	const FRoadNetwork& InNetwork, const FWiesbadenTrafficLightSettings& InSettings)
{
	Network = &InNetwork;
	Settings = InSettings;
	Lights.Reset();
	ConnectionToLight.Reset();
	ElapsedSeconds = 0.0;

	// Connections einmal nach Kreuzungsknoten buendeln. Vorher suchte jede
	// Ampel das GANZE Connections-Array ab (1073 Ampeln x Hunderttausende
	// Verbindungen); jetzt ein Durchgang plus direkter Zugriff.
	TMap<int64, TArray<int32>> ConnectionsByNode;
	ConnectionsByNode.Reserve(InNetwork.Intersections.Num());
	for (int32 ConnectionIndex = 0; ConnectionIndex < InNetwork.Connections.Num(); ++ConnectionIndex)
	{
		ConnectionsByNode.FindOrAdd(InNetwork.Connections[ConnectionIndex].IntersectionNodeId)
			.Add(ConnectionIndex);
	}

	for (const FRoadIntersection& Intersection : InNetwork.Intersections)
	{
		if (Intersection.Control != EIntersectionControl::TrafficSignals)
		{
			continue;
		}

		// Index der Ampel im GEFILTERTEN Lights-Array - nicht der Laufindex
		// ueber Intersections. Genau diese Verwechslung war der Kopplungs-
		// Defekt: nur ~1073 von ~20213 Kreuzungen sind Ampeln, der
		// Intersections-Index lag also fast immer ausserhalb von Lights,
		// GetConnectionAspect fiel auf "gruen" zurueck und kein Fahrzeug
		// hielt je an Rot.
		const int32 LightIndex = Lights.Num();

		FWiesbadenTrafficLight Light;
		Light.NodeId = Intersection.NodeId;
		Light.Location = Intersection.Location;
		Light.GroupCount = 4;   // zwei Achsen mal geradeaus/links

		// Connections dieser Kreuzung einer Richtungsgruppe zuordnen.
		if (const TArray<int32>* NodeConnections = ConnectionsByNode.Find(Intersection.NodeId))
		{
			for (const int32 ConnectionIndex : *NodeConnections)
			{
				const FLaneConnection& Connection = InNetwork.Connections[ConnectionIndex];
				if (!InNetwork.Lanes.IsValidIndex(Connection.FromLaneId))
				{
					continue;
				}

						const int32 Group = ComputeGroupIndex(
					InNetwork.Lanes[Connection.FromLaneId], Connection.TurnType);
				Light.ConnectionGroups.Add(ConnectionIndex, Group);
				ConnectionToLight.Add(ConnectionIndex, LightIndex);
			}
		}

		BuildSignalProgram(Light, Intersection);
		Light.PhaseOffsetSeconds = ComputePhaseOffset(Intersection, InNetwork, Light.CycleSeconds);

		Lights.Add(MoveTemp(Light));
	}
}

namespace
{
	/**
	 * Fahrbahnbreite, ab der eine Kreuzung als klein bzw. gross gilt (m).
	 *
	 * Die Untergrenze ist keine gegriffene Zahl: FIntersectionArm::HalfWidthCm
	 * steht ohne OSM-Angabe auf 325 cm, eine gewoehnliche zweispurige Strasse
	 * ist also 6,5 m breit - das ist die kleinste Kreuzung, die im Netz
	 * vorkommt. 20 m sind rund sechs Fahrstreifen; darueber hinaus laenger zu
	 * schalten bringt nichts mehr.
	 */
	constexpr double SmallJunctionMeters = 6.5;
	constexpr double LargeJunctionMeters = 20.0;

	/** Armzahl, ab der die Zahl der Arme voll durchschlaegt. */
	constexpr int32 SmallJunctionArms = 3;
	constexpr int32 LargeJunctionArms = 6;

	/** Gewicht der Breite gegenueber der Armzahl. */
	constexpr double WidthWeight = 0.75;

	/** Groesse einer Kreuzung ohne Armdaten (synthetische Netze). */
	constexpr double UnknownJunctionSize = 0.5;

	/** Anteil der Abbiege-Gruenzeit, der an der kleinsten Kreuzung bleibt. */
	constexpr double MinLeftGreenFraction = 0.6;

	/** Kuerzester Umlauf nach dem Runden (s). */
	constexpr double MinQuantisedCycleSeconds = 20.0;
}

double FWiesbadenTrafficLightSystem::WidestApproachMeters(const FRoadIntersection& Intersection)
{
	double MaxHalfCm = 0.0;
	for (const FIntersectionArm& Arm : Intersection.Arms)
	{
		MaxHalfCm = FMath::Max(MaxHalfCm, Arm.HalfWidthCm);
	}
	// Die BREITESTE Zufahrt, nicht der Mittelwert: was zu queren ist, bestimmt
	// die groesste Strasse. Eine Hauptstrasse mit drei einmuendenden
	// Wohnstrassen ist eine grosse Kreuzung, auch wenn der Mittelwert das
	// verwischt.
	return (MaxHalfCm * 2.0) / 100.0;
}

double FWiesbadenTrafficLightSystem::JunctionSize01(const FRoadIntersection& Intersection)
{
	const double WidthM = WidestApproachMeters(Intersection);
	if (WidthM <= 0.0)
	{
		return UnknownJunctionSize;
	}

	const double WidthTerm = FMath::Clamp(
		(WidthM - SmallJunctionMeters) / (LargeJunctionMeters - SmallJunctionMeters), 0.0, 1.0);

	const double ArmTerm = FMath::Clamp(
		static_cast<double>(Intersection.GetArmCount() - SmallJunctionArms)
			/ static_cast<double>(LargeJunctionArms - SmallJunctionArms), 0.0, 1.0);

	return FMath::Clamp(WidthWeight * WidthTerm + (1.0 - WidthWeight) * ArmTerm, 0.0, 1.0);
}

double FWiesbadenTrafficLightSystem::GreenSecondsFor(
	const FWiesbadenTrafficLightSettings& InSettings, double Size01)
{
	const double MinGreen = FMath::Max(InSettings.MinGreenSecondsPerCycle, 1.0);
	const double MaxGreen = FMath::Max(InSettings.GreenSecondsPerCycle, MinGreen);
	return FMath::Lerp(MinGreen, MaxGreen, FMath::Clamp(Size01, 0.0, 1.0));
}

double FWiesbadenTrafficLightSystem::LeftGreenSecondsFor(
	const FWiesbadenTrafficLightSettings& InSettings, double Size01)
{
	// Auch die Abbiegephase folgt der Groesse, aber schwaecher: selbst an
	// einem kleinen Knoten muessen die Wartenden herauskommen.
	const double Full = FMath::Max(InSettings.LeftTurnGreenSeconds, 1.0);
	return FMath::Lerp(Full * MinLeftGreenFraction, Full, FMath::Clamp(Size01, 0.0, 1.0));
}

double FWiesbadenTrafficLightSystem::ClearanceSecondsFor(
	const FWiesbadenTrafficLightSettings& InSettings, double WidthMeters)
{
	const double SpeedMs = FMath::Max(InSettings.ClearanceSpeedKmh, 1.0) / 3.6;
	const double NeededSeconds = FMath::Max(WidthMeters, 0.0) / SpeedMs;
	return FMath::Max(FMath::Max(InSettings.AllRedSeconds, 0.0), NeededSeconds);
}

double FWiesbadenTrafficLightSystem::QuantiseCycle(
	const FWiesbadenTrafficLightSettings& InSettings, double DesiredCycleSeconds,
	double RequiredCycleSeconds)
{
	const double Quantum = InSettings.CycleQuantumSeconds;
	if (Quantum <= 0.0)
	{
		// Raster abgeschaltet: jede Kreuzung bekommt ihren Wunschumlauf - dann
		// gibt es auch keine gemeinsame Welle mehr, das ist die Zusage.
		return FMath::Max(FMath::Max(DesiredCycleSeconds, RequiredCycleSeconds), 0.0);
	}

	// Nach unten ist bei einem Umlauf Schluss, in dem ueberhaupt noch etwas
	// abfliessen kann; das Raster darf nicht auf 0 runden.
	const double Floor = FMath::Max(MinQuantisedCycleSeconds, Quantum);

	// Aufrunden, nicht nur runden, sobald der Umlauf eine Untergrenze tragen
	// muss.
	//
	// DAS IST DIE STELLE, AN DER DAS RASTER SONST GEBROCHEN WAERE: kaufmaennisch
	// gerundet konnte der Umlauf UNTER die Summe aus festen Zeiten und
	// Mindestgruen fallen. Die Gruenzeit wurde dann hochgezogen und der
	// tatsaechliche Umlauf lag zwischen zwei Rasterstufen - genau die krumme
	// Zahl, gegen die das Raster antritt. Solche Ampeln laufen gegen die Welle
	// davon, und zwar unsichtbar: die Statistik zeigt nur den Mittelwert.
	const double Needed = FMath::Max(RequiredCycleSeconds, Floor);
	const double CeilToQuantum = FMath::CeilToDouble(Needed / Quantum) * Quantum;

	const double Rounded = FMath::RoundToDouble(DesiredCycleSeconds / Quantum) * Quantum;

	return FMath::Max(Rounded, CeilToQuantum);
}

void FWiesbadenTrafficLightSystem::BuildSignalProgram(FWiesbadenTrafficLight& Light,
	const FRoadIntersection& Intersection) const
{
	Light.Phases.Reset();

	// Gibt es auf dieser Achse ueberhaupt Linksabbieger? Eine Abbiegephase fuer
	// eine Bewegung, die es an dieser Kreuzung nicht gibt, verschenkt nur
	// Umlaufzeit - und davon haengt ab, wie lange alle anderen warten.
	bool bHasLeft[2] = { false, false };
	for (const TPair<int32, int32>& Pair : Light.ConnectionGroups)
	{
		const int32 Group = Pair.Value;
		if ((Group % 2) == 1)
		{
			bHasLeft[FMath::Clamp(Group / 2, 0, 1)] = true;
		}
	}

	// -- Die Groesse der Kreuzung bestimmt die Zeiten. ----------------------
	//
	// Vorher trug jede Kreuzung dieselbe Gruenzeit, und damit denselben
	// Umlauf: die Wohnstrassenkreuzung mit zwei Fahrstreifen genauso wie der
	// sechsspurige Knoten. Fuer den Fahrer heisst das, vor einer leeren
	// Querstrasse so lange zu stehen wie vor einer Hauptachse.
	Light.SizeScore = static_cast<float>(JunctionSize01(Intersection));
	const double WidthM = WidestApproachMeters(Intersection);

	// Feste Zeiten je Phase: Rot-Gelb vorweg, Gelb und Raeumzeit hinterher.
	// Die Raeumzeit folgt der Breite - ueber eine 20-m-Kreuzung braucht man
	// laenger als ueber eine 6-m-Kreuzung.
	const double Fixed = FMath::Max(Settings.RedAmberSeconds, 0.0)
		+ FMath::Max(Settings.AmberSeconds, 0.0)
		+ ClearanceSecondsFor(Settings, WidthM);

	// Die GRUENZEITEN sind die Eingabe, der Umlauf ihre Summe. Ein fester
	// Umlauf mit Gruen als Rest laesst jede zusaetzliche Phase die
	// Hauptrichtung auffressen - nachgemessen ging das Geradeaus-Gruen mit
	// einer Abbiegephase in 30 s Umlauf von 9 auf 3,5 Sekunden zurueck.
	const bool bLeftPhases = Settings.bProtectedLeftTurns;
	const double ThroughGreen = GreenSecondsFor(Settings, Light.SizeScore);
	const double LeftGreen = LeftGreenSecondsFor(Settings, Light.SizeScore);

	int32 ThroughPhases = 0;
	int32 LeftPhases = 0;
	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		++ThroughPhases;
		if (bLeftPhases && bHasLeft[Axis])
		{
			++LeftPhases;
		}
	}

	const double DesiredCycle = ThroughPhases * (ThroughGreen + Fixed)
		+ LeftPhases * (LeftGreen + Fixed);

	// -- Umlauf auf das gemeinsame Raster. ----------------------------------
	//
	// Ohne das Raster haette jede Kreuzung ihren krummen Wunschumlauf, und die
	// gruene Welle waere hin: sie setzt voraus, dass die Ampeln einer Achse im
	// gleichen Takt laufen. Gerundet wird der Umlauf, die Differenz geht in
	// die Hauptrichtung - so wie real auch: gemeinsamer Umlauf im Zug, eigene
	// Aufteilung je Knoten.
	//
	// Mitgegeben wird, was der Umlauf MINDESTENS tragen muss: feste Zeiten,
	// Abbiegephasen und das Mindestgruen der Hauptrichtung. Sonst rundet das
	// Raster nach unten, die Gruenzeit muss hochgezogen werden, und der
	// tatsaechliche Umlauf liegt zwischen zwei Rasterstufen - dann laeuft diese
	// Ampel gegen die Welle davon.
	const double MinGreen = FMath::Max(Settings.MinGreenSecondsPerCycle, 1.0);
	const double LeftBudget = LeftPhases * (LeftGreen + Fixed);
	const double RequiredCycle = ThroughPhases * (MinGreen + Fixed) + LeftBudget;

	const double Cycle = QuantiseCycle(Settings, DesiredCycle, RequiredCycle);

	// Was nach den festen Zeiten und den Abbiegephasen uebrig bleibt, teilen
	// sich die Geradeaus-Phasen. Die Untergrenze steht hier nur noch als Netz:
	// nach RequiredCycle kann sie rechnerisch nicht mehr greifen.
	const double ThroughBudget = Cycle - LeftBudget - ThroughPhases * Fixed;
	const double AdjustedThroughGreen = FMath::Max(
		ThroughBudget / FMath::Max(ThroughPhases, 1), MinGreen);

	const double ThroughSlot = AdjustedThroughGreen + Fixed;
	const double LeftSlot = LeftGreen + Fixed;

	for (int32 Axis = 0; Axis < 2; ++Axis)
	{
		FWiesbadenSignalPhase Through;
		Through.Group = GroupForApproach(Axis, false);
		Through.DurationSeconds = static_cast<float>(ThroughSlot);
		Light.Phases.Add(Through);

		if (bLeftPhases && bHasLeft[Axis])
		{
			// NACHLAUFEND, nicht vorlaufend: erst raeumt der Geradeausverkehr
			// derselben Achse, dann biegen die Wartenden ab.
			FWiesbadenSignalPhase Left;
			Left.Group = GroupForApproach(Axis, true);
			Left.DurationSeconds = static_cast<float>(LeftSlot);
			Light.Phases.Add(Left);
		}
	}

	// Der tatsaechliche Umlauf ist die Summe der Phasen, nicht der Wunsch. Mit
	// RequiredCycle trifft sie das Raster; die Summe bleibt trotzdem die
	// massgebliche Zahl - an ihr laeuft die Schaltung, und ein Test haelt
	// fest, dass beide uebereinstimmen (Traffic.UmlaufGroesse).
	Light.CycleSeconds = 0.0;
	for (const FWiesbadenSignalPhase& Phase : Light.Phases)
	{
		Light.CycleSeconds += Phase.DurationSeconds;
	}
	Light.GreenSeconds = AdjustedThroughGreen;
}

double FWiesbadenTrafficLightSystem::ComputePhaseOffset(
	const FRoadIntersection& Intersection, const FRoadNetwork& InNetwork, double CycleSeconds) const
{
	const double Cycle = FMath::Max(CycleSeconds, MinCycleSeconds);

	// Rueckfall: ein Hash aus Knoten-Id und Startwert. Damit schaltet wenigstens
	// nicht die ganze Stadt im Gleichtakt.
	const double HashOffset = HashFraction(
		Hash2(static_cast<uint32>(Intersection.NodeId), static_cast<uint32>(Settings.RandomSeed)))
		* Cycle;

	if (!Settings.bGreenWave)
	{
		return HashOffset;
	}

	// Hauptachse der Kreuzung: der Arm mit der hoechsten Strassenklasse.
	const FIntersectionArm* MainArm = nullptr;
	double BestWeight = -1.0;
	for (const FIntersectionArm& Arm : Intersection.Arms)
	{
		if (!InNetwork.Segments.IsValidIndex(Arm.SegmentId))
		{
			continue;
		}
		const double Weight = RoadClassRank(InNetwork.Segments[Arm.SegmentId].HighwayType);
		if (Weight > BestWeight)
		{
			BestWeight = Weight;
			MainArm = &Arm;
		}
	}

	if (!MainArm)
	{
		return HashOffset;
	}

	FVector2D Dir(MainArm->OutwardDirection.X, MainArm->OutwardDirection.Y);
	if (!Dir.Normalize())
	{
		return HashOffset;
	}

	// Richtung auf eine Halbebene normieren.
	//
	// Zwei benachbarte Ampeln derselben Strasse koennen ihren Hauptarm in
	// ENTGEGENGESETZTE Richtungen zeigen haben - ohne diese Normierung haetten
	// sie Versatz mit verschiedenem Vorzeichen, und aus der Welle wuerde ein
	// Gegentakt.
	if (Dir.X < 0.0 || (FMath::IsNearlyZero(Dir.X) && Dir.Y < 0.0))
	{
		Dir = -Dir;
	}

	const double SpeedCmS = FMath::Max(Settings.GreenWaveSpeedKmh, 5.0) * 100000.0 / 3600.0;
	const double ProjectionCm = Intersection.Location.X * Dir.X + Intersection.Location.Y * Dir.Y;

	// Fahrzeit vom Ursprung der Achse bis hierher, auf den Umlauf gefaltet.
	double Offset = FMath::Fmod(ProjectionCm / SpeedCmS, Cycle);
	if (Offset < 0.0)
	{
		Offset += Cycle;
	}
	return Offset;
}

void FWiesbadenTrafficLightSystem::Reset()
{
	Network = nullptr;
	Lights.Reset();
	ConnectionToLight.Reset();
	ElapsedSeconds = 0.0;
	Settings = FWiesbadenTrafficLightSettings();
}

void FWiesbadenTrafficLightSystem::Tick(float DeltaSeconds)
{
	if (DeltaSeconds > 0.0f)
	{
		ElapsedSeconds += DeltaSeconds;
	}
}

void FWiesbadenTrafficLightSystem::GetProgramStatistics(
	int32& OutWithLeftPhase, double& OutMeanCycleSeconds,
	double& OutMinCycleSeconds, double& OutMaxCycleSeconds) const
{
	OutWithLeftPhase = 0;
	OutMeanCycleSeconds = 0.0;
	OutMinCycleSeconds = 0.0;
	OutMaxCycleSeconds = 0.0;
	if (Lights.Num() == 0)
	{
		return;
	}

	double CycleSum = 0.0;
	OutMinCycleSeconds = TNumericLimits<double>::Max();
	for (const FWiesbadenTrafficLight& Light : Lights)
	{
		CycleSum += Light.CycleSeconds;
		OutMinCycleSeconds = FMath::Min(OutMinCycleSeconds, Light.CycleSeconds);
		OutMaxCycleSeconds = FMath::Max(OutMaxCycleSeconds, Light.CycleSeconds);
		for (const FWiesbadenSignalPhase& Phase : Light.Phases)
		{
			if ((Phase.Group % 2) == 1)
			{
				++OutWithLeftPhase;
				break;
			}
		}
	}
	OutMeanCycleSeconds = CycleSum / Lights.Num();
}

bool FWiesbadenTrafficLightSystem::HasTrafficLightAt(int64 NodeId) const
{
	for (const FWiesbadenTrafficLight& Light : Lights)
	{
		if (Light.NodeId == NodeId)
		{
			return true;
		}
	}
	return false;
}

ESignalAspect FWiesbadenTrafficLightSystem::GetGroupAspect(int32 LightIndex, int32 Group) const
{
    if (!Lights.IsValidIndex(LightIndex))
    {
        return ESignalAspect::Green;
    }
    const FWiesbadenTrafficLight& Light = Lights[LightIndex];
    if (Light.Phases.Num() == 0)
    {
        return ESignalAspect::Green;
    }

    const double Cycle = FMath::Max(Light.CycleSeconds, MinCycleSeconds);
    double Phase = FMath::Fmod(ElapsedSeconds + Light.PhaseOffsetSeconds, Cycle);
    if (Phase < 0.0)
    {
        Phase += Cycle;
    }

    // Das laufende Zeitfenster suchen. Wenige Phasen je Kreuzung - eine
    // lineare Suche ist hier billiger als jede Vorberechnung.
    int32 Active = Light.Phases.Num() - 1;
    double PhaseStart = 0.0;
    for (int32 i = 0; i < Light.Phases.Num(); ++i)
    {
        const double End = PhaseStart + Light.Phases[i].DurationSeconds;
        if (Phase < End)
        {
            Active = i;
            break;
        }
        PhaseStart = End;
    }

    // NUR die freigegebene Gruppe ist ueberhaupt nicht-rot. Damit koennen sich
    // Linksabbieger und Gegenverkehr nicht begegnen.
    if (Light.Phases[Active].Group != Group)
    {
        return ESignalAspect::Red;
    }

    const double RA = FMath::Max(Settings.RedAmberSeconds, 0.0);
    const double AM = FMath::Max(Settings.AmberSeconds, 0.0);
    const double AR = FMath::Max(Settings.AllRedSeconds, 0.0);
    const double Slot = Light.Phases[Active].DurationSeconds;
    const double Green = FMath::Max(Slot - RA - AM - AR, 0.0);

    const double T = Phase - PhaseStart;
    if (T < RA) { return ESignalAspect::RedAmber; }
    if (T < RA + Green) { return ESignalAspect::Green; }
    if (T < RA + Green + AM) { return ESignalAspect::Amber; }
    return ESignalAspect::Red;   // Allrot-Raeumzeit
}

ESignalAspect FWiesbadenTrafficLightSystem::GetConnectionAspect(int32 ConnectionIndex) const
{
    const int32* LightIdx = ConnectionToLight.Find(ConnectionIndex);
    if (!LightIdx || !Lights.IsValidIndex(*LightIdx))
    {
        return ESignalAspect::Green;
    }
    const FWiesbadenTrafficLight& Light = Lights[*LightIdx];
    const int32* GroupPtr = Light.ConnectionGroups.Find(ConnectionIndex);
    if (!GroupPtr)
    {
        return ESignalAspect::Green;
    }
    return GetGroupAspect(*LightIdx, *GroupPtr);
}

bool FWiesbadenTrafficLightSystem::IsConnectionGreen(int32 ConnectionIndex) const
{
    return GetConnectionAspect(ConnectionIndex) == ESignalAspect::Green;
}

bool FWiesbadenTrafficLightSystem::AnyControlledConnectionRed() const
{
	// Frueh-Ausstieg beim ersten Rot: bei staffelphasigen Kreuzungen ist fast
	// immer eine kontrollierte Verbindung rot, die Schleife bricht praktisch
	// sofort ab. Die Sim ruft das nur, bis EINMAL Rot beobachtet wurde.
	for (const TPair<int32, int32>& Pair : ConnectionToLight)
	{
		if (!IsConnectionGreen(Pair.Key))
		{
			return true;
		}
	}
	return false;
}
