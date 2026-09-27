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
		double GarageY0 = -1300.0;
		double GarageY1 = -700.0;
		double PortalY0 = -1520.0;
		double PortalY1 = -1340.0;
		double ClearHeightCm = 270.0;
	};

	/**
	 * EINE Zufahrtsbucht, nicht zwei gegenueberliegende Oeffnungen.
	 *
	 * GEMESSEN am 21.09.2026 auf Alkis17: das Grundstueck wird von genau einer
	 * Strasse bedient (Wolkenbruch, 0,7 m vom Garagenanker; die naechste
	 * andere liegt 57,6 m weg), und diese Strasse FAELLT quer ueber das
	 * Grundstueck - rund 11 cm Hoehe je Grad Umfangswinkel.
	 *
	 * Das Portal lag frueher auf der anderen Fassadenhaelfte, 65 Grad von der
	 * Garage entfernt. Daraus folgten rund 7 m Hoehenunterschied zwischen den
	 * beiden Zufahrtspunkten - und ein Plateau hat EINE Hoehe. Ebenerdige
	 * Ankunft fuer Auto und Fuss war damit nicht an zu wenig Erdbau
	 * gescheitert, sondern an Arithmetik.
	 *
	 * Das Portal steht darum jetzt unmittelbar NEBEN der Garage, mit einem
	 * schmalen Pfeiler dazwischen. Beide sehen dieselbe Stelle der Strasse.
	 * Der Test SebboHq.EineZufahrtsbucht haelt das fest.
	 */
	FArrivalOpenings GroundFloorOpenings(const FSebboHqDimensions& D)
	{
		const double Half = D.FootprintCm * 0.5 + FMath::Max(0.0, D.PodiumOversizeCm);
		constexpr double PfeilerCm = 40.0;      // Wandstueck zwischen Tor und Tuer
		constexpr double TuerbreiteCm = 180.0;  // lichte Breite des Personeneingangs

		FArrivalOpenings Openings;
		Openings.GarageY0 = FMath::Max(-Half + 120.0, Openings.GarageY0);
		Openings.GarageY1 = FMath::Min(Half - 120.0, Openings.GarageY1);

		// Das Portal folgt der Garage, statt eigene Zahlen zu fuehren: zwei
		// unabhaengige Werte liefen beim ersten Versuch genau deshalb
		// auseinander.
		Openings.PortalY1 = Openings.GarageY0 - PfeilerCm;
		Openings.PortalY0 = Openings.PortalY1 - TuerbreiteCm;
		Openings.PortalY0 = FMath::Max(-Half + 120.0, Openings.PortalY0);

		// LICHTE HOEHE: der Fussgaenger muss MIT seiner Schritthoehe durchpassen.
		//
		// 270 cm standen hier mit dem Vermerk, die Oeffnung bleibe "frei fuer
		// die reale Pawn-Kapsel". Das galt fuer eine STEHENDE Kapsel auf dem
		// Innenboden: 60 + 180 = 240 cm. AWiesbadenFootPawn hebt die Kapsel
		// aber vor JEDEM Schritt um MaxStepHeightCm = 40 cm an, und der Belag
		// vor dem Portal liegt gemessen 107 cm ueber dem Plateau. Oberkante
		// beim Anheben damit 107 + 180 + 40 = 327 cm - gegen eine
		// Sturzunterkante von 270 cm.
		//
		// GEMESSEN am 21.09.2026 auf Alkis17: die Sonde blieb bei Schritt 8
		// von 14 im Sturz stecken, oertlich (1670, -1430, 312), Groesse
		// (60, 180, 85) - genau dieses Bauteil.
		//
		// Die Oeffnung nimmt jetzt, was das Geschoss hergibt. Der Sturz bleibt
		// als Bauteil erhalten, nur schlanker.
		//
		// 20 cm Abstand zur Decke waren ein gegriffener Wert und genau 3 cm zu
		// viel: mit 335 cm blieb die Sonde bei Schritt 11 von 14 haengen, weil
		// der Belag am Tuerlauf auf 118 cm gestiegen war (118 + 180 + 40 = 338).
		// 5 cm lassen dem Sturz noch eine Kante und dem Gehenden 12 cm Luft.
		Openings.ClearHeightCm = D.FloorHeightCm - D.SlabCm - 5.0;
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
		//
		// Die Reihenfolge wird SORTIERT statt vorausgesetzt. Vorher stand
		// "erst Garage, dann Portal" fest verdrahtet; seit beide in derselben
		// Bucht liegen, ist das Portal das untere - die feste Reihenfolge
		// haette Wandstuecke mit negativer Breite erzeugt und die Oeffnungen
		// an der falschen Stelle gelassen.
		struct FLuecke { double Y0; double Y1; };
		TArray<FLuecke> Luecken;
		Luecken.Add({ Openings.GarageY0, Openings.GarageY1 });
		Luecken.Add({ Openings.PortalY0, Openings.PortalY1 });
		Luecken.Sort([](const FLuecke& A, const FLuecke& B) { return A.Y0 < B.Y0; });

		double Laufend = -Half;
		for (const FLuecke& Luecke : Luecken)
		{
			if (Luecke.Y0 > Laufend)
			{
				AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half,
					Laufend, Luecke.Y0, Z0, Z1, 0);
			}
			// Sturz ueber der Oeffnung.
			AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half,
				Luecke.Y0, Luecke.Y1, Openings.ClearHeightCm, Z1, 0);
			Laufend = FMath::Max(Laufend, Luecke.Y1);
		}
		if (Half > Laufend)
		{
			AddBetween(Parts, EHqMaterial::Glass, Half - Wall, Half, Laufend, Half, Z0, Z1, 0);
		}

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

