// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/StreetFurnitureShapes.h"

namespace
{
	/** Kantenlaenge der Engine-Basismeshes (Cube und Cylinder sind 100 cm). */
	constexpr double EngineMeshSizeCm = 100.0;

	/**
	 * Sammelt die Teile eines Moebels im OERTLICHEN System und setzt sie zum
	 * Schluss in die Welt.
	 *
	 * Oertlich heisst: Ursprung am Fusspunkt, +X ist die Blickrichtung des
	 * Moebels, +Y quer nach links, +Z nach oben. So steht in jeder Bauvorschrift
	 * unten "45 cm hoch" und nicht eine Weltkoordinate.
	 */
	struct FPartBuilder
	{
		const FTransform ToWorld;
		TArray<FFurniturePart>& Parts;

		FPartBuilder(const FFurnitureInstance& Instance, TArray<FFurniturePart>& InParts)
			: ToWorld(FTransform(Instance.Rotation, Instance.Location))
			, Parts(InParts)
		{
		}

		/** Quader: Mitte und Kantenlaengen in Zentimetern. */
		void Box(EFurnitureMaterialKind Material, const FVector& CenterCm, const FVector& SizeCm)
		{
			Add(EFurnitureMeshKind::Box, Material, CenterCm, SizeCm);
		}

		/** Stehender Zylinder: Mitte, Durchmesser, Hoehe. */
		void Cylinder(EFurnitureMaterialKind Material, const FVector& CenterCm,
			double DiameterCm, double HeightCm)
		{
			Add(EFurnitureMeshKind::Cylinder, Material, CenterCm,
				FVector(DiameterCm, DiameterCm, HeightCm));
		}

	private:
		void Add(EFurnitureMeshKind Mesh, EFurnitureMaterialKind Material,
			const FVector& CenterCm, const FVector& SizeCm)
		{
			FFurniturePart Part;
			Part.Mesh = Mesh;
			Part.Material = Material;
			// Die Skalierung wandert mit in die Welt-Transformation; die
			// Moebel-Drehung ist reiner Yaw, also bleibt die Skalierung
			// achsentreu (kein Scherfall).
			Part.Transform = FTransform(
				FQuat::Identity, CenterCm, SizeCm / EngineMeshSizeCm) * ToWorld;
			Parts.Add(Part);
		}
	};
}

int32 WiesbadenStreetFurniture::GetPartCount(EStreetFurnitureKind Kind, int32 Variant)
{
	switch (Kind)
	{
	// Sitzflaeche + zwei Wangen, Variante 0 zusaetzlich mit Rueckenlehne.
	case EStreetFurnitureKind::Bench:          return Variant == 0 ? 4 : 3;
	// Variante 1 traegt einen Reflektorring.
	case EStreetFurnitureKind::Bollard:        return Variant == 0 ? 1 : 2;
	case EStreetFurnitureKind::WasteBasket:    return 2;   // Korb + Standrohr
	case EStreetFurnitureKind::VendingMachine: return 2;   // Kasten + Front
	case EStreetFurnitureKind::Recycling:      return 2;   // Behaelter + Deckel
	case EStreetFurnitureKind::FireHydrant:    return 2;   // Koerper + Kappe
	case EStreetFurnitureKind::PostBox:        return 2;   // Kasten + Standrohr
	case EStreetFurnitureKind::PicnicTable:    return 7;   // Platte + 2 Baenke + 4 Beine
	default:                                   return 0;
	}
}

