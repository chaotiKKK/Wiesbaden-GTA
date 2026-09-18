// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenPedestrianSimulation.h"

#include "WiesbadenReal.h"

#include "GIS/PolygonUtils.h"

namespace
{
	/** Schrittlaenge eines Fussgaengers in cm - bestimmt den Rhythmus der Hebung. */
	constexpr double StrideLengthCm = 75.0;

	/** Abstand zwischen zwei gespawnten Figuren auf demselben Gehweg, in cm. */
	constexpr double SpawnSpacingCm = 900.0;

	/** Sekunden zwischen zwei Aktualisierungen der Segmentliste in Spielernaehe. */
	constexpr float NearbyRefreshSeconds = 1.5f;

	/** Deterministische Streuung aus einem Zaehler, Ergebnis 0..1. */
	float PseudoRandom01(int32 Seed)
	{
		// Bewusst deterministisch statt FMath::Rand: der Ablauf bleibt damit
		// zwischen zwei Laeufen reproduzierbar und ist im Test pruefbar.
		const uint32 Hashed = GetTypeHash(Seed * 2654435761u);
		return static_cast<float>(Hashed % 10000u) / 10000.0f;
	}
}

double FWiesbadenPedestrianSimulation::ComputeSidewalkCenterOffsetCm(
	double CarriagewayWidthCm, double SidewalkWidthCm)
{
	// Bordsteinkante liegt bei der halben Fahrbahnbreite; gelaufen wird in der
	// Mitte des anschliessenden Gehwegs.
	return FMath::Max(0.0, CarriagewayWidthCm) * 0.5 + FMath::Max(0.0, SidewalkWidthCm) * 0.5;
}

bool FWiesbadenPedestrianSimulation::HasSidewalkOnSide(EOSMSidewalkType Type, bool bRightSide)
{
	switch (Type)
	{
	case EOSMSidewalkType::Both:
		return true;
	case EOSMSidewalkType::Left:
		return !bRightSide;
	case EOSMSidewalkType::Right:
		return bRightSide;
	default:
		// None und Separate: kein Gehweg an dieser Achse. "Separate" ist als
		// eigener Weg gemappt und wird ueber sein eigenes Segment bedient.
		return false;
	}
}

double FWiesbadenPedestrianSimulation::AdvanceAlongSegment(
	double DistanceCm, double SpeedCmS, double SegmentLengthCm,
	float DeltaSeconds, bool& bInOutForward)
{
	if (SegmentLengthCm <= 1.0)
	{
		return 0.0;
	}

	const double Step = FMath::Max(0.0, SpeedCmS) * FMath::Max(0.0f, DeltaSeconds);
	double Next = DistanceCm + (bInOutForward ? Step : -Step);

	// Am Ende umkehren statt zu verschwinden. Die Figur laeuft den Gehweg
	// zurueck - ohne Querungslogik ist das die ehrlichste Loesung.
	if (Next > SegmentLengthCm)
	{
		Next = SegmentLengthCm - (Next - SegmentLengthCm);
		bInOutForward = false;
	}
	else if (Next < 0.0)
	{
		Next = -Next;
		bInOutForward = true;
	}

	return FMath::Clamp(Next, 0.0, SegmentLengthCm);
}

float FWiesbadenPedestrianSimulation::ComputeStridePhase(
	double DistanceCm, double StrideCm, float Offset)
{
	if (StrideCm <= KINDA_SMALL_NUMBER)
	{
		return 0.0f;
	}

	const double Cycles = DistanceCm / StrideCm + static_cast<double>(Offset);
	return static_cast<float>(Cycles - FMath::FloorToDouble(Cycles));
}

void FWiesbadenPedestrianSimulation::Initialize(
	const FRoadNetwork& InNetwork, const FWiesbadenPedestrianSettings& InSettings)
{
	Network = &InNetwork;
	Settings = InSettings;
	Pedestrians.Reset();
	WalkableSegmentIndices.Reset();
	NearbySegmentIndices.Reset();
	Report = FWiesbadenPedestrianReport();
	SpawnCounter = 0;
	NearbyRefreshTimer = 0.0f;

	double TotalSidewalkCm = 0.0;

	for (int32 Index = 0; Index < InNetwork.Segments.Num(); ++Index)
	{
		const FRoadSegment& Segment = InNetwork.Segments[Index];
		if (Segment.Centerline.Num() < 2)
		{
			continue;
		}

		const bool bLeft = HasSidewalkOnSide(Segment.SidewalkType, false);
		const bool bRight = HasSidewalkOnSide(Segment.SidewalkType, true);
		if (!bLeft && !bRight)
		{
			continue;
		}

		WalkableSegmentIndices.Add(Index);
		TotalSidewalkCm += Segment.LengthCm * ((bLeft && bRight) ? 2.0 : 1.0);
	}

	Report.WalkablePathCount = WalkableSegmentIndices.Num();
	Report.TotalSidewalkKm = TotalSidewalkCm / 100000.0;

	UE_LOG(LogWbCore, Log,
		TEXT("Fussgaenger-Simulation: %d Segmente mit Gehweg, %.1f km Gehweg."),
		Report.WalkablePathCount, Report.TotalSidewalkKm);
}

