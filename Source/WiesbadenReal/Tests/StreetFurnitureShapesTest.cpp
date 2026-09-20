// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "World/StreetFurnitureShapes.h"

namespace
{
	FFurnitureInstance MakeInstance(
		EStreetFurnitureKind Kind, const FVector& Location = FVector::ZeroVector,
		double Yaw = 0.0, int32 Variant = 0)
	{
		FFurnitureInstance Instance;
		Instance.Kind = Kind;
		Instance.Location = Location;
		Instance.Rotation = FRotator(0.0, Yaw, 0.0);
		Instance.Variant = Variant;
		Instance.NodeId = 1;
		return Instance;
	}

	/** Umschliessende Box aller Teile - Engine-Meshes sind 100 cm und zentriert. */
	FBox PartsBounds(const TArray<FFurniturePart>& Parts)
	{
		FBox Box(ForceInit);
		for (const FFurniturePart& Part : Parts)
		{
			const FVector Half = Part.Transform.GetScale3D() * 50.0;
			const FVector Center = Part.Transform.GetLocation();
			// Nur Yaw im Spiel, darum genuegt die Achsenbox der gedrehten Halbmasse.
			const FVector Rotated = Part.Transform.GetRotation().RotateVector(Half).GetAbs();
			Box += FBox(Center - Rotated, Center + Rotated);
		}
		return Box;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStreetFurnitureShapesTest,
	"WiesbadenReal.World.StreetFurniture.Shapes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FStreetFurnitureShapesTest::RunTest(const FString& Parameters)
{
	const FStreetFurnitureDimensions D;

	// --- Die angekuendigte Teilezahl MUSS die gebaute sein --------------------
	//
	// Der Spawner reserviert danach. Liefe der Bauer auseinander, waere der
	// Fehler eine stille Umkopierung bei jedem Bake - nichts, was auffiele.
	for (int32 KindIndex = 0; KindIndex < static_cast<int32>(EStreetFurnitureKind::MAX); ++KindIndex)
	{
		const EStreetFurnitureKind Kind = static_cast<EStreetFurnitureKind>(KindIndex);
		for (int32 Variant = 0; Variant < 3; ++Variant)
		{
			TArray<FFurniturePart> Parts;
			WiesbadenStreetFurniture::BuildParts(MakeInstance(Kind, FVector::ZeroVector, 0.0, Variant), D, Parts);
			TestEqual(
				*FString::Printf(TEXT("Teilezahl stimmt (Art %d, Variante %d)"), KindIndex, Variant),
				Parts.Num(), WiesbadenStreetFurniture::GetPartCount(Kind, Variant));
			TestTrue(*FString::Printf(TEXT("Art %d hat ueberhaupt Teile"), KindIndex), Parts.Num() > 0);
		}
	}

	// --- Nichts steckt im Boden, nichts schwebt, nichts ist riesig -----------
	for (int32 KindIndex = 0; KindIndex < static_cast<int32>(EStreetFurnitureKind::MAX); ++KindIndex)
	{
		const EStreetFurnitureKind Kind = static_cast<EStreetFurnitureKind>(KindIndex);
		TArray<FFurniturePart> Parts;
		WiesbadenStreetFurniture::BuildParts(MakeInstance(Kind), D, Parts);
		const FBox Bounds = PartsBounds(Parts);

		TestTrue(*FString::Printf(TEXT("Art %d sitzt nicht im Boden"), KindIndex),
			Bounds.Min.Z >= -1.0);
		TestTrue(*FString::Printf(TEXT("Art %d beruehrt den Boden"), KindIndex),
			Bounds.Min.Z <= 5.0);
		TestTrue(*FString::Printf(TEXT("Art %d bleibt unter 2,5 m"), KindIndex),
			Bounds.Max.Z <= 250.0);
		TestTrue(*FString::Printf(TEXT("Art %d bleibt unter 3 m breit"), KindIndex),
			Bounds.GetSize().X <= 300.0 && Bounds.GetSize().Y <= 300.0);
	}

	// --- Die Bank: Lehne im Ruecken, Laenge quer zur Blickrichtung -----------
	{
		TArray<FFurniturePart> Parts;
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::Bench), D, Parts);

		// Das hoechste Teil ist die Lehne; sie liegt entgegen der Blickrichtung.
		const FFurniturePart* Highest = nullptr;
		for (const FFurniturePart& Part : Parts)
		{
			if (!Highest || Part.Transform.GetLocation().Z > Highest->Transform.GetLocation().Z)
			{
				Highest = &Part;
			}
		}
		if (TestNotNull(TEXT("Bank hat ein hoechstes Teil"), Highest))
		{
			TestTrue(TEXT("Die Lehne steht im Ruecken (entgegen der Blickrichtung)"),
				Highest->Transform.GetLocation().X < 0.0);
			TestEqual(TEXT("Die Lehne ist aus Holz"),
				Highest->Material, EFurnitureMaterialKind::Wood);
		}

		const FBox Bounds = PartsBounds(Parts);
		TestTrue(TEXT("Die Bank ist quer zur Blickrichtung laenger als tief"),
			Bounds.GetSize().Y > Bounds.GetSize().X);

		// Variante 1 ist die Bank OHNE Lehne und damit niedriger.
		TArray<FFurniturePart> NoBack;
		WiesbadenStreetFurniture::BuildParts(
			MakeInstance(EStreetFurnitureKind::Bench, FVector::ZeroVector, 0.0, 1), D, NoBack);
		TestTrue(TEXT("Bank ohne Lehne ist niedriger"),
			PartsBounds(NoBack).Max.Z < Bounds.Max.Z);
	}

