// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "GIS/WiesbadenPlacementAudit.h"

#include "WiesbadenReal.h"

#include "GIS/BuildingGenerator.h"
#include "GIS/GeoCoordinateConverter.h"
#include "GIS/OSMTypes.h"
#include "GIS/PolygonUtils.h"
#include "GIS/RoadFurnitureGenerator.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/WiesbadenBuildingClearance.h"
#include "GIS/WiesbadenRegionAssets.h"
#include "GIS/WiesbadenRoadClearance.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	// -- Konstanten, die die Platzierung tatsaechlich ausfuellt --------------
	//
	// Sie stammen vom Spawner, nicht aus Vermutung: die Tafel ist eine
	// Engine-Plane der Kante SignSizeCm (60 cm) und sitzt MITTLIG ueber dem
	// Ankerpunkt - sie ragt also 30 cm ueber ihn hinaus. Der Mastfuss (3 cm)
	// und der Laternenkopf (Kappe 32 cm * Skalierung 0,18 = rund 6 cm) liegen
	// dagegen INNERHALB der heutigen 20-cm-Pruefung: fuer Laternen und
	// Leitpfosten ist der Punkttest also ausreichend, fuer Schilder nicht.
	constexpr double TafelRadiusCm = 30.0;
	constexpr double FahrbahnZuschlagCm = 20.0;
	constexpr double TafelSweepCm = FahrbahnZuschlagCm + TafelRadiusCm;

	// -- Schwellen der Regeln ------------------------------------------------
	constexpr double BlickToleranzGrad = 45.0;
	constexpr double LaengsToleranzCm = 100.0;
	constexpr double MoebelToleranzCm = 100.0;
	constexpr double MoebelWeitCm = 250.0;
	constexpr double DopplungRadiusCm = 300.0;
	constexpr double BahnKorridorCm = 200.0;
	constexpr double NachStrasseMaxCm = 1000.0;

	/** Abweichung zweier Blickrichtungen in Grad (0 = gleich, 180 = entgegengesetzt). */
	double WinkelabweichungGrad(double YawA, double YawB)
	{
		return FMath::Abs(FMath::FindDeltaAngleDegrees(YawA, YawB));
	}

	/** Lesbare Artbezeichnung eines Strassenmoebels fuer Bericht und JSON. */
	FString MoebelArt(EStreetFurnitureKind Kind)
	{
		switch (Kind)
		{
		case EStreetFurnitureKind::Bench:          return TEXT("Bank");
		case EStreetFurnitureKind::Bollard:        return TEXT("Poller");
		case EStreetFurnitureKind::WasteBasket:    return TEXT("Abfallkorb");
		case EStreetFurnitureKind::VendingMachine: return TEXT("Automat");
		case EStreetFurnitureKind::Recycling:      return TEXT("Recycling");
		case EStreetFurnitureKind::FireHydrant:    return TEXT("Hydrant");
		case EStreetFurnitureKind::PostBox:        return TEXT("Briefkasten");
		case EStreetFurnitureKind::PicnicTable:    return TEXT("Picknick-Tisch");
		default:                                   return TEXT("Moebel");
		}
	}

	/** Lesbare Artbezeichnung eines Regions-Objekts fuer Bericht und JSON. */
	FString RegionsArt(ERegionAssetCategory Category)
	{
		switch (Category)
		{
		case ERegionAssetCategory::Tree:       return TEXT("Baum");
		case ERegionAssetCategory::Waterfront: return TEXT("Ufer-Objekt");
		case ERegionAssetCategory::Industrial: return TEXT("Industrie-Objekt");
		default:                               return TEXT("Objekt");
		}
	}

	/** Rechter Normalenvektor einer horizontalen Richtung (UE: +X vor, +Y rechts). */
	FVector RechteNormale(const FVector& Direction)
	{
		const FVector N = Direction.GetSafeNormal2D();
		return FVector(-N.Y, N.X, 0.0);
	}

	/** Welche Seite der Fahrbahn liegt der Punkt? true = rechts der Richtung. */
	bool LiegtRechts(const FVector& P, const FVector& AchsenAnfang, const FVector& Richtung)
	{
		const FVector Right = RechteNormale(Richtung);
		return FVector::DotProduct(P - AchsenAnfang, Right) > 0.0;
	}

	// -- Zellgitter ueber die befahrbaren Segmente ---------------------------
	/**
	 * Beantwortet "wo ist die naechste Fahrbahnachse?" fuer einen Punkt.
	 *
	 * Ohne diese Frage gibt es fuer ein Schild keine Blickrichtung: die
	 * Platzierung leitet sie aus der naechsten Achse ab, und der Audit muss
	 * dieselbe Ableitung rechnen, sonst misst er gegen eine andere Wahrheit
	 * als die, die gebaut wird.
	 */
	struct FSegmentIndex
	{
		static constexpr double CellCm = 5000.0;

		const FRoadNetwork* Network = nullptr;
		TMap<FIntPoint, TArray<int32>> Cells;

		static FIntPoint CellOf(const FVector2D& P)
		{
			return FIntPoint(
				FMath::FloorToInt(P.X / CellCm),
				FMath::FloorToInt(P.Y / CellCm));
		}

		static const TArray<FVector>& LinieVon(const FRoadSegment& Seg)
		{
			return Seg.TrimmedCenterline.Num() >= 2 ? Seg.TrimmedCenterline : Seg.Centerline;
		}

		void Build(const FRoadNetwork& InNetwork)
		{
			Network = &InNetwork;
			for (int32 Index = 0; Index < InNetwork.Segments.Num(); ++Index)
			{
				const FRoadSegment& Seg = InNetwork.Segments[Index];
				if (!FOSMTagParser::IsDrivable(Seg.HighwayType))
				{
					continue;
				}
				const TArray<FVector>& Line = LinieVon(Seg);
				if (Line.Num() < 2)
				{
					continue;
				}

				FBox2D Box(ForceInit);
				for (const FVector& P : Line)
				{
					Box += FVector2D(P.X, P.Y);
				}

				const FIntPoint Min = CellOf(Box.Min);
				const FIntPoint Max = CellOf(Box.Max);
				for (int32 X = Min.X; X <= Max.X; ++X)
				{
					for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
					{
						Cells.FindOrAdd(FIntPoint(X, Y)).Add(Index);
					}
				}
			}
		}

		/**
		 * Naechste Achse unterhalb von MaxCm.
		 *
		 * @param OutProj   Punkt auf der Achse, am naechsten zu P.
		 * @param OutRichtung Einheitsrichtung dieser Achse (Laufrichtung des Ways).
		 */
		bool NaechsteAchse(const FVector2D& P, double MaxCm,
			FVector2D& OutProj, FVector& OutRichtung) const
		{
			if (!Network)
			{
				return false;
			}

			const FIntPoint Base = CellOf(P);
			double BestSq = MaxCm * MaxCm;
			bool bFound = false;

			for (int32 DX = -1; DX <= 1; ++DX)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const TArray<int32>* Indices = Cells.Find(Base + FIntPoint(DX, DY));
					if (!Indices)
					{
						continue;
					}

					for (const int32 Index : *Indices)
					{
						const FRoadSegment& Seg = Network->Segments[Index];
						const TArray<FVector>& Line = LinieVon(Seg);
						for (int32 i = 0; i + 1 < Line.Num(); ++i)
						{
							const FVector A = Line[i];
							const FVector AB = Line[i + 1] - A;
							const double L2 = AB.SizeSquared2D();
							const FVector P3(P.X, P.Y, 0.0);
							const double T = (L2 > 1.0)
								? FMath::Clamp(FVector::DotProduct(P3 - A, AB) / L2, 0.0, 1.0)
								: 0.0;
							const FVector Proj = A + AB * T;
							const double D2 = FVector::DistSquared2D(P3, Proj);
							if (D2 < BestSq)
							{
								BestSq = D2;
								OutProj = FVector2D(Proj.X, Proj.Y);
								OutRichtung = AB.GetSafeNormal2D();
								bFound = true;
							}
						}
					}
				}
			}
			return bFound;
		}
	};

	// -- Bahnkorridor aus den OSM-Bahnwegen ---------------------------------
	/**
	 * Korridor um alle AKTIVEN Bahnwege des Datensatzes.
	 *
	 * Die Bahnachsen stehen nicht im Datensatz, sie kommen aus demselben OSM,
	 * aus dem auch die Strecke gebaut wurde (die Punkte des Nerobergbahns sind
	 * way 39223618/39223619 samt Nachbarn). Deshalb braucht der Audit keine
	 * zweite Kopie der Achsdaten: er nimmt den Korridor aus der Quelle selbst
	 * und deckt zugleich Hauptbahnen, Tram und Nerotalbahn ab.
	 */
	struct FKorridorIndex
	{
		static constexpr double CellCm = 5000.0;

		struct FStrecke
		{
			FVector2D A;
			FVector2D B;
		};

		TArray<FStrecke> Strecken;
		TMap<FIntPoint, TArray<int32>> Cells;
		int32 WegCount = 0;

		static FIntPoint CellOf(const FVector2D& P)
		{
			return FIntPoint(
				FMath::FloorToInt(P.X / CellCm),
				FMath::FloorToInt(P.Y / CellCm));
		}

		static bool IstAktiveBahn(const FString& Art)
		{
			// Ausdruecklich NICHT dabei: platform, platform_edge (Bahnsteig),
			// disused/abandoned/razed/proposed (kein Betrieb mehr, keiner geplant).
			return Art == TEXT("rail") || Art == TEXT("tram")
				|| Art == TEXT("funicular") || Art == TEXT("subway")
				|| Art == TEXT("light_rail") || Art == TEXT("narrow_gauge");
		}

		void AddStrecke(const FVector2D& A, const FVector2D& B)
		{
			if (FVector2D::DistSquared(A, B) < 1.0)
			{
				return;
			}

			const int32 Index = Strecken.Add(FStrecke{ A, B });

			FBox2D Box(ForceInit);
			Box += A;
			Box += B;
			const FIntPoint Min = CellOf(Box.Min);
			const FIntPoint Max = CellOf(Box.Max);
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
				{
					Cells.FindOrAdd(FIntPoint(X, Y)).Add(Index);
				}
			}
		}

		void Build(const FOSMDataSet& Data, const UGeoCoordinateConverter& Converter)
		{
			for (const TPair<FOSMId, FOSMWay>& Pair : Data.Ways)
			{
				const FOSMWay& Way = Pair.Value;
				if (!IstAktiveBahn(Way.GetTag(TEXT("railway"))))
				{
					continue;
				}
				++WegCount;

				FVector2D Vorher = FVector2D::ZeroVector;
				bool bVorher = false;
				for (const FOSMId NodeId : Way.NodeIds)
				{
					const FOSMNode* Node = Data.Nodes.Find(NodeId);
					if (!Node)
					{
						bVorher = false;
						continue;
					}

					const FVector Welt = Converter.GeoToUnrealGround(Node->Location);
					const FVector2D P(Welt.X, Welt.Y);
					if (bVorher)
					{
						AddStrecke(Vorher, P);
					}
					Vorher = P;
					bVorher = true;
				}
			}
		}

		/** Liegt der Punkt im Korridor (Abstand <= BahnKorridorCm)? */
		bool IstImKorridor(const FVector2D& P) const
		{
			const FIntPoint Base = CellOf(P);
			const double RadiusSq = BahnKorridorCm * BahnKorridorCm;

			for (int32 DX = -1; DX <= 1; ++DX)
			{
				for (int32 DY = -1; DY <= 1; ++DY)
				{
					const TArray<int32>* Indices = Cells.Find(Base + FIntPoint(DX, DY));
					if (!Indices)
					{
						continue;
					}
					for (const int32 Index : *Indices)
					{
						const FStrecke& S = Strecken[Index];
						const FVector2D& A = S.A;
						const FVector2D AB = S.B - A;
						const double L2 = AB.SizeSquared();
						const double T = (L2 > 1.0)
							? FMath::Clamp(FVector2D::DotProduct(P - A, AB) / L2, 0.0, 1.0)
							: 0.0;
						const FVector2D Naechster = A + AB * T;
						if (FVector2D::DistSquared(P, Naechster) <= RadiusSq)
						{
							return true;
						}
					}
				}
			}
			return false;
		}
	};

	/** Zellgitter ueber die Schilder - fuer die Entdopplung (Radius 300 cm). */
	struct FSchildIndex
	{
		static constexpr double CellCm = 5000.0;

		TMap<FIntPoint, TArray<int32>> Cells;

		static FIntPoint CellOf(const FVector2D& P)
		{
			return FIntPoint(
				FMath::FloorToInt(P.X / CellCm),
				FMath::FloorToInt(P.Y / CellCm));
		}

		void Build(const TArray<FSignInstance>& Signs)
		{
			for (int32 Index = 0; Index < Signs.Num(); ++Index)
			{
				const FVector2D P(Signs[Index].Location.X, Signs[Index].Location.Y);
				Cells.FindOrAdd(CellOf(P)).Add(Index);
			}
		}
	};
}