void FWiesbadenPedestrianSimulation::SetObserverLocation(const FVector& InLocation)
{
	ObserverLocation = InLocation;
	bHasObserver = true;
}

void FWiesbadenPedestrianSimulation::RefreshNearbySegments()
{
	NearbySegmentIndices.Reset();
	NearbySidewalkLengthCm = 0.0;
	if (!Network || !bHasObserver)
	{
		return;
	}

	const double RadiusCm = Settings.SpawnRadiusMeters * 100.0;
	const double RadiusSq = RadiusCm * RadiusCm;

	for (const int32 SegmentIndex : WalkableSegmentIndices)
	{
		const FRoadSegment& Segment = Network->Segments[SegmentIndex];

		// Mittelpunkt der Achse genuegt als Naehe-Kriterium; Segmente sind
		// kurz gegenueber dem Spawn-Radius.
		const FVector& Mid = Segment.Centerline[Segment.Centerline.Num() / 2];
		const double Dx = Mid.X - ObserverLocation.X;
		const double Dy = Mid.Y - ObserverLocation.Y;

		// Horizontal gemessen: am Hang ist der Hoehenunterschied fuer die
		// Frage "ist das in Sichtweite?" ohne Belang.
		if (Dx * Dx + Dy * Dy <= RadiusSq)
		{
			NearbySegmentIndices.Add(SegmentIndex);

			// Bezugsgroesse fuer die erlebte Dichte. Faellt bei der ohnehin
			// noetigen Umkreissuche ab - beide Seiten zaehlen, wo es beide gibt.
			const bool bLeftWalk = HasSidewalkOnSide(Segment.SidewalkType, false);
			const bool bRightWalk = HasSidewalkOnSide(Segment.SidewalkType, true);
			NearbySidewalkLengthCm +=
				Segment.LengthCm * ((bLeftWalk && bRightWalk) ? 2.0 : 1.0);
		}
	}
}

int32 FWiesbadenPedestrianSimulation::GetTargetPedestrianCount() const
{
	// EINE Quelle fuer die Zielzahl - SpawnMissing und die Diagnose lesen
	// denselben Wert, sonst melden sie frueher oder spaeter Verschiedenes.
	return FMath::RoundToInt(
		static_cast<float>(Settings.TargetPedestriansInRadius)
		* FMath::Clamp(Settings.Density, 0.0f, 1.0f)
		* static_cast<float>(GetOuterFractionHere()));
}

double FWiesbadenPedestrianSimulation::GetOuterFractionHere() const
{
	return ComputeOuterFraction(
		FVector2D(ObserverLocation.X, ObserverLocation.Y), Settings);
}

double FWiesbadenPedestrianSimulation::ComputeOuterFraction(
	const FVector2D& Location, const FWiesbadenPedestrianSettings& Settings)
{
	const double RingMeters = FMath::Max(50.0, Settings.FalloffRingMeters);
	const double DistanceMeters = FVector2D::Distance(Location, Settings.CityCentreCm) / 100.0;

	// Ganze Ringe, nicht der stetige Abstand: so bleibt die Dichte innerhalb
	// eines Viertels gleich und springt nicht bei jedem Schritt. Der erste
	// Ring ist die Innenstadt und bleibt unangetastet.
	const int32 Ring = FMath::FloorToInt(DistanceMeters / RingMeters);
	if (Ring <= 0)
	{
		return 1.0;
	}

	const double Keep = FMath::Clamp(1.0 - Settings.OuterFalloffPerRing, 0.0, 1.0);
	const double Fraction = FMath::Pow(Keep, static_cast<double>(Ring));

	return FMath::Max(Fraction, FMath::Clamp(Settings.MinOuterFraction, 0.0, 1.0));
}