	// --- Drehung und Ort wirken auf alle Teile -------------------------------
	{
		const FVector Wo(123456.0, -654321.0, 4242.0);
		TArray<FFurniturePart> Gedreht;
		WiesbadenStreetFurniture::BuildParts(
			MakeInstance(EStreetFurnitureKind::Bench, Wo, 90.0), D, Gedreht);
		const FBox Bounds = PartsBounds(Gedreht);

		TestTrue(TEXT("Um 90 Grad gedreht liegt die Bank laengs X"),
			Bounds.GetSize().X > Bounds.GetSize().Y);
		TestTrue(TEXT("Die Bank steht am uebergebenen Ort"),
			FMath::Abs(Bounds.GetCenter().X - Wo.X) < 100.0
			&& FMath::Abs(Bounds.GetCenter().Y - Wo.Y) < 100.0);
		TestTrue(TEXT("Und auf der uebergebenen Hoehe"), Bounds.Min.Z >= Wo.Z - 1.0);
	}

	// --- Werkstoffe: der Hydrant ist farbig, der Poller metallisch -----------
	{
		TArray<FFurniturePart> Hydrant;
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::FireHydrant), D, Hydrant);
		TestEqual(TEXT("Hydrantenkoerper traegt Signalfarbe"),
			Hydrant[0].Material, EFurnitureMaterialKind::Signal);
		TestEqual(TEXT("Hydrantenkoerper ist ein Zylinder"),
			Hydrant[0].Mesh, EFurnitureMeshKind::Cylinder);

		TArray<FFurniturePart> Poller;
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::Bollard), D, Poller);
		TestEqual(TEXT("Poller ist aus Metall"), Poller[0].Material, EFurnitureMaterialKind::Metal);
		TestEqual(TEXT("Poller ist ein Zylinder"), Poller[0].Mesh, EFurnitureMeshKind::Cylinder);
	}

	// --- Der Bauer haengt an, er loescht nicht -------------------------------
	//
	// Der Spawner sammelt alle Moebel der Stadt in EINEN Puffer; ein Bauer,
	// der ihn zuruecksetzt, liesse genau ein Moebel uebrig.
	{
		TArray<FFurniturePart> Gesammelt;
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::Bollard), D, Gesammelt);
		const int32 NachErstem = Gesammelt.Num();
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::PostBox), D, Gesammelt);
		TestTrue(TEXT("Zweites Moebel kommt dazu, statt das erste zu ersetzen"),
			Gesammelt.Num() > NachErstem);
	}

	// --- Masse sind Einstellung, nicht Code ----------------------------------
	{
		FStreetFurnitureDimensions Gross = D;
		Gross.BollardHeightCm = D.BollardHeightCm * 2.0;
		TArray<FFurniturePart> Normal;
		TArray<FFurniturePart> Hoch;
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::Bollard), D, Normal);
		WiesbadenStreetFurniture::BuildParts(MakeInstance(EStreetFurnitureKind::Bollard), Gross, Hoch);
		TestTrue(TEXT("Ein hoeherer Sollwert ergibt einen hoeheren Poller"),
			PartsBounds(Hoch).Max.Z > PartsBounds(Normal).Max.Z * 1.5);
	}

	// --- Mesh-Pfade: jede Art hat einen, Varianten nur wo sie gebaut sind ---
	//
	// Der Spawner sucht danach; ein Tippfehler faellt sonst erst im Spiel auf,
	// wo das Moebel still auf die Primitive zurueckfaellt.
	for (int32 KindIndex = 0; KindIndex < static_cast<int32>(EStreetFurnitureKind::MAX); ++KindIndex)
	{
		const EStreetFurnitureKind Kind = static_cast<EStreetFurnitureKind>(KindIndex);
		const FString Pfad = WiesbadenStreetFurniture::GetMeshPath(Kind, 0);
		TestTrue(*FString::Printf(TEXT("Art %d hat einen Mesh-Pfad"), KindIndex), !Pfad.IsEmpty());
		TestTrue(TEXT("Pfad zeigt in den Moebel-Ordner"),
			Pfad.StartsWith(TEXT("/Game/Assets/Furniture/SM_WbFurn_")));
		// Unreal-Objektpfad: Paket.Objekt, beide Teile gleich benannt.
		FString Paket, Objekt;
		TestTrue(TEXT("Pfad traegt den Objektnamen"), Pfad.Split(TEXT("."), &Paket, &Objekt));
		TestTrue(TEXT("Paket endet auf den Objektnamen"), Paket.EndsWith(Objekt));
	}

	{
		// Nur Bank und Poller haben ein zweites Mesh.
		TestNotEqual(TEXT("Bank Variante 1 ist ein eigenes Mesh"),
			WiesbadenStreetFurniture::GetMeshPath(EStreetFurnitureKind::Bench, 1),
			WiesbadenStreetFurniture::GetMeshPath(EStreetFurnitureKind::Bench, 0));
		TestNotEqual(TEXT("Poller Variante 1 ist ein eigenes Mesh"),
			WiesbadenStreetFurniture::GetMeshPath(EStreetFurnitureKind::Bollard, 1),
			WiesbadenStreetFurniture::GetMeshPath(EStreetFurnitureKind::Bollard, 0));
		TestEqual(TEXT("Briefkasten hat nur ein Mesh"),
			WiesbadenStreetFurniture::GetMeshPath(EStreetFurnitureKind::PostBox, 1),
			WiesbadenStreetFurniture::GetMeshPath(EStreetFurnitureKind::PostBox, 0));
	}

	{
		// Die Variantenzahl des Bake-Passes und die Zahl der gebauten Meshes
		// muessen zusammenpassen - sonst zeigt eine Variante ins Leere.
		for (int32 KindIndex = 0; KindIndex < static_cast<int32>(EStreetFurnitureKind::MAX); ++KindIndex)
		{
			const EStreetFurnitureKind Kind = static_cast<EStreetFurnitureKind>(KindIndex);
			const int32 Varianten = URoadFurnitureGenerator::GetFurnitureVariantCount(Kind);
			const bool bEigenesZweites =
				WiesbadenStreetFurniture::GetMeshPath(Kind, 1)
				!= WiesbadenStreetFurniture::GetMeshPath(Kind, 0);
			TestEqual(*FString::Printf(TEXT("Art %d: Variantenzahl passt zum Mesh-Bestand"), KindIndex),
				Varianten > 1, bEigenesZweites);
		}
	}

	return true;
}