const FPlacementRuleCount* FPlacementAuditReport::Finde(const FString& Schluessel) const
{
	for (const FPlacementRuleCount& Regel : Regeln)
	{
		if (Regel.Schluessel == Schluessel)
		{
			return &Regel;
		}
	}
	return nullptr;
}

FString FPlacementAuditReport::ToString() const
{
	if (!bSuccess)
	{
		return FString::Printf(TEXT("Platzierungs-Audit FEHLGESCHLAGEN: %s"), *ErrorMessage);
	}

	int32 Gesamt = 0;
	FString Zeilen;
	for (const FPlacementRuleCount& Regel : Regeln)
	{
		Gesamt += Regel.Verstoesse;
		Zeilen += FString::Printf(TEXT("\n      %-24s %7d / %d"),
			*Regel.Schluessel, Regel.Verstoesse, Regel.Geprueft);
	}

	return FString::Printf(
		TEXT("Platzierungs-Audit: %d Schilder, %d Leitpfosten, %d Laternen, %d Moebel, ")
		TEXT("%d Regions-Objekte (%d Baeume), %d Gebaeude, %d Bahnsegmente - %d Verstoesse%s"),
		SignCount, DelineatorCount, StreetLampCount, FurnitureCount,
		RegionAssetCount, TreeCount, BuildingCount, RailSegmentCount,
		Gesamt, *Zeilen);
}

