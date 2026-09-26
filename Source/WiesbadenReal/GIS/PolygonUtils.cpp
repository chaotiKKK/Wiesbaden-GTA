// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/PolygonUtils.h"

#include "WiesbadenReal.h"

#include "Algo/Reverse.h"

double FPolygonUtils::ComputeSignedArea(const TArray<FVector2D>& Polygon)
{
	const int32 Count = Polygon.Num();
	if (Count < 3)
	{
		return 0.0;
	}

	// Shoelace-Formel. Summation in double, weil die Koordinaten in cm
	// vorliegen und Flaechen damit schnell 1e10 erreichen - in float waere der
	// Verlust bereits bei einem Stadtblock relevant.
	double Sum = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D& Current = Polygon[Index];
		const FVector2D& Next = Polygon[(Index + 1) % Count];
		Sum += static_cast<double>(Current.X) * static_cast<double>(Next.Y)
			- static_cast<double>(Next.X) * static_cast<double>(Current.Y);
	}

	return Sum * 0.5;
}

void FPolygonUtils::EnsureWinding(TArray<FVector2D>& Polygon, bool bCounterClockwise)
{
	if (Polygon.Num() < 3)
	{
		return;
	}

	const bool bIsCCW = ComputeSignedArea(Polygon) > 0.0;
	if (bIsCCW != bCounterClockwise)
	{
		Algo::Reverse(Polygon);
	}
}

FVector2D FPolygonUtils::ComputeCentroid(const TArray<FVector2D>& Polygon)
{
	const int32 Count = Polygon.Num();
	if (Count == 0)
	{
		return FVector2D::ZeroVector;
	}
	if (Count < 3)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Polygon)
		{
			Sum += P;
		}
		return Sum / static_cast<double>(Count);
	}

	const double SignedArea = ComputeSignedArea(Polygon);

	// Bei degeneriertem Polygon (Flaeche ~ 0, etwa einem entarteten
	// Gebaeudeumriss aus fehlerhaften Daten) ist die Schwerpunktformel
	// singulaer; dann der arithmetische Mittelpunkt.
	if (FMath::Abs(SignedArea) < AreaEpsilon)
	{
		FVector2D Sum = FVector2D::ZeroVector;
		for (const FVector2D& P : Polygon)
		{
			Sum += P;
		}
		return Sum / static_cast<double>(Count);
	}

	double Cx = 0.0;
	double Cy = 0.0;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D& Current = Polygon[Index];
		const FVector2D& Next = Polygon[(Index + 1) % Count];
		const double Cross = static_cast<double>(Current.X) * static_cast<double>(Next.Y)
			- static_cast<double>(Next.X) * static_cast<double>(Current.Y);
		Cx += (static_cast<double>(Current.X) + static_cast<double>(Next.X)) * Cross;
		Cy += (static_cast<double>(Current.Y) + static_cast<double>(Next.Y)) * Cross;
	}

	const double Factor = 1.0 / (6.0 * SignedArea);
	return FVector2D(Cx * Factor, Cy * Factor);
}

int32 FPolygonUtils::RemoveDuplicatePoints(TArray<FVector2D>& Polygon, double Tolerance)
{
	if (Polygon.Num() < 2)
	{
		return 0;
	}

	const double ToleranceSquared = Tolerance * Tolerance;
	const int32 OriginalCount = Polygon.Num();

	TArray<FVector2D> Filtered;
	Filtered.Reserve(OriginalCount);
	Filtered.Add(Polygon[0]);

	for (int32 Index = 1; Index < OriginalCount; ++Index)
	{
		if (FVector2D::DistSquared(Polygon[Index], Filtered.Last()) > ToleranceSquared)
		{
			Filtered.Add(Polygon[Index]);
		}
	}

	// Geschlossenen Ring oeffnen: OSM liefert erster == letzter Node.
	if (Filtered.Num() >= 2
		&& FVector2D::DistSquared(Filtered[0], Filtered.Last()) <= ToleranceSquared)
	{
		Filtered.Pop(EAllowShrinking::No);
	}

	Polygon = MoveTemp(Filtered);
	return OriginalCount - Polygon.Num();
}

int32 FPolygonUtils::RemoveCollinearPoints(TArray<FVector2D>& Polygon, double AngleToleranceDegrees)
{
	if (Polygon.Num() < 3)
	{
		return 0;
	}

	const double CosThreshold = FMath::Cos(FMath::DegreesToRadians(180.0 - AngleToleranceDegrees));
	const int32 OriginalCount = Polygon.Num();

	// Iterativ, weil das Entfernen eines Punkts seine Nachbarn kollinear
	// machen kann. Die Schleife terminiert, weil jede Runde entweder einen
	// Punkt entfernt oder abbricht.
	bool bChanged = true;
	while (bChanged && Polygon.Num() > 3)
	{
		bChanged = false;

		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			const int32 PrevIndex = (Index - 1 + Polygon.Num()) % Polygon.Num();
			const int32 NextIndex = (Index + 1) % Polygon.Num();

			const FVector2D ToPrev = Polygon[PrevIndex] - Polygon[Index];
			const FVector2D ToNext = Polygon[NextIndex] - Polygon[Index];

			const double LenPrev = ToPrev.Size();
			const double LenNext = ToNext.Size();

			if (LenPrev < GeometryEpsilon || LenNext < GeometryEpsilon)
			{
				Polygon.RemoveAt(Index, EAllowShrinking::No);
				bChanged = true;
				break;
			}

			// Kollinear heisst: die Richtungen zu den Nachbarn sind
			// entgegengesetzt, der Winkel also ~180 Grad.
			const double CosAngle = FVector2D::DotProduct(ToPrev / LenPrev, ToNext / LenNext);
			if (CosAngle <= CosThreshold)
			{
				Polygon.RemoveAt(Index, EAllowShrinking::No);
				bChanged = true;
				break;
			}
		}
	}

	return OriginalCount - Polygon.Num();
}

