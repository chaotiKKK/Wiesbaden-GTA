// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenBuildingClearance.h"

#include "GIS/BuildingGenerator.h"

FIntPoint FWiesbadenBuildingClearance::CellOf(const FVector2D& Point)
{
	return FIntPoint(
		FMath::FloorToInt(Point.X / CellSizeCm),
		FMath::FloorToInt(Point.Y / CellSizeCm));
}

double FWiesbadenBuildingClearance::DistanceToRotatedBox2D(
	const FVector2D& Point, const FVector2D& CenterCm,
	const FVector2D& ExtentCm, double YawDeg)
{
	// In das Rechteck hineindrehen: dort ist es achsparallel und der Abstand
	// eine Zeile. Zurueckgedreht wird nicht - der Abstand ist drehinvariant.
	const double Rad = FMath::DegreesToRadians(YawDeg);
	const double C = FMath::Cos(Rad);
	const double S = FMath::Sin(Rad);

	const double Dx = Point.X - CenterCm.X;
	const double Dy = Point.Y - CenterCm.Y;

	const double LocalX = Dx * C + Dy * S;
	const double LocalY = -Dx * S + Dy * C;

	// Ueberstand je Achse. Negativ heisst "innerhalb" und zaehlt nicht.
	const double OverX = FMath::Max(FMath::Abs(LocalX) - FMath::Abs(ExtentCm.X), 0.0);
	const double OverY = FMath::Max(FMath::Abs(LocalY) - FMath::Abs(ExtentCm.Y), 0.0);

	return FMath::Sqrt(OverX * OverX + OverY * OverY);
}

void FWiesbadenBuildingClearance::BuildAround(
	const TArray<FGeneratedBuilding>& Buildings,
	const FVector2D& Center, double AreaRadiusCm, double ExtraMarginCm)
{
	Footprints.Reset();
	Cells.Reset();

	const double Margin = FMath::Max(ExtraMarginCm, 0.0);

	for (const FGeneratedBuilding& Building : Buildings)
	{
		FVector2D FootCenter = Building.FootprintCenterCm;
		FVector2D FootExtent = Building.FootprintExtentCm;
		double Yaw = Building.FootprintYawDegrees;

		// Rueckfall auf die achsparallele Box: aeltere Backungen koennen den
		// gedrehten Grundriss noch nicht tragen. Lieber etwas zu viel sperren
		// als ein Gebaeude gar nicht zu kennen - genau das war der Fehler.
		if (FootExtent.IsNearlyZero())
		{
			if (!Building.Bounds.IsValid)
			{
				continue;
			}
			const FVector BoxCenter = Building.Bounds.GetCenter();
			const FVector BoxExtent = Building.Bounds.GetExtent();
			FootCenter = FVector2D(BoxCenter.X, BoxCenter.Y);
			FootExtent = FVector2D(BoxExtent.X, BoxExtent.Y);
			Yaw = 0.0;
		}

		// Grob vorsortieren: nur was in Reichweite liegt, kommt ins Gitter.
		const double Reach = FootExtent.Size() + Margin + AreaRadiusCm;
		if (FVector2D::DistSquared(FootCenter, Center) > Reach * Reach)
		{
			continue;
		}

		FFootprint Foot;
		Foot.CenterCm = FootCenter;
		Foot.ExtentCm = FVector2D(
			FMath::Abs(FootExtent.X) + Margin, FMath::Abs(FootExtent.Y) + Margin);
		Foot.YawDeg = Yaw;

		const int32 Index = Footprints.Add(Foot);

		// In alle Zellen eintragen, die der umschliessende Kreis beruehrt. Der
		// Kreis statt der gedrehten Box: eine Zelle zu viel kostet nur einen
		// Abstandstest, eine zu wenig laesst ein Gebaeude verschwinden.
		const double Reichweite = Foot.ExtentCm.Size();
		const FIntPoint Min = CellOf(Foot.CenterCm - FVector2D(Reichweite, Reichweite));
		const FIntPoint Max = CellOf(Foot.CenterCm + FVector2D(Reichweite, Reichweite));
		for (int32 CellX = Min.X; CellX <= Max.X; ++CellX)
		{
			for (int32 CellY = Min.Y; CellY <= Max.Y; ++CellY)
			{
				Cells.FindOrAdd(FIntPoint(CellX, CellY)).Add(Index);
			}
		}
	}
}

bool FWiesbadenBuildingClearance::IsBlocked(const FVector2D& Point, double RadiusCm) const
{
	const double Radius = FMath::Max(RadiusCm, 0.0);

	// Der Kreis kann ueber die eigene Zelle hinausragen - die Nachbarzellen
	// gehoeren dazu, sonst endet die Pruefung an der Zellgrenze.
	const FIntPoint Min = CellOf(Point - FVector2D(Radius, Radius));
	const FIntPoint Max = CellOf(Point + FVector2D(Radius, Radius));

	for (int32 CellX = Min.X; CellX <= Max.X; ++CellX)
	{
		for (int32 CellY = Min.Y; CellY <= Max.Y; ++CellY)
		{
			const TArray<int32>* Indices = Cells.Find(FIntPoint(CellX, CellY));
			if (!Indices)
			{
				continue;
			}
			for (const int32 Index : *Indices)
			{
				const FFootprint& Foot = Footprints[Index];
				if (DistanceToRotatedBox2D(
					Point, Foot.CenterCm, Foot.ExtentCm, Foot.YawDeg) <= Radius)
				{
					return true;
				}
			}
		}
	}

	return false;
}