void WiesbadenStreetFurniture::BuildParts(
	const FFurnitureInstance& Instance,
	const FStreetFurnitureDimensions& D,
	TArray<FFurniturePart>& OutParts)
{
	FPartBuilder B(Instance, OutParts);
	const double T = FMath::Max(0.5, D.PlankThicknessCm);

	switch (Instance.Kind)
	{
	case EStreetFurnitureKind::Bench:
	{
		// Sitzlatten quer zur Blickrichtung: Laenge liegt auf Y.
		B.Box(EFurnitureMaterialKind::Wood,
			FVector(0.0, 0.0, D.BenchSeatHeightCm),
			FVector(D.BenchDepthCm, D.BenchLengthCm, T));

		// Gusseiserne Wangen an den Enden, vom Boden bis unter die Sitzflaeche.
		for (const double Side : { 1.0, -1.0 })
		{
			B.Box(EFurnitureMaterialKind::Metal,
				FVector(0.0, Side * (D.BenchLengthCm * 0.5 - T), D.BenchSeatHeightCm * 0.5),
				FVector(D.BenchDepthCm, T, D.BenchSeatHeightCm));
		}

		// Lehne im Ruecken des Sitzenden, also entgegen der Blickrichtung.
		if (Instance.Variant == 0 && D.BenchBackHeightCm > 0.0)
		{
			B.Box(EFurnitureMaterialKind::Wood,
				FVector(-(D.BenchDepthCm * 0.5 - T * 0.5), 0.0,
					D.BenchSeatHeightCm + D.BenchBackHeightCm * 0.5),
				FVector(T, D.BenchLengthCm, D.BenchBackHeightCm));
		}
		break;
	}

	case EStreetFurnitureKind::Bollard:
	{
		const double Diameter = D.BollardRadiusCm * 2.0;
		B.Cylinder(EFurnitureMaterialKind::Metal,
			FVector(0.0, 0.0, D.BollardHeightCm * 0.5), Diameter, D.BollardHeightCm);

		if (Instance.Variant != 0)
		{
			// Reflektorring knapp unter der Kuppe, minimal groesser als der
			// Schaft - sonst verschwindet er im Z-Fighting.
			B.Cylinder(EFurnitureMaterialKind::Signal,
				FVector(0.0, 0.0, D.BollardHeightCm - 12.0), Diameter + 1.0, 6.0);
		}
		break;
	}

	case EStreetFurnitureKind::WasteBasket:
	{
		const double Diameter = D.BasketRadiusCm * 2.0;
		const double BasketCenterZ = FMath::Max(
			D.BasketHeightCm * 0.5, D.BasketTopHeightCm - D.BasketHeightCm * 0.5);
		B.Cylinder(EFurnitureMaterialKind::Metal,
			FVector(0.0, 0.0, BasketCenterZ), Diameter, D.BasketHeightCm);

		// Standrohr vom Boden bis zum Korbboden.
		const double PostHeight = FMath::Max(0.0, BasketCenterZ - D.BasketHeightCm * 0.5);
		if (PostHeight > 0.0)
		{
			B.Cylinder(EFurnitureMaterialKind::Metal,
				FVector(0.0, 0.0, PostHeight * 0.5), 8.0, PostHeight);
		}
		break;
	}

	case EStreetFurnitureKind::VendingMachine:
	{
		B.Box(EFurnitureMaterialKind::Metal,
			FVector(0.0, 0.0, D.VendingHeightCm * 0.5),
			FVector(D.VendingDepthCm, D.VendingWidthCm, D.VendingHeightCm));

		// Bedienseite: eine duenne farbige Platte auf der Blickseite.
		B.Box(EFurnitureMaterialKind::Signal,
			FVector(D.VendingDepthCm * 0.5, 0.0, D.VendingHeightCm * 0.62),
			FVector(T * 0.5, D.VendingWidthCm * 0.8, D.VendingHeightCm * 0.5));
		break;
	}

	case EStreetFurnitureKind::Recycling:
	{
		B.Box(EFurnitureMaterialKind::Signal,
			FVector(0.0, 0.0, D.RecyclingHeightCm * 0.5),
			FVector(D.RecyclingDepthCm, D.RecyclingWidthCm, D.RecyclingHeightCm));

		// Deckel etwas ueberstehend - so sieht man die Fuge.
		B.Box(EFurnitureMaterialKind::Metal,
			FVector(0.0, 0.0, D.RecyclingHeightCm + T * 0.5),
			FVector(D.RecyclingDepthCm + 4.0, D.RecyclingWidthCm + 4.0, T));
		break;
	}

	case EStreetFurnitureKind::FireHydrant:
	{
		const double Diameter = D.HydrantRadiusCm * 2.0;
		B.Cylinder(EFurnitureMaterialKind::Signal,
			FVector(0.0, 0.0, D.HydrantHeightCm * 0.5), Diameter, D.HydrantHeightCm);

		// Kappe: flacher, etwas breiterer Deckel.
		B.Cylinder(EFurnitureMaterialKind::Metal,
			FVector(0.0, 0.0, D.HydrantHeightCm + 4.0), Diameter + 4.0, 8.0);
		break;
	}

	case EStreetFurnitureKind::PostBox:
	{
		B.Box(EFurnitureMaterialKind::Signal,
			FVector(0.0, 0.0, D.PostBoxStandHeightCm + D.PostBoxHeightCm * 0.5),
			FVector(D.PostBoxDepthCm, D.PostBoxWidthCm, D.PostBoxHeightCm));

		if (D.PostBoxStandHeightCm > 0.0)
		{
			B.Cylinder(EFurnitureMaterialKind::Metal,
				FVector(0.0, 0.0, D.PostBoxStandHeightCm * 0.5),
				10.0, D.PostBoxStandHeightCm);
		}
		break;
	}

	case EStreetFurnitureKind::PicnicTable:
	{
		// Tischplatte laengs auf Y, wie die Sitzbaenke.
		B.Box(EFurnitureMaterialKind::Wood,
			FVector(0.0, 0.0, D.PicnicTableHeightCm),
			FVector(D.PicnicTableWidthCm, D.PicnicLengthCm, T));

		// Zwei Sitzbaenke, links und rechts der Platte.
		const double SeatOffset = D.PicnicTableWidthCm * 0.5 + 25.0;
		for (const double Side : { 1.0, -1.0 })
		{
			B.Box(EFurnitureMaterialKind::Wood,
				FVector(Side * SeatOffset, 0.0, D.PicnicSeatHeightCm),
				FVector(30.0, D.PicnicLengthCm, T));
		}

		// Vier Beine unter den Plattenecken.
		for (const double SideX : { 1.0, -1.0 })
		{
			for (const double SideY : { 1.0, -1.0 })
			{
				B.Box(EFurnitureMaterialKind::Metal,
					FVector(SideX * (D.PicnicTableWidthCm * 0.5 - T),
						SideY * (D.PicnicLengthCm * 0.5 - T),
						D.PicnicTableHeightCm * 0.5),
					FVector(T, T, D.PicnicTableHeightCm));
			}
		}
		break;
	}

	default:
		break;
	}
}