bool FPolygonUtils::IsPointInPolygon(const FVector2D& Point, const TArray<FVector2D>& Polygon)
{
	const int32 Count = Polygon.Num();
	if (Count < 3)
	{
		return false;
	}

	bool bInside = false;

	// Crossing-Number-Test. Die asymmetrische Bedingung (>= fuer den einen,
	// < fuer den anderen Endpunkt) verhindert Doppelzaehlung an Vertices, die
	// exakt auf der Teststrahlhoehe liegen - bei achsenparallelen
	// Gebaeudegrundrissen ist das der Normalfall, nicht die Ausnahme.
	for (int32 Index = 0, PrevIndex = Count - 1; Index < Count; PrevIndex = Index++)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[PrevIndex];

		if (((A.Y > Point.Y) != (B.Y > Point.Y))
			&& (Point.X < (B.X - A.X) * (Point.Y - A.Y) / (B.Y - A.Y) + A.X))
		{
			bInside = !bInside;
		}
	}

	return bInside;
}

bool FPolygonUtils::IsPointInTriangle(const FVector2D& P, const FVector2D& A, const FVector2D& B, const FVector2D& C)
{
	// Kreuzprodukte der Kantenvektoren mit den Vektoren zum Testpunkt. Haben
	// alle drei dasselbe Vorzeichen, liegt P innen. Null bedeutet: auf der
	// Kante - das wird als innen gewertet, damit Ear-Clipping keine Ohren
	// akzeptiert, deren Kante ein anderes Vertex beruehrt.
	const double D1 = (P.X - B.X) * (A.Y - B.Y) - (A.X - B.X) * (P.Y - B.Y);
	const double D2 = (P.X - C.X) * (B.Y - C.Y) - (B.X - C.X) * (P.Y - C.Y);
	const double D3 = (P.X - A.X) * (C.Y - A.Y) - (C.X - A.X) * (P.Y - A.Y);

	const bool bHasNegative = (D1 < 0.0) || (D2 < 0.0) || (D3 < 0.0);
	const bool bHasPositive = (D1 > 0.0) || (D2 > 0.0) || (D3 > 0.0);

	return !(bHasNegative && bHasPositive);
}

bool FPolygonUtils::SegmentIntersection(
	const FVector2D& A1, const FVector2D& A2,
	const FVector2D& B1, const FVector2D& B2,
	FVector2D& OutIntersection)
{
	const FVector2D DirA = A2 - A1;
	const FVector2D DirB = B2 - B1;

	const double Denominator = DirA.X * DirB.Y - DirA.Y * DirB.X;

	// Denominator == 0: parallel oder kollinear. Kollineare Ueberlappung
	// liefert keinen eindeutigen Punkt und wird daher als "kein Schnitt"
	// behandelt; die Aufrufer (Kreuzungsbildung) brauchen einen eindeutigen Punkt.
	if (FMath::Abs(Denominator) < UE_DOUBLE_SMALL_NUMBER)
	{
		return false;
	}

	const FVector2D Delta = B1 - A1;

	const double T = (Delta.X * DirB.Y - Delta.Y * DirB.X) / Denominator;
	const double U = (Delta.X * DirA.Y - Delta.Y * DirA.X) / Denominator;

	if (T < 0.0 || T > 1.0 || U < 0.0 || U > 1.0)
	{
		return false;
	}

	OutIntersection = A1 + DirA * T;
	return true;
}

FBox2D FPolygonUtils::ComputeBounds2D(const TArray<FVector2D>& Polygon)
{
	FBox2D Bounds(ForceInit);
	for (const FVector2D& P : Polygon)
	{
		Bounds += P;
	}
	return Bounds;
}

bool FPolygonUtils::IsValidEar(
	const TArray<FVector2D>& Polygon,
	const TArray<int32>& Indices,
	int32 PrevPos,
	int32 CurrPos,
	int32 NextPos)
{
	const FVector2D& A = Polygon[Indices[PrevPos]];
	const FVector2D& B = Polygon[Indices[CurrPos]];
	const FVector2D& C = Polygon[Indices[NextPos]];

	// Konvexitaetstest: bei CCW-Orientierung des Polygons ist ein Vertex
	// konvex, wenn das Kreuzprodukt positiv ist.
	const double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
	if (Cross <= 0.0)
	{
		return false;
	}

	// Flaechentest: Nulldreiecke wuerden zwar den Konvexitaetstest passieren,
	// erzeugen aber degenerierte Geometrie mit undefinierten Normalen.
	if (FMath::Abs(Cross) * 0.5 < AreaEpsilon)
	{
		return false;
	}

	// Kein anderes Vertex des Polygons darf im Ohr liegen.
	for (int32 Pos = 0; Pos < Indices.Num(); ++Pos)
	{
		if (Pos == PrevPos || Pos == CurrPos || Pos == NextPos)
		{
			continue;
		}

		const FVector2D& P = Polygon[Indices[Pos]];

		// Vertices, die geometrisch mit einer Ohrecke identisch sind, treten an
		// Brueckenstellen zwangslaeufig auf (dort wird ein Punkt dupliziert)
		// und duerfen das Ohr nicht blockieren.
		if (FVector2D::DistSquared(P, A) < GeometryEpsilon * GeometryEpsilon
			|| FVector2D::DistSquared(P, B) < GeometryEpsilon * GeometryEpsilon
			|| FVector2D::DistSquared(P, C) < GeometryEpsilon * GeometryEpsilon)
		{
			continue;
		}

		if (IsPointInTriangle(P, A, B, C))
		{
			return false;
		}
	}

	return true;
}

