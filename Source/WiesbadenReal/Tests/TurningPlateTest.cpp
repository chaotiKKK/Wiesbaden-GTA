// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/GeoCoordinateConverter.h"
#include "GIS/IHeightSampler.h"
#include "GIS/RoadNetworkGenerator.h"
#include "GIS/RoadTypeLibrary.h"
#include "GIS/WiesbadenRoadClearance.h"
#include "GIS/WiesbadenTrafficSimulation.h"

namespace
{
	/** Gelaende mit 8 % Gefaelle nach Osten. */
	class FTurningPlateSlope final : public IHeightSampler
	{
	public:
		virtual double SampleHeightCm(const FVector2D& WorldXY) const override { return 11000.0 + WorldXY.X * 0.08; }
		virtual bool HasValidData() const override { return true; }
	};

	FOSMWay MakeTurningPlateRoad(FOSMId Id, const TArray<FOSMId>& NodeIds)
	{
		FOSMWay Way;
		Way.Id = Id;
		Way.NodeIds = NodeIds;
		Way.Tags.Add(TEXT("highway"), TEXT("residential"));
		return Way;
	}
}

/**
 * Wendeplatten an Sackgassen (URoadNetworkGenerator::BuildTurningPlates).
 *
 * Wendende Autos fuhren ueber die Wiese: die Wendeschleife lag hinter dem
 * Fahrbahnende, dort war kein Pflaster. Jetzt liegt an jeder Sackgasse eine
 * Platte, die die Schleife samt halber Fahrzeugbreite abdeckt, auf der Ebene
 * der Strasse - und Baeume/Laternen halten Abstand.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTurningPlateTest,
	"WiesbadenReal.GIS.RoadNetworkGenerator.Wendeplatten",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTurningPlateTest::RunTest(const FString& Parameters)
{
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>();
	Converter->InitializeWithWiesbadenOrigin();
	URoadTypeLibrary* TypeLibrary = NewObject<URoadTypeLibrary>();
	TypeLibrary->ApplyBuiltInDefaults();

	// T-Kreuzung in Node 1; Ost-, Nord- und Westende sind Sackgassen.
	FOSMDataSet DataSet;
	DataSet.Nodes.Add(1, FOSMNode(1, 8.2400, 50.0824));
	DataSet.Nodes.Add(2, FOSMNode(2, 8.2410, 50.0824));
	DataSet.Nodes.Add(3, FOSMNode(3, 8.2400, 50.0830));
	DataSet.Nodes.Add(4, FOSMNode(4, 8.2390, 50.0824));
	DataSet.Ways.Add(100, MakeTurningPlateRoad(100, { 2, 1 }));
	DataSet.Ways.Add(101, MakeTurningPlateRoad(101, { 3, 1 }));
	DataSet.Ways.Add(102, MakeTurningPlateRoad(102, { 4, 1 }));

	const FTurningPlateSlope Slope;
	FRoadGenerationSettings Settings;
	FRoadNetwork Network;
	FRoadMeshData MeshData;
	URoadNetworkGenerator* Generator = NewObject<URoadNetworkGenerator>();
	const FRoadGenerationReport Report = Generator->Generate(DataSet, Converter, TypeLibrary, &Slope, Settings, Network, &MeshData);
	if (!TestTrue(TEXT("Netz erzeugt"), Report.bSuccess))
	{
		return false;
	}
	if (!TestEqual(TEXT("drei Sackgassen - drei Wendeplatten"), Network.TurningPlates.Num(), 3))
	{
		return false;
	}

	// Die Wendeschleifen des Verkehrs liegen ganz auf den Platten, samt halber
	// Fahrzeugbreite, und fahren auf deren Ebene.
	FRoadNetwork Verkehr = Network;
	FWiesbadenTrafficSimulation::AddDeadEndTurnarounds(Verkehr);
	int32 Schleifen = 0;
	for (const FLaneConnection& C : Verkehr.Connections)
	{
		// Wenden im selben Abschnitt: die Haarnadel aus dem Generator ist jetzt
		// eine Schleife ueber die Platte.
		if (C.TurnType != ETurnType::UTurn
			|| Verkehr.Lanes[C.FromLaneId].SegmentId != Verkehr.Lanes[C.ToLaneId].SegmentId)
		{
			continue;
		}
		const FRoadTurningPlate* Platte = Network.TurningPlates.FindByPredicate(
			[&C](const FRoadTurningPlate& P) { return P.LaneId == C.FromLaneId; });
		if (!TestNotNull(TEXT("Schleife hat ihre Platte"), Platte))
		{
			continue;
		}
		++Schleifen;
		double Weitester = 0.0;
		double HoehenFehler = 0.0;
		for (int32 i = 1; i + 1 < C.ConnectionPath.Num(); ++i)
		{
			const FVector& P = C.ConnectionPath[i];
			Weitester = FMath::Max(Weitester, FVector2D::Distance(FVector2D(P), FVector2D(Platte->Center)));
			HoehenFehler = FMath::Max(HoehenFehler, FMath::Abs(P.Z - Platte->HeightAt(FVector2D(P))));
		}
		TestTrue(FString::Printf(TEXT("Schleife samt 95 cm halber Breite auf der Platte (%.0f + 95 <= %.0f cm)"),
			Weitester, Platte->RadiusCm), Weitester + 95.0 <= Platte->RadiusCm);
		TestTrue(FString::Printf(TEXT("Schleife faehrt auf der Plattenebene (%.1f cm)"), HoehenFehler), HoehenFehler < 0.5);
	}
	TestEqual(TEXT("drei Wendeschleifen"), Schleifen, 3);

	for (const FRoadTurningPlate& Platte : Network.TurningPlates)
	{
		// Die Platte setzt die Strasse fort: Laengsgefaelle wie der Hang in
		// Fahrtrichtung (Ostast 8 %, Nordast 0 %), quer eben wie das
		// Fahrbahnband, und am Fahrbahnende ohne Stufe. Den Hang quer dazu
		// gleicht das Gelaendeanschmiegen beim Bake an das Pflaster an.
		const FRoadLane& Spur = Network.Lanes[Platte.LaneId];
		const FVector2D D = FVector2D(Spur.GetExitDirection()).GetSafeNormal();
		const double Laengs = FVector2D::DotProduct(Platte.Gradient, D);
		TestTrue(FString::Printf(TEXT("Laengsgefaelle wie der Hang (%.3f statt %.3f)"), Laengs, 0.08 * D.X),
			FMath::Abs(Laengs - 0.08 * D.X) < 0.01);
		TestTrue(TEXT("quer eben"), (Platte.Gradient - D * Laengs).Size() < 1e-6);
		const double Stufe = FMath::Abs(Platte.HeightAt(FVector2D(Spur.GetEndPoint())) - Spur.GetEndPoint().Z);
		TestTrue(FString::Printf(TEXT("keine Stufe am Fahrbahnende (%.1f cm)"), Stufe), Stufe < 2.0);

		// Pflaster im Kanal der Kreuzungen (Material, Kollision, Hoehenabfrage).
		int32 Ecken = 0;
		for (const FRoadMeshSection& Section : MeshData.Sections)
		{
			if (Section.Channel != ERoadMeshChannel::Intersection)
			{
				continue;
			}
			for (const FVector& V : Section.Vertices)
			{
				Ecken += FVector2D::Distance(FVector2D(V), FVector2D(Platte.Center)) <= Platte.RadiusCm + 1.0 ? 1 : 0;
			}
		}
		TestTrue(FString::Printf(TEXT("Pflaster auf der Platte (%d Ecken)"), Ecken), Ecken >= 10);
	}

	// Freihaltung: der hintere Rand der Platte liegt ausserhalb jedes
	// Fahrbahnbandes - nur die Platte haelt ihn frei.
	const FRoadTurningPlate& Erste = Network.TurningPlates[0];
	const FVector2D Aussen = FVector2D(Erste.Center)
		+ (FVector2D(Erste.Center) - FVector2D(Network.Lanes[Erste.LaneId].GetEndPoint())).GetSafeNormal() * (Erste.RadiusCm - 30.0);
	FWiesbadenRoadClearance MitPlatten;
	MitPlatten.Build(Network, 0.0, false);
	TestTrue(TEXT("Baeume halten Abstand vom Plattenrand"), MitPlatten.IsBlocked(Aussen));
	FRoadNetwork OhnePlatten = Network;
	OhnePlatten.TurningPlates.Reset();
	FWiesbadenRoadClearance NurBaender;
	NurBaender.Build(OhnePlatten, 0.0, false);
	TestFalse(TEXT("Gegenprobe: ohne Platte waere der Rand frei"), NurBaender.IsBlocked(Aussen));

	// Abschaltbar: ohne Wendeplatten bleibt das Netz wie bisher.
	FRoadGenerationSettings Ohne = Settings;
	Ohne.bGenerateTurningPlates = false;
	FRoadNetwork NetzOhne;
	Generator->Generate(DataSet, Converter, TypeLibrary, &Slope, Ohne, NetzOhne, nullptr);
	TestEqual(TEXT("abgeschaltet: keine Platten"), NetzOhne.TurningPlates.Num(), 0);
	return true;
}