void FWiesbadenPedestrianSimulation::SpawnMissing()
{
	if (!Network || NearbySegmentIndices.Num() == 0)
	{
		return;
	}

	// Zielzahl, nach aussen ausgeduennt.
	//
	// Bisher galt ueberall dieselbe Dichte: am Nordfriedhof und auf der
	// Platter Strasse Richtung Taunusstein liefen so viele Menschen herum wie
	// in der Fussgaengerzone. Dort geht in Wirklichkeit niemand zu Fuss.
	const int32 Target = GetTargetPedestrianCount();

	int32 Missing = Target - Pedestrians.Num();
	if (Missing <= 0)
	{
		return;
	}

	// Pro Bild begrenzt nachfuellen, damit beim Betreten eines Viertels nicht
	// schlagartig hunderte Figuren erscheinen.
	Missing = FMath::Min(Missing, 12);

	for (int32 Spawned = 0; Spawned < Missing; ++Spawned)
	{
		const int32 SegmentIndex =
			NearbySegmentIndices[(SpawnCounter * 7 + Spawned * 3) % NearbySegmentIndices.Num()];
		const FRoadSegment& Segment = Network->Segments[SegmentIndex];

		const float Roll = PseudoRandom01(SpawnCounter + Spawned * 131);
		const bool bRight = HasSidewalkOnSide(Segment.SidewalkType, true)
			&& (!HasSidewalkOnSide(Segment.SidewalkType, false) || Roll < 0.5f);

		if (!HasSidewalkOnSide(Segment.SidewalkType, bRight))
		{
			continue;
		}

		FWiesbadenPedestrian Walker;
		Walker.SegmentIndex = SegmentIndex;
		Walker.bRightSide = bRight;

		const float Position = PseudoRandom01(SpawnCounter + Spawned * 977);
		Walker.DistanceAlongCm = FMath::Fmod(
			static_cast<double>(Position) * Segment.LengthCm + Spawned * SpawnSpacingCm,
			FMath::Max(1.0, Segment.LengthCm));

		const float SpeedRoll = PseudoRandom01(SpawnCounter + Spawned * 613) * 2.0f - 1.0f;
		Walker.SpeedCmS = Settings.WalkSpeedMetersPerS * 100.0
			* (1.0 + SpeedRoll * Settings.WalkSpeedVariation);

		Walker.bForward = PseudoRandom01(SpawnCounter + Spawned * 271) < 0.5f;
		Walker.StrideOffset = PseudoRandom01(SpawnCounter + Spawned * 449);

		Pedestrians.Add(Walker);
		++SpawnCounter;
	}
}

void FWiesbadenPedestrianSimulation::DespawnDistant()
{
	if (!Network || !bHasObserver)
	{
		return;
	}

	const double RadiusCm = Settings.DespawnRadiusMeters * 100.0;
	const double RadiusSq = RadiusCm * RadiusCm;

	for (int32 Index = Pedestrians.Num() - 1; Index >= 0; --Index)
	{
		const FWiesbadenPedestrian& Walker = Pedestrians[Index];
		if (!Network->Segments.IsValidIndex(Walker.SegmentIndex))
		{
			Pedestrians.RemoveAtSwap(Index);
			continue;
		}

		const FRoadSegment& Segment = Network->Segments[Walker.SegmentIndex];
		const FVector& Mid = Segment.Centerline[Segment.Centerline.Num() / 2];
		const double Dx = Mid.X - ObserverLocation.X;
		const double Dy = Mid.Y - ObserverLocation.Y;

		if (Dx * Dx + Dy * Dy > RadiusSq)
		{
			Pedestrians.RemoveAtSwap(Index);
		}
	}
}

void FWiesbadenPedestrianSimulation::Tick(float DeltaSeconds)
{
	if (!Network || Settings.Density <= 0.0f)
	{
		Pedestrians.Reset();
		Report.SimulatedCount = 0;
		return;
	}

	NearbyRefreshTimer -= DeltaSeconds;
	if (NearbyRefreshTimer <= 0.0f)
	{
		NearbyRefreshTimer = NearbyRefreshSeconds;
		RefreshNearbySegments();
	}

	for (FWiesbadenPedestrian& Walker : Pedestrians)
	{
		// Zerplatzte laufen erst recht nicht. Nach Ablauf verschwinden sie
		// und tauchen an anderer Stelle wieder auf - der Spawner fuellt den
		// Umkreis von selbst nach.
		if (Walker.BurstSeconds > 0.0f)
		{
			Walker.BurstSeconds = FMath::Max(0.0f, Walker.BurstSeconds - DeltaSeconds);
			if (Walker.BurstSeconds <= 0.0f)
			{
				Walker.DistanceAlongCm = 0.0;
				Walker.SegmentIndex = INDEX_NONE;
			}
			continue;
		}

		// Am Boden liegende laufen nicht. Nach Ablauf der Zeit stehen sie
		// wieder auf und gehen weiter - eine Leiche, die liegen bleibt,
		// braeuchte eine Verwaltung, die es hier nicht gibt.
		if (Walker.DownSeconds > 0.0f)
		{
			Walker.DownSeconds = FMath::Max(0.0f, Walker.DownSeconds - DeltaSeconds);
			continue;
		}

		if (!Network->Segments.IsValidIndex(Walker.SegmentIndex))
		{
			continue;
		}

		const FRoadSegment& Segment = Network->Segments[Walker.SegmentIndex];
		Walker.DistanceAlongCm = AdvanceAlongSegment(
			Walker.DistanceAlongCm, Walker.SpeedCmS, Segment.LengthCm, DeltaSeconds, Walker.bForward);
	}

	DespawnDistant();
	SpawnMissing();

	Report.SimulatedCount = Pedestrians.Num();
}

