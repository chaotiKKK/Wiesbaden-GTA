// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/SebboHqShape.h"

namespace
{
	void Add(TArray<FHqPart>& Parts, EHqPrimitive Primitive, EHqMaterial Material,
		const FVector& CenterCm, const FVector& SizeCm, int32 Floor = -1)
	{
		FHqPart Part;
		Part.Primitive = Primitive;
		Part.Material = Material;
		Part.CenterCm = CenterCm;
		Part.SizeCm = SizeCm;
		Part.Floor = Floor;
		Parts.Add(Part);
	}

	/**
	 * Quader ueber seine Kanten statt ueber Mitte und Groesse.
	 *
	 * Fuer Waende ist das die lesbarere Schreibweise: "von der Innenkante bis
	 * zur Aussenkante" steht direkt da, statt aus Mitte plus halber Dicke
	 * zurueckgerechnet werden zu muessen.
	 */
	void AddBetween(TArray<FHqPart>& Parts, EHqMaterial Material,
		double X0, double X1, double Y0, double Y1, double Z0, double Z1, int32 Floor = -1)
	{
		if (X1 <= X0 || Y1 <= Y0 || Z1 <= Z0)
		{
			return;     // entartet - eine Tueroeffnung ohne Platz ist keine Wand
		}
		Add(Parts, EHqPrimitive::Box, Material,
			FVector((X0 + X1) * 0.5, (Y0 + Y1) * 0.5, (Z0 + Z1) * 0.5),
			FVector(X1 - X0, Y1 - Y0, Z1 - Z0), Floor);
	}

	struct FArrivalOpenings
	{
		double GarageY0 = -1400.0;
		double GarageY1 = -700.0;
		double PortalY0 = 650.0;
		double PortalY1 = 850.0;
		double ClearHeightCm = 270.0;
	};

	FArrivalOpenings GroundFloorOpenings(const FSebboHqDimensions& D)
	{
		const double Half = D.FootprintCm * 0.5 + FMath::Max(0.0, D.PodiumOversizeCm);
		FArrivalOpenings Openings;
		Openings.GarageY0 = FMath::Max(-Half + 120.0, Openings.GarageY0);
		Openings.GarageY1 = FMath::Min(Half - 120.0, Openings.GarageY1);
		Openings.PortalY0 = FMath::Max(-Half + 120.0, Openings.PortalY0);
		Openings.PortalY1 = FMath::Min(Half - 120.0, Openings.PortalY1);
		Openings.ClearHeightCm = FMath::Min(Openings.ClearHeightCm, D.FloorHeightCm - D.SlabCm - 20.0);
		return Openings;
	}

	void AddGroundFloorFacade(const FSebboHqDimensions& D, TArray<FHqPart>& Parts)
	{
		const double Half = D.FootprintCm * 0.5 + FMath::Max(0.0, D.PodiumOversizeCm);
		const double Wall = 30.0;
		const double Z0 = D.SlabCm;
		const double Z1 = D.FloorHeightCm;
		const FArrivalOpenings Openings = GroundFloorOpenings(D);

		// Die +X-Fassade ist in Abschnitte geteilt: Garage und Portal sind echte
		// Luecken, nicht vor eine Vollwand gestellte Attrappen.
		AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half, -Half, Openings.GarageY0, Z0, Z1, 0);
		AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half, Openings.GarageY1, Openings.PortalY0, Z0, Z1, 0);
		AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half, Openings.PortalY1, Half, Z0, Z1, 0);
		AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half, Openings.GarageY0, Openings.GarageY1,
			Openings.ClearHeightCm, Z1, 0);
		AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half, Openings.PortalY0, Openings.PortalY1,
			Openings.ClearHeightCm, Z1, 0);

		// Die drei anderen Fassaden halten die Erdgeschoss-Silhouette geschlossen,
		// der Innenraum bleibt aber fuer die zwei Ankunftswege begehbar.
		AddBetween(Parts, EHqMaterial::Glass, -Half, -Half + Wall, -Half, Half, Z0, Z1, 0);
		AddBetween(Parts, EHqMaterial::Glass, -Half, Half, -Half, -Half + Wall, Z0, Z1, 0);
		AddBetween(Parts, EHqMaterial::Glass, -Half, Half, Half - Wall, Half, Z0, Z1, 0);
	}
}

double SebboHq::GetRoofHeightCm(const FSebboHqDimensions& D)
{
	return D.TotalHeightCm();
}

double SebboHq::GetHelipadHeightCm(const FSebboHqDimensions& D)
{
	// Der Landeplatz liegt auf der Attika, nicht darueber: ein Deck, das ueber
	// dem Dachrand schwebt, waere eine Rampe ins Nichts.
	return D.TotalHeightCm() + 20.0;
}

