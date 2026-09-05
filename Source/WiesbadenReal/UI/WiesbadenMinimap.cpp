// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "UI/WiesbadenMinimap.h"

#include "GIS/BuildingGenerator.h"

FVector2D FWorldMapProjection::Project(const FVector& World) const
{
	return FVector2D(
		ScreenCentre.X + (World.X - ViewCentreWorld.X) * ScalePxPerCm,
		ScreenCentre.Y - (World.Y - ViewCentreWorld.Y) * ScalePxPerCm);  // -Y: Norden oben
}

FVector2D FWorldMapProjection::Unproject(const FVector2D& Screen) const
{
	if (ScalePxPerCm <= 0.0f)
	{
		return ViewCentreWorld;
	}
	return FVector2D(
		ViewCentreWorld.X + (Screen.X - ScreenCentre.X) / ScalePxPerCm,
		ViewCentreWorld.Y - (Screen.Y - ScreenCentre.Y) / ScalePxPerCm);  // -Y: Norden oben
}

bool FWiesbadenMinimap::ComputeNetworkBoundsXY(
	const FRoadNetwork& Network, FVector2D& OutMin, FVector2D& OutMax)
{
	bool bAny = false;
	FVector2D Min(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
	FVector2D Max(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
	for (const FRoadSegment& Segment : Network.Segments)
	{
		const TArray<FVector>& Line = Segment.Centerline.Num() >= 2
			? Segment.Centerline
			: Segment.TrimmedCenterline;
		for (const FVector& P : Line)
		{
			Min.X = FMath::Min(Min.X, P.X);
			Min.Y = FMath::Min(Min.Y, P.Y);
			Max.X = FMath::Max(Max.X, P.X);
			Max.Y = FMath::Max(Max.Y, P.Y);
			bAny = true;
		}
	}
	if (bAny)
	{
		OutMin = Min;
		OutMax = Max;
	}
	return bAny;
}

FWorldMapProjection FWiesbadenMinimap::MakeWorldMapProjection(
	const FVector2D& WorldMin, const FVector2D& WorldMax,
	const FVector2D& ScreenCentre, const FVector2D& ScreenSizePx, float MarginFrac)
{
	FWorldMapProjection Proj;
	Proj.WorldMin = WorldMin;
	Proj.WorldMax = WorldMax;
	Proj.ScreenCentre = ScreenCentre;
	Proj.ViewCentreWorld = (WorldMin + WorldMax) * 0.5;   // Voll-Fit: Netzmitte in der Bildmitte

	const double WorldW = FMath::Max(WorldMax.X - WorldMin.X, 1.0);
	const double WorldH = FMath::Max(WorldMax.Y - WorldMin.Y, 1.0);
	const float Margin = FMath::Clamp(MarginFrac, 0.1f, 1.0f);

	// Gleichmaessig: der Massstab, der auf BEIDE Achsen passt (die engere begrenzt).
	const double SxPerCm = (ScreenSizePx.X * Margin) / WorldW;
	const double SyPerCm = (ScreenSizePx.Y * Margin) / WorldH;
	Proj.ScalePxPerCm = static_cast<float>(FMath::Min(SxPerCm, SyPerCm));
	return Proj;
}

FWorldMapProjection FWiesbadenMinimap::MakeZoomedProjection(
	const FWorldMapProjection& Fit, float ZoomFactor, const FVector2D& DesiredCentreWorld)
{
	FWorldMapProjection Proj = Fit;
	const float Zoom = FMath::Clamp(ZoomFactor, WorldMapMinZoom, WorldMapMaxZoom);
	Proj.ScalePxPerCm = Fit.ScalePxPerCm * Zoom;

	// Sichtbare Welt-Halbausdehnung bei diesem Massstab (Pixel-Mitte / Pixel-je-cm).
	const double HalfVisX = (Proj.ScalePxPerCm > 0.0f) ? (Proj.ScreenCentre.X / Proj.ScalePxPerCm) : 0.0;
	const double HalfVisY = (Proj.ScalePxPerCm > 0.0f) ? (Proj.ScreenCentre.Y / Proj.ScalePxPerCm) : 0.0;

	const FVector2D NetCentre = (Fit.WorldMin + Fit.WorldMax) * 0.5;

	// Blickzentrum je Achse klemmen: das Sichtfenster darf die Netzgrenzen nicht
	// verlassen. Ist das Fenster breiter als das Netz (Zoom 1 bzw. schmale Achse),
	// gibt es kein gueltiges Intervall -> auf die Netzmitte zentrieren.
	auto ClampAxis = [](double C, double Lo, double Hi, double Half, double NetC) -> double
	{
		const double Min = Lo + Half;
		const double Max = Hi - Half;
		return (Min > Max) ? NetC : FMath::Clamp(C, Min, Max);
	};
	Proj.ViewCentreWorld = FVector2D(
		ClampAxis(DesiredCentreWorld.X, Fit.WorldMin.X, Fit.WorldMax.X, HalfVisX, NetCentre.X),
		ClampAxis(DesiredCentreWorld.Y, Fit.WorldMin.Y, Fit.WorldMax.Y, HalfVisY, NetCentre.Y));
	return Proj;
}

void FWiesbadenMinimap::BuildWorldMapLines(
	const FRoadNetwork& Network, const FWorldMapProjection& Proj,
	int32 MaxLines, float MinSegmentPx, TArray<FMinimapLine>& OutLines)
{
	OutLines.Reset();
	if (!Proj.IsValid())
	{
		return;
	}

	const float MinPxSq = MinSegmentPx * MinSegmentPx;

	// Hauptstrassen ZUERST: bei begrenztem Linien-Budget (MaxLines) muss das
	// erkennbare Skelett garantiert gezeichnet werden, sonst verbrauchen die
	// zahlreichen Nebenstrassen das Budget und die Hauptachsen fehlen ganz.
	// Sie sind dicker + gelb, also auch unter den duennen Nebenstrassen sichtbar.
	for (int32 Pass = 0; Pass < 2 && OutLines.Num() < MaxLines; ++Pass)
	{
		const bool bWantMajor = (Pass == 0);
		for (const FRoadSegment& Segment : Network.Segments)
		{
			if (OutLines.Num() >= MaxLines)
			{
				break;
			}
			const bool bMajor = IsMajorRoad(Segment.HighwayType);
			if (bMajor != bWantMajor)
			{
				continue;
			}
			const TArray<FVector>& Line = Segment.Centerline.Num() >= 2
				? Segment.Centerline
				: Segment.TrimmedCenterline;
			if (Line.Num() < 2)
			{
				continue;
			}
			// DEZIMIEREN statt je Teilsegment cullen: bei Stadt-Zoom liegen die
			// Mittellinienpunkte dichter als ein Pixel beisammen. Wuerde man jedes
			// Teilsegment einzeln gegen MinSegmentPx pruefen, faellt JEDES weg und
			// die ganze Strasse verschwindet (genau der Fehler im ersten Wurf: 0
			// Linien aus 125.000 Segmenten). Stattdessen dicht liegende Punkte zu
			// einer sichtbaren Linie zusammenfassen: erst zeichnen, wenn der
			// naechste Punkt >= MinSegmentPx vom zuletzt gezeichneten entfernt ist.
			FVector2D LastPt = Proj.Project(Line[0]);
			for (int32 Index = 1; Index < Line.Num() && OutLines.Num() < MaxLines; ++Index)
			{
				const FVector2D P = Proj.Project(Line[Index]);
				if (FVector2D::DistSquared(P, LastPt) < MinPxSq)
				{
					continue;  // noch zu nah - Punkt sammeln, nicht zeichnen
				}
				FMinimapLine& L = OutLines.AddDefaulted_GetRef();
				L.Start = LastPt;
				L.End = P;
				L.bMajor = bMajor;
				L.Thickness = bMajor ? 2.2f : 1.0f;
				LastPt = P;
			}
		}
	}
}

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


void FWiesbadenMinimap::BuildWorldMapBuildings(
	const TArray<FGeneratedBuilding>& Buildings, const FWorldMapProjection& Proj,
	int32 MaxQuads, float MinAreaPx, TArray<FWorldMapQuad>& OutQuads)
{
	OutQuads.Reset();
	if (!Proj.IsValid() || Buildings.Num() == 0)
	{
		return;
	}

	// Groesste zuerst: unter dem Deckel sollen die sichtbaren grossen Bauten/
	// Bloecke landen (kleine sind bei Stadt-Zoom ohnehin sub-pixel). Sortierte
	// Index-Liste, damit die Buildings-Array selbst unangetastet bleibt.
	TArray<int32> Order;
	Order.Reserve(Buildings.Num());
	for (int32 I = 0; I < Buildings.Num(); ++I) { Order.Add(I); }
	Order.Sort([&Buildings](int32 A, int32 B)
	{
		return Buildings[A].FootprintAreaSqm > Buildings[B].FootprintAreaSqm;
	});

	const float MinArea = FMath::Max(MinAreaPx, 0.0f);
	for (int32 Idx : Order)
	{
		if (OutQuads.Num() >= MaxQuads) { break; }
		const FGeneratedBuilding& B = Buildings[Idx];
		const double Ex = B.FootprintExtentCm.X;   // Halbmasse entlang Box-X
		const double Ey = B.FootprintExtentCm.Y;   // Halbmasse entlang Box-Y
		if (Ex <= 1.0 || Ey <= 1.0) { continue; }

		// Grob projizierte Flaeche; degenerierte/unsichtbare ueberspringen.
		const double ScrArea = (2.0 * Ex * Proj.ScalePxPerCm) * (2.0 * Ey * Proj.ScalePxPerCm);
		if (ScrArea < MinArea) { continue; }

		const double Yaw = FMath::DegreesToRadians(B.FootprintYawDegrees);
		const FVector2D Fwd(FMath::Cos(Yaw), FMath::Sin(Yaw));   // Box-X in Welt
		const FVector2D Right(-Fwd.Y, Fwd.X);                    // Box-Y in Welt
		const FVector2D Cc = B.FootprintCenterCm;

		auto Corner = [&](double Sx, double Sy)
		{
			const FVector2D W = Cc + Fwd * (Sx * Ex) + Right * (Sy * Ey);
			return Proj.Project(FVector(W.X, W.Y, 0.0));
		};

		FWorldMapQuad Q;
		Q.A = Corner(+1.0, +1.0);
		Q.B = Corner(+1.0, -1.0);
		Q.C = Corner(-1.0, -1.0);
		Q.D = Corner(-1.0, +1.0);
		OutQuads.Add(Q);
	}
}
