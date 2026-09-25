// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "World/WiesbadenDennoWork.h"

using namespace WiesbadenDennoWork;

namespace
{
	/** Sicherheitsabstand zu Moebeln: Denno ist ~35 cm breit, die Arme schwingen. */
	constexpr double MarginCm = 20.0;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoWorkPathsTest,
	"WiesbadenReal.World.DennoWork.Paths",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoWorkPathsTest::RunTest(const FString& Parameters)
{
	// Jede Kante des Wegenetzes frei von Moebeln und Waenden.
	for (const TPair<int32, int32>& Edge : Edges())
	{
		const TCHAR* Hit = SegmentHit(Nodes()[Edge.Key], Nodes()[Edge.Value], MarginCm);
		TestNull(*FString::Printf(TEXT("Kante %d-%d frei (%s)"), Edge.Key, Edge.Value, Hit ? Hit : TEXT("-")), Hit);
	}
	// Jeder Knoten erreicht jeden (Cafe und Friseur haengen am Durchgang).
	for (int32 A = 0; A < Nodes().Num(); ++A)
	{
		for (int32 B = 0; B < Nodes().Num(); ++B)
		{
			if (FindNodePath(A, B).IsEmpty())
			{
				AddError(FString::Printf(TEXT("Knoten %d erreicht %d nicht"), A, B));
			}
		}
	}
	// Plaetze: gerade vom Knoten erreichbar und nicht im Moebel.
	TArray<FDennoSpot> AllSpots = ChoreSpots();
	AllSpots.Add(FetchSpot());
	AllSpots.Add(HandoverSpot());
	AllSpots.Add(RestSpot());
	for (const FDennoSeat& Seat : Seats())
	{
		AllSpots.Add(Seat.Work);
	}
	for (const FDennoSpot& Spot : AllSpots)
	{
		const FString Where = FString::Printf(TEXT("Platz (%.0f, %.0f)"), Spot.Pos.X, Spot.Pos.Y);
		const TCHAR* Hit = SegmentHit(Nodes()[Spot.Node], Spot.Pos, MarginCm);
		TestNull(*FString::Printf(TEXT("%s vom Knoten %d frei (%s)"), *Where, Spot.Node, Hit ? Hit : TEXT("-")), Hit);
		const TCHAR* Inside = SegmentHit(Spot.Pos, Spot.Pos, 10.0);
		TestNull(*FString::Printf(TEXT("%s nicht im Moebel (%s)"), *Where, Inside ? Inside : TEXT("-")), Inside);
	}
	// Gaeste: vom Knoten zum Zugang frei; die Tueren fuehren auf ihren Knoten.
	for (const FDennoSeat& Seat : Seats())
	{
		const TCHAR* Hit = SegmentHit(Nodes()[Seat.Node], Seat.Approach, MarginCm);
		TestNull(*FString::Printf(TEXT("Zugang zu Sitz (%.0f, %.0f) frei (%s)"), Seat.Pos.X, Seat.Pos.Y,
			Hit ? Hit : TEXT("-")), Hit);
	}
	for (const bool bSalon : { false, true })
	{
		const TCHAR* Hit = SegmentHit(DoorOutside(bSalon), Nodes()[DoorNode(bSalon)], MarginCm);
		TestNull(*FString::Printf(TEXT("%s-Tuer frei (%s)"), bSalon ? TEXT("Friseur") : TEXT("Cafe"),
			Hit ? Hit : TEXT("-")), Hit);
	}

	// Die Pruefung selbst: eine Strecke quer durch den Tresen faellt auf.
	TestNotNull(TEXT("Strecke durch den Tresen wird erkannt"), SegmentHit({ 300.0, 500.0 }, { 300.0, 640.0 }, 0.0));
	TestNull(TEXT("Strecke im freien Raum nicht"), SegmentHit({ 300.0, 300.0 }, { 470.0, 300.0 }, 0.0));

	// Wegsuche: vom Cafe in den Friseur durch den Durchgang.
	const TArray<FVector2D> Path = PathFromNode(DoorNode(false), Seats()[3].Node, { Seats()[3].Approach });
	TestTrue(TEXT("Weg Cafetuer -> Friseurstuhl endet am Zugang"), !Path.IsEmpty() && Path.Last() == Seats()[3].Approach);
	TestTrue(TEXT("... und geht durch die Trennwand-Oeffnung (Y 340..460)"), Path.ContainsByPredicate(
		[](const FVector2D& P) { return FMath::Abs(P.X) < 80.0 && P.Y > 340.0 && P.Y < 460.0; }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDennoWorkPlanTest,
	"WiesbadenReal.World.DennoWork.Plan",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDennoWorkPlanTest::RunTest(const FString& Parameters)
{
	FRandomStream Random(42);
	int32 SpotIndex = INDEX_NONE;
	const int32 CafeSeat = 1;
	const int32 SalonSeat = 3;
	TestFalse(TEXT("Sitz 2 ist im Cafe"), Seats()[CafeSeat].bSalon);
	TestTrue(TEXT("Sitz 4 ist im Friseur"), Seats()[SalonSeat].bSalon);

	// Gaeste zuerst, der Friseurstuhl vor dem Cafe.
	FDennoTaskPick Pick = PickTask({ { CafeSeat, true, false }, { SalonSeat, true, false } }, INDEX_NONE, Random, SpotIndex);
	TestTrue(TEXT("Friseurgast wartet: Haare schneiden"), Pick.Task == EDennoTask::CutHair && Pick.Seat == SalonSeat);
	TestTrue(TEXT("... hinter seinem Stuhl"), Pick.Spot.Pos == Seats()[SalonSeat].Work.Pos);
	TestTrue(TEXT("... 10-14 s"), Pick.Seconds >= 10.0 && Pick.Seconds <= 14.0);
	Pick = PickTask({ { CafeSeat, true, false } }, INDEX_NONE, Random, SpotIndex);
	TestTrue(TEXT("Cafegast wartet: erst die Bestellung holen"), Pick.Task == EDennoTask::Fetch && Pick.Seat == CafeSeat);
	TestTrue(TEXT("... am Tresen"), Pick.Spot.Pos == FetchSpot().Pos);
	for (const FDennoGuestView& Guest : TArray<FDennoGuestView>{ { CafeSeat, false, false }, { SalonSeat, true, true } })
	{
		Pick = PickTask({ Guest }, INDEX_NONE, Random, SpotIndex);
		TestTrue(TEXT("Gast unterwegs oder schon bedient: aufraeumen"),
			Pick.Task == EDennoTask::Sweep || Pick.Task == EDennoTask::Wipe || Pick.Task == EDennoTask::Tidy);
	}

	// Aufraeumen: alle drei Arten, nie derselbe Platz zweimal hintereinander.
	int32 Last = INDEX_NONE;
	TSet<EDennoTask> Kinds;
	for (int32 I = 0; I < 300; ++I)
	{
		Pick = PickTask({}, Last, Random, SpotIndex);
		TestTrue(TEXT("Putzplatz gewaehlt"), ChoreSpots().IsValidIndex(SpotIndex));
		if (SpotIndex == Last)
		{
			AddError(FString::Printf(TEXT("Platz %d zweimal hintereinander"), SpotIndex));
		}
		TestTrue(TEXT("Dauer 4-8 s (hektisch)"), Pick.Seconds >= 4.0 && Pick.Seconds <= 8.0);
		TestTrue(TEXT("Aufgabe passt zum Platz"), Pick.Task == ChoreSpots()[SpotIndex].Task);
		Kinds.Add(Pick.Task);
		Last = SpotIndex;
	}
	TestEqual(TEXT("fegen, wischen und aufraeumen kommen vor"), Kinds.Num(), 3);

	// Freie Sitze: nur der passenden Art, nie ein besetzter.
	const TArray<FDennoGuestView> Two = { { 0, true, false }, { 2, false, false } };
	for (int32 I = 0; I < 20; ++I)
	{
		TestEqual(TEXT("einziger freier Cafeplatz"), PickFreeSeat(Two, false, Random), 1);
		const int32 Salon = PickFreeSeat(Two, true, Random);
		TestTrue(TEXT("Friseurplatz"), Salon == 3 || Salon == 4);
	}
	TestEqual(TEXT("Cafe voll"), PickFreeSeat({ { 0, true, false }, { 1, true, false }, { 2, true, false } }, false, Random),
		static_cast<int32>(INDEX_NONE));

	// Bewegungen je Aufgabe.
	TestTrue(TEXT("Holen = Aufraeumen am Tresen"), WorkAnim(EDennoTask::Fetch) == EDennoAnim::Tidy);
	TestTrue(TEXT("Uebergabe einmal"), IsOneShot(EDennoTask::Handover) && IsOneShot(EDennoTask::Serve));
	TestFalse(TEXT("Fegen als Schleife"), IsOneShot(EDennoTask::Sweep));
	TestTrue(TEXT("Hektisch, aber der Schritt passt: Abspieltempo 0,8-1,2"),
		WalkSpeedCmS / WalkAnimSpeedCmS > 0.8 && WalkSpeedCmS / WalkAnimSpeedCmS < 1.2);
	return true;
}