double SebboHq::GetHelipadOffsetCm(const FSebboHqDimensions& D)
{
	// So weit nach +X, dass der Anflug frei von der Krone bleibt, und so weit
	// nach innen, dass die Aufsetzflaeche ganz auf dem Dach liegt. Beide
	// Grenzen sind eng: der Test HelipadApproach haelt sie fest.
	return D.FootprintCm * 0.31;
}

double SebboHq::GetCrownHalfWidthCm(const FSebboHqDimensions& D)
{
	// Knapp ueber der Kernbreite - die Krone ist ein abgesetzter Aufsatz auf
	// dem Kern, kein Deckel ueber dem halben Dach.
	return D.FootprintCm * 0.16;
}

double SebboHq::GetCoreTopHeightCm(const FSebboHqDimensions& D)
{
	// Der Kern ragt ein Geschoss ueber die Attika - das ist der Dachaufbau mit
	// dem Ausstieg. Krone und Mast sitzen darauf.
	return D.TotalHeightCm() + D.FloorHeightCm;
}

void SebboHq::BuildShell(const FSebboHqDimensions& D, TArray<FHqPart>& OutParts)
{
	const double Half = D.FootprintCm * 0.5;
	const double PodiumHalf = Half + FMath::Max(0.0, D.PodiumOversizeCm);
	const int32 Podium = FMath::Clamp(D.PodiumFloors, 0, D.FloorCount);

	OutParts.Reserve(OutParts.Num() + D.FloorCount * 3 + 12);

	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		const bool bPodium = Floor < Podium;
		const double Seite = bPodium ? PodiumHalf * 2.0 : D.FootprintCm;
		const double Unterkante = Floor * D.FloorHeightCm;

		// Geschossdecke als sichtbares Bruestungsband - erst daran liest man
		// von aussen ab, wieviele Geschosse der Turm hat.
		Add(OutParts, EHqPrimitive::Box, EHqMaterial::Concrete,
			FVector(0.0, 0.0, Unterkante + D.SlabCm * 0.5),
			FVector(Seite + 20.0, Seite + 20.0, D.SlabCm), Floor);

		// Glasband darueber bis zur naechsten Decke. Das Erdgeschoss hat an
		// der Platter-Strassen-Seite zwei reale Oeffnungen und wird deshalb als
		// Fassade statt als massiver Glasklotz gebaut.
		const double GlasHoehe = D.FloorHeightCm - D.SlabCm;
		if (Floor == 0)
		{
			AddGroundFloorFacade(D, OutParts);
		}
		else if (GlasHoehe > 0.0)
		{
			Add(OutParts, EHqPrimitive::Box, EHqMaterial::Glass,
				FVector(0.0, 0.0, Unterkante + D.SlabCm + GlasHoehe * 0.5),
				FVector(Seite, Seite, GlasHoehe), Floor);
		}
	}

	// Attika: schliesst den Turm oben ab, damit das oberste Glasband nicht
	// als offene Kante endet.
	Add(OutParts, EHqPrimitive::Box, EHqMaterial::Concrete,
		FVector(0.0, 0.0, D.TotalHeightCm() + D.SlabCm * 0.5),
		FVector(D.FootprintCm + 40.0, D.FootprintCm + 40.0, D.SlabCm));

	// DER ERSCHLIESSUNGSKERN STEHT NICHT HIER, sondern in BuildVerticalCore:
	// er ist seit Stufe 2 hohl (Waende um Treppenhaus und Schacht) und damit
	// ein eigenes Bauteil mit eigenem Test. Die Huelle kennt nur seine
	// Oberkante, weil Krone und Mast darauf sitzen.
	const double KernOberkante = GetCoreTopHeightCm(D);

	// Krone: abgesetzter, schmalerer Aufsatz - die Silhouette, die den Turm
	// von einem Bueroblock unterscheidet.
	if (D.CrownHeightCm > 0.0)
	{
		const double KroneSeite = GetCrownHalfWidthCm(D) * 2.0;
		Add(OutParts, EHqPrimitive::Box, EHqMaterial::Metal,
			FVector(0.0, 0.0, KernOberkante + D.CrownHeightCm * 0.5),
			FVector(KroneSeite, KroneSeite, D.CrownHeightCm));
		// Mast mit Befeuerung ganz oben.
		Add(OutParts, EHqPrimitive::Cylinder, EHqMaterial::Metal,
			FVector(0.0, 0.0, KernOberkante + D.CrownHeightCm + 300.0),
			FVector(40.0, 40.0, 600.0));
	}

	// Landeplatz auf dem Dach, quer zum Kern versetzt, damit der Ausstieg
	// nicht in der Aufsetzflaeche liegt.
	const double PadZ = GetHelipadHeightCm(D);
	const double PadVersatz = GetHelipadOffsetCm(D);
	Add(OutParts, EHqPrimitive::Cylinder, EHqMaterial::Concrete,
		FVector(PadVersatz, 0.0, PadZ), FVector(D.HelipadDiameterCm, D.HelipadDiameterCm, 30.0));
	// Aussenring als Rand - Hubschrauberlandeplaetze haben einen.
	Add(OutParts, EHqPrimitive::Cylinder, EHqMaterial::Metal,
		FVector(PadVersatz, 0.0, PadZ + 16.0),
		FVector(D.HelipadDiameterCm + 60.0, D.HelipadDiameterCm + 60.0, 8.0));

	// Das "H": drei Balken, flach aufgelegt.
	const double HBreite = D.HelipadDiameterCm * 0.34;
	const double HHoehe = D.HelipadDiameterCm * 0.50;
	const double Strich = HBreite * 0.26;
	for (const double Seite : { 1.0, -1.0 })
	{
		Add(OutParts, EHqPrimitive::Box, EHqMaterial::Marking,
			FVector(PadVersatz, Seite * (HBreite * 0.5 - Strich * 0.5), PadZ + 18.0),
			FVector(HHoehe, Strich, 6.0));
	}
	Add(OutParts, EHqPrimitive::Box, EHqMaterial::Marking,
		FVector(PadVersatz, 0.0, PadZ + 18.0), FVector(Strich, HBreite, 6.0));
}

