// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenMinimap.h"

bool FWiesbadenMinimap::IsMajorRoad(EOSMHighwayType Type)
{
	switch (Type)
	{
	case EOSMHighwayType::Motorway:
	case EOSMHighwayType::MotorwayLink:
	case EOSMHighwayType::Trunk:
	case EOSMHighwayType::TrunkLink:
	case EOSMHighwayType::Primary:
	case EOSMHighwayType::PrimaryLink:
	case EOSMHighwayType::Secondary:
	case EOSMHighwayType::SecondaryLink:
		return true;
	default:
		return false;
	}
}

namespace
{
	/** Quadrierter Abstand eines Punktes zur Strecke A-B, nur in der Ebene. */
	double DistanceToSegmentSq(const FVector2D& P, const FVector2D& A, const FVector2D& B)
	{
		const FVector2D AB = B - A;
		const double LengthSq = AB.SizeSquared();
		if (LengthSq <= UE_DOUBLE_SMALL_NUMBER)
		{
			return FVector2D::DistSquared(P, A);
		}
		const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSq, 0.0, 1.0);
		return FVector2D::DistSquared(P, A + AB * T);
	}
}

void FWiesbadenMinimap::BuildLines(
	const FRoadNetwork& Network,
	const FVector& PlayerLocation,
	double PlayerYawDegrees,
	const FVector2D& CenterPx,
	const FMinimapSettings& Settings,
	TArray<FMinimapLine>& OutLines)
{
	OutLines.Reset();

	const double RangeCm = FMath::Max(Settings.RangeCm, 100.0);
	const double RangeSq = RangeCm * RangeCm;
	const float Radius = FMath::Max(Settings.DiameterPx, 10.0f) * 0.5f;
	const double PixelsPerCm = Radius / RangeCm;

	// Drehung: Bei mitdrehender Karte zeigt die Fahrtrichtung nach oben. Dazu
	// wird die Welt um -Yaw gedreht; die zusaetzlichen 90 Grad bringen die
	// Fahrzeug-Vorwaertsachse (+X) auf die Bildschirm-Hochachse.
	const double RotationDeg = Settings.bRotateWithPlayer ? -PlayerYawDegrees : 0.0;
	const double RotationRad = FMath::DegreesToRadians(RotationDeg);
	const double CosR = FMath::Cos(RotationRad);
	const double SinR = FMath::Sin(RotationRad);

	const FVector2D Player2D(PlayerLocation.X, PlayerLocation.Y);

	auto ToScreen = [&](const FVector& World) -> FVector2D
	{
		const double DX = World.X - Player2D.X;
		const double DY = World.Y - Player2D.Y;

		const double RX = DX * CosR - DY * SinR;
		const double RY = DX * SinR + DY * CosR;

		// Bildschirm: X nach rechts (Welt-Y), Y nach unten (Welt-X negativ),
		// damit Norden oben liegt.
		return CenterPx + FVector2D(RY * PixelsPerCm, -RX * PixelsPerCm);
	};

	// Hauptstrassen zuerst: Wird die Obergrenze erreicht, fehlen eher
	// Wohnstrassen als die Achsen, an denen man sich orientiert.
	for (int32 Pass = 0; Pass < 2 && OutLines.Num() < Settings.MaxLines; ++Pass)
	{
		const bool bWantMajor = (Pass == 0);

		for (const FRoadSegment& Segment : Network.Segments)
		{
			if (OutLines.Num() >= Settings.MaxLines)
			{
				break;
			}
			if (IsMajorRoad(Segment.HighwayType) != bWantMajor)
			{
				continue;
			}

			// UNGEKUERZTE Mittellinie.
			//
			// TrimmedCenterline ist an jeder Kreuzung um mehrere Meter
			// zurueckgeschnitten, damit sich die Fahrbahnbaender nicht
			// ueberlappen. Auf einer Karte ist das falsch: Dort gehoert der
			// Strassenverlauf hin, nicht die Fahrbahnflaeche. Gezeichnet mit
			// der getrimmten Linie zeigte die Minikarte an jeder Kreuzung eine
			// Luecke - ein Fehler, den es in den Daten gar nicht gibt.
			const TArray<FVector>& Line = Segment.Centerline.Num() >= 2
				? Segment.Centerline
				: Segment.TrimmedCenterline;

			if (Line.Num() < 2)
			{
				continue;
			}

			// Vorauswahl ueber die HUELLBOX des Segments, nicht ueber seinen
			// ersten Punkt.
			//
			// Hier stand ein Abstandstest auf Line[0]. Eine lange Strasse,
			// deren Anfang weit entfernt liegt, die aber direkt am Spieler
			// vorbeifuehrt, fiel damit vollstaendig heraus - auf der Karte
			// fehlten ganze Strassenzuege. Das Nerotal ist so ein Fall: ein
			// durchgehender Zug von mehreren Kilometern.
			//
			// Die Huellbox ist genauso billig und schliesst nur aus, was
			// wirklich ausserhalb liegt.
			{
				FVector2D BoxMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
				FVector2D BoxMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
				for (const FVector& Point : Line)
				{
					BoxMin.X = FMath::Min(BoxMin.X, Point.X);
					BoxMin.Y = FMath::Min(BoxMin.Y, Point.Y);
					BoxMax.X = FMath::Max(BoxMax.X, Point.X);
					BoxMax.Y = FMath::Max(BoxMax.Y, Point.Y);
				}

				if (Player2D.X < BoxMin.X - RangeCm || Player2D.X > BoxMax.X + RangeCm
					|| Player2D.Y < BoxMin.Y - RangeCm || Player2D.Y > BoxMax.Y + RangeCm)
				{
					continue;
				}
			}

			for (int32 Index = 1; Index < Line.Num(); ++Index)
			{
				const FVector2D A(Line[Index - 1].X, Line[Index - 1].Y);
				const FVector2D B(Line[Index].X, Line[Index].Y);

				if (DistanceToSegmentSq(Player2D, A, B) > RangeSq)
				{
					continue;
				}

				FMinimapLine MapLine;
				MapLine.Start = ToScreen(Line[Index - 1]);
				MapLine.End = ToScreen(Line[Index]);
				MapLine.bMajor = bWantMajor;
				MapLine.Thickness = bWantMajor ? 3.0f : 1.6f;
				OutLines.Add(MapLine);

				if (OutLines.Num() >= Settings.MaxLines)
				{
					break;
				}
			}
		}
	}
}