bool FPolygonUtils::TriangulatePolygon(const TArray<FVector2D>& Polygon, TArray<int32>& OutIndices)
{
	OutIndices.Reset();

	if (Polygon.Num() < 3)
	{
		return false;
	}

	// Ear-Clipping setzt eine bekannte Orientierung voraus. Statt das
	// Eingabearray zu kopieren und zu drehen, wird bei CW-Eingabe die
	// Index-Reihenfolge invertiert - das spart eine Kopie des Vertexarrays.
	const bool bInputIsCCW = ComputeSignedArea(Polygon) > 0.0;

	TArray<int32> Indices;
	Indices.Reserve(Polygon.Num());
	if (bInputIsCCW)
	{
		for (int32 Index = 0; Index < Polygon.Num(); ++Index)
		{
			Indices.Add(Index);
		}
	}
	else
	{
		for (int32 Index = Polygon.Num() - 1; Index >= 0; --Index)
		{
			Indices.Add(Index);
		}
	}

	OutIndices.Reserve((Polygon.Num() - 2) * 3);

	// Fortschrittszaehler gegen Endlosschleifen bei selbstueberschneidenden
	// Polygonen: findet die Schleife in einem vollen Umlauf kein Ohr, ist das
	// Polygon nicht einfach und wird abgebrochen. Ohne diese Absicherung
	// haengt der Import an fehlerhaften OSM-Umrissen fest.
	int32 FailedAttempts = 0;
	int32 CurrPos = 0;

	while (Indices.Num() > 3)
	{
		const int32 Count = Indices.Num();

		if (FailedAttempts > Count)
		{
			UE_LOG(LogWbGIS, Verbose,
				TEXT("Ear-Clipping abgebrochen: kein gueltiges Ohr bei %d Restpunkten. ")
				TEXT("Polygon ist nicht einfach (Selbstueberschneidung). ")
				TEXT("%d Dreiecke bereits erzeugt."),
				Count, OutIndices.Num() / 3);
			// Die bereits erzeugten Dreiecke bleiben gueltig; der Rest wird als
			// Fan geschlossen. Ein Loch in der Fassade ist sichtbarer als eine
			// leicht falsche Dachflaeche.
			for (int32 Pos = 1; Pos + 1 < Count; ++Pos)
			{
				OutIndices.Add(Indices[0]);
				OutIndices.Add(Indices[Pos]);
				OutIndices.Add(Indices[Pos + 1]);
			}
			return OutIndices.Num() >= 3;
		}

		CurrPos = CurrPos % Count;
		const int32 PrevPos = (CurrPos - 1 + Count) % Count;
		const int32 NextPos = (CurrPos + 1) % Count;

		if (IsValidEar(Polygon, Indices, PrevPos, CurrPos, NextPos))
		{
			OutIndices.Add(Indices[PrevPos]);
			OutIndices.Add(Indices[CurrPos]);
			OutIndices.Add(Indices[NextPos]);

			Indices.RemoveAt(CurrPos, EAllowShrinking::No);
			FailedAttempts = 0;

			// Nach dem Entfernen zeigt CurrPos auf den bisherigen Nachfolger.
			// Einen Schritt zurueckgehen, weil der Vorgaenger durch das
			// Entfernen konvex geworden sein kann.
			CurrPos = (CurrPos - 1 + Indices.Num()) % Indices.Num();
		}
		else
		{
			++CurrPos;
			++FailedAttempts;
		}
	}

	// Restdreieck.
	if (Indices.Num() == 3)
	{
		const FVector2D& A = Polygon[Indices[0]];
		const FVector2D& B = Polygon[Indices[1]];
		const FVector2D& C = Polygon[Indices[2]];
		const double Cross = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);

		if (FMath::Abs(Cross) * 0.5 >= AreaEpsilon)
		{
			OutIndices.Add(Indices[0]);
			OutIndices.Add(Indices[1]);
			OutIndices.Add(Indices[2]);
		}
	}

	return OutIndices.Num() >= 3;
}