double SebboHq::GetAccessFloorCm(const FSebboHqDimensions& D)
{
	return D.SlabCm + 15.0;
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

	OutParts.Reserve(OutParts.Num() + D.FloorCount * 8 + 12);

	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		const bool bPodium = Floor < Podium;
		const double Seite = bPodium ? PodiumHalf * 2.0 : D.FootprintCm;
		const double Unterkante = Floor * D.FloorHeightCm;

		// Die Geschossdecke umschliesst den Kern als Ring. Ein massiver Quader
		// wuerde den Aufzugsschacht auf JEDEM Geschoss verschliessen.
		const double DeckenHalf = Seite * 0.5 + 10.0;
		const double KernHalf = D.CoreCm * 0.5;
		AddBetween(OutParts, EHqMaterial::Concrete,
			-DeckenHalf, -KernHalf, -DeckenHalf, DeckenHalf,
			Unterkante, Unterkante + D.SlabCm, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete,
			KernHalf, DeckenHalf, -DeckenHalf, DeckenHalf,
			Unterkante, Unterkante + D.SlabCm, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete,
			-KernHalf, KernHalf, -DeckenHalf, -KernHalf,
			Unterkante, Unterkante + D.SlabCm, Floor);
		AddBetween(OutParts, EHqMaterial::Concrete,
			-KernHalf, KernHalf, KernHalf, DeckenHalf,
			Unterkante, Unterkante + D.SlabCm, Floor);

		// Das Erdgeschoss hat zwei echte Oeffnungen. Auch die Regelgeschosse
		// brauchen einen HOHLEN Innenraum: ein vollflaechiger 30x30-m-Glasquader
		// blockierte Aufzug, Treppe und jede kuenftige Inneneinrichtung.
		const double GlasHoehe = D.FloorHeightCm - D.SlabCm;
		if (Floor == 0)
		{
			AddGroundFloorFacade(D, OutParts);
		}
		else if (GlasHoehe > 0.0)
		{
			const double Kante = Seite * 0.5;
			constexpr double Fassadentiefe = 18.0;
			const double Z0 = Unterkante + D.SlabCm;
			const double Z1 = Unterkante + D.FloorHeightCm;
			AddBetween(OutParts, EHqMaterial::Glass,
				Kante - Fassadentiefe, Kante, -Kante, Kante, Z0, Z1, Floor);
			AddBetween(OutParts, EHqMaterial::Glass,
				-Kante, -Kante + Fassadentiefe, -Kante, Kante, Z0, Z1, Floor);
			AddBetween(OutParts, EHqMaterial::Glass,
				-Kante, Kante, Kante - Fassadentiefe, Kante, Z0, Z1, Floor);
			AddBetween(OutParts, EHqMaterial::Glass,
				-Kante, Kante, -Kante, -Kante + Fassadentiefe, Z0, Z1, Floor);
		}
	}

	// Attika: schliesst den Turm oben ab, damit das oberste Glasband nicht
	// als offene Kante endet. Als RING um den Kern wie die Geschossdecken -
	// ein Vollquader lag auch ueber dem Treppenhaus: die Treppenprobe steckte
	// im letzten Lauf bei 58,85 m mit dem Kopf an der Attika-Unterkante, der
	// Turm war nicht bis aufs Dach begehbar. Den Kern schliesst
	// BuildVerticalCore auf Dachhoehe selbst (Austrittspodest, Schachtdeckel).
	{
		const double AttikaHalf = (D.FootprintCm + 40.0) * 0.5;
		const double KernHalf = D.CoreCm * 0.5;
		const double Z0 = D.TotalHeightCm();
		const double Z1 = Z0 + D.SlabCm;
		AddBetween(OutParts, EHqMaterial::Concrete, -AttikaHalf, -KernHalf, -AttikaHalf, AttikaHalf, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, KernHalf, AttikaHalf, -AttikaHalf, AttikaHalf, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, -KernHalf, KernHalf, -AttikaHalf, -KernHalf, Z0, Z1);
		AddBetween(OutParts, EHqMaterial::Concrete, -KernHalf, KernHalf, KernHalf, AttikaHalf, Z0, Z1);
	}

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