bool FWiesbadenPedestrianSimulation::SampleSidewalk(
	const FWiesbadenPedestrian& Walker, FVector& OutLocation, FRotator& OutRotation) const
{
	if (!Network || !Network->Segments.IsValidIndex(Walker.SegmentIndex))
	{
		return false;
	}

	const FRoadSegment& Segment = Network->Segments[Walker.SegmentIndex];
	// Die GEKUERZTE Mittellinie, nicht die volle.
	//
	// Die volle Mittellinie laeuft bis in den Kreuzungsmittelpunkt. Ein
	// Fussgaenger auf ihr steht dort mit halber Fahrbahnbreite plus halber
	// Gehwegbreite Versatz - und das liegt mitten auf der Kreuzungsplatte, die
	// nach allen Seiten weiter reicht als die halbe Fahrbahnbreite eines Arms.
	// Im Bild von oben stehen die Figuren dadurch auf dem Asphalt.
	//
	// Die gekuerzte Linie endet am Kreuzungsrand. Fussgaenger laufen damit bis
	// an die Kreuzung heran und nicht hinein.
	const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
		? Segment.TrimmedCenterline
		: Segment.Centerline;
	if (Line.Num() < 2)
	{
		return false;
	}

	// Punkt auf der Achse suchen.
	double Travelled = 0.0;
	int32 SegmentPart = 0;
	double LocalAlpha = 0.0;

	for (int32 Index = 0; Index + 1 < Line.Num(); ++Index)
	{
		const double PartLength = FVector::Dist(Line[Index], Line[Index + 1]);
		if (Travelled + PartLength >= Walker.DistanceAlongCm || Index + 2 == Line.Num())
		{
			SegmentPart = Index;
			LocalAlpha = PartLength > KINDA_SMALL_NUMBER
				? FMath::Clamp((Walker.DistanceAlongCm - Travelled) / PartLength, 0.0, 1.0)
				: 0.0;
			break;
		}
		Travelled += PartLength;
	}

	const FVector A = Line[SegmentPart];
	const FVector B = Line[SegmentPart + 1];
	const FVector OnAxis = FMath::Lerp(A, B, LocalAlpha);

	const FVector Direction = (B - A).GetSafeNormal();
	if (Direction.IsNearlyZero())
	{
		return false;
	}

	// Seitlicher Versatz auf den Gehweg.
	const FVector2D Left2D = FPolygonUtils::GetLeftNormal(FVector2D(Direction.X, Direction.Y));
	const double Offset = ComputeSidewalkCenterOffsetCm(Segment.CarriagewayWidthCm, Segment.SidewalkWidthCm);
	const double Sign = Walker.bRightSide ? -1.0 : 1.0;

	// Der Gehweg liegt um die Bordsteinhoehe ueber der Fahrbahn.
	const double Bob = FMath::Sin(ComputeStridePhase(
		Walker.DistanceAlongCm, StrideLengthCm, Walker.StrideOffset) * 2.0f * PI) * Settings.StrideBobCm;

	// Geliefert wird die FUSSposition, nicht die Koerpermitte.
	//
	// Hier wurde BodyCenterHeightCm (88 cm) aufaddiert - die halbe Koerperhoehe.
	// Das war richtig, solange die Figur der Engine-Zylinder war, dessen
	// Ursprung in der Mitte liegt. Die Menschfigur hat ihren Ursprung zwischen
	// den Fuessen; sie schwebte dadurch um genau diesen Betrag ueber dem Boden.
	//
	// Wie hoch ein Mesh ueber seinem Ursprung ansetzt, weiss nur der Spawner -
	// er hat das Mesh. Die Simulation bleibt datenrein und liefert den Punkt,
	// auf dem die Figur STEHT.
	OutLocation = FVector(
		OnAxis.X + Left2D.X * Offset * Sign,
		OnAxis.Y + Left2D.Y * Offset * Sign,
		OnAxis.Z + Segment.KerbHeightCm + Bob);

	const FVector Facing = Walker.bForward ? Direction : -Direction;
	OutRotation = Facing.Rotation();
	return true;
}