FSebboHqArrivalLayout SebboHq::BuildArrivalFacilities(const FSebboHqDimensions& D)
{
	FSebboHqArrivalLayout Layout;
	const double Half = D.FootprintCm * 0.5 + FMath::Max(0.0, D.PodiumOversizeCm);
	const FArrivalOpenings Openings = GroundFloorOpenings(D);
	const double FloorZ = D.SlabCm + 15.0;

	// Die Ziele liegen HINTER den Oeffnungen. Die Zufahrt bleibt auf der
	// Strassenseite +X; eine spaetere Tiefgarage kann von dieser ebenerdigen
	// Annahme aus weiter in den Bau gefuehrt werden.
	Layout.GarageTarget.CenterCm = FVector(Half - 420.0,
		(Openings.GarageY0 + Openings.GarageY1) * 0.5, 120.0);
	Layout.GarageTarget.ExtentCm = FVector(350.0, (Openings.GarageY1 - Openings.GarageY0) * 0.5 - 30.0, 120.0);
	Layout.PedestrianTarget.CenterCm = FVector(Half - 150.0,
		(Openings.PortalY0 + Openings.PortalY1) * 0.5, 90.0);
	Layout.PedestrianTarget.ExtentCm = FVector(110.0, (Openings.PortalY1 - Openings.PortalY0) * 0.5 - 10.0, 90.0);
	Layout.HelicopterTarget.CenterCm = FVector(GetHelipadOffsetCm(D), 0.0, GetHelipadHeightCm(D));
	Layout.HelicopterTarget.ExtentCm = FVector(D.HelipadDiameterCm * 0.35,
		D.HelipadDiameterCm * 0.35, 200.0);

	// Garage: Boden und eine kurze, private Einfassung. Kein zweites
	// Strassenband; bis zur Fassadenlinie bleibt die Oeffentlichkeit Sache der
	// RoadNetwork-Pipeline.
	AddBetween(Layout.Parts, EHqMaterial::Concrete,
		Half - 900.0, Half + 20.0, Openings.GarageY0, Openings.GarageY1,
		D.SlabCm, FloorZ);
	AddBetween(Layout.Parts, EHqMaterial::Concrete,
		Half - 900.0, Half - 860.0, Openings.GarageY0, Openings.GarageY1,
		FloorZ, Openings.ClearHeightCm);
	AddBetween(Layout.Parts, EHqMaterial::Metal,
		Half - 900.0, Half, Openings.GarageY0, Openings.GarageY0 + 25.0,
		FloorZ, Openings.ClearHeightCm);
	AddBetween(Layout.Parts, EHqMaterial::Metal,
		Half - 900.0, Half, Openings.GarageY1 - 25.0, Openings.GarageY1,
		FloorZ, Openings.ClearHeightCm);
	// Haltstreifen vor der Platter Strasse: die Zufahrt bleibt privat, aber
	// die Konfliktstelle mit dem durchlaufenden Verkehr ist sichtbar markiert.
	Add(Layout.Parts, EHqPrimitive::Box, EHqMaterial::Marking,
		FVector(Half - 110.0, (Openings.GarageY0 + Openings.GarageY1) * 0.5, FloorZ + 3.0),
		FVector(12.0, Openings.GarageY1 - Openings.GarageY0 - 80.0, 6.0));

	// Personeneingang: ebenerdiger Vorraum mit schlankem Sturz - die Oeffnung
	// selbst bleibt frei fuer die reale Pawn-Kapsel.
	AddBetween(Layout.Parts, EHqMaterial::Concrete,
		Half - 260.0, Half + 20.0, Openings.PortalY0, Openings.PortalY1,
		D.SlabCm, FloorZ);
	AddBetween(Layout.Parts, EHqMaterial::Metal,
		Half - 60.0, Half, Openings.PortalY0, Openings.PortalY1,
		Openings.ClearHeightCm, D.FloorHeightCm - D.SlabCm);

	return Layout;
}