bool FPolygonUtils::BridgeHoleIntoRing(TArray<FVector2D>& Ring, const TArray<FVector2D>& Hole)
{
	if (Ring.Num() < 3 || Hole.Num() < 3)
	{
		return false;
	}

	// Schritt 1: Das Loch-Vertex mit maximalem X waehlen. Von dort verlaeuft
	// ein Strahl in +X-Richtung garantiert aus dem Loch heraus.
	int32 HoleStartIndex = 0;
	for (int32 Index = 1; Index < Hole.Num(); ++Index)
	{
		if (Hole[Index].X > Hole[HoleStartIndex].X)
		{
			HoleStartIndex = Index;
		}
	}

	const FVector2D M = Hole[HoleStartIndex];

	// Schritt 2: Naechstgelegenen Schnittpunkt des Strahls M -> +X mit einer
	// Kante des Aussenrings finden.
	double BestIntersectionX = TNumericLimits<double>::Max();
	int32 BestEdgeIndex = INDEX_NONE;

	for (int32 Index = 0; Index < Ring.Num(); ++Index)
	{
		const FVector2D& A = Ring[Index];
		const FVector2D& B = Ring[(Index + 1) % Ring.Num()];

		// Kante muss die Hoehe von M ueberspannen.
		if ((A.Y > M.Y) == (B.Y > M.Y))
		{
			continue;
		}

		const double DeltaY = B.Y - A.Y;
		if (FMath::Abs(DeltaY) < UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const double IntersectionX = A.X + (M.Y - A.Y) * (B.X - A.X) / DeltaY;

		// Nur Schnittpunkte rechts von M zaehlen.
		if (IntersectionX >= M.X - GeometryEpsilon && IntersectionX < BestIntersectionX)
		{
			BestIntersectionX = IntersectionX;
			BestEdgeIndex = Index;
		}
	}

	if (BestEdgeIndex == INDEX_NONE)
	{
		UE_LOG(LogWbGIS, Verbose,
			TEXT("Loch-Bruecke fehlgeschlagen: kein Schnittpunkt rechts des Lochs. ")
			TEXT("Das Loch liegt vermutlich ausserhalb des Aussenrings (fehlerhafte Multipolygon-Relation)."));
		return false;
	}

	// Schritt 3: Brueckenkandidat ist der Endpunkt der Schnittkante mit
	// groesserem X - dieser ist von M aus garantiert sichtbar, wenn kein
	// reflexes Vertex des Rings dazwischenliegt.
	const FVector2D& EdgeA = Ring[BestEdgeIndex];
	const FVector2D& EdgeB = Ring[(BestEdgeIndex + 1) % Ring.Num()];

	int32 BridgeIndex = (EdgeA.X > EdgeB.X) ? BestEdgeIndex : (BestEdgeIndex + 1) % Ring.Num();

	const FVector2D IntersectionPoint(BestIntersectionX, M.Y);

	// Schritt 4: Sichtbarkeitspruefung. Liegt ein Ring-Vertex im Dreieck
	// (M, Schnittpunkt, Kandidat), verdeckt es die Bruecke. In diesem Fall wird
	// das Vertex mit dem kleinsten Winkel zur +X-Achse gewaehlt - der
	// Standardansatz nach Eberly. Ohne diesen Schritt kreuzt die Bruecke bei
	// L- und U-foermigen Grundrissen (in Wiesbadens Gruenderzeit-Bloecken die
	// Regel) den Aussenring und zerstoert die Triangulierung.
	double BestAngle = TNumericLimits<double>::Max();
	int32 VisibleIndex = BridgeIndex;
	bool bFoundBlocking = false;

	for (int32 Index = 0; Index < Ring.Num(); ++Index)
	{
		if (Index == BridgeIndex)
		{
			continue;
		}

		const FVector2D& P = Ring[Index];

		if (P.X < M.X)
		{
			continue;
		}

		if (!IsPointInTriangle(P, M, IntersectionPoint, Ring[BridgeIndex]))
		{
			continue;
		}

		const FVector2D ToP = P - M;
		const double Length = ToP.Size();
		if (Length < GeometryEpsilon)
		{
			continue;
		}

		// Winkel zur +X-Achse, ueber den Cosinus - monoton im relevanten Bereich.
		const double Angle = FMath::Acos(FMath::Clamp(ToP.X / Length, -1.0, 1.0));

		if (Angle < BestAngle)
		{
			BestAngle = Angle;
			VisibleIndex = Index;
			bFoundBlocking = true;
		}
	}

	if (bFoundBlocking)
	{
		BridgeIndex = VisibleIndex;
	}

	// Schritt 5: Bruecke einfuegen. Der Ring wird an der Stelle BridgeIndex
	// aufgetrennt und die Loch-Sequenz eingeschoben:
	//   ... Ring[BridgeIndex], Hole[Start..Start], Ring[BridgeIndex], ...
	// Beide Brueckenpunkte werden dupliziert; das ist bei diesem Verfahren
	// erforderlich, damit der Ring einfach zusammenhaengend bleibt.
	TArray<FVector2D> Result;
	Result.Reserve(Ring.Num() + Hole.Num() + 2);

	for (int32 Index = 0; Index <= BridgeIndex; ++Index)
	{
		Result.Add(Ring[Index]);
	}

	for (int32 Offset = 0; Offset <= Hole.Num(); ++Offset)
	{
		Result.Add(Hole[(HoleStartIndex + Offset) % Hole.Num()]);
	}

	for (int32 Index = BridgeIndex; Index < Ring.Num(); ++Index)
	{
		Result.Add(Ring[Index]);
	}

	Ring = MoveTemp(Result);
	return true;
}

bool FPolygonUtils::TriangulatePolygonWithHoles(
	const TArray<FVector2D>& Outer,
	const TArray<TArray<FVector2D>>& Holes,
	TArray<FVector2D>& OutVertices,
	TArray<int32>& OutIndices)
{
	OutVertices.Reset();
	OutIndices.Reset();

	if (Outer.Num() < 3)
	{
		return false;
	}

	TArray<FVector2D> Ring = Outer;
	RemoveDuplicatePoints(Ring);
	if (Ring.Num() < 3)
	{
		return false;
	}

	// Aussenring CCW, Loecher CW - so entstehen beim Brueckenverfahren
	// gegenlaeufige Umlaufsinne und der kombinierte Ring bleibt einfach.
	EnsureWinding(Ring, /*bCounterClockwise=*/true);

	// Loecher nach absteigendem Maximal-X sortieren. Wird von rechts nach links
	// gebrueckt, koennen bereits eingefuegte Bruecken die naechste
	// Strahlberechnung nicht mehr stoeren.
	TArray<TArray<FVector2D>> SortedHoles;
	SortedHoles.Reserve(Holes.Num());

	for (const TArray<FVector2D>& RawHole : Holes)
	{
		TArray<FVector2D> Hole = RawHole;
		RemoveDuplicatePoints(Hole);

		if (Hole.Num() < 3)
		{
			continue;
		}

		// Zu kleine Loecher werden verworfen: unterhalb ~1 m^2 sind sie im
		// Spiel nicht wahrnehmbar, erzeugen aber Bruecken, die die
		// Triangulierung destabilisieren.
		if (ComputeArea(Hole) < 100.0 * 100.0)
		{
			continue;
		}

		EnsureWinding(Hole, /*bCounterClockwise=*/false);
		SortedHoles.Add(MoveTemp(Hole));
	}

	SortedHoles.Sort([](const TArray<FVector2D>& A, const TArray<FVector2D>& B)
	{
		double MaxAX = -TNumericLimits<double>::Max();
		for (const FVector2D& P : A) { MaxAX = FMath::Max(MaxAX, static_cast<double>(P.X)); }

		double MaxBX = -TNumericLimits<double>::Max();
		for (const FVector2D& P : B) { MaxBX = FMath::Max(MaxBX, static_cast<double>(P.X)); }

		return MaxAX > MaxBX;
	});

	int32 BridgedCount = 0;
	for (const TArray<FVector2D>& Hole : SortedHoles)
	{
		if (BridgeHoleIntoRing(Ring, Hole))
		{
			++BridgedCount;
		}
	}

	if (BridgedCount < SortedHoles.Num())
	{
		UE_LOG(LogWbGIS, Verbose, TEXT("%d von %d Loechern konnten nicht eingebrueckt werden."),
			SortedHoles.Num() - BridgedCount, SortedHoles.Num());
	}

	OutVertices = MoveTemp(Ring);
	return TriangulatePolygon(OutVertices, OutIndices);
}

bool FPolygonUtils::OffsetPolyline(
	const TArray<FVector2D>& Points,
	double Offset,
	TArray<FVector2D>& OutOffsetPoints,
	double MiterLimit)
{
	OutOffsetPoints.Reset();

	// Duplikate entfernen, ohne den Ring zu schliessen - hier handelt es sich
	// um eine offene Polylinie, der letzte Punkt darf nicht wegfallen.
	TArray<FVector2D> Clean;
	Clean.Reserve(Points.Num());
	for (const FVector2D& P : Points)
	{
		if (Clean.Num() == 0
			|| FVector2D::DistSquared(P, Clean.Last()) > GeometryEpsilon * GeometryEpsilon)
		{
			Clean.Add(P);
		}
	}

	if (Clean.Num() < 2)
	{
		return false;
	}

	// Offset 0 ist zulaessig und liefert die Eingabe zurueck (wird fuer die
	// Mittelspur gebraucht).
	if (FMath::Abs(Offset) < UE_DOUBLE_SMALL_NUMBER)
	{
		OutOffsetPoints = MoveTemp(Clean);
		return true;
	}

	const int32 Count = Clean.Num();
	OutOffsetPoints.Reserve(Count);

	const double MaxMiterLength = FMath::Abs(Offset) * MiterLimit;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		if (Index == 0)
		{
			const FVector2D Direction = (Clean[1] - Clean[0]).GetSafeNormal();
			OutOffsetPoints.Add(Clean[0] + GetLeftNormal(Direction) * Offset);
			continue;
		}

		if (Index == Count - 1)
		{
			const FVector2D Direction = (Clean[Count - 1] - Clean[Count - 2]).GetSafeNormal();
			OutOffsetPoints.Add(Clean[Count - 1] + GetLeftNormal(Direction) * Offset);
			continue;
		}

		const FVector2D DirIn = (Clean[Index] - Clean[Index - 1]).GetSafeNormal();
		const FVector2D DirOut = (Clean[Index + 1] - Clean[Index]).GetSafeNormal();

		const FVector2D NormalIn = GetLeftNormal(DirIn);
		const FVector2D NormalOut = GetLeftNormal(DirOut);

		// Winkelhalbierende der beiden Kantennormalen.
		FVector2D Bisector = NormalIn + NormalOut;
		const double BisectorLength = Bisector.Size();

		// Bisector ~ 0 bedeutet eine 180-Grad-Kehre (Haarnadel). Dort ist kein
		// Miter definiert; der Punkt wird mit der Eingangsnormalen versetzt.
		if (BisectorLength < UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			OutOffsetPoints.Add(Clean[Index] + NormalIn * Offset);
			continue;
		}

		Bisector /= BisectorLength;

		// Miter-Laenge: Offset / cos(halber Innenwinkel). Der Kosinus ist das
		// Skalarprodukt von Winkelhalbierender und Kantennormale.
		const double CosHalfAngle = FVector2D::DotProduct(Bisector, NormalIn);

		if (FMath::Abs(CosHalfAngle) < UE_DOUBLE_KINDA_SMALL_NUMBER)
		{
			OutOffsetPoints.Add(Clean[Index] + NormalIn * Offset);
			continue;
		}

		const double MiterLength = Offset / CosHalfAngle;

		if (FMath::Abs(MiterLength) > MaxMiterLength)
		{
			// Bevel-Fallback: beide Kantenversaetze einzeln setzen. Erzeugt
			// einen zusaetzlichen Punkt und damit eine abgeschraegte Ecke
			// statt einer Nadelspitze.
			OutOffsetPoints.Add(Clean[Index] + NormalIn * Offset);
			OutOffsetPoints.Add(Clean[Index] + NormalOut * Offset);
		}
		else
		{
			OutOffsetPoints.Add(Clean[Index] + Bisector * MiterLength);
		}
	}

	return OutOffsetPoints.Num() >= 2;
}