FPlacementAuditReport FWiesbadenPlacementAudit::Run(
	const FRoadNetwork& Network,
	const FRoadFurnitureLayout& Furniture,
	const FRegionAssetLayout& RegionAssets,
	const TArray<FGeneratedBuilding>& Buildings,
	const FOSMDataSet& OSMData,
	const UGeoCoordinateConverter& Converter)
{
	const double StartSeconds = FPlatformTime::Seconds();

	FPlacementAuditReport Report;

	if (Network.IsEmpty())
	{
		Report.bSuccess = false;
		Report.ErrorMessage = TEXT("Strassennetz ist leer - der Audit misst gegen nichts.");
		Report.DurationSeconds = FPlatformTime::Seconds() - StartSeconds;
		return Report;
	}

	// -- Registrierung: feste Reihenfolge, feste Schluessel -----------------
	// Die Schluessel sind der Vertrag zu Spec und JSON - Tests und der
	// Nachher-Lauf fragen ueber sie nach.
	//
	// RESERVE ist Pflicht, kein Stil: die Zeiger unten werden ueber
	// AddDefaulted_GetRef vergeben und bei jeder Vergrößerung des Arrays
	// ungültig (gemessen: Registries 1-4 zaehlten ins Nichts, der Test las
	// 0, weil Freiplatz-Speicher nicht nullstellt). Erst wenn das Array
	// seine endgültige Größe hat, sind alle Zeiger stabil.
	Report.Regeln.Reserve(16);

	auto RegelAnlegen = [&Report](const TCHAR* Schluessel, const TCHAR* Beschriftung)
		-> FPlacementRuleCount*
	{
		FPlacementRuleCount& Neu = Report.Regeln.AddDefaulted_GetRef();
		Neu.Schluessel = Schluessel;
		Neu.Beschriftung = Beschriftung;
		return &Neu;
	};

	FPlacementRuleCount* R1Fahrbahn = RegelAnlegen(TEXT("R1-SchildFahrbahn"),
		TEXT("Schild mit Mastfuss auf der Fahrbahn"));
	FPlacementRuleCount* R1Blick = RegelAnlegen(TEXT("R1-Blickrichtung"),
		TEXT("OSM-Knotenschild schaut nicht in den ankommenden Verkehr"));
	FPlacementRuleCount* R2Blick = RegelAnlegen(TEXT("R2-Kreuzungsblick"),
		TEXT("Kreuzungsschild schaut nicht weg von der Kreuzung"));
	FPlacementRuleCount* R3Doppel = RegelAnlegen(TEXT("R3-Dopplung"),
		TEXT("Gleiche Zeichen-Id mehrfach innerhalb 3 m"));
	FPlacementRuleCount* R3Tempo = RegelAnlegen(TEXT("R3-TempolimitBlick"),
		TEXT("Abgeleitetes Tempo-/Zonenschild schaut falsch herum"));
	FPlacementRuleCount* R4Fahrbahn = RegelAnlegen(TEXT("R4-Fahrbahn"),
		TEXT("Leitpfosten/Laterne mit Fuss auf der Fahrbahn"));
	FPlacementRuleCount* R4Fuss = RegelAnlegen(TEXT("R4-Fussabdruck"),
		TEXT("Tafel ragt auf die Fahrbahn, der Mastfuss steht frei"));
	FPlacementRuleCount* R4Laengs = RegelAnlegen(TEXT("R4-Laengsversatz"),
		TEXT("Schild laengs der Strasse um mehr als 1 m verschoben"));
	FPlacementRuleCount* R5Versatz = RegelAnlegen(TEXT("R5-Moebelversatz"),
		TEXT("Strassenmoebel verschoben, obwohl der kartierte Ort frei war"));
	FPlacementRuleCount* R5Weit = RegelAnlegen(TEXT("R5-Moebelweit"),
		TEXT("Strassenmoebel mehr als 2,5 m vom kartierten Ort"));
	FPlacementRuleCount* R6Gebaeude = RegelAnlegen(TEXT("R6-Gebaeude"),
		TEXT("Bewuchs/Objekt in einem Gebaeudegrundriss"));
	FPlacementRuleCount* R6Bahn = RegelAnlegen(TEXT("R6-Bahn"),
		TEXT("Bewuchs/Objekt im Bahnkorridor (+-2 m)"));
	FPlacementRuleCount* R6Strasse = RegelAnlegen(TEXT("R6-Fahrbahn"),
		TEXT("Bewuchs/Objekt auf der Fahrbahn"));

	auto Verstoss = [](FPlacementRuleCount* Regel, const FString& Art,
		const FVector& Location, int64 QuellId)
	{
		if (!Regel)
		{
			return;
		}
		++Regel->Verstoesse;
		if (Regel->Beispiele.Num() < MaxBeispieleJeRegel)
		{
			FPlacementViolation& V = Regel->Beispiele.AddDefaulted_GetRef();
			V.Regel = Regel->Schluessel;
			V.Art = Art;
			V.Location = Location;
			V.QuellId = QuellId;
		}
	};

	// -- Bestand -------------------------------------------------------------
	Report.SignCount = Furniture.Signs.Num();
	Report.DelineatorCount = Furniture.Delineators.Num();
	Report.StreetLampCount = Furniture.StreetLamps.Num();
	Report.FurnitureCount = Furniture.Furniture.Num();
	Report.RegionAssetCount = RegionAssets.Assets.Num();
	Report.BuildingCount = Buildings.Num();
	for (const FPlacedRegionAsset& Asset : RegionAssets.Assets)
	{
		if (Asset.Category == ERegionAssetCategory::Tree)
		{
			++Report.TreeCount;
		}
	}

	// -- Netze ---------------------------------------------------------------
	// Zwei Sichten auf dieselbe Frage, wie bei der Platzierung:
	//   NetFahrbahn  - der Ankerpunkt (heute ueberprueft, 20 cm Zuschlag).
	//   NetTafel     - dieselbe Fahrbahn plus Tafel-Halbbreite (50 cm).
	// Der Unterschied zwischen beiden Zaelern IST der "Fussabdruck statt Punkt"
	// aus Regel R4.
	FWiesbadenRoadClearance NetFahrbahn;
	NetFahrbahn.Build(Network, FahrbahnZuschlagCm, /*bIncludeSidewalk=*/false);

	FWiesbadenRoadClearance NetTafel;
	NetTafel.Build(Network, TafelSweepCm, /*bIncludeSidewalk=*/false);

	FWiesbadenBuildingClearance NetGebaeude;
	if (Buildings.Num() > 0)
	{
		FBox2D Box(ForceInit);
		for (const FGeneratedBuilding& Building : Buildings)
		{
			Box += Building.FootprintCenterCm - Building.FootprintExtentCm;
			Box += Building.FootprintCenterCm + Building.FootprintExtentCm;
		}
		const double RadiusCm = 0.5 * Box.GetSize().Size() + 1000.0;
		NetGebaeude.BuildAround(Buildings, Box.GetCenter(), RadiusCm, 0.0);
	}

	// -- Bahnkorridor aus OSM -------------------------------------------------
	FKorridorIndex Bahn;
	if (Converter.IsInitialized())
	{
		Bahn.Build(OSMData, Converter);
		Report.bRailCheckActive = Bahn.WegCount > 0;
	}
	Report.RailSegmentCount = Bahn.Strecken.Num();

	FSegmentIndex Segmente;
	Segmente.Build(Network);

	FSchildIndex SchildIndex;
	SchildIndex.Build(Furniture.Signs);

	// Knoten, an denen die Platzierung KREUZUNGSSchilder ableitet
	// (Stopp/Vorfahrt/Verkehrszeicheninsel) - von OSM-Knotenschildern trennt
	// sie die Zeichen-Id.
	TMap<int64, int32> KreuzungBeiKnoten;
	for (int32 Index = 0; Index < Network.Intersections.Num(); ++Index)
	{
		KreuzungBeiKnoten.Add(Network.Intersections[Index].NodeId, Index);
	}
	auto IstKreuzungsschild = [&KreuzungBeiKnoten](const FSignInstance& Sign)
	{
		if (Sign.SourceSegmentId != INDEX_NONE || Sign.SourceNodeId == 0)
		{
			return false;
		}
		const bool bKontrollzeichen = Sign.SignId == TEXT("206")
			|| Sign.SignId == TEXT("205") || Sign.SignId == TEXT("215");
		return bKontrollzeichen && KreuzungBeiKnoten.Contains(Sign.SourceNodeId);
	};

	// Kartierte OSM-Position zu einer Node-Id (fuer die Versatz-Fragen).
	auto KartierterPunkt = [&OSMData, &Converter](int64 NodeId, FVector2D& Out) -> bool
	{
		if (NodeId == 0)
		{
			return false;
		}
		const FOSMNode* Node = OSMData.Nodes.Find(NodeId);
		if (!Node)
		{
			return false;
		}
		const FVector Welt = Converter.GeoToUnrealGround(Node->Location);
		Out = FVector2D(Welt.X, Welt.Y);
		return true;
	};

	// -- Schilder ------------------------------------------------------------
	for (const FSignInstance& Sign : Furniture.Signs)
	{
		const FVector P = Sign.Location;
		const FVector2D XY(P.X, P.Y);

		// R1: steht der Mastfuss auf der Fahrbahn?
		++R1Fahrbahn->Geprueft;
		if (NetFahrbahn.IsBlocked(XY))
		{
			Verstoss(R1Fahrbahn, Sign.SignId, P, Sign.SourceNodeId);
		}

		// R4: Fuss frei, aber die Tafel ragt ueber den Belag?
		++R4Fuss->Geprueft;
		if (!NetFahrbahn.IsBlocked(XY) && NetTafel.IsBlocked(XY))
		{
			Verstoss(R4Fuss, Sign.SignId, P, Sign.SourceNodeId);
		}

		FVector AchsenRichtung = FVector::ZeroVector;
		FVector2D AchsenProjekt = FVector2D::ZeroVector;
		const bool bAchseGefunden =
			Segmente.NaechsteAchse(XY, NachStrasseMaxCm, AchsenProjekt, AchsenRichtung);

		if (IstKreuzungsschild(Sign))
		{
			// R2: das Kontrollschild steht AUSSEN vor der Kreuzung und schaut
			// von ihr weg - in den ankommenden Verkehr.
			const int32* KreuzungIndex = KreuzungBeiKnoten.Find(Sign.SourceNodeId);
			if (KreuzungIndex && Network.Intersections.IsValidIndex(*KreuzungIndex))
			{
				const FVector Mitte = Network.Intersections[*KreuzungIndex].Location;
				const FVector Blick = (P - Mitte).GetSafeNormal2D();
				++R2Blick->Geprueft;
				if (!Blick.IsNearlyZero()
					&& WinkelabweichungGrad(Sign.Rotation.Yaw, Blick.Rotation().Yaw) > BlickToleranzGrad)
				{
					Verstoss(R2Blick, Sign.SignId, P, Sign.SourceNodeId);
				}
			}
		}
		else if (Sign.SourceSegmentId != INDEX_NONE)
		{
			// R3 (Blickrichtung): abgeleitetes Tempo-/Zonenschild. Es steht
			// per Bau auf der RECHTEN Seite seiner Achse und blickt heute
			// entlang der Achse - also genau ZU dem Verkehr, der es nicht
			// gilt. Erwartet: gegen die Fahrtrichtung.
			const FRoadSegment* Seg = Network.Segments.IsValidIndex(Sign.SourceSegmentId)
				? &Network.Segments[Sign.SourceSegmentId] : nullptr;
			if (Seg && Seg->Centerline.Num() >= 2)
			{
				const FVector Dir = (Seg->Centerline[1] - Seg->Centerline[0]).GetSafeNormal2D();
				const bool bRechts = LiegtRechts(P, Seg->Centerline[0], Dir);
				const double Erwartet = Dir.Rotation().Yaw + (bRechts ? 180.0 : 0.0);

				++R3Tempo->Geprueft;
				if (WinkelabweichungGrad(Sign.Rotation.Yaw, Erwartet) > BlickToleranzGrad)
				{
					Verstoss(R3Tempo, Sign.SignId, P, Sign.SourceSegmentId);
				}

				// R4 (Laengsversatz): gegenueber dem KARTIERTEN Startknoten.
				FVector2D Kartiert;
				if (KartierterPunkt(Seg->StartNodeId, Kartiert))
				{
					const FVector D(P.X - Kartiert.X, P.Y - Kartiert.Y, 0.0);
					const double Laengs = FMath::Abs(FVector::DotProduct(D, Dir));
					++R4Laengs->Geprueft;
					if (Laengs > LaengsToleranzCm)
					{
						Verstoss(R4Laengs, Sign.SignId, P, Seg->StartNodeId);
					}
				}
			}
		}
		else
		{
			// R1 (Blickrichtung): OSM-Knotenschild. Die Seite entscheidet:
			// rechts der Achsrichtung schaut es GEGEN den Verkehr dieser
			// Richtung, links mit ihm. Die Achse kommt aus derselben Naehe-
			// suche wie bei der Platzierung.
			if (bAchseGefunden)
			{
				const bool bRechts = LiegtRechts(P,
					FVector(AchsenProjekt.X, AchsenProjekt.Y, 0.0), AchsenRichtung);
				const double Erwartet = AchsenRichtung.Rotation().Yaw + (bRechts ? 180.0 : 0.0);
				++R1Blick->Geprueft;
				if (WinkelabweichungGrad(Sign.Rotation.Yaw, Erwartet) > BlickToleranzGrad)
				{
					Verstoss(R1Blick, Sign.SignId, P, Sign.SourceNodeId);
				}
			}

			// R4 (Laengsversatz): gegenueber dem kartierten Knoten.
			FVector2D Kartiert;
			if (KartierterPunkt(Sign.SourceNodeId, Kartiert))
			{
				const FVector D(P.X - Kartiert.X, P.Y - Kartiert.Y, 0.0);
				++R4Laengs->Geprueft;
				if (bAchseGefunden)
				{
					const double Laengs = FMath::Abs(FVector::DotProduct(D, AchsenRichtung));
					if (Laengs > LaengsToleranzCm)
					{
						Verstoss(R4Laengs, Sign.SignId, P, Sign.SourceNodeId);
					}
				}
			}
		}
	}

	// R3 (Dopplung): dasselbe Zeichen mehrfach an derselben Stelle.
	// Gezaehlt wird die ZUSAETZLICHE Instanz - genau so viele muessten nach
	// der Umsetzung verschwinden.
	for (int32 Index = 0; Index < Furniture.Signs.Num(); ++Index)
	{
		const FSignInstance& Sign = Furniture.Signs[Index];
		const FVector2D XY(Sign.Location.X, Sign.Location.Y);

		++R3Doppel->Geprueft;

		bool bDoppelt = false;
		const FIntPoint Base = FSchildIndex::CellOf(XY);
		for (int32 DX = -1; DX <= 1 && !bDoppelt; ++DX)
		{
			for (int32 DY = -1; DY <= 1 && !bDoppelt; ++DY)
			{
				const TArray<int32>* Nachbarn = SchildIndex.Cells.Find(Base + FIntPoint(DX, DY));
				if (!Nachbarn)
				{
					continue;
				}
				for (const int32 Anderer : *Nachbarn)
				{
					if (Anderer >= Index)
					{
						continue;
					}
					const FSignInstance& A = Furniture.Signs[Anderer];
					if (A.SignId != Sign.SignId)
					{
						continue;
					}
					const FVector2D AX(A.Location.X, A.Location.Y);
					if (FVector2D::DistSquared(XY, AX)
						<= DopplungRadiusCm * DopplungRadiusCm)
					{
						bDoppelt = true;
						break;
					}
				}
			}
		}

		if (bDoppelt)
		{
			Verstoss(R3Doppel, Sign.SignId, Sign.Location, Sign.SourceNodeId);
		}
	}

	// -- Leitpfosten und Laternen -------------------------------------------
	for (const FDelineatorInstance& Item : Furniture.Delineators)
	{
		++R4Fahrbahn->Geprueft;
		if (NetFahrbahn.IsBlocked(FVector2D(Item.Location.X, Item.Location.Y)))
		{
			Verstoss(R4Fahrbahn, TEXT("Leitpfosten"), Item.Location, Item.SegmentId);
		}
	}
	for (const FStreetLampInstance& Item : Furniture.StreetLamps)
	{
		++R4Fahrbahn->Geprueft;
		if (NetFahrbahn.IsBlocked(FVector2D(Item.Location.X, Item.Location.Y)))
		{
			Verstoss(R4Fahrbahn, TEXT("Laterne"), Item.Location, Item.NodeId);
		}
	}

	// -- Strassenmoebel ------------------------------------------------------
	for (const FFurnitureInstance& Item : Furniture.Furniture)
	{
		FVector2D Kartiert;
		if (!KartierterPunkt(Item.NodeId, Kartiert))
		{
			continue;
		}

		const FVector2D XY(Item.Location.X, Item.Location.Y);
		const double Versatz = FVector2D::Distance(XY, Kartiert);

		++R5Versatz->Geprueft;
		++R5Weit->Geprueft;

		// Der kartierte Ort war frei - das Moebel wurde also ANGEDOCKT, nicht
		// gerettet. Genau das ist die Verschiebung, die Regel R5 beendet.
		if (Versatz > MoebelToleranzCm && !NetFahrbahn.IsBlocked(Kartiert))
		{
			Verstoss(R5Versatz, MoebelArt(Item.Kind), Item.Location, Item.NodeId);
		}
		if (Versatz > MoebelWeitCm)
		{
			Verstoss(R5Weit, MoebelArt(Item.Kind), Item.Location, Item.NodeId);
		}
	}

	// -- Bewuchs und Regions-Objekte ----------------------------------------
	for (const FPlacedRegionAsset& Asset : RegionAssets.Assets)
	{
		const FVector2D XY(Asset.Location.X, Asset.Location.Y);
		const FString Art = RegionsArt(Asset.Category);

		++R6Gebaeude->Geprueft;
		++R6Bahn->Geprueft;
		++R6Strasse->Geprueft;

		if (!NetGebaeude.IsEmpty() && NetGebaeude.IsBlocked(XY, 0.0))
		{
			Verstoss(R6Gebaeude, Art, Asset.Location, 0);
		}
		if (Report.bRailCheckActive && Bahn.IstImKorridor(XY))
		{
			Verstoss(R6Bahn, Art, Asset.Location, 0);
		}
		if (NetFahrbahn.IsBlocked(XY))
		{
			Verstoss(R6Strasse, Art, Asset.Location, 0);
		}
	}

	Report.bSuccess = true;
	Report.DurationSeconds = FPlatformTime::Seconds() - StartSeconds;
	return Report;
}