void FWiesbadenPedestrianSimulation::CollectPlaced(TArray<FPlacedPedestrian>& OutPlaced) const
{
	OutPlaced.Reset();
	OutPlaced.Reserve(Pedestrians.Num());

	for (const FWiesbadenPedestrian& Walker : Pedestrians)
	{
		FPlacedPedestrian Placed;
		if (!SampleSidewalk(Walker, Placed.Location, Placed.Rotation))
		{
			continue;
		}

		if (Walker.BurstSeconds > 0.0f)
		{
			// Zerplatzt: in einem Wimpernschlag flach und breit gezogen,
			// dann verschwindend. Der Verlauf laeuft RUECKWAERTS ueber die
			// Restzeit, damit der erste Frame der heftigste ist - ein
			// Aufprall wirkt sonst wie ein Aufblasen.
			const float T = FMath::Clamp(
				1.0f - Walker.BurstSeconds / FMath::Max(BurstDurationSeconds, 0.01f), 0.0f, 1.0f);

			Placed.Location.Z -= 80.0 * T;
			Placed.Rotation.Roll += 90.0f;
			Placed.StridePhase = 0.0f;
			Placed.ScaleFactor = FVector(
				1.0f + 2.2f * T,       // in die Laenge
				1.0f + 2.2f * T,       // und in die Breite
				FMath::Max(0.04f, 1.0f - T));  // platt
			OutPlaced.Add(Placed);
			continue;
		}

		if (Walker.DownSeconds > 0.0f)
		{
			// Umgefallen: um die eigene Querachse gekippt und auf
			// Schulterhoehe abgesenkt, damit die Figur auf dem Gehweg
			// liegt statt in ihm zu stecken.
			Placed.Rotation.Roll += 88.0f;
			Placed.Location.Z -= 78.0;
			Placed.StridePhase = 0.0f;
			OutPlaced.Add(Placed);
			continue;
		}

		Placed.StridePhase = ComputeStridePhase(Walker.DistanceAlongCm, StrideLengthCm, Walker.StrideOffset);
		OutPlaced.Add(Placed);
	}
}


int32 FWiesbadenPedestrianSimulation::BurstNear(const FVector& Location, double RadiusCm)
{
	const double RadiusSq = RadiusCm * RadiusCm;
	int32 Struck = 0;

	for (FWiesbadenPedestrian& Walker : Pedestrians)
	{
		if (Walker.BurstSeconds > 0.0f)
		{
			continue;
		}

		FVector WalkerLocation;
		FRotator WalkerRotation;
		if (!SampleSidewalk(Walker, WalkerLocation, WalkerRotation))
		{
			continue;
		}

		const FVector Delta = WalkerLocation - Location;
		if (Delta.X * Delta.X + Delta.Y * Delta.Y > RadiusSq)
		{
			continue;
		}
		if (FMath::Abs(Delta.Z) > 250.0)
		{
			continue;
		}

		Walker.BurstSeconds = BurstDurationSeconds;
		Walker.DownSeconds = 0.0f;
		++Struck;
	}

	return Struck;
}

int32 FWiesbadenPedestrianSimulation::StrikeNear(const FVector& Location, double RadiusCm,
	float DownForSeconds)
{
	const double RadiusSq = RadiusCm * RadiusCm;
	int32 Struck = 0;

	for (FWiesbadenPedestrian& Walker : Pedestrians)
	{
		if (Walker.DownSeconds > 0.0f)
		{
			continue;
		}

		FVector WalkerLocation;
		FRotator WalkerRotation;
		if (!SampleSidewalk(Walker, WalkerLocation, WalkerRotation))
		{
			continue;
		}

		// Waagerecht messen: ein Fussgaenger einen Stock tiefer am Hang ist
		// nicht in Reichweite einer Kettensaege, sein Abstand in der Ebene
		// aber klein.
		const FVector Delta = WalkerLocation - Location;
		if (Delta.X * Delta.X + Delta.Y * Delta.Y > RadiusSq)
		{
			continue;
		}
		if (FMath::Abs(Delta.Z) > 250.0)
		{
			continue;
		}

		Walker.DownSeconds = DownForSeconds;
		++Struck;
	}

	return Struck;
}