bool FPolygonUtils::BuildRibbonMesh(
	const TArray<FVector2D>& Centerline,
	double Width,
	TArray<FVector2D>& OutVertices,
	TArray<int32>& OutIndices,
	TArray<FVector2D>& OutUVs)
{
	OutVertices.Reset();
	OutIndices.Reset();
	OutUVs.Reset();

	if (Centerline.Num() < 2 || Width <= GeometryEpsilon)
	{
		return false;
	}

	const double HalfWidth = Width * 0.5;

	TArray<FVector2D> LeftEdge;
	TArray<FVector2D> RightEdge;

	if (!OffsetPolyline(Centerline, HalfWidth, LeftEdge)
		|| !OffsetPolyline(Centerline, -HalfWidth, RightEdge))
	{
		return false;
	}

	// Die Bevel-Fallbacks im Offsetting koennen unterschiedlich viele Punkte je
	// Seite erzeugen (innen wird gemitert, aussen gebevelt). Fuer ein
	// regelmaessiges Quad-Netz werden beide Seiten daher auf die Stuetzpunkte
	// der Mittellinie zurueckprojiziert, statt die Offsetpunkte direkt zu paaren.
	const int32 Count = Centerline.Num();
	OutVertices.Reserve(Count * 2);
	OutUVs.Reserve(Count * 2);
	OutIndices.Reserve((Count - 1) * 6);

	double AccumulatedLength = 0.0;

	for (int32 Index = 0; Index < Count; ++Index)
	{
		FVector2D Direction;
		if (Index == 0)
		{
			Direction = (Centerline[1] - Centerline[0]).GetSafeNormal();
		}
		else if (Index == Count - 1)
		{
			Direction = (Centerline[Count - 1] - Centerline[Count - 2]).GetSafeNormal();
			AccumulatedLength += FVector2D::Distance(Centerline[Index - 1], Centerline[Index]);
		}
		else
		{
			// Gemittelte Richtung glaettet die Kante am Knick.
			const FVector2D DirIn = (Centerline[Index] - Centerline[Index - 1]).GetSafeNormal();
			const FVector2D DirOut = (Centerline[Index + 1] - Centerline[Index]).GetSafeNormal();
			Direction = (DirIn + DirOut).GetSafeNormal();
			if (Direction.IsNearlyZero())
			{
				Direction = DirIn;
			}
			AccumulatedLength += FVector2D::Distance(Centerline[Index - 1], Centerline[Index]);
		}

		const FVector2D Normal = GetLeftNormal(Direction);

		OutVertices.Add(Centerline[Index] + Normal * HalfWidth);
		OutVertices.Add(Centerline[Index] - Normal * HalfWidth);

		// V in Metern: die Strassentextur behaelt so ueber die gesamte Stadt
		// denselben Massstab, unabhaengig von der Segmentlaenge.
		const double VMeters = AccumulatedLength / 100.0;
		OutUVs.Add(FVector2D(0.0, VMeters));
		OutUVs.Add(FVector2D(1.0, VMeters));

		if (Index > 0)
		{
			const int32 Base = (Index - 1) * 2;
			// Wicklung so, dass die Vorderseite nach +Z zeigt.
			//
			// Hier stand zuvor (0,2,1) bzw. (1,2,3) mit dem Vermerk, die Normalen
			// zeigten damit nach +Z. Das war falsch herum: Fahrbahn, Gehweg und
			// Markierungen wurden von oben durch Backface-Culling entfernt und
			// waren im Spiel unsichtbar. Der Fehler blieb lange unentdeckt, weil
			// die Vertex-Normalen fest auf FVector::UpVector stehen und damit
			// korrekt aussehen - fuer das Culling zaehlt aber allein die Wicklung.
			//
			// Pruefkriterium: CrossProduct(B - A, C - A).Z > 0 (Test
			// GIS.PolygonUtils.RibbonWinding).
			OutIndices.Add(Base + 0);
			OutIndices.Add(Base + 1);
			OutIndices.Add(Base + 2);

			OutIndices.Add(Base + 1);
			OutIndices.Add(Base + 3);
			OutIndices.Add(Base + 2);
		}
	}

	return OutIndices.Num() >= 3;
}