FSebboHqArrivalLayout SebboHq::BuildArrivalFacilities(
	const FSebboHqDimensions& D, const FSebboHqAnschluss* Anschluss)
{
	FSebboHqArrivalLayout Layout;
	const double Half = D.FootprintCm * 0.5 + FMath::Max(0.0, D.PodiumOversizeCm);
	const FArrivalOpenings Openings = GroundFloorOpenings(D);
	const double FloorZ = GetAccessFloorCm(D);

	// Deckenhoehe: sanfte Kuppe ueber die GANZE Laenge (der oeffentliche
	// Fuss-/Radweg liegt im Trog 2,14 m unter Strassenniveau - ohne Kuppe
	// verschliesst die Deckenunterkante den Weg), am Aussenrand auf die
	// gemessene Strassenhoehe abgezogen (EndHoehe).
	//
	// Die Kuppe darf NICHT steiler sein: SebboHq.Schwellenrampe laesst
	// hoechstens 6 cm Kante zwischen benachbarten Deckstuecken zu. 40 cm Hub
	// ueber 12 m Sinus geben bei 30-cm-Stuecken rund 3,1 cm - die fruehere
	// 35-cm-Kuppe ueber nur 4,3 m war mit 15 cm je Stueck deutlich zu steil.
	constexpr double WegZone0Cm = 190.0;
	constexpr double WegZone1Cm = 620.0;
	constexpr double KuppenHubCm = 40.0;
	const auto EndHoehe = [&Anschluss, FloorZ](const TArray<double>& Reihe, double Anteil) -> double
	{
		if (Reihe.Num() == 0)
		{
			return FloorZ;
		}
		const double Stelle = FMath::Clamp(Anteil, 0.0, 1.0) * (Reihe.Num() - 1);
		const int32 I0 = FMath::Clamp(static_cast<int32>(Stelle), 0, Reihe.Num() - 1);
		const int32 I1 = FMath::Min(I0 + 1, Reihe.Num() - 1);
		return FMath::Lerp(Reihe[I0], Reihe[I1], Stelle - I0);
	};
	const auto BelagHoehe = [FloorZ, KuppenHubCm](
		double LaengeCm, double WegAnteil, double EndZ) -> double
	{
		return FloorZ + KuppenHubCm * FMath::Sin(PI * WegAnteil)
			+ (EndZ - FloorZ) * WegAnteil;
	};

	// Die Ziele liegen HINTER den Oeffnungen. Die Zufahrt bleibt auf der
	// Strassenseite +X; eine spaetere Tiefgarage kann von dieser ebenerdigen
	// Annahme aus weiter in den Bau gefuehrt werden.
	Layout.GarageTarget.CenterCm = FVector(Half - 420.0,
		(Openings.GarageY0 + Openings.GarageY1) * 0.5, 120.0);
	Layout.GarageTarget.ExtentCm = FVector(350.0, (Openings.GarageY1 - Openings.GarageY0) * 0.5 - 30.0, 120.0);
	// Das Portalziel nimmt die VOLLE lichte Hoehe des Durchgangs, nicht die
	// Hoehe einer Person auf dem geplanten Innenboden.
	//
	// Vorher stand es bei 90 +- 90 cm, also 0..180 - berechnet fuer jemanden,
	// der auf dem privaten Boden (60 cm) steht. GEMESSEN am 21.09.2026 liegt
	// der Belag im Tuerlauf aber auf 146 cm: die Kapsel kam durch das Portal
	// und endete mit 236 cm Mitte 56 cm UEBER dem Volumen. Der Weg war frei,
	// die Ankunft wurde trotzdem nicht erkannt.
	//
	// Wer durch die Tuer geht, ist angekommen - unabhaengig davon, wie hoch
	// der Belag dort gerade liegt. Darum deckt das Ziel den Durchgang ab.
	Layout.PedestrianTarget.CenterCm = FVector(Half - 150.0,
		(Openings.PortalY0 + Openings.PortalY1) * 0.5, Openings.ClearHeightCm * 0.5);
	Layout.PedestrianTarget.ExtentCm = FVector(110.0,
		(Openings.PortalY1 - Openings.PortalY0) * 0.5 - 10.0, Openings.ClearHeightCm * 0.5);
	Layout.HelicopterTarget.CenterCm = FVector(GetHelipadOffsetCm(D), 0.0, GetHelipadHeightCm(D));
	Layout.HelicopterTarget.ExtentCm = FVector(D.HelipadDiameterCm * 0.35,
		D.HelipadDiameterCm * 0.35, 200.0);

	// Garage: Boden und eine kurze, private Einfassung. Kein zweites
	// Strassenband; bis zur Fassadenlinie bleibt die Oeffentlichkeit Sache der
	// RoadNetwork-Pipeline.
	AddBetween(Layout.Parts, EHqMaterial::Concrete,
		Half - 900.0, Half, Openings.GarageY0, Openings.GarageY1,
		D.SlabCm, FloorZ);

	// Zufahrt und Personeneingang EBENERDIG ueber die Platter Strasse.
	//
	// Die alte Konstruktion endete auf EINER starren Hoehe. Die Platter
	// Fahrbahn faellt aber an den Oeffnungen entlang rund 6 % (gemessen:
	// 11032 cm am Portalrand bis 10999 cm am Garagenrand) - ein starres
	// Deck endet dort mit einer Kante bis 15 cm, nicht "ebenerdig mit der
	// Platter Strasse". Gleichzeitig liegt die Deckenkante auf keinen Fall
	// 15 cm TIEFER als der Belag, sonst rollt das Auto beim Auffahren eine
	// Stufe hinab.
	//
	// Jetzt: Deck mit derselben Kuppe wie bisher (40 cm Sinus ueber die
	// volle Laenge - sie gibt dem oeffentlichen Fuss-/Radweg 35825899 im
	// Trog 2,14 m unter Strassenniveau seine lichte Hoehe), aber am
	// Aussenrand je Y-Spalte auf die GEMESSENE Strassenoberflaeche
	// abgezogen (FSebboHqAnschluss). Das Deck laeuft so stufenlos auf
	// Strassenniveau zu, und die 15-cm-Kante entfaellt.
	{
		constexpr int32 Segmente = 40;
		constexpr double DeckStaerkeCm = 15.0;
		const auto BaueDeck = [&](double LaengeCm, double Y0, double Y1,
			const TArray<double>& Reihe)
		{
			const int32 Spalten = FMath::Max(1, Reihe.Num());
			for (int32 s = 0; s < Spalten; ++s)
			{
				const double SY0 = Y0 + (Y1 - Y0) * s / Spalten;
				const double SY1 = Y0 + (Y1 - Y0) * (s + 1) / Spalten;
				const double EndZ = EndHoehe(Reihe, (s + 0.5) / Spalten);
				for (int32 i = 0; i < Segmente; ++i)
				{
					const double T0 = static_cast<double>(i) / Segmente;
					const double T1 = static_cast<double>(i + 1) / Segmente;
					const double Oben = BelagHoehe(LaengeCm, (T0 + T1) * 0.5, EndZ);
					AddBetween(Layout.Parts, EHqMaterial::Concrete,
						Half + LaengeCm * T0, Half + LaengeCm * T1, SY0, SY1,
						Oben - DeckStaerkeCm, Oben);
					// Widerlager nur ausserhalb des 2-m-Wegs. Zwischen
					// X=1890 und 2320 cm bleibt die volle Breite offen.
					if (Half + LaengeCm * T1 <= Half + WegZone0Cm
						|| Half + LaengeCm * T0 >= Half + WegZone1Cm)
					{
						AddBetween(Layout.Parts, EHqMaterial::Concrete,
							Half + LaengeCm * T0, Half + LaengeCm * T1, SY0, SY1,
							FloorZ - 250.0, Oben - DeckStaerkeCm);
					}
				}
			}
		};

		BaueDeck(GarageBridgeLengthCm, Openings.GarageY0, Openings.GarageY1,
			Anschluss ? Anschluss->GarageZCm : TArray<double>());
		BaueDeck(PedestrianBridgeLengthCm, Openings.PortalY0, Openings.PortalY1,
			Anschluss ? Anschluss->PortalZCm : TArray<double>());

		// Trennung und Absturzschutz liegen oberhalb des Decks; der Weg unten
		// bleibt auch an seinen Raendern frei.
		AddBetween(Layout.Parts, EHqMaterial::Metal,
			Half + 70.0, Half + PedestrianBridgeLengthCm - 40.0,
			Openings.PortalY0, Openings.PortalY0 + 10.0, FloorZ + 50.0, FloorZ + 125.0);
		AddBetween(Layout.Parts, EHqMaterial::Metal,
			Half + 70.0, Half + GarageBridgeLengthCm - 40.0,
			Openings.GarageY1 - 10.0, Openings.GarageY1, FloorZ + 50.0, FloorZ + 125.0);
		AddBetween(Layout.Parts, EHqMaterial::Metal,
			Half + 70.0, Half + PedestrianBridgeLengthCm - 40.0,
			Openings.PortalY1, Openings.GarageY0, FloorZ + 50.0, FloorZ + 105.0);
	}
	AddBetween(Layout.Parts, EHqMaterial::Concrete,
		Half - 900.0, Half - 860.0, Openings.GarageY0, Openings.GarageY1,
		FloorZ, Openings.ClearHeightCm);
	AddBetween(Layout.Parts, EHqMaterial::Metal,
		Half - 900.0, Half, Openings.GarageY0, Openings.GarageY0 + 25.0,
		FloorZ, Openings.ClearHeightCm);
	AddBetween(Layout.Parts, EHqMaterial::Metal,
		Half - 900.0, Half, Openings.GarageY1 - 25.0, Openings.GarageY1,
		FloorZ, Openings.ClearHeightCm);
	// Haltstreifen vor der Platter Strasse auf dem Fahrspurende - auf der
	// HOEHE DES DECKS dort (bei geneigtem Anschluss liegt FloorZ+13 falsch).
	const double HaltstreifenZ = BelagHoehe(GarageBridgeLengthCm,
		1100.0 / GarageBridgeLengthCm,
		EndHoehe(Anschluss ? Anschluss->GarageZCm : TArray<double>(), 0.5)) + 13.0;
	Add(Layout.Parts, EHqPrimitive::Box, EHqMaterial::Marking,
		FVector(Half + 1100.0, (Openings.GarageY0 + Openings.GarageY1) * 0.5, HaltstreifenZ),
		FVector(12.0, Openings.GarageY1 - Openings.GarageY0 - 80.0, 6.0));

	// Personeneingang: ebenerdiger Vorraum mit schlankem Sturz - die Oeffnung
	// selbst bleibt frei fuer die reale Pawn-Kapsel.
	AddBetween(Layout.Parts, EHqMaterial::Concrete,
		Half - 160.0, Half + 20.0, Openings.PortalY0, Openings.PortalY1,
		D.SlabCm, FloorZ);

	// UND EINE RAMPE AUF DEN HALLENBODEN statt einer Kante.
	//
	// Der Vorraum liegt auf FloorZ (SlabCm + 15), die Halle auf SlabCm. An
	// seinem inneren Rand stand damit eine 15 cm hohe Stufe quer im Weg -
	// 2,9 m hinter der Tuer, im Dunkeln. Gesehen hat sie niemand, weil das
	// Gelaende bis dahin ueber ihr lag: erst seit die Strassenboeschung den
	// Grundriss nicht mehr zuschuettet, ist der Vorraum ueberhaupt die
	// Trittflaeche (GEMESSEN am 21.09.2026 auf Alkis17, fuss_profil: Wechsel
	// auf den Vorraum bei x 1720, auf die Halle bei x 1430).
	//
	// 15 cm sind unter der Schrittgrenze der Kapsel (40 cm) - die Sonde haette
	// nie etwas gemeldet. Es ist trotzdem eine Stolperkante.
	{
		// 200 cm lang, nicht 100 - 1,5 cm Fall je Stufe statt 3.
		//
		// NICHT, weil die kurze Rampe fehlte: sie war da. Die Sonde tastete
		// alle 10 cm, die Stufen waren 10 cm lang, und ein Lot genau auf der
		// Kante eines Quaders trifft ihn nicht - das Profil zeigte darum
		// durchgehend den Hallenboden. Der Messfehler ist im Taster behoben
		// (3 cm Versatz); die laengere Rampe bleibt, weil sie flacher ist.
		constexpr int32 StufenZahl = 10;
		constexpr double RampeLaengeCm = 200.0;
		const double FallCm = FloorZ - D.SlabCm;
		for (int32 i = 0; i < StufenZahl; ++i)
		{
			const double X1 = Half - 160.0 - RampeLaengeCm * i / StufenZahl;
			const double X0 = Half - 160.0 - RampeLaengeCm * (i + 1) / StufenZahl;
			const double Oben = FloorZ - FallCm * (i + 1) / StufenZahl;
			AddBetween(Layout.Parts, EHqMaterial::Concrete,
				X0, X1, Openings.PortalY0, Openings.PortalY1,
				D.SlabCm - 20.0, Oben);
		}
	}
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
	const double TuerHoehe = 300.0;
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

		// --- Handlauf am offenen Lauf (innere Kante) --------------------------
		// Pfosten auf der innersten Stufenkante, ein schraeger Stab darueber.
		// Rein dekorativ, OHNE Kollision (bCollision = false): die Figurenprobe
		// fuehrt die 80-cm-Kapsel auf der Laufmitte (Kapselfreiheit bis zur
		// Pfostenkante ~79 cm, also knapp doppelt Kapselradius), aber
		// Steckenbleiben zaehlt als Fehlschlag - ein Handlauf soll dort nicht
		// der neue Stolpergrund sein. Die Wandseite bleibt frei, weil die Probe
		// an den Uebergaengen dicht an der Wand steht.
		{
			const double Lauflaenge = (Innen * 2.0) - 40.0;   // wie im Lauf
			const double LaufX0 = -Innen + 20.0;
			const double LaufX1 = LaufX0 + Lauflaenge;
			const double RailY = TrennY - 5.0 - 4.0;          // innerste Stufenkante
			const double Steig = D.FloorHeightCm - StufenHoehe;  // erste Stufe fehlt
			const double Winkel = FMath::RadiansToDegrees(FMath::Atan2(Steig, Lauflaenge));
			const double RailLaenge = FMath::Sqrt(Steig * Steig + Lauflaenge * Lauflaenge);

			FHqPart Schiene;
			Schiene.Material = EHqMaterial::Metal;
			Schiene.CenterCm = FVector((LaufX0 + LaufX1) * 0.5, RailY,
				Z0 + PodestDicke + (StufenHoehe + D.FloorHeightCm) * 0.5 + 90.0);
			Schiene.SizeCm = FVector(RailLaenge, 8.0, 8.0);
			Schiene.Rotation = FRotator(-Winkel, 0.0, 0.0);   // steigt mit +X
			Schiene.bCollision = false;
			Schiene.Floor = Floor;
			OutParts.Add(Schiene);

			const int32 PfostenZahl = 7;
			for (int32 P = 0; P < PfostenZahl; ++P)
			{
				const double X = LaufX0 + 45.0 + (Lauflaenge - 90.0) * P / (PfostenZahl - 1);
				// Stufenoberkante dort - die Stufen steigen linear ueber den Lauf.
				const double StufenOberkante = Z0 + PodestDicke + StufenHoehe
					+ (X - LaufX0) * Steig / Lauflaenge;
				FHqPart Stab;
				Stab.Primitive = EHqPrimitive::Cylinder;
				Stab.Material = EHqMaterial::Metal;
				Stab.CenterCm = FVector(X, RailY, StufenOberkante + 42.0);
				Stab.SizeCm = FVector(7.0, 7.0, 92.0);
				Stab.bCollision = false;
				Stab.Floor = Floor;
				OutParts.Add(Stab);
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
		// Boden des Dachaufbaus buendig mit der Attika-Oberkante (die Attika ist
		// ein Ring um den Kern): Austrittspodest ueber der inneren Haelfte des
		// Treppenhauses - 25 cm ueber der letzten Stufe, steigbar - und ein
		// Deckel ueber dem Aufzugsschacht. Ueber dem letzten LAUF bleibt es
		// offen, sonst stoesst man dort wieder mit dem Kopf an.
		const double BodenOben = Z0 + D.SlabCm;
		AddBetween(OutParts, EHqMaterial::Concrete, -Innen, Innen, TrennY, TreppeY1, Z0, BodenOben);
		AddBetween(OutParts, EHqMaterial::Concrete, -Innen, Innen, -Trennung, Innen, Z0, BodenOben);
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

void SebboHq::BuildDachaufbauten(const FSebboHqDimensions& D, TArray<FSebboHqDachProp>& OutProps)
{
	// Standflaeche der Dachaufbauten ist die Oberkante der Attika, nicht die
	// Attika-Unterkante: die Attika ist ein Deckel auf dem obersten Geschoss
	// (TotalHeight bis + SlabCm), auf dem der Spieler steht.
	const double DachZ = GetRoofHeightCm(D) + D.SlabCm;
	const double KernOberkante = GetCoreTopHeightCm(D);

	// Satellitenschuessel: die Schale ist ins Mesh gebaut, die Oeffnung steht
	// im lokalen -X (Talseite) in rund 30 Grad Elevation - Yaw 0 genuegt.
	FSebboHqDachProp Schuessel;
	Schuessel.MeshPfad = TEXT("/Game/SebboTower/Meshes/SM_WbSeboDachSchuessel");
	Schuessel.PosCm = FVector(-950.0, -780.0, DachZ);
	Schuessel.ExtentCm = FVector(85.0, 65.0, 195.0);
	OutProps.Add(Schuessel);

	// Zwei Antennenmasten, gegeneinander versetzt.
	FSebboHqDachProp MastA;
	MastA.MeshPfad = TEXT("/Game/SebboTower/Meshes/SM_WbSeboDachMast");
	MastA.PosCm = FVector(-1180.0, 620.0, DachZ);
	MastA.ExtentCm = FVector(30.0, 70.0, 400.0);
	OutProps.Add(MastA);

	FSebboHqDachProp MastB = MastA;
	MastB.PosCm = FVector(-620.0, -1230.0, DachZ);
	MastB.YawDeg = 45.0;
	OutProps.Add(MastB);

	// Dachreklame auf der Krone, +X zur Platter Strasse. Auf der Krone (nicht
	// auf der Dachflaeche), damit die Wortmarke ueber den Sockel hinausschaut,
	// und so weit zurueckgesetzt (X 330 bei Kronehalb 480), dass ihr Vorsprung
	// den Anflugkorridor des Landeplatzes (ab X 545) nicht erreicht.
	if (D.CrownHeightCm > 0.0)
	{
		FSebboHqDachProp Logo;
		Logo.MeshPfad = TEXT("/Game/SebboTower/Meshes/SM_WbSeboDachLogo");
		Logo.PosCm = FVector(330.0, 0.0, KernOberkante + D.CrownHeightCm);
		Logo.ExtentCm = FVector(13.0, 240.0, 190.0);
		OutProps.Add(Logo);
	}
}

void SebboHq::BuildInnenausbau(const FSebboHqDimensions& D, TArray<FHqPart>& OutParts)
{
	// Vier Zonen je Geschoss, immer gleich (siehe Header): Lobby an der
	// -X-Kernwand vor den beiden Tueroeffnungen, Schreibtischwinkel zur
	// Platter Strasse (+X), Sitzungstisch zum +Y-Fenster, Regal und Sofa zum
	// -Y-Fenster. Dazwischen bleibt ein Ring von mindestens 2 m Gangbreite.
	//
	// DIE KERNMASSE STEHEN ZUM ZWEITEN MAL HIER (wie GetStairWalk): Wand 25,
	// Tuer 110 x 300, Podestdicke 20. Es gilt die Regel aus BuildVerticalCore:
	// wer diese Zahlen getrennt neu erfindet, baut eine zweite Wahrheit.
	const double Aussen = D.CoreCm * 0.5;
	const double Innen = Aussen - 25.0;
	const double Trennung = 12.5;
	const double TuerBreite = 110.0;
	const double TuerHoehe = 300.0;
	const double PodestDicke = 20.0;
	const double TrennY = (-Innen + -Trennung) * 0.5;
	const double PodestMitteY = (TrennY + -Trennung) * 0.5;

	OutParts.Reserve(OutParts.Num() + D.FloorCount * 40);

	for (int32 Floor = 0; Floor < D.FloorCount; ++Floor)
	{
		const double Z0 = Floor * D.FloorHeightCm;
		const double Zboden = Z0 + D.SlabCm;      // Oberkante Geschossdecke
		const double Z1 = Z0 + D.FloorHeightCm;   // Unterkante der Decke darueber
		const double TuerZ = Z0 + PodestDicke;    // Oberkante Treppenpodest

		// Bauteil mit der vollen FHqPart-Freiheit (Drehung, Kollision).
		const auto Bauteil = [&OutParts, Floor](EHqPrimitive Primitive, EHqMaterial Material,
			const FVector& Center, const FVector& Size, bool bCollide = true,
			const FRotator& Rotation = FRotator::ZeroRotator)
		{
			FHqPart Teil;
			Teil.Primitive = Primitive;
			Teil.Material = Material;
			Teil.CenterCm = Center;
			Teil.SizeCm = Size;
			Teil.Floor = Floor;
			Teil.Rotation = Rotation;
			Teil.bCollision = bCollide;
			OutParts.Add(Teil);
		};

		// --- Beleuchtung: vier flache Panels buendig unter der Decke --------
		// Leuchtende Laternenglas-Material, ohne Kollision. Dazu spannt der
		// Actor drei echte Punktlichter, die im Tick dem Spieler in die
		// naechste Etage folgen (Laternen-Muster der Strassenmoebel).
		const auto Leuchte = [&Bauteil, Z1](double X, double Y, double SX, double SY)
		{
			Bauteil(EHqPrimitive::Box, EHqMaterial::Lamp,
				FVector(X, Y, Z1 - 4.0), FVector(SX, SY, 8.0), false);
		};
		Leuchte(-1000.0, 56.0, 45.0, 320.0);   // Lobby vor beiden Kern-Tueren
		Leuchte(1000.0, 0.0, 45.0, 300.0);     // Schreibtischwinkel, Platter Strasse
		Leuchte(0.0, 1000.0, 300.0, 45.0);     // Sitzungswinkel
		Leuchte(0.0, -1000.0, 300.0, 45.0);    // Regal-/Sofaecke

		// --- Fluchttuer zum Treppenhaus, OFFEN gegen die Wand ---------------
		// Die Luecke liegt auf der -X-Seite wie in BuildVerticalCore; der
		// Fluegel schlaegt 90 Grad auf und liegt an der Bueroseite der Wand
		// neben der Oeffnung (Band an der -Y-Laibung). Die Schacht-Luecke daneben
		// traegt bereits die Schiebetueren des Aufzugs. Deko ohne Kollision.
		Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
			FVector(-419.0, PodestMitteY - TuerBreite * 0.5 - 52.5, TuerZ + 142.5),
			FVector(8.0, 105.0, TuerHoehe - 15.0), false);
		// Klinke: kurzer waagerechter Stab auf Griffhoehe, aus der Fluegelflaeche.
		Bauteil(EHqPrimitive::Cylinder, EHqMaterial::Metal,
			FVector(-408.0, PodestMitteY - TuerBreite * 0.5 - 15.0, TuerZ + 105.0),
			FVector(4.0, 4.0, 14.0), false, FRotator(90.0, 0.0, 0.0));

		// --- Lobby: zwei Baenke an den Wangen, zwei Pflanzkuebel ------------
		// Die Bankflucht liegt seitlich des Laufwegs von den Kern-Tueren nach
		// +X (der Mittelgang bleibt frei).
		for (const double Y : { -330.0, 330.0 })
		{
			Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
				FVector(-800.0, Y, Zboden + 17.5), FVector(140.0, 40.0, 35.0));
			Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
				FVector(-800.0, Y, Zboden + 39.0), FVector(150.0, 45.0, 8.0));
		}
		for (const double Y : { -600.0, 600.0 })
		{
			Bauteil(EHqPrimitive::Cylinder, EHqMaterial::Metal,
				FVector(-1350.0, Y, Zboden + 20.0), FVector(40.0, 40.0, 40.0));
			Bauteil(EHqPrimitive::Cylinder, EHqMaterial::Plant,
				FVector(-1350.0, Y, Zboden + 100.0), FVector(70.0, 70.0, 120.0));
		}

		// --- Schreibtischwinkel (+X): zwei Plaetze mit Containern und Stuehlen
		for (const double Y : { -260.0, 260.0 })
		{
			Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
				FVector(1250.0, Y, Zboden + 75.0), FVector(90.0, 170.0, 6.0));
			Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
				FVector(1250.0, Y - 55.0, Zboden + 35.0), FVector(85.0, 55.0, 70.0));
			Bauteil(EHqPrimitive::Cylinder, EHqMaterial::Metal,
				FVector(1140.0, Y, Zboden + 21.0), FVector(8.0, 8.0, 42.0));
			Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
				FVector(1140.0, Y, Zboden + 45.0), FVector(45.0, 45.0, 6.0));
			Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
				FVector(1118.0, Y, Zboden + 73.0), FVector(7.0, 45.0, 50.0));
		}

		// --- Sitzungswinkel (+Y): Tuerk auf einem Sockel, zwei Sessel --------
		Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
			FVector(0.0, 950.0, Zboden + 72.0), FVector(200.0, 110.0, 8.0));
		Bauteil(EHqPrimitive::Box, EHqMaterial::Metal,
			FVector(0.0, 950.0, Zboden + 35.0), FVector(40.0, 40.0, 70.0));
		for (const double X : { -80.0, 80.0 })
		{
			Bauteil(EHqPrimitive::Cylinder, EHqMaterial::Metal,
				FVector(X, 830.0, Zboden + 21.0), FVector(8.0, 8.0, 42.0));
			Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
				FVector(X, 830.0, Zboden + 45.0), FVector(45.0, 45.0, 6.0));
			Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
				FVector(X, 808.0, Zboden + 73.0), FVector(45.0, 7.0, 50.0));
		}

		// --- Regal-/Sofaecke (-Y): Regalwand und Sofa mit Ruecken zur Wand ---
		for (const double X : { 160.0, 340.0 })
		{
			Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
				FVector(X, -1350.0, Zboden + 95.0), FVector(8.0, 40.0, 190.0));
		}
		for (const double H : { 30.0, 95.0, 160.0 })
		{
			Bauteil(EHqPrimitive::Box, EHqMaterial::Wood,
				FVector(250.0, -1350.0, Zboden + H), FVector(196.0, 40.0, 6.0));
		}
		Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
			FVector(-250.0, -1130.0, Zboden + 19.0), FVector(180.0, 70.0, 38.0));
		Bauteil(EHqPrimitive::Box, EHqMaterial::Fabric,
			FVector(-250.0, -1159.0, Zboden + 65.0), FVector(180.0, 12.0, 55.0));
	}
}