bool FWiesbadenPlacementAudit::WriteJson(const FPlacementAuditReport& Report, const FString& Path)
{
	FString Json;
	Json += TEXT("{\n");
	Json += FString::Printf(TEXT("  \"dauerSekunden\": %.1f,\n"), Report.DurationSeconds);
	Json += TEXT("  \"bestand\": {\n");
	Json += FString::Printf(TEXT("    \"schilder\": %d,\n"), Report.SignCount);
	Json += FString::Printf(TEXT("    \"leitpfosten\": %d,\n"), Report.DelineatorCount);
	Json += FString::Printf(TEXT("    \"laternen\": %d,\n"), Report.StreetLampCount);
	Json += FString::Printf(TEXT("    \"moebel\": %d,\n"), Report.FurnitureCount);
	Json += FString::Printf(TEXT("    \"regionsObjekte\": %d,\n"), Report.RegionAssetCount);
	Json += FString::Printf(TEXT("    \"baeume\": %d,\n"), Report.TreeCount);
	Json += FString::Printf(TEXT("    \"gebaeude\": %d,\n"), Report.BuildingCount);
	Json += FString::Printf(TEXT("    \"bahnsegmente\": %d,\n"), Report.RailSegmentCount);
	Json += FString::Printf(TEXT("    \"bahnkorridorAktiv\": %s\n"),
		Report.bRailCheckActive ? TEXT("true") : TEXT("false"));
	Json += TEXT("  },\n");

	int32 Gesamt = 0;
	Json += TEXT("  \"regeln\": {\n");
	for (int32 i = 0; i < Report.Regeln.Num(); ++i)
	{
		const FPlacementRuleCount& Regel = Report.Regeln[i];
		Gesamt += Regel.Verstoesse;

		Json += FString::Printf(TEXT("    \"%s\": {\n"), *Regel.Schluessel);
		Json += FString::Printf(TEXT("      \"beschriftung\": \"%s\",\n"), *Regel.Beschriftung);
		Json += FString::Printf(TEXT("      \"geprueft\": %d,\n"), Regel.Geprueft);
		Json += FString::Printf(TEXT("      \"verstoesse\": %d,\n"), Regel.Verstoesse);
		Json += TEXT("      \"beispiele\": [");
		for (int32 b = 0; b < Regel.Beispiele.Num(); ++b)
		{
			const FPlacementViolation& V = Regel.Beispiele[b];
			Json += (b == 0 ? TEXT("\n") : TEXT(",\n"));
			Json += FString::Printf(
				TEXT("        { \"art\": \"%s\", \"x\": %.1f, \"y\": %.1f, \"z\": %.1f, \"quelle\": %lld }"),
				*V.Art, V.Location.X, V.Location.Y, V.Location.Z, V.QuellId);
		}
		if (Regel.Beispiele.Num() > 0)
		{
			Json += TEXT("\n      ]");
		}
		Json += TEXT("\n    }");
		Json += (i + 1 < Report.Regeln.Num()) ? TEXT(",\n") : TEXT("\n");
	}
	Json += TEXT("  },\n");
	Json += FString::Printf(TEXT("  \"verstoesseGesamt\": %d,\n"), Gesamt);
	Json += FString::Printf(TEXT("  \"erfolg\": %s,\n"),
		Report.bSuccess ? TEXT("true") : TEXT("false"));
	Json += FString::Printf(TEXT("  \"fehler\": \"%s\"\n"), *Report.ErrorMessage);
	Json += TEXT("}\n");

	if (!FFileHelper::SaveStringToFile(Json, *Path,
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		UE_LOG(LogWbCore, Error,
			TEXT("Platzierungs-Audit: Bericht konnte nicht geschrieben werden: %s"), *Path);
		return false;
	}
	return true;
}