double FPolygonUtils::ComputePolylineLength(const TArray<FVector2D>& Points)
{
	double Length = 0.0;
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		Length += FVector2D::Distance(Points[Index - 1], Points[Index]);
	}
	return Length;
}

double FPolygonUtils::PointSegmentDistanceSquared(const FVector2D& P, const FVector2D& A, const FVector2D& B)
{
	const FVector2D AB = B - A;
	const double LengthSquared = AB.SizeSquared();

	if (LengthSquared < UE_DOUBLE_SMALL_NUMBER)
	{
		return FVector2D::DistSquared(P, A);
	}

	const double T = FMath::Clamp(FVector2D::DotProduct(P - A, AB) / LengthSquared, 0.0, 1.0);
	return FVector2D::DistSquared(P, A + AB * T);
}

void FPolygonUtils::SimplifyRecursive(
	const TArray<FVector2D>& Points,
	int32 FirstIndex,
	int32 LastIndex,
	double ToleranceSquared,
	TArray<bool>& OutKeep)
{
	if (LastIndex <= FirstIndex + 1)
	{
		return;
	}

	double MaxDistanceSquared = 0.0;
	int32 MaxIndex = INDEX_NONE;

	for (int32 Index = FirstIndex + 1; Index < LastIndex; ++Index)
	{
		const double DistanceSquared = PointSegmentDistanceSquared(
			Points[Index], Points[FirstIndex], Points[LastIndex]);

		if (DistanceSquared > MaxDistanceSquared)
		{
			MaxDistanceSquared = DistanceSquared;
			MaxIndex = Index;
		}
	}

	if (MaxIndex != INDEX_NONE && MaxDistanceSquared > ToleranceSquared)
	{
		OutKeep[MaxIndex] = true;
		SimplifyRecursive(Points, FirstIndex, MaxIndex, ToleranceSquared, OutKeep);
		SimplifyRecursive(Points, MaxIndex, LastIndex, ToleranceSquared, OutKeep);
	}
}

void FPolygonUtils::SimplifyPolyline(const TArray<FVector2D>& Points, double Tolerance, TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();

	if (Points.Num() <= 2)
	{
		OutPoints = Points;
		return;
	}

	TArray<bool> Keep;
	Keep.Init(false, Points.Num());
	Keep[0] = true;
	Keep[Points.Num() - 1] = true;

	SimplifyRecursive(Points, 0, Points.Num() - 1, Tolerance * Tolerance, Keep);

	OutPoints.Reserve(Points.Num());
	for (int32 Index = 0; Index < Points.Num(); ++Index)
	{
		if (Keep[Index])
		{
			OutPoints.Add(Points[Index]);
		}
	}
}

void FPolygonUtils::ResamplePolyline(const TArray<FVector2D>& Points, double MaxSegmentLength, TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();

	if (Points.Num() < 2 || MaxSegmentLength <= GeometryEpsilon)
	{
		OutPoints = Points;
		return;
	}

	OutPoints.Reserve(Points.Num() * 2);
	OutPoints.Add(Points[0]);

	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector2D& Start = Points[Index - 1];
		const FVector2D& End = Points[Index];
		const double SegmentLength = FVector2D::Distance(Start, End);

		if (SegmentLength <= MaxSegmentLength)
		{
			OutPoints.Add(End);
			continue;
		}

		const int32 Subdivisions = FMath::CeilToInt32(SegmentLength / MaxSegmentLength);
		for (int32 Step = 1; Step <= Subdivisions; ++Step)
		{
			const double Alpha = static_cast<double>(Step) / static_cast<double>(Subdivisions);
			OutPoints.Add(FMath::Lerp(Start, End, Alpha));
		}
	}
}