FString FWiesbadenMinimap::FindStreetName(
	const FRoadNetwork& Network,
	const FVector& PlayerLocation,
	double MaxDistanceCm)
{
	const FVector2D Player2D(PlayerLocation.X, PlayerLocation.Y);
	const double MaxSq = MaxDistanceCm * MaxDistanceCm;

	double BestSq = MaxSq;
	FString BestName;

	for (const FRoadSegment& Segment : Network.Segments)
	{
		if (Segment.StreetName.IsEmpty())
		{
			continue;
		}

		// Ebenfalls die ungekuerzte Linie: Sonst faende man mitten auf einer
		// Kreuzung keine Strasse.
		const TArray<FVector>& Line = Segment.Centerline.Num() >= 2
			? Segment.Centerline
			: Segment.TrimmedCenterline;

		if (Line.Num() < 2)
		{
			continue;
		}

		// Vorauswahl ueber die Huellbox, aus demselben Grund wie oben: Auf
		// einer langen Strasse stuende sonst kein Name im HUD, weil ihr
		// Anfang kilometerweit entfernt liegt.
		{
			FVector2D BoxMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
			FVector2D BoxMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
			for (const FVector& Point : Line)
			{
				BoxMin.X = FMath::Min(BoxMin.X, Point.X);
				BoxMin.Y = FMath::Min(BoxMin.Y, Point.Y);
				BoxMax.X = FMath::Max(BoxMax.X, Point.X);
				BoxMax.Y = FMath::Max(BoxMax.Y, Point.Y);
			}

			if (Player2D.X < BoxMin.X - MaxDistanceCm || Player2D.X > BoxMax.X + MaxDistanceCm
				|| Player2D.Y < BoxMin.Y - MaxDistanceCm || Player2D.Y > BoxMax.Y + MaxDistanceCm)
			{
				continue;
			}
		}

		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			const FVector2D A(Line[Index - 1].X, Line[Index - 1].Y);
			const FVector2D B(Line[Index].X, Line[Index].Y);

			const double DistSq = DistanceToSegmentSq(Player2D, A, B);
			if (DistSq < BestSq)
			{
				BestSq = DistSq;
				BestName = Segment.StreetName;
			}
		}
	}

	return BestName;
}