void SebboHq::BuildVerticalCore(const FSebboHqDimensions& D, TArray<FHqPart>& OutParts)
{
	// HOHL, NICHT MASSIV. Die Darstellung ist rein additiv - ein Raum entsteht
	// hier nur aus Waenden um eine Leere, nie aus einem Vollkoerper. Ein
	// frueherer Entwurf setzte "Oeffnung" und "Kabinenraum" als solide Kaesten
	// und nannte das begehbar; im Spiel waere es ein Betonklotz gewesen.
	//
	// Der Kern sitzt MITTIG im Grundriss (nicht daneben): mit CoreCm = 900 und
	// einem 3000er Grundriss bleibt er auf allen Seiten 1050 cm von der Fassade
	// entfernt. Ein Entwurf davor legte ihn auf X = 1500 - genau die
	// Fassadenkante - und liess den Schacht 3 m aus dem Turm ragen.
	//
	//   +Y-Haelfte: Aufzugsschacht (leer, ohne Boeden - es ist ein Schacht)
	//   -Y-Haelfte: Treppenhaus (Podeste und Laeufe, begehbar)
	//   Tueroeffnungen je Geschoss auf der -X-Seite, zum Buerogeschoss hin.

	const double Aussen = D.CoreCm * 0.5;          // 450 bei CoreCm = 900
	const double Wand = 25.0;                      // Wandstaerke
	const double Innen = Aussen - Wand;            // lichte Innenkante
	const double Trennung = Wand * 0.5;            // halbe Mittelwand bei Y = 0
	const double TuerBreite = 110.0;
	const double TuerHoehe = 210.0;
	const double PodestDicke = 20.0;
	const double StufenHoehe = 25.0;               // steil, aber begehbar
	const double KernOberkante = GetCoreTopHeightCm(D);

	// Mitte der beiden Kammern in Y.
	const double SchachtMitteY = (Trennung + Innen) * 0.5;

	// DAS TREPPENHAUS WIRD IN DER QUERE NOCHMALS GETEILT.
	//
	// Vorher deckte das Podest jedes Geschosses den GANZEN Grundriss der
	// -Y-Haelfte - und lag damit als Decke ueber dem Lauf, der von unten
	// genau dorthin steigt. Nachgerechnet blieben ueber der achten Stufe
	// 155 cm, ueber der fuenfzehnten 5 cm, und die sechzehnte lag IM Podest.
	// Man kam 220 von 400 cm hoch und stand mit dem Kopf an der Decke. Die
	// Stufenhoehe stimmte, die Lueckenfreiheit auch - nur begehbar war es
	// nicht.
	//
	// Jetzt liegt der LAUF in der aeusseren Haelfte (-Y) und das PODEST in
	// der inneren; sie ueberdecken sich nicht mehr. Ueber jeder Stufe steht
	// damit der Lauf des naechsten Geschosses, und der ist 380 cm hoeher.
	// Das ist auch die uebliche Bauweise: im Podest bleibt die Treppenoeffnung.
	const double TreppeY0 = -Innen;                        // Aussenkante
	const double TreppeY1 = -Trennung;                     // Mittelwand
	const double TrennY = (TreppeY0 + TreppeY1) * 0.5;     // Lauf | Podest
	const double PodestMitteY = (TrennY + TreppeY1) * 0.5; // dorthin die Tuer

	OutParts.Reserve(OutParts.Num() + D.FloorCount * 30 + 8);

	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		const double Z0 = Floor * D.FloorHeightCm;
		const double Z1 = Z0 + D.FloorHeightCm;

		// --- Die vier Aussenwaende des Kerns ---------------------------------
		// +X, +Y und -Y laufen durch; -X traegt die beiden Tueroeffnungen.
		AddBetween(OutParts, EHqMaterial::Concrete, Innen, Aussen, -Aussen, Aussen, Z0, Z1, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, Aussen, Innen, Aussen, Z0, Z1, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, Aussen, -Aussen, -Innen, Z0, Z1, Floor);

		// Mittelwand zwischen Schacht und Treppenhaus.
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, Aussen, -Trennung, Trennung, Z0, Z1, Floor);

		// --- Die -X-Wand mit zwei Tueroeffnungen ------------------------------
		// Je Kammer eine Oeffnung: Stueck links davon, Stueck rechts davon, und
		// der Sturz darueber. Die Luecke dazwischen IST die Tuer.
		const double TuerSchachtY0 = SchachtMitteY - TuerBreite * 0.5;
		const double TuerSchachtY1 = SchachtMitteY + TuerBreite * 0.5;
		// Die Tuer fuehrt auf das PODEST, nicht auf den Lauf - sonst traete
		// man aus dem Buerogeschoss mitten in die Treppe.
		const double TuerTreppeY0 = PodestMitteY - TuerBreite * 0.5;
		const double TuerTreppeY1 = PodestMitteY + TuerBreite * 0.5;

		const double Sturz = Z0 + PodestDicke + TuerHoehe;
		// Wandstuecke zwischen den Oeffnungen, ueber die volle Geschosshoehe.
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, -Aussen, TuerTreppeY0, Z0, Z1, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, TuerTreppeY1, TuerSchachtY0, Z0, Z1, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, TuerSchachtY1, Aussen, Z0, Z1, Floor);
		// Sturz ueber beiden Oeffnungen.
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, TuerTreppeY0, TuerTreppeY1, Sturz, Z1, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, TuerSchachtY0, TuerSchachtY1, Sturz, Z1, Floor);

		// --- Treppenhaus: Podest und Lauf -------------------------------------
		// Das Podest ist der Boden des Geschosses; der Lauf fuehrt von hier zum
		// naechsten. Der Schacht bekommt bewusst KEINEN Boden.
		AddBetween(OutParts, EHqMaterial::Concrete,
			-Innen, Innen, TrennY, TreppeY1, Z0, Z0 + PodestDicke, Floor);

		{
			// Gerader Lauf laengs X ueber die Treppenhaus-Haelfte. Die Stufen
			// sind 25 cm hoch - steil, aber der Spieler kommt hinauf; bei einer
			// ganzen Geschosshoehe je Stufe kaeme er es nicht.
			const int32 Stufen = FMath::Max(1, FMath::RoundToInt(D.FloorHeightCm / StufenHoehe));
			const double Lauflaenge = (Innen * 2.0) - 40.0;
			const double Auftritt = Lauflaenge / Stufen;
			for (int32 Stufe = 0; Stufe < Stufen; ++Stufe)
			{
				const double StufeX0 = -Innen + 20.0 + Stufe * Auftritt;
				const double StufeZ = Z0 + PodestDicke + (Stufe + 1) * (D.FloorHeightCm / Stufen);
				AddBetween(OutParts, EHqMaterial::Concrete,
					StufeX0, StufeX0 + Auftritt, TreppeY0 + 20.0, TrennY - 5.0,
					StufeZ - PodestDicke, StufeZ, Floor);
			}
		}
	}

	// --- Dachaufbau ueber der Attika ----------------------------------------
	// Er sitzt AUF dem obersten Geschoss, nicht darueber in der Luft: ein
	// frueherer Entwurf setzte ihn auf Z 6200 bei einer Attika-Oberkante von
	// 6045 und liess ihn 155 cm schweben.
	{
		const double Z0 = D.TotalHeightCm();
		const double Z1 = KernOberkante;
		AddBetween(OutParts, EHqMaterial::Concrete, Innen, Aussen, -Aussen, Aussen, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, Aussen, Innen, Aussen, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, Aussen, -Aussen, -Innen, Z0, Z1);
		// Die -X-Seite bleibt bis auf den Sturz offen: das ist der Ausstieg aufs Dach.
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, -Aussen, -TuerBreite * 0.5, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen, TuerBreite * 0.5, Aussen, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, -Aussen, -Innen,
			-TuerBreite * 0.5, TuerBreite * 0.5, Z0 + TuerHoehe, Z1);
		// Decke des Dachaufbaus.
		AddBetween(OutParts, EHqMaterial::Metal, -Aussen, Aussen, -Aussen, Aussen,
			Z1, Z1 + PodestDicke);
	}
}