void FPolygonUtils::SmoothPolylineChaikin(
	const TArray<FVector2D>& Points,
	int32 Iterations,
	TArray<FVector2D>& OutPoints,
	bool bClosed)
{
	OutPoints = Points;

	if (Points.Num() < 3 || Iterations <= 0)
	{
		return;
	}

	for (int32 Iteration = 0; Iteration < Iterations; ++Iteration)
	{
		TArray<FVector2D> Refined;
		Refined.Reserve(OutPoints.Num() * 2);

		const int32 Count = OutPoints.Num();
		const int32 SegmentCount = bClosed ? Count : Count - 1;

		// Bei offenen Linien bleiben Anfang und Ende fix - sonst wuerden
		// Strassen an Kreuzungen von ihrem Anschlusspunkt wegwandern.
		if (!bClosed)
		{
			Refined.Add(OutPoints[0]);
		}

		for (int32 Index = 0; Index < SegmentCount; ++Index)
		{
			const FVector2D& A = OutPoints[Index];
			const FVector2D& B = OutPoints[(Index + 1) % Count];

			// Klassische Chaikin-Gewichte 1/4 und 3/4.
			Refined.Add(A * 0.75 + B * 0.25);
			Refined.Add(A * 0.25 + B * 0.75);
		}

		if (!bClosed)
		{
			Refined.Add(OutPoints.Last());
		}

		OutPoints = MoveTemp(Refined);
	}
}

bool FPolygonUtils::TrimPolyline(
	const TArray<FVector2D>& Points,
	double TrimStart,
	double TrimEnd,
	TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();

	if (Points.Num() < 2)
	{
		return false;
	}

	const double TotalLength = ComputePolylineLength(Points);
	const double RequestedTrim = FMath::Max(0.0, TrimStart) + FMath::Max(0.0, TrimEnd);

	// Kurze Strassenstuecke zwischen zwei dicht benachbarten Kreuzungen (in
	// der Wiesbadener Innenstadt haeufig, z. B. Verbindungsstuecke von unter
	// 15 m) wuerden vollstaendig wegfallen. Dann wird der Trim proportional
	// reduziert, sodass 20 % der Laenge erhalten bleiben - die Kreuzungsflaechen
	// ueberlappen dort leicht, was optisch unauffaellig ist, waehrend eine
	// fehlende Strasse eine Luecke im Fahrbahnnetz erzeugen wuerde.
	double EffectiveStart = FMath::Max(0.0, TrimStart);
	double EffectiveEnd = FMath::Max(0.0, TrimEnd);

	if (RequestedTrim > TotalLength * 0.8 && RequestedTrim > 0.0)
	{
		const double Scale = (TotalLength * 0.8) / RequestedTrim;
		EffectiveStart *= Scale;
		EffectiveEnd *= Scale;
	}

	const double StartDistance = EffectiveStart;
	const double EndDistance = TotalLength - EffectiveEnd;

	if (EndDistance - StartDistance < GeometryEpsilon)
	{
		return false;
	}

	// Punkte entlang der Bogenlaenge einsammeln und an den Trimmgrenzen
	// interpolierte Stuetzpunkte einfuegen.
	double Accumulated = 0.0;

	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector2D& A = Points[Index - 1];
		const FVector2D& B = Points[Index];
		const double SegmentLength = FVector2D::Distance(A, B);

		if (SegmentLength < UE_DOUBLE_SMALL_NUMBER)
		{
			continue;
		}

		const double SegmentStart = Accumulated;
		const double SegmentEnd = Accumulated + SegmentLength;

		// Startgrenze faellt in dieses Segment.
		if (SegmentStart <= StartDistance && SegmentEnd > StartDistance)
		{
			const double Alpha = (StartDistance - SegmentStart) / SegmentLength;
			OutPoints.Add(FMath::Lerp(A, B, Alpha));
		}

		// Innenliegende Originalpunkte uebernehmen.
		if (SegmentEnd > StartDistance && SegmentEnd < EndDistance)
		{
			OutPoints.Add(B);
		}

		// Endgrenze faellt in dieses Segment.
		if (SegmentStart < EndDistance && SegmentEnd >= EndDistance)
		{
			const double Alpha = (EndDistance - SegmentStart) / SegmentLength;
			OutPoints.Add(FMath::Lerp(A, B, Alpha));
			break;
		}

		Accumulated = SegmentEnd;
	}

	RemoveDuplicatePoints(OutPoints);

	// RemoveDuplicatePoints schliesst Ringe; bei einer geraden Strasse mit
	// genau 2 Punkten darf das nicht zum Verlust des Endpunkts fuehren.
	if (OutPoints.Num() < 2)
	{
		return false;
	}

	return true;
}

FVector2D FPolygonUtils::GetStartTangent(const TArray<FVector2D>& Points)
{
	for (int32 Index = 1; Index < Points.Num(); ++Index)
	{
		const FVector2D Delta = Points[Index] - Points[0];
		if (Delta.SizeSquared() > GeometryEpsilon * GeometryEpsilon)
		{
			return Delta.GetSafeNormal();
		}
	}
	return FVector2D(1.0, 0.0);
}

FVector2D FPolygonUtils::GetEndTangent(const TArray<FVector2D>& Points)
{
	const int32 Last = Points.Num() - 1;
	for (int32 Index = Last - 1; Index >= 0; --Index)
	{
		const FVector2D Delta = Points[Last] - Points[Index];
		if (Delta.SizeSquared() > GeometryEpsilon * GeometryEpsilon)
		{
			return Delta.GetSafeNormal();
		}
	}
	return FVector2D(1.0, 0.0);
}

