// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenDennoWork.h"

// Alle Masse Laden-lokal in cm aus Tools/Blender/build_denno_shop.py: dort
// liegt der Laden in Blender bei y < 0 (ins Haus), hier ist +Y ins Haus -
// Laden-Y = -Blender-y * 100, X unveraendert.

namespace WiesbadenDennoWork
{
namespace
{
	enum ENode : int32
	{
		CafeDoor, CafeWest, CafeMid, CafeEast, CafeNorth, CafeTables12, CafeTables23,
		CafeCounter, CafeCounterWest, CafeCounterEast, CafePassage,
		SalonPassage, SalonMid, SalonBack, SalonFront, SalonDoor, SalonChair1, SalonChair2,
		NodeCount
	};

	FDennoSpot Spot(double X, double Y, double Yaw, int32 Node, EDennoTask Task)
	{
		FDennoSpot S;
		S.Pos = FVector2D(X, Y);
		S.YawDeg = Yaw;
		S.Node = Node;
		S.Task = Task;
		return S;
	}
}

const TArray<FVector2D>& Nodes()
{
	static const TArray<FVector2D> N = {
		{ 97.0, 55.0 },     // CafeDoor: hinter der Cafetuer (Tuer X 40..155)
		{ 97.0, 300.0 },    // CafeWest: Gang an der Trennwand
		{ 305.0, 300.0 },   // CafeMid: hinter den Tischen
		{ 475.0, 300.0 },   // CafeEast
		{ 570.0, 330.0 },   // CafeNorth: vor der Sitzbank
		{ 305.0, 150.0 },   // CafeTables12: zwischen Tisch 1 und 2
		{ 475.0, 150.0 },   // CafeTables23: zwischen Tisch 2 und 3
		{ 305.0, 520.0 },   // CafeCounter: vor dem Tresen
		{ 180.0, 520.0 },   // CafeCounterWest: Siebtraeger
		{ 450.0, 520.0 },   // CafeCounterEast: Kuchenvitrine
		{ 55.0, 400.0 },    // CafePassage: Durchgang in der Trennwand (Y 340..460)
		{ -55.0, 400.0 },   // SalonPassage
		{ -300.0, 300.0 },  // SalonMid
		{ -300.0, 480.0 },  // SalonBack: vor Waschplatz und Handtuechern
		{ -300.0, 100.0 },  // SalonFront
		{ -97.0, 55.0 },    // SalonDoor: hinter der Friseurtuer
		{ -470.0, 180.0 },  // SalonChair1: hinter dem ersten Friseurstuhl
		{ -470.0, 390.0 },  // SalonChair2
	};
	check(N.Num() == NodeCount);
	return N;
}

const TArray<TPair<int32, int32>>& Edges()
{
	static const TArray<TPair<int32, int32>> E = {
		{ CafeDoor, CafeWest }, { CafeWest, CafeMid }, { CafeMid, CafeEast }, { CafeEast, CafeNorth },
		{ CafeMid, CafeTables12 }, { CafeEast, CafeTables23 }, { CafeMid, CafeCounter },
		{ CafeWest, CafeCounterWest }, { CafeCounter, CafeCounterWest }, { CafeCounter, CafeCounterEast },
		{ CafeEast, CafeCounterEast }, { CafeWest, CafePassage }, { CafePassage, SalonPassage },
		{ SalonPassage, SalonMid }, { SalonPassage, SalonBack }, { SalonMid, SalonBack },
		{ SalonMid, SalonFront }, { SalonFront, SalonDoor }, { SalonMid, SalonChair1 },
		{ SalonMid, SalonChair2 }, { SalonBack, SalonChair2 }, { SalonChair1, SalonChair2 },
	};
	return E;
}

TArray<int32> FindNodePath(int32 From, int32 To)
{
	const TArray<FVector2D>& N = Nodes();
	if (!N.IsValidIndex(From) || !N.IsValidIndex(To))
	{
		return {};
	}
	TArray<double> Dist;
	TArray<int32> Prev;
	TArray<bool> Done;
	Dist.Init(TNumericLimits<double>::Max(), N.Num());
	Prev.Init(INDEX_NONE, N.Num());
	Done.Init(false, N.Num());
	Dist[From] = 0.0;
	for (int32 Step = 0; Step < N.Num(); ++Step)
	{
		int32 Best = INDEX_NONE;
		for (int32 I = 0; I < N.Num(); ++I)
		{
			if (!Done[I] && Dist[I] < TNumericLimits<double>::Max() && (Best == INDEX_NONE || Dist[I] < Dist[Best]))
			{
				Best = I;
			}
		}
		if (Best == INDEX_NONE || Best == To)
		{
			break;
		}
		Done[Best] = true;
		for (const TPair<int32, int32>& Edge : Edges())
		{
			const int32 Other = Edge.Key == Best ? Edge.Value : Edge.Value == Best ? Edge.Key : INDEX_NONE;
			if (Other == INDEX_NONE || Done[Other])
			{
				continue;
			}
			const double Via = Dist[Best] + FVector2D::Distance(N[Best], N[Other]);
			if (Via < Dist[Other])
			{
				Dist[Other] = Via;
				Prev[Other] = Best;
			}
		}
	}
	if (From != To && Prev[To] == INDEX_NONE)
	{
		return {};
	}
	TArray<int32> Path;
	for (int32 At = To; At != INDEX_NONE; At = Prev[At])
	{
		Path.Insert(At, 0);
		if (At == From)
		{
			break;
		}
	}
	return Path;
}

int32 NearestNode(const FVector2D& Pos)
{
	const TArray<FVector2D>& N = Nodes();
	int32 Best = 0;
	for (int32 I = 1; I < N.Num(); ++I)
	{
		if (FVector2D::DistSquared(N[I], Pos) < FVector2D::DistSquared(N[Best], Pos))
		{
			Best = I;
		}
	}
	return Best;
}

const TArray<FDennoSpot>& ChoreSpots()
{
	static const TArray<FDennoSpot> S = {
		// Fegen: freie Bodenflaechen in Cafe und Friseur.
		Spot(200.0, 400.0, 45.0, CafeWest, EDennoTask::Sweep),
		Spot(400.0, 420.0, 0.0, CafeMid, EDennoTask::Sweep),
		Spot(560.0, 420.0, 90.0, CafeNorth, EDennoTask::Sweep),
		Spot(130.0, 180.0, 90.0, CafeDoor, EDennoTask::Sweep),
		Spot(-400.0, 290.0, 180.0, SalonMid, EDennoTask::Sweep),
		Spot(-180.0, 400.0, 180.0, SalonPassage, EDennoTask::Sweep),
		Spot(-200.0, 100.0, 0.0, SalonFront, EDennoTask::Sweep),
		// Tische abwischen: von den Gaengen zwischen den Tischen aus.
		Spot(285.0, 135.0, 180.0, CafeTables12, EDennoTask::Wipe),
		Spot(325.0, 135.0, 0.0, CafeTables12, EDennoTask::Wipe),
		Spot(455.0, 135.0, 180.0, CafeTables23, EDennoTask::Wipe),
		Spot(495.0, 135.0, 0.0, CafeTables23, EDennoTask::Wipe),
		// Aufraeumen: Tresen (drei Stellen), Handtuchregal, Empfang.
		Spot(180.0, 545.0, 90.0, CafeCounterWest, EDennoTask::Tidy),
		Spot(330.0, 545.0, 90.0, CafeCounter, EDennoTask::Tidy),
		Spot(450.0, 545.0, 90.0, CafeCounterEast, EDennoTask::Tidy),
		Spot(-340.0, 590.0, 90.0, SalonBack, EDennoTask::Tidy),
		Spot(-155.0, 255.0, -90.0, SalonMid, EDennoTask::Tidy),
	};
	return S;
}

const TArray<FDennoSeat>& Seats()
{
	static const TArray<FDennoSeat> S = []
	{
		TArray<FDennoSeat> Out;
		// Cafe: die hinteren Stuehle der drei Bistrotische (Blick zum Fenster);
		// die vorderen stehen zwischen Tisch und Fensterbruestung ohne Zugang.
		const double TableX[] = { 220.0, 390.0, 560.0 };
		const int32 TableNode[] = { CafeMid, CafeMid, CafeEast };
		const FDennoSpot ServeSpot[] = {
			Spot(285.0, 135.0, 180.0, CafeTables12, EDennoTask::Serve),
			Spot(325.0, 135.0, 0.0, CafeTables12, EDennoTask::Serve),
			Spot(495.0, 135.0, 0.0, CafeTables23, EDennoTask::Serve) };
		for (int32 I = 0; I < 3; ++I)
		{
			FDennoSeat Seat;
			Seat.Pos = FVector2D(TableX[I], 197.0);
			Seat.YawDeg = -90.0;
			Seat.Approach = FVector2D(TableX[I], 250.0);
			Seat.Node = TableNode[I];
			Seat.Work = ServeSpot[I];
			Seat.CupPos = FVector2D(TableX[I] + 6.0, 158.0);
			Out.Add(Seat);
		}
		// Friseur: zwei Stuehle vor den Spiegeln, Blick zur Suedwand (-X).
		const double ChairY[] = { 180.0, 390.0 };
		const int32 ChairNode[] = { SalonChair1, SalonChair2 };
		for (int32 I = 0; I < 2; ++I)
		{
			FDennoSeat Seat;
			Seat.Pos = FVector2D(-565.0, ChairY[I]);
			Seat.YawDeg = 180.0;
			Seat.LiftCm = 10.0;
			Seat.Approach = Nodes()[ChairNode[I]];
			Seat.Node = ChairNode[I];
			Seat.bSalon = true;
			Seat.Work = Spot(-505.0, ChairY[I], 180.0, ChairNode[I], EDennoTask::CutHair);
			Out.Add(Seat);
		}
		return Out;
	}();
	return S;
}

FDennoSpot FetchSpot()
{
	return Spot(200.0, 545.0, 90.0, CafeCounterWest, EDennoTask::Fetch);   // am Siebtraeger
}

FDennoSpot HandoverSpot()
{
	// In der Cafetuer auf der Schwelle (Stufe auf Ladenbodenhoehe) - weiter
	// draussen stuende sie ueber dem tiefer liegenden Gehweg in der Luft.
	return Spot(97.0, -35.0, -90.0, CafeDoor, EDennoTask::Handover);
}

FDennoSpot RestSpot()
{
	return Spot(300.0, 340.0, -90.0, CafeMid, EDennoTask::Idle);
}

FVector2D DoorOutside(bool bSalon)
{
	return FVector2D(bSalon ? -97.0 : 97.0, -35.0);
}

int32 DoorNode(bool bSalon)
{
	return bSalon ? SalonDoor : CafeDoor;
}

TArray<FVector2D> PathFromNode(int32 FromNode, int32 ToNode, const TArray<FVector2D>& Tail)
{
	TArray<FVector2D> Out;
	const TArray<int32> Chain = FindNodePath(FromNode, ToNode);
	for (int32 I = 1; I < Chain.Num(); ++I)
	{
		Out.Add(Nodes()[Chain[I]]);
	}
	Out.Append(Tail);
	return Out;
}

const TArray<FObstacle>& Obstacles()
{
	static const TArray<FObstacle> O = {
		{ { 105.0, 575.0 }, { 565.0, 650.0 }, TEXT("Tresen") },
		{ { 184.0, 52.0 }, { 256.0, 218.0 }, TEXT("Tisch 1 mit Stuehlen") },
		{ { 354.0, 52.0 }, { 426.0, 218.0 }, TEXT("Tisch 2 mit Stuehlen") },
		{ { 524.0, 52.0 }, { 596.0, 218.0 }, TEXT("Tisch 3 mit Stuehlen") },
		{ { 625.0, 240.0 }, { 680.0, 480.0 }, TEXT("Sitzbank") },
		{ { 610.0, 60.0 }, { 670.0, 120.0 }, TEXT("Pflanze Cafe") },
		{ { -6.0, 35.0 }, { 6.0, 340.0 }, TEXT("Trennwand vorn") },
		{ { -6.0, 460.0 }, { 6.0, 650.0 }, TEXT("Trennwand hinten") },
		{ { -680.0, 120.0 }, { -645.0, 240.0 }, TEXT("Spiegelablage 1") },
		{ { -680.0, 330.0 }, { -645.0, 450.0 }, TEXT("Spiegelablage 2") },
		{ { -595.0, 150.0 }, { -535.0, 210.0 }, TEXT("Friseurstuhl 1") },
		{ { -595.0, 360.0 }, { -535.0, 420.0 }, TEXT("Friseurstuhl 2") },
		{ { -275.0, 515.0 }, { -165.0, 650.0 }, TEXT("Waschplatz") },
		{ { -380.0, 620.0 }, { -300.0, 650.0 }, TEXT("Handtuchregal") },
		{ { -225.0, 165.0 }, { -85.0, 225.0 }, TEXT("Empfang") },
		{ { -55.0, 100.0 }, { -6.0, 320.0 }, TEXT("Wartebank") },
		{ { -665.0, 570.0 }, { -605.0, 630.0 }, TEXT("Pflanze Friseur") },
		{ { 175.0, -2.0 }, { 635.0, 25.0 }, TEXT("Fensterbruestung Cafe") },
		{ { -635.0, -2.0 }, { -175.0, 25.0 }, TEXT("Fensterbruestung Friseur") },
		{ { 680.0, 0.0 }, { 700.0, 650.0 }, TEXT("Nordwand") },
		{ { -700.0, 0.0 }, { -680.0, 650.0 }, TEXT("Suedwand") },
		{ { -700.0, 650.0 }, { 700.0, 670.0 }, TEXT("Rueckwand") },
	};
	return O;
}

const TCHAR* SegmentHit(const FVector2D& A, const FVector2D& B, double MarginCm)
{
	for (const FObstacle& O : Obstacles())
	{
		const FVector2D Lo = O.Min - FVector2D(MarginCm, MarginCm);
		const FVector2D Hi = O.Max + FVector2D(MarginCm, MarginCm);
		// Liang-Barsky: Strecke gegen achsenparallelen Kasten.
		double T0 = 0.0, T1 = 1.0;
		const FVector2D D = B - A;
		bool bMiss = false;
		const double P[4] = { -D.X, D.X, -D.Y, D.Y };
		const double Q[4] = { A.X - Lo.X, Hi.X - A.X, A.Y - Lo.Y, Hi.Y - A.Y };
		for (int32 K = 0; K < 4 && !bMiss; ++K)
		{
			if (FMath::IsNearlyZero(P[K]))
			{
				bMiss = Q[K] < 0.0;
				continue;
			}
			const double R = Q[K] / P[K];
			if (P[K] < 0.0)
			{
				T0 = FMath::Max(T0, R);
			}
			else
			{
				T1 = FMath::Min(T1, R);
			}
			bMiss = T0 > T1;
		}
		if (!bMiss)
		{
			return O.Name;
		}
	}
	return nullptr;
}

EDennoAnim WorkAnim(EDennoTask Task)
{
	switch (Task)
	{
	case EDennoTask::Sweep: return EDennoAnim::Sweep;
	case EDennoTask::Wipe: return EDennoAnim::Wipe;
	case EDennoTask::Tidy:
	case EDennoTask::Fetch: return EDennoAnim::Tidy;
	case EDennoTask::CutHair: return EDennoAnim::CutHair;
	case EDennoTask::Serve: return EDennoAnim::Serve;
	case EDennoTask::Handover: return EDennoAnim::Handover;
	default: return EDennoAnim::Idle;
	}
}

bool IsOneShot(EDennoTask Task)
{
	return Task == EDennoTask::Serve || Task == EDennoTask::Handover;
}

FDennoTaskPick PickTask(const TArray<FDennoGuestView>& Guests, int32 LastSpot, FRandomStream& Random,
	int32& OutSpotIndex)
{
	OutSpotIndex = INDEX_NONE;
	FDennoTaskPick Pick;
	// Gaeste zuerst - der Friseurstuhl vor dem Cafe: ein halber Haarschnitt
	// wartet schlechter als ein Kaffee.
	for (const bool bSalon : { true, false })
	{
		for (const FDennoGuestView& Guest : Guests)
		{
			if (!Seats().IsValidIndex(Guest.Seat) || !Guest.bSeated || Guest.bServed
				|| Seats()[Guest.Seat].bSalon != bSalon)
			{
				continue;
			}
			Pick.Seat = Guest.Seat;
			if (bSalon)
			{
				Pick.Task = EDennoTask::CutHair;
				Pick.Spot = Seats()[Guest.Seat].Work;
				Pick.Seconds = Random.FRandRange(10.0f, 14.0f);
			}
			else
			{
				Pick.Task = EDennoTask::Fetch;
				Pick.Spot = FetchSpot();
				Pick.Seconds = FetchSeconds;
			}
			return Pick;
		}
	}
	// Sonst aufraeumen: erst die Art (fegen 35 %, wischen 30 %, aufraeumen 35 %),
	// dann ein Platz dieser Art - nie derselbe Platz zweimal hintereinander.
	const float Roll = Random.FRand();
	const EDennoTask Kind = Roll < 0.35f ? EDennoTask::Sweep : Roll < 0.65f ? EDennoTask::Wipe : EDennoTask::Tidy;
	TArray<int32> Candidates;
	for (int32 I = 0; I < ChoreSpots().Num(); ++I)
	{
		if (ChoreSpots()[I].Task == Kind && I != LastSpot)
		{
			Candidates.Add(I);
		}
	}
	OutSpotIndex = Candidates[Random.RandRange(0, Candidates.Num() - 1)];
	Pick.Task = Kind;
	Pick.Spot = ChoreSpots()[OutSpotIndex];
	Pick.Seconds = Kind == EDennoTask::Sweep ? Random.FRandRange(5.0f, 8.0f)
		: Kind == EDennoTask::Wipe ? Random.FRandRange(4.0f, 6.0f) : Random.FRandRange(4.0f, 7.0f);
	return Pick;
}

int32 PickFreeSeat(const TArray<FDennoGuestView>& Guests, bool bSalon, FRandomStream& Random)
{
	TArray<int32> Free;
	for (int32 I = 0; I < Seats().Num(); ++I)
	{
		if (Seats()[I].bSalon == bSalon
			&& !Guests.ContainsByPredicate([I](const FDennoGuestView& G) { return G.Seat == I; }))
		{
			Free.Add(I);
		}
	}
	return Free.IsEmpty() ? INDEX_NONE : Free[Random.RandRange(0, Free.Num() - 1)];
}
}