bool FPolygonUtils::ComputeConvexHull(const TArray<FVector2D>& Points, TArray<FVector2D>& OutHull)
{
	OutHull.Reset();

	if (Points.Num() < 3)
	{
		OutHull = Points;
		return false;
	}

	TArray<FVector2D> Sorted = Points;
	Sorted.Sort([](const FVector2D& A, const FVector2D& B)
	{
		if (!FMath::IsNearlyEqual(A.X, B.X, GeometryEpsilon))
		{
			return A.X < B.X;
		}
		return A.Y < B.Y;
	});

	// Duplikate entfernen - identische Punkte brechen den Kreuzprodukttest.
	for (int32 Index = Sorted.Num() - 1; Index > 0; --Index)
	{
		if (FVector2D::DistSquared(Sorted[Index], Sorted[Index - 1]) < GeometryEpsilon * GeometryEpsilon)
		{
			Sorted.RemoveAt(Index, EAllowShrinking::No);
		}
	}

	if (Sorted.Num() < 3)
	{
		OutHull = Sorted;
		return false;
	}

	auto Cross = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) -> double
	{
		return (static_cast<double>(A.X) - O.X) * (static_cast<double>(B.Y) - O.Y)
			- (static_cast<double>(A.Y) - O.Y) * (static_cast<double>(B.X) - O.X);
	};

	// Andrew's Monotone Chain: untere und obere Huelle separat aufbauen.
	TArray<FVector2D> Hull;
	Hull.Reserve(Sorted.Num() * 2);

	for (const FVector2D& P : Sorted)
	{
		while (Hull.Num() >= 2 && Cross(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0)
		{
			Hull.Pop(EAllowShrinking::No);
		}
		Hull.Add(P);
	}

	const int32 LowerHullSize = Hull.Num() + 1;

	for (int32 Index = Sorted.Num() - 2; Index >= 0; --Index)
	{
		const FVector2D& P = Sorted[Index];
		while (Hull.Num() >= LowerHullSize && Cross(Hull[Hull.Num() - 2], Hull.Last(), P) <= 0.0)
		{
			Hull.Pop(EAllowShrinking::No);
		}
		Hull.Add(P);
	}

	// Der letzte Punkt ist eine Wiederholung des ersten.
	if (Hull.Num() > 1)
	{
		Hull.Pop(EAllowShrinking::No);
	}

	OutHull = MoveTemp(Hull);
	return OutHull.Num() >= 3;
}

bool FPolygonUtils::ComputeMinimumAreaBox2D(
	const TArray<FVector2D>& Polygon,
	FVector2D& OutCenter,
	FVector2D& OutExtent,
	double& OutYawRadians)
{
	OutCenter = FVector2D::ZeroVector;
	OutExtent = FVector2D::ZeroVector;
	OutYawRadians = 0.0;

	if (Polygon.Num() < 3)
	{
		return false;
	}

	bool bFound = false;
	double BestArea = TNumericLimits<double>::Max();

	// Bei einem flaechenminimalen Rechteck liegt immer eine Kante auf einer
	// Polygonkante - es genuegt, die Kantenrichtungen durchzugehen.
	for (int32 Index = 0; Index < Polygon.Num(); ++Index)
	{
		const FVector2D& A = Polygon[Index];
		const FVector2D& B = Polygon[(Index + 1) % Polygon.Num()];

		const FVector2D Edge = B - A;
		const double Length = Edge.Size();
		if (Length < KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D AxisX = Edge / Length;
		const FVector2D AxisY(-AxisX.Y, AxisX.X);

		double MinX = TNumericLimits<double>::Max();
		double MaxX = -TNumericLimits<double>::Max();
		double MinY = TNumericLimits<double>::Max();
		double MaxY = -TNumericLimits<double>::Max();

		for (const FVector2D& Point : Polygon)
		{
			const double ProjX = FVector2D::DotProduct(Point, AxisX);
			const double ProjY = FVector2D::DotProduct(Point, AxisY);

			MinX = FMath::Min(MinX, ProjX);
			MaxX = FMath::Max(MaxX, ProjX);
			MinY = FMath::Min(MinY, ProjY);
			MaxY = FMath::Max(MaxY, ProjY);
		}

		const double Width = MaxX - MinX;
		const double Height = MaxY - MinY;
		const double Area = Width * Height;

		if (Area < BestArea && Area > KINDA_SMALL_NUMBER)
		{
			BestArea = Area;
			bFound = true;

			// Mittelpunkt liegt in Achsenkoordinaten - zurueck in Weltkoordinaten.
			const double CenterProjX = (MinX + MaxX) * 0.5;
			const double CenterProjY = (MinY + MaxY) * 0.5;
			OutCenter = AxisX * CenterProjX + AxisY * CenterProjY;
			OutExtent = FVector2D(Width * 0.5, Height * 0.5);
			OutYawRadians = FMath::Atan2(AxisX.Y, AxisX.X);
		}
	}

	return bFound;
}

bool FPolygonUtils::IsInsideRotatedBox2D(
	const FVector2D& Point, const FVector2D& Center,
	const FVector2D& Extent, double YawDeg)
{
	if (Extent.IsNearlyZero())
	{
		return false;
	}

	const FVector2D Delta = Point - Center;
	const double Yaw = FMath::DegreesToRadians(-YawDeg);
	const double CosYaw = FMath::Cos(Yaw);
	const double SinYaw = FMath::Sin(Yaw);
	const FVector2D Local(
		Delta.X * CosYaw - Delta.Y * SinYaw,
		Delta.X * SinYaw + Delta.Y * CosYaw);

	return FMath::Abs(Local.X) <= Extent.X
		&& FMath::Abs(Local.Y) <= Extent.Y;
}
