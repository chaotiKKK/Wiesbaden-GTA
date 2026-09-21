// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

// MESSWERKZEUGE des SebboTower - getrennt vom Ankunftsverhalten.
//
// Beide Sonden gehoeren zur Klasse AWiesbadenSebboHq, aber nicht in die Datei,
// die ihr Verhalten traegt: sie waren dort 783 von 1310 Zeilen gewachsen, und
// wer am Ankunftsverhalten etwas aendern wollte, las an einer halben Tausend
// Zeilen Messcode vorbei. Der Actor enthaelt jetzt nur noch, was im Spiel
// passiert; hier steht, womit es nachgemessen wird.
//
//   ProbeStaircase   -WbTreppenProbe   Saved/Diagnose/treppenprobe.json
//   ProbeArrival     -WbAnkunftProbe   Saved/Diagnose/ankunftsprobe.json
//
// REIN VERSCHOBEN: kein Verhalten geaendert, keine Messgroesse ergaenzt.

#include "World/WiesbadenSebboHq.h"

#include "Components/BoxComponent.h"
#include "CollisionQueryParams.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// DIESELBE Kategorie wie der Actor, nicht eine eigene: die Logzeilen der
// Sonden sollen sich durch das Verschieben nicht aendern.
DEFINE_LOG_CATEGORY_STATIC(LogWbSebboHq, Log, All);

namespace
{
	/**
	 * Ein Schritt mit der Kapsel der Spielfigur - dieselbe Regel wie
	 * AWiesbadenFootPawn::TryStep: anheben, vorwaerts, absetzen. Nur wenn alle
	 * drei Teilstuecke frei sind, war es eine Stufe und keine Wand.
	 */
	bool KapselSchritt(const UWorld* World, const AActor* /*Selbst*/,
		const FVector& Von, const FVector& Richtung, double Weite,
		double MaxStufeCm, FVector& OutNach, FString& OutGrund,
		UPrimitiveComponent** OutBlocker = nullptr)
	{
		OutNach = Von;
		if (!World)
		{
			OutGrund = TEXT("keine Welt");
			return false;
		}

		// KEIN AddIgnoredActor(Selbst)! Der Turm IST das, wogegen hier
		// getastet wird. Aus der Bodensuche uebernommen, wo das Ignorieren
		// richtig ist, hat es die Sonde blind gemacht: sie traf nur noch das
		// Landscape und meldete ueberall "nichts unter den Fuessen".
		FCollisionQueryParams Params(SCENE_QUERY_STAT(WbTreppenProbe), false);
		const FCollisionShape Kapsel = FCollisionShape::MakeCapsule(40.0f, 90.0f);

		// bStartPenetrating NICHT als Wand werten.
		//
		// Die Kapsel steht mit ihrer Unterkante auf der Trittflaeche. Ein
		// Sweep, der beruehrend beginnt, meldet genau dort einen Treffer -
		// die erste Fassung der Sonde las das als "kein Kopfraum" und kam
		// keinen einzigen Schritt weit, obwohl die Treppe frei war.
		const auto Versperrt = [](const FHitResult& H)
		{
			return H.bBlockingHit && !H.bStartPenetrating;
		};

		FHitResult Treffer;
		const FVector Hoch = Von + FVector(0.0, 0.0, MaxStufeCm);
		if (World->SweepSingleByChannel(Treffer, Von, Hoch, FQuat::Identity,
			ECC_Pawn, Kapsel, Params) && Versperrt(Treffer))
		{
			OutGrund = FString::Printf(TEXT("kein Kopfraum (%s)"),
				*GetNameSafe(Treffer.GetActor()));
			if (OutBlocker) { *OutBlocker = Treffer.GetComponent(); }
			return false;
		}

		const FVector Vor = Hoch + Richtung.GetSafeNormal() * Weite;
		if (World->SweepSingleByChannel(Treffer, Hoch, Vor, FQuat::Identity,
			ECC_Pawn, Kapsel, Params) && Versperrt(Treffer))
		{
			// MIT Namen: "eine Wand quer im Weg" sagt nicht, ob die Fassade,
			// das Gelaende oder ein Nachbarhaus im Weg stand - und genau das
			// ist die Frage, sobald die Sonde ausserhalb des Turms laeuft.
			OutGrund = FString::Printf(TEXT("eine Wand quer im Weg (%s)"),
				*GetNameSafe(Treffer.GetComponent()));
			if (OutBlocker) { *OutBlocker = Treffer.GetComponent(); }
			return false;
		}

		// Absetzen: bis zu einer vollen Stufe nach unten suchen.
		const FVector Tief = Vor - FVector(0.0, 0.0, MaxStufeCm * 2.0);
		if (World->SweepSingleByChannel(Treffer, Vor, Tief, FQuat::Identity,
			ECC_Pawn, Kapsel, Params))
		{
			// EIN SCHRITT, DER IN DER GEOMETRIE ENDET, IST KEINER.
			//
			// GEMESSEN am 21.09.2026: der Fussweg meldete "Portal begehbar",
			// alle 14 Schritte, und endete 3,47 m UEBER dem Portalvolumen
			// (Volumenkoordinaten Z 347 bei halber Hoehe 90). Vor liegt eine
			// volle Stufe HOEHER als der Ausgangspunkt; steckte die Kapsel
			// dort, wurde sie bisher trotzdem dorthin gesetzt und meldete
			// Erfolg. Ueber 14 Schritte ratscht das 5,6 m nach oben - die
			// Sonde kletterte durch das Gebaeude und gab das als begangenen
			// Weg aus.
			//
			// Geprueft wird der Sweep-Start, NICHT eine Ueberlappung der
			// Endlage: eine Kapsel, die auf dem Landscape aufsetzt, meldet
			// dort regelmaessig Ueberlappung, und die erste Fassung dieser
			// Pruefung liess deshalb keinen einzigen Schritt mehr zu.
			if (Treffer.bStartPenetrating)
			{
				OutGrund = FString::Printf(TEXT("steckt in der Geometrie (%s)"),
					*GetNameSafe(Treffer.GetComponent()));
				if (OutBlocker) { *OutBlocker = Treffer.GetComponent(); }
				return false;
			}
			OutNach = Treffer.Location;
			return true;
		}
		OutGrund = TEXT("nichts unter den Fuessen");
		// Nichts unter den Fuessen - das waere ein Loch, kein Schritt.
		return false;
	}
}

void AWiesbadenSebboHq::ProbeStaircase() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Die Masse des Treppenhauses noch einmal hier auszurechnen waere eine
	// zweite Wahrheit. Sie stehen darum in denselben Groessen wie im Bauteil:
	// Aussen = CoreCm/2, Wand 25, daraus Innen und die Quer-Teilung.
	const double Aussen = Dimensions.CoreCm * 0.5;
	const double Innen = Aussen - 25.0;
	const double Trennung = 12.5;
	const double TrennY = (-Innen + -Trennung) * 0.5;
	const double LaufY = (-Innen + 20.0 + TrennY - 5.0) * 0.5;   // Mitte des Laufs
	const double PodestY = (TrennY + -Trennung) * 0.5;           // Mitte des Podests
	const double LaufX0 = -Innen + 40.0;
	const double LaufX1 = Innen - 40.0;
	const double Kapselmitte = 90.0;
	const double MaxStufe = 40.0;      // wie AWiesbadenFootPawn::MaxStepHeightCm

	const FRotator Drehung(0.0, HeadingDegrees, 0.0);
	const FVector Fuss = BuiltBase;   // NICHT GetActorLocation(), siehe Header
	const auto NachWelt = [&](const FVector& Oertlich)
	{
		return Fuss + Drehung.RotateVector(Oertlich);
	};

	// WO FAENGT DER WEG AN? Nicht zwingend im Erdgeschoss.
	//
	// Der Turm setzt sich auf den Bodenpunkt seiner MITTE. Am Hang liegt das
	// Gelaende an der Treppenhausecke hoeher und schneidet durch die unteren
	// Stufen - die Sonde lief dort gegen das Landscape. Gemessen wird das
	// darum, statt es zu uebergehen: Bodenhoehe an der Treppe suchen und auf
	// der ersten Stufe darueber beginnen.
	double GelaendeUeberFuss = 0.0;
	{
		const FVector Oben = NachWelt(FVector(LaufX0, LaufY, 20000.0));
		FHitResult Boden;
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbTreppenProbeBoden), true);
		P.AddIgnoredActor(this);
		if (World->LineTraceSingleByChannel(Boden, Oben,
			Oben - FVector(0.0, 0.0, 40000.0), ECC_WorldStatic, P))
		{
			GelaendeUeberFuss = Boden.Location.Z - Fuss.Z;
		}
	}
	// Stufenlage GENAU wie im Bauteil (SebboHqShape::BuildVerticalCore).
	//
	// Eine eigene, aehnliche Rechnung reicht nicht: mit LaufX0 als Nullpunkt
	// und 16 gleichen Teilen lag die Sonde eine halbe Stufe daneben und stand
	// in der Luft ("nichts unter den Fuessen"). Die Stufen beginnen bei
	// -Innen + 20 und sind (Innen*2 - 40)/16 tief.
	const int32 Stufenzahl = 16;
	const double Stufenhoehe = Dimensions.FloorHeightCm / Stufenzahl;
	const double Auftritt = ((Innen * 2.0) - 40.0) / Stufenzahl;
	const auto StufeMitteX = [&](int32 k) { return -Innen + 20.0 + (k + 0.5) * Auftritt; };
	const auto StufeObenZ = [&](int32 k) { return 20.0 + (k + 1) * Stufenhoehe; };

	int32 ErsteStufe = 0;
	while (StufeObenZ(ErsteStufe) < GelaendeUeberFuss + 10.0 && ErsteStufe < Stufenzahl - 2)
	{
		++ErsteStufe;
	}
	// Zwei Zentimeter Luft: eine Kapsel, die die Flaeche genau beruehrt,
	// meldet beim Sweep sofort einen Treffer.
	const double StartHoehe = StufeObenZ(ErsteStufe) + 2.0;
	const double StartX = StufeMitteX(ErsteStufe);

	FVector Jetzt = NachWelt(FVector(StartX, LaufY, StartHoehe + Kapselmitte));
	const double StartZ = Jetzt.Z;

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Treppenprobe: Gelaende an der Treppe %.0f cm ueber dem Fusspunkt, ")
		TEXT("Start auf Stufe %d (%.0f cm)."),
		GelaendeUeberFuss, ErsteStufe, StartHoehe);

	const FVector VorwaertsX = Drehung.RotateVector(FVector(1.0, 0.0, 0.0));
	const FVector QuerY = Drehung.RotateVector(FVector(0.0, 1.0, 0.0));

	int32 ErreichtesGeschoss = 0;
	int32 Schritte = 0;
	int32 Gescheitert = 0;
	FString Woran;

	// Je Geschoss: den Lauf hinauf, quer aufs Podest, ueber das Podest zurueck,
	// quer auf den naechsten Lauf. Genau der Weg, den ein Mensch geht.
	for (int32 Geschoss = 0; Geschoss < Dimensions.FloorCount; ++Geschoss)
	{
		bool bGeschossGeschafft = true;

		// 1) Den Lauf hinauf (in +X), Schrittweite 40 cm.
		const int32 SchritteLauf = FMath::CeilToInt((Innen - 20.0 - StartX) / 40.0);
		for (int32 i = 0; i < (Geschoss == 0 ? SchritteLauf
			: FMath::CeilToInt((LaufX1 - LaufX0) / 40.0)); ++i)
		{
			FVector Nach;
			FString Grund;
			if (!KapselSchritt(World, this, Jetzt, VorwaertsX, 40.0, MaxStufe, Nach, Grund))
			{
				bGeschossGeschafft = false;
				Woran = FString::Printf(
					TEXT("Lauf in Geschoss %d, Schritt %d von %d, Hoehe %.0f cm - %s"),
					Geschoss, i, SchritteLauf, Jetzt.Z - Fuss.Z, *Grund);
				break;
			}
			Jetzt = Nach;
			++Schritte;
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		// Im obersten Geschoss endet der Weg auf dem Dachaufbau - kein
		// weiterer Lauf mehr.
		if (Geschoss + 1 >= Dimensions.FloorCount)
		{
			ErreichtesGeschoss = Geschoss + 1;
			break;
		}

		// 2) Quer auf das Podest des erreichten Geschosses.
		{
			const int32 SchritteQuer = FMath::CeilToInt(FMath::Abs(PodestY - LaufY) / 40.0);
			for (int32 i = 0; i < SchritteQuer; ++i)
			{
				FVector Nach;
				FString Grund;
				if (!KapselSchritt(World, this, Jetzt, QuerY, 40.0, MaxStufe, Nach, Grund))
				{
					bGeschossGeschafft = false;
					Woran = FString::Printf(TEXT("Uebertritt aufs Podest in Geschoss %d - %s"), Geschoss + 1, *Grund);
					break;
				}
				Jetzt = Nach;
				++Schritte;
			}
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		// 3) Ueber das Podest zurueck zum Anfang des naechsten Laufs.
		{
			const int32 SchritteZurueck = FMath::CeilToInt((LaufX1 - LaufX0) / 40.0);
			for (int32 i = 0; i < SchritteZurueck; ++i)
			{
				FVector Nach;
				FString Grund;
				if (!KapselSchritt(World, this, Jetzt, -VorwaertsX, 40.0, MaxStufe, Nach, Grund))
				{
					bGeschossGeschafft = false;
					Woran = FString::Printf(TEXT("Podest in Geschoss %d - %s"), Geschoss + 1, *Grund);
					break;
				}
				Jetzt = Nach;
				++Schritte;
			}
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		// 4) Quer zurueck auf den naechsten Lauf.
		{
			const int32 SchritteQuer = FMath::CeilToInt(FMath::Abs(PodestY - LaufY) / 40.0);
			for (int32 i = 0; i < SchritteQuer; ++i)
			{
				FVector Nach;
				FString Grund;
				if (!KapselSchritt(World, this, Jetzt, -QuerY, 40.0, MaxStufe, Nach, Grund))
				{
					bGeschossGeschafft = false;
					Woran = FString::Printf(TEXT("Uebertritt auf den Lauf in Geschoss %d - %s"), Geschoss + 1, *Grund);
					break;
				}
				Jetzt = Nach;
				++Schritte;
			}
		}
		if (!bGeschossGeschafft) { ++Gescheitert; break; }

		ErreichtesGeschoss = Geschoss + 1;
	}

	const double Gestiegen = Jetzt.Z - StartZ;
	const double Sollhoehe = SebboHq::GetRoofHeightCm(Dimensions);

	// GESCHOSSE AUS DER HOEHE, nicht aus dem Schleifenzaehler.
	//
	// Der Zaehler zaehlte Durchlaeufe, nicht Geschosse: er meldete "3 von 15",
	// waehrend die Kapsel nachweislich 57,7 m gestiegen war. Die erreichte
	// Hoehe ist die einzige Zahl, die hier etwas aussagt - aus ihr folgt das
	// Geschoss, nicht umgekehrt.
	const double ErreichteHoehe = Jetzt.Z - Fuss.Z;
	ErreichtesGeschoss = FMath::Clamp(
		FMath::FloorToInt(ErreichteHoehe / Dimensions.FloorHeightCm), 0, Dimensions.FloorCount);
	const bool bDachErreicht = ErreichteHoehe >= Sollhoehe - Dimensions.FloorHeightCm;

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Treppenprobe: %d von %d Geschossen erreicht, %d Schritte, ")
		TEXT("%.1f m gestiegen (Ziel %.1f m).%s%s"),
		ErreichtesGeschoss, Dimensions.FloorCount, Schritte,
		Gestiegen / 100.0, Sollhoehe / 100.0,
		bDachErreicht ? TEXT(" DACH ERREICHT. Ende: ") : TEXT(" GESCHEITERT an: "),
		*Woran);

	// Ergebnis auch als Datei - eine Log-Zeile geht in 200.000 anderen unter.
	const FString Pfad = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("treppenprobe.json");
	const FString Inhalt = FString::Printf(
		TEXT("{\n \"geschosse_erreicht\": %d,\n \"geschosse_gesamt\": %d,\n")
		TEXT(" \"schritte\": %d,\n \"gestiegen_m\": %.2f,\n \"dachhoehe_m\": %.2f,\n")
		TEXT(" \"gelaende_ueber_fusspunkt_cm\": %.0f,\n \"erreichte_hoehe_m\": %.2f,\n")
		TEXT(" \"dach_erreicht\": %s,\n \"ende\": \"%s\"\n}\n"),
		ErreichtesGeschoss, Dimensions.FloorCount, Schritte,
		Gestiegen / 100.0, Sollhoehe / 100.0, GelaendeUeberFuss, ErreichteHoehe / 100.0,
		bDachErreicht ? TEXT("true") : TEXT("false"), *Woran);
	FFileHelper::SaveStringToFile(Inhalt, *Pfad);
}

namespace
{
	/** Ergebnis eines abgetasteten Ankunftswegs. */
	struct FWbAnkunftsweg
	{
		bool bFrei = false;
		double WegCm = 0.0;
		FString Woran;
	};

	/**
	 * Steckt der Koerper in etwas ANDEREM als dem Gelaende?
	 *
	 * GEMESSEN am 21.09.2026: der Fahrzeugquader stand mit Unterkante 10012 cm
	 * ueber einem Belag von 9987 cm - 25 cm Luft - und
	 * OverlapBlockingTestByChannel meldete trotzdem eine blockierende
	 * Ueberlappung mit dem LandscapeHeightfieldCollisionComponent. Dieselbe
	 * Falschmeldung hatte zuvor schon die Fusskapsel keinen Schritt weit
	 * kommen lassen.
	 *
	 * Das Gelaende wird darum uebergangen: der Quader wird ohnehin mit
	 * Bodenfreiheit UEBER den hoechsten Belag unter seiner Laenge gesetzt, er
	 * KANN dort nicht stecken. Eine Wand meldet sich weiterhin.
	 */
	bool SteckenderKoerper(const UWorld* World, const FVector& Ort, const FQuat& Drehung,
		const FCollisionShape& Form, const FCollisionQueryParams& Params, FString& OutName)
	{
		TArray<FOverlapResult> Ueberlappungen;
		World->OverlapMultiByChannel(Ueberlappungen, Ort, Drehung,
			ECC_WorldStatic, Form, Params);
		for (const FOverlapResult& U : Ueberlappungen)
		{
			UPrimitiveComponent* Teil = U.GetComponent();
			if (Teil && !Teil->IsA<ULandscapeHeightfieldCollisionComponent>())
			{
				OutName = GetNameSafe(Teil);
				return true;
			}
		}
		return false;
	}

	/** Liegt der Weltpunkt im Zielvolumen? Genau die Frage, die der Overlap stellt. */
	bool ImZielvolumen(const UBoxComponent* Volumen, const FVector& Punkt)
	{
		if (!Volumen)
		{
			return false;
		}
		const FVector Oertlich =
			Volumen->GetComponentTransform().InverseTransformPosition(Punkt);
		const FVector Halb = Volumen->GetScaledBoxExtent();
		return FMath::Abs(Oertlich.X) <= Halb.X
			&& FMath::Abs(Oertlich.Y) <= Halb.Y
			&& FMath::Abs(Oertlich.Z) <= Halb.Z;
	}

	/** Ueberschneiden sich zwei Zielvolumen? Dann setzte eine Ankunft zwei Zustaende. */
	bool VolumenUeberschneiden(const UBoxComponent* A, const UBoxComponent* B)
	{
		if (!A || !B)
		{
			return false;
		}
		return A->Bounds.GetBox().Intersect(B->Bounds.GetBox());
	}
}

void AWiesbadenSebboHq::ProbeArrival() const
{
	const UWorld* World = GetWorld();
	if (!World || !bBuilt)
	{
		return;
	}

	// DIESELBE Form wie der Bau, nicht eine zweite Rechnung daneben.
	const FSebboHqArrivalLayout Layout = SebboHq::BuildArrivalFacilities(Dimensions);
	const double Half = Dimensions.FootprintCm * 0.5 + FMath::Max(0.0, Dimensions.PodiumOversizeCm);
	const double BodenZ = Dimensions.SlabCm + 15.0;     // Oberkante des privaten Bodens
	const double Kapselmitte = 90.0;
	const double MaxStufe = 40.0;                       // wie AWiesbadenFootPawn

	const FRotator Drehung(0.0, HeadingDegrees, 0.0);
	const FVector Fuss = BuiltBase;
	const auto NachWelt = [&](const FVector& Oertlich)
	{
		return Fuss + Drehung.RotateVector(Oertlich);
	};

	// HIER STAND `StrasseUeberFuss`. Ersatzlos gestrichen, mit allem, was
	// daran hing: `belag_garage`, `belag_portal` und `strasse_gefunden`.
	//
	// Die Groesse hat einmal gebraucht, was sie mass - die Bodenhoehe fuer den
	// Startpunkt der Fusssonde. Seit die ihren Belag am eigenen Startpunkt
	// tastet, war der Hoehenwert toter Code, und uebrig blieben zwei Namen,
	// die nicht hielten, was sie versprachen:
	//
	//   strasse_gefunden  hiess in Wahrheit "das Lot hat IRGENDETWAS
	//     getroffen". Zuletzt gemeldet: `portal: true` neben einem
	//     `belag_portal: Landscape_...` - "Strasse gefunden" ueber einer Wiese.
	//   belag_*           war der ACTOR-Name des Getroffenen. Bei einer
	//     Stadtkachel ist das `WiesbadenCityChunk_...` und sagt gerade nicht,
	//     ob dort Fahrbahn oder Hauswand liegt - erst die Komponente sagt es.
	//
	// Und beide tasteten 8 m vor der Fassade, wo weder jemand geht noch faehrt.
	// Was dort zaehlt, misst `schwelle_*_cm` an der Schwelle und nennt die
	// getroffene Komponente dazu.

	// SCHWELLENHOEHE - die Stufe, ueber die Rad und Fuss wirklich muessen.
	//
	// Hier stand `BodenZ - StrasseGarageZ`, also der Abstand zu dem Belag, den
	// `StrasseUeberFuss` 8 m VOR der Fassade antastet. Das ergab
	// "bordstein_garage_cm: 116" direkt neben einem Wagen, der die Garage
	// gemessen ebenerdig erreicht - ein Widerspruch in derselben Tabelle.
	// Beide Zahlen stimmten: 8 m draussen liegt die Wiese wirklich 116 cm
	// tiefer, nur faehrt dort niemand ueber eine Kante. Die Schwelle liegt an
	// der Fassade, und davor liegt die Schuerze.
	//
	// Darum wird an der Fassadenlinie getastet und der TURM NICHT ignoriert:
	// die Schuerze IST der Belag, auf dem das Rad aufsetzt. Ein Lot, das sie
	// wegblendet, misst den Boden unter der Rampe und meldet wieder eine
	// Stufe, die es nicht gibt.
	//
	// Und sie meldet MIT, worauf sie getreten ist: "116" allein liess nicht
	// erkennen, ob da Wiese, Fahrbahn oder ein Vordach unter dem Lot lag.
	const auto SchwelleAn = [&](double Y, FString& OutBelag)
	{
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeSchwelle), true);
		// Von 2 m ueber dem Innenboden - das liegt noch in der lichten Hoehe der
		// Oeffnung; ein Lot von weit oben traefe den Turm selbst.
		const FVector Oben = NachWelt(FVector(Half + 50.0, Y, BodenZ + 200.0));
		FHitResult Boden;
		if (!World->LineTraceSingleByChannel(Boden, Oben,
			Oben - FVector(0.0, 0.0, 6200.0), ECC_WorldStatic, P))
		{
			OutBelag = TEXT("nichts gefunden");
			return TNumericLimits<double>::Lowest();   // unuebersehbar statt still 0
		}
		OutBelag = GetNameSafe(Boden.GetComponent());
		return BodenZ - (Boden.Location.Z - Fuss.Z);
	};

	// Die Bodenhoehen ZUERST - beide Wege setzen darauf auf.
	const double GarageY = Layout.GarageTarget.CenterCm.Y;
	const double PortalY = Layout.PedestrianTarget.CenterCm.Y;
	FString SchwelleBelagGarage;
	FString SchwelleBelagPortal;
	const double StufeGarageCm = SchwelleAn(GarageY, SchwelleBelagGarage);
	const double StufePortalCm = SchwelleAn(PortalY, SchwelleBelagPortal);

	// --- 1) Auto: durch die Garagenoeffnung -----------------------------------
	//
	// DER WAGEN FOLGT DEM BELAG, statt auf fester Hoehe zu fliegen.
	//
	// GEMESSEN am 21.09.2026 auf Alkis17, Hoehenprofil entlang der Zufahrt:
	//
	//     x 2400..1800   9987 .. 10020 cm   Landscape
	//     x 1700..1300  10052 .. 10094 cm   RoadCollisionStaticMesh
	//
	// Das ist eine gleichmaessige Rampe von 107 cm auf 11 m (rund 10 %) OHNE
	// Absatz, und sie endet mit 10094 cm buendig am Garagenboden (10096 cm).
	// Ein Quader auf fester Hoehe streifte sie trotzdem: er stiess bei 10176
	// an, also auf seiner eigenen Mittelhoehe und 124 cm ueber dem Belag
	// darunter. Gemessen war das kein Hindernis der Zufahrt, sondern die
	// Flughoehe der Sonde.
	//
	// Ein Auto faehrt auf dem Belag. Der Quader tut das jetzt auch: Schritt
	// fuer Schritt wird die Oberflaeche abgetastet und die Fahrhoehe daraus
	// gesetzt. Eine echte Wand haelt ihn weiterhin auf - sie steht in jeder
	// Hoehe im Weg.
	double AutoTrefferWeltZCm = 0.0;
	double AutoQuaderUnterkanteCm = 0.0;
	double AutoBelagZCm = 0.0;
	FString AutoBelagName = TEXT("-");
	FWbAnkunftsweg Wagenweg;
	{
		const FCollisionShape Wagen = FCollisionShape::MakeBox(FVector(225.0, 100.0, 75.0));
		// ECC_WorldStatic, nicht ECC_Vehicle: dieses Projekt richtet den
		// Fahrzeugkanal nirgends ein - dort antwortet die gebaute Geometrie
		// gar nicht, und die Sonde meldete jede Wand als freie Durchfahrt.
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeAuto), false);
		// Fuer die BELAGSUCHE zaehlt auch der Garagenboden - der Wagen faehrt
		// am Ende darauf. Darum hier NICHT den Turm ignorieren.
		FCollisionQueryParams Belag(SCENE_QUERY_STAT(WbAnkunftProbeBelag), true);

		const double StartX = Half + 700.0;
		const double ZielX = Layout.GarageTarget.CenterCm.X;
		constexpr double SchrittCm = 50.0;
		constexpr double BodenfreiheitCm = 20.0;

		// Fahrhoehe an einer Stelle: Belag plus halbe Quaderhoehe plus
		// Bodenfreiheit. Ohne Belag gibt es dort keine Zufahrt.
		const auto PoseBei = [&](double X, double SuchhoeheCm, FVector& Out) -> bool
		{
			// UEBER DIE GANZE QUADERLAENGE tasten, nicht nur unter der Mitte.
			// Der Quader ist 4,5 m lang; auf einer 10-%-Rampe liegt sein Heck
			// 22 cm hoeher als seine Mitte. Mit nur einem Taster steckte er am
			// Anfahrpunkt im Gelaende. Ein Auto ruht auf seinem HOECHSTEN
			// Aufstandspunkt - der zaehlt.
			const FVector Grund = NachWelt(FVector(X, GarageY, 0.0));
			bool bGefunden = false;
			double HoechsterCm = -TNumericLimits<double>::Max();
			// Laengs UND QUER tasten. Gemessen hat die Zufahrt 48 bis 87 cm
			// Quergefaelle ueber die 2 m Quaderbreite - die linke Seite liegt
			// durchweg hoeher als die Mitte. Mit nur einer Tastreihe in der
			// Mitte grub sich der Quader auf der hohen Seite ein und meldete
			// "versperrt", obwohl in Fahrtrichtung kein Absatz liegt.
			for (const double Versatz : { -200.0, 0.0, 200.0 })
			for (const double Quer : { -100.0, 0.0, 100.0 })
			{
				// VON DER BISHERIGEN FAHRHOEHE herab, nicht von 200 m.
				//
				// GEMESSEN: ein Taster von ganz oben findet, sobald er unter
				// das Gebaeude kommt, dessen DACH - der Quader wurde damit auf
				// 10856 cm gesetzt, 7,6 m ueber dem Garagenboden, und stiess
				// prompt an ein Bauteil in 125 m Hoehe. Die Zufahrt liegt
				// unter dem Turm, nicht auf ihm.
				const FVector Oben = NachWelt(FVector(X + Versatz, GarageY + Quer, 0.0))
					+ FVector(0.0, 0.0, SuchhoeheCm);
				FHitResult T;
				if (World->LineTraceSingleByChannel(T, Oben,
					Oben - FVector(0.0, 0.0, SuchhoeheCm + 600.0), ECC_WorldStatic, Belag))
				{
					HoechsterCm = FMath::Max(HoechsterCm, T.Location.Z);
					bGefunden = true;
				}
			}
			if (!bGefunden)
			{
				return false;
			}
			Out = FVector(Grund.X, Grund.Y, HoechsterCm + 75.0 + BodenfreiheitCm);
			return true;
		};

		FVector Jetzt = FVector::ZeroVector;
		if (!PoseBei(StartX, 20000.0, Jetzt))
		{
			Wagenweg.bFrei = false;
			Wagenweg.Woran = TEXT("am Anfahrpunkt liegt kein Belag");
		}
		else if (SteckenderKoerper(World, Jetzt, Drehung.Quaternion(), Wagen, P,
			AutoBelagName))
		{
			// Der Startpunkt wird eigens geprueft: ein Sweep, der steckend
			// beginnt, meldet bStartPenetrating, und das als "nicht versperrt"
			// zu lesen hatte frueher einen Quader im Hang als freie Durchfahrt
			// ausgegeben.
			AutoQuaderUnterkanteCm = Jetzt.Z - 75.0;
			Wagenweg.bFrei = false;
			Wagenweg.Woran = FString::Printf(
				TEXT("Startpunkt steckt in der Geometrie (%s)"), *AutoBelagName);
		}
		else
		{
			Wagenweg.bFrei = true;
			double Gefahren = 0.0;
			for (double X = StartX - SchrittCm; X >= ZielX - 1.0; X -= SchrittCm)
			{
				FVector Naechste;
				// 150 cm ueber der bisherigen Fahrhoehe suchen: das reicht fuer
				// jede Rampe und bleibt unter jedem Dach.
				const double SuchhoeheCm = (Jetzt.Z - 75.0) + 150.0
					- NachWelt(FVector(X, GarageY, 0.0)).Z;
				if (!PoseBei(X, FMath::Max(SuchhoeheCm, 150.0), Naechste))
				{
					Wagenweg.bFrei = false;
					Wagenweg.Woran = FString::Printf(
						TEXT("kein Belag bei x = %.0f"), X);
					break;
				}
				FHitResult Treffer;
				if (World->SweepSingleByChannel(Treffer, Jetzt, Naechste,
					Drehung.Quaternion(), ECC_WorldStatic, Wagen, P)
					&& Treffer.bBlockingHit && !Treffer.bStartPenetrating)
				{
					Wagenweg.bFrei = false;
					Wagenweg.Woran = FString::Printf(TEXT("versperrt durch %s"),
						*GetNameSafe(Treffer.GetComponent()));
					AutoTrefferWeltZCm = Treffer.Location.Z;
					AutoQuaderUnterkanteCm = Jetzt.Z - 75.0;
					FHitResult Darunter;
					const FVector Lot(Treffer.Location.X, Treffer.Location.Y,
						Treffer.Location.Z + 3000.0);
					if (World->LineTraceSingleByChannel(Darunter, Lot,
						Lot - FVector(0.0, 0.0, 6000.0), ECC_WorldStatic, Belag))
					{
						AutoBelagZCm = Darunter.Location.Z;
						AutoBelagName = GetNameSafe(Darunter.GetComponent());
					}
					break;
				}
				Gefahren += (Naechste - Jetzt).Size();
				Jetzt = Naechste;
			}
			Wagenweg.WegCm = Gefahren;
			if (Wagenweg.bFrei)
			{
				Wagenweg.Woran = TEXT("Durchfahrt frei");
			}
		}
	}

	// --- 1b) Hoehenprofil des Anfahrwegs --------------------------------------
	//
	// "versperrt durch RoadCollisionStaticMesh" sagt nicht, OB die Zufahrt
	// wirklich verbaut ist. Der Quader faehrt geradeaus auf fester Hoehe; ein
	// Belag, der unterwegs ansteigt, blockiert ihn auch dann, wenn ein Auto
	// dort muehelos hochfuehre. Darum die Oberflaeche Punkt fuer Punkt.
	FString AutoProfilJson;
	{
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProfil), true);
		P.AddIgnoredActor(this);   // der Turm ist hier nicht die Frage
		int32 N = 0;
		for (double X = Half + 700.0; X >= Layout.GarageTarget.CenterCm.X - 50.0; X -= 100.0)
		{
			const FVector Oben = NachWelt(FVector(X, GarageY, 0.0)) + FVector(0.0, 0.0, 20000.0);
			FHitResult Treffer;
			if (!World->LineTraceSingleByChannel(Treffer, Oben,
				Oben - FVector(0.0, 0.0, 40000.0), ECC_WorldStatic, P))
			{
				continue;
			}
			// UND QUER: der Quader ist 2 m breit. Liegt die Fahrbahn an seinen
			// Raendern hoeher als in der Mitte, stoesst er dort an, ohne dass
			// das Laengsprofil davon etwas zeigt.
			double LinksCm = 0.0;
			double RechtsCm = 0.0;
			for (int32 Seite = 0; Seite < 2; ++Seite)
			{
				const double Quer = Seite == 0 ? -100.0 : 100.0;
				const FVector ObenQ = NachWelt(FVector(X, GarageY + Quer, 0.0))
					+ FVector(0.0, 0.0, 20000.0);
				FHitResult TQ;
				if (World->LineTraceSingleByChannel(TQ, ObenQ,
					ObenQ - FVector(0.0, 0.0, 40000.0), ECC_WorldStatic, P))
				{
					(Seite == 0 ? LinksCm : RechtsCm) = TQ.Location.Z;
				}
			}
			AutoProfilJson += FString::Printf(
				TEXT("%s{\"x_lokal\": %.0f, \"z_cm\": %.0f, \"links\": %.0f, ")
				TEXT("\"rechts\": %.0f, \"belag\": \"%s\"}"),
				N > 0 ? TEXT(",\n  ") : TEXT(""), X, Treffer.Location.Z,
				LinksCm, RechtsCm, *GetNameSafe(Treffer.GetComponent()));
			++N;
		}
	}

	// --- 1c) Hoehenprofil des Portaldurchgangs --------------------------------
	//
	// "Portal begehbar" beweist keinen ebenen Durchgang. KapselSchritt sucht
	// den Boden bis zu ZWEI Stufen nach unten (80 cm) - ein Absatz von 44 cm
	// laesst die Kapsel also durch, ohne dass irgendetwas meldet. Genau das
	// war der offene Befund schwelle_portal_cm = -44: das Gelaende liegt vor
	// dem Personeneingang hoeher als der Innenboden, und man steigt hinein
	// statt hinauf.
	//
	// Darum dieselbe Behandlung wie bei der Zufahrt: die Oberflaeche Punkt
	// fuer Punkt, ENG (10 cm), damit eine Kante nicht zwischen zwei Tastern
	// verschwindet - und MIT dem Turm, denn seine Bodenplatte ist hier die
	// Trittflaeche und nicht die Frage.
	FString FussProfilJson;
	double GroessterAbsatzCm = 0.0;
	double AbsatzBeiXCm = 0.0;
	bool bAbsatzImDurchgang = false;
	{
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftFussProfil), true);
		int32 N = 0;
		double VorigesZ = 0.0;
		bool bVorigesGueltig = false;
		// BIS TIEF INS HAUS. Ein Profil, das an der Portalmitte endet, zeigt
		// die Schwelle gar nicht: gesucht ist die Stelle, an der das
		// Gelaende aufhoert und der Innenboden anfaengt.
		//
		// DIE 3 CM VERSATZ SIND KEIN SCHOENHEITSFEHLER. Ohne sie liegen die
		// Taster auf demselben Zentimeterraster wie die Bauteile, und ein Lot
		// genau auf der Kante eines Quaders trifft ihn nicht: die 10 cm
		// langen Stufen der Portalrampe waren damit komplett unsichtbar, bei
		// 20 cm langen blinkte jede zweite Probe auf den Hallenboden durch.
		// Beides sah wie fehlende Geometrie aus und war die Messung.
		for (double X = Half + 500.0 - 3.0; X >= -Half; X -= 10.0)
		{
			const FVector Oben = NachWelt(FVector(X, PortalY, BodenZ + 200.0));
			FHitResult Treffer;
			if (!World->LineTraceSingleByChannel(Treffer, Oben,
				Oben - FVector(0.0, 0.0, 8000.0), ECC_WorldStatic, P))
			{
				bVorigesGueltig = false;
				continue;
			}
			const double ZUeberFuss = Treffer.Location.Z - Fuss.Z;
			if (bVorigesGueltig)
			{
				// Der Gehende kommt von aussen: ein SPRUNG NACH UNTEN ist der
				// Absatz, ueber den er stolpert.
				const double Absatz = VorigesZ - ZUeberFuss;
				if (Absatz > GroessterAbsatzCm)
				{
					GroessterAbsatzCm = Absatz;
					AbsatzBeiXCm = X;
					// WO er liegt, ist die halbe Aussage. Der groesste Absatz des
					// letzten Laufs war die Bordkante der Wolkenbruch, 1,9 m VOR
					// dem Haus - unter dem Namen "portal_absatz" las sich das wie
					// eine Stufe in der Tuer.
					bAbsatzImDurchgang = X <= Half;
				}
			}
			VorigesZ = ZUeberFuss;
			bVorigesGueltig = true;

			FussProfilJson += FString::Printf(
				TEXT("%s{\"x_lokal\": %.0f, \"z_ueber_fuss_cm\": %.0f, \"belag\": \"%s\"}"),
				N > 0 ? TEXT(",\n  ") : TEXT(""), X, ZUeberFuss,
				*GetNameSafe(Treffer.GetComponent()));
			++N;
		}
	}

	// --- 2) Fuss: durch das Portal --------------------------------------------
	FWbAnkunftsweg Fussweg;
	FVector FussEnde = FVector::ZeroVector;
	UPrimitiveComponent* FussBlocker = nullptr;
	{
		// AUF DER STRASSE starten, nicht auf dem privaten Boden. Der erste
		// Lauf setzte die Kapsel auf Portalhoehe an, waehrend der Gehweg
		// 1,66 m tiefer lag - sie meldete im ersten Schritt "nichts unter
		// den Fuessen" und das sah aus wie ein verbautes Portal.
		//
		// UND DIE HOEHE AM STARTPUNKT NEHMEN, nicht 4 m weiter draussen. Hier
		// stand StrassePortalZ - das ist der Belag bei Half+800, gesetzt wird
		// die Kapsel aber bei Half+400. Solange dort dasselbe Gelaende lag,
		// fiel der Unterschied nicht auf; seit die Wolkenbruch bis vor das
		// Grundstueck reicht, misst das aeussere Lot die Fahrbahn und das
		// innere den Hang darueber. Die Kapsel startete 22 cm zu tief und
		// meldete "steckt in der Geometrie (RoadCollisionStaticMesh)" - ein
		// verbautes Portal, das es nicht gab. Dass es vorher gutging, lag nur
		// daran, dass SteckenderKoerper das Landscape ausnimmt.
		constexpr double StartXLokal = 400.0;
		double StartZ = BodenZ + 2.0;
		{
			FCollisionQueryParams PS(SCENE_QUERY_STAT(WbAnkunftProbeFussStart), true);
			PS.AddIgnoredActor(this);
			const FVector Oben = NachWelt(
				FVector(Half + StartXLokal, PortalY, BodenZ + 2000.0));
			FHitResult Belag;
			if (World->LineTraceSingleByChannel(Belag, Oben,
				Oben - FVector(0.0, 0.0, 8000.0), ECC_WorldStatic, PS))
			{
				StartZ = (Belag.Location.Z - Fuss.Z) + 2.0;
			}
		}
		FVector Jetzt = NachWelt(FVector(Half + StartXLokal, PortalY, StartZ + Kapselmitte));
		const FVector Start = Jetzt;
		const FVector NachInnen = Drehung.RotateVector(FVector(-1.0, 0.0, 0.0));
		const double ZielX = Layout.PedestrianTarget.CenterCm.X;
		const int32 SchritteSoll = FMath::CeilToInt((Half + StartXLokal - ZielX) / 40.0);
		Fussweg.bFrei = true;
		for (int32 i = 0; i < SchritteSoll; ++i)
		{
			FVector Nach;
			FString Grund;
			if (!KapselSchritt(World, this, Jetzt, NachInnen, 40.0, MaxStufe, Nach, Grund,
				&FussBlocker))
			{
				Fussweg.bFrei = false;
				Fussweg.Woran = FString::Printf(TEXT("Schritt %d von %d - %s"),
					i, SchritteSoll, *Grund);
				break;
			}
			Jetzt = Nach;
		}
		FussEnde = Jetzt;
		Fussweg.WegCm = (Jetzt - Start).Size2D();
		if (Fussweg.bFrei)
		{
			Fussweg.Woran = TEXT("Portal begehbar");
		}
	}

	// --- 3) Helikopter: Lot auf den Landeplatz --------------------------------
	//
	// Zwei Fragen, nicht eine: liegt unter dem Ziel wirklich das Pad, und ist
	// der Luftraum darueber frei? Ein Lot allein beantwortet nur die erste.
	FWbAnkunftsweg Heliweg;
	double PadHoeheCm = 0.0;
	bool bPadGefunden = false;
	{
		const double PadSoll = SebboHq::GetHelipadHeightCm(Dimensions);
		const FVector Mitte = Layout.HelicopterTarget.CenterCm;
		const FVector Oben = NachWelt(FVector(Mitte.X, Mitte.Y, PadSoll + 15000.0));
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeHeli), false);

		FHitResult Lot;
		const bool bPad = World->LineTraceSingleByChannel(Lot, Oben,
			NachWelt(FVector(Mitte.X, Mitte.Y, -1000.0)), ECC_WorldStatic, P);
		// -1 WAERE KEINE HOEHE, sondern ein Merkzettel. Fehlt der Aufsetzpunkt,
		// sagt das Feld das auch (null); eine Zahl steht nur da, wo gemessen
		// wurde.
		bPadGefunden = bPad;
		PadHoeheCm = bPad ? (Lot.Location.Z - Fuss.Z) : 0.0;

		// Anflug mit Rotorradius statt mit einem Strich.
		FHitResult Anflug;
		const FCollisionShape Rotor =
			FCollisionShape::MakeSphere(static_cast<float>(Dimensions.HelipadDiameterCm * 0.35));
		// Der Anflug endet, wo die Rotorebene ueber dem Platz schwebt - eine
		// Kugel, die bis 4 m ueber die Flaeche faehrt, ragt mit ihrem unteren
		// Rand hinein und meldete den Landeplatz selbst als Hindernis.
		const double SchwebeZ = PadSoll + Rotor.GetSphereRadius() + 100.0;
		const bool bLuftraum = !World->SweepSingleByChannel(Anflug, Oben,
			NachWelt(FVector(Mitte.X, Mitte.Y, SchwebeZ)),
			FQuat::Identity, ECC_WorldStatic, Rotor, P);

		const bool bPadTrifft = bPad && FMath::Abs(PadHoeheCm - PadSoll) <= 40.0;
		Heliweg.bFrei = bPadTrifft && bLuftraum;
		Heliweg.WegCm = PadHoeheCm;
		Heliweg.Woran = !bPad ? FString(TEXT("kein Landeplatz unter dem Ziel"))
			: !bPadTrifft ? FString::Printf(
				TEXT("Aufsetzpunkt %.0f cm statt %.0f cm"), PadHoeheCm, PadSoll)
			: !bLuftraum ? FString::Printf(TEXT("Anflug versperrt durch %s"),
				*GetNameSafe(Anflug.GetComponent()))
			: FString(TEXT("Landeplatz frei"));
	}

	// --- 3b) Das Gelaende rings um den Grundriss ------------------------------
	//
	// Zwei Punkte (Garage, Portal) sagen, DASS es nicht passt. Ein Plateau
	// braucht die ganze Verteilung: wieviel muss abgegraben, wieviel
	// aufgefuellt werden, und auf welcher Hoehe wird beides am kleinsten.
	//
	// UND WIRKLICH NUR GELAENDE. Das Lot trifft, was zuerst kommt: im
	// gemessenen Ring waren 3 von 24 Punkten das Dach eines Nachbarhauses
	// (WiesbadenCityActor_0 auf +400 cm). Die standen als "max_cm: 400" in der
	// Abnahmetabelle und zogen auch den Mittelwert hoch - eine Hausecke als
	// Gelaendehoehe. Gezaehlt wird darum nur, was auf dem Landscape liegt; der
	// Schnitt zeigt weiterhin JEDEN Punkt samt Kennzeichen, damit die
	// Auslassung nachpruefbar bleibt.
	double GelaendeMinCm = TNumericLimits<double>::Max();
	double GelaendeMaxCm = -TNumericLimits<double>::Max();
	double GelaendeSummeCm = 0.0;
	int32 GelaendePunkte = 0;
	int32 RingPunkteGesamt = 0;
	FString GelaendeSchnitt;
	{
		constexpr int32 Schritte = 24;
		const double Ring = Half + 300.0;
		FCollisionQueryParams P(SCENE_QUERY_STAT(WbAnkunftProbeRing), true);
		P.AddIgnoredActor(this);
		for (int32 i = 0; i < Schritte; ++i)
		{
			const double Winkel = 2.0 * PI * i / Schritte;
			const FVector Punkt(Ring * FMath::Cos(Winkel), Ring * FMath::Sin(Winkel), 0.0);
			FHitResult Boden;
			bool bGetroffen = false;
			for (const double StartHoehe : { 400.0, 4000.0 })
			{
				const FVector Oben = NachWelt(FVector(Punkt.X, Punkt.Y, StartHoehe));
				if (World->LineTraceSingleByChannel(Boden, Oben,
					Oben - FVector(0.0, 0.0, StartHoehe + 6000.0), ECC_WorldStatic, P))
				{
					bGetroffen = true;
					break;
				}
			}
			if (!bGetroffen)
			{
				continue;
			}
			const double H = Boden.Location.Z - Fuss.Z;
			// Der ganze Schnitt, nicht nur seine Spannweite: aus Min/Max
			// allein ist ein Strassenanschnitt nicht von einer gleichmaessigen
			// Hangneigung zu unterscheiden.
			// Und die KOMPONENTE, nicht der Actor: eine Stadtkachel heisst
			// immer WiesbadenCityChunk_..., ob dort Fahrbahn oder Hauswand
			// liegt, sagt erst der Komponentenname.
			const bool bGelaende = Boden.GetComponent()
				&& Boden.GetComponent()->IsA<ULandscapeHeightfieldCollisionComponent>();
			GelaendeSchnitt += FString::Printf(
				TEXT("%s{\"grad\": %.0f, \"cm\": %.0f, \"was\": \"%s\", \"gelaende\": %s}"),
				RingPunkteGesamt > 0 ? TEXT(", ") : TEXT(""),
				FMath::RadiansToDegrees(Winkel), H,
				*GetNameSafe(Boden.GetComponent()),
				bGelaende ? TEXT("true") : TEXT("false"));
			++RingPunkteGesamt;
			if (!bGelaende)
			{
				continue;   // ein Dach ist keine Gelaendehoehe
			}
			GelaendeMinCm = FMath::Min(GelaendeMinCm, H);
			GelaendeMaxCm = FMath::Max(GelaendeMaxCm, H);
			GelaendeSummeCm += H;
			++GelaendePunkte;
		}
	}
	const double GelaendeMittelCm = GelaendePunkte > 0 ? GelaendeSummeCm / GelaendePunkte : 0.0;
	if (GelaendePunkte == 0)
	{
		GelaendeMinCm = 0.0;
		GelaendeMaxCm = 0.0;
	}

	// --- 4) Die Naht: erreicht der Weg auch das Zielvolumen? ------------------
	//
	// Getastet wurde gegen die gebaute Geometrie; den Ankunftszustand setzt
	// aber das Volumen. Beide muessen dasselbe meinen.
	const FVector GarageZielWelt = NachWelt(FVector(
		Layout.GarageTarget.CenterCm.X, GarageY, BodenZ + 75.0));
	const bool bGarageTrifftVolumen = ImZielvolumen(GarageArrivalVolume, GarageZielWelt);
	const bool bFussTrifftVolumen = ImZielvolumen(PedestrianArrivalVolume, FussEnde);

	// GENAU DIE ZAHLEN, die ImZielvolumen vergleicht - keine nachgerechneten.
	// Der Widerspruch "rechnerisch drin, gemessen draussen" laesst sich nur so
	// aufloesen: Endpunkt in Volumenkoordinaten gegen die halbe Ausdehnung.
	FVector FussImVolumen = FVector::ZeroVector;
	FVector PortalHalb = FVector::ZeroVector;
	FVector PortalWeltMitte = FVector::ZeroVector;
	if (PedestrianArrivalVolume)
	{
		FussImVolumen = PedestrianArrivalVolume->GetComponentTransform()
			.InverseTransformPosition(FussEnde);
		PortalHalb = PedestrianArrivalVolume->GetScaledBoxExtent();
		PortalWeltMitte = PedestrianArrivalVolume->GetComponentLocation();
	}
	const bool bHeliTrifftVolumen = ImZielvolumen(HelipadArrivalVolume,
		NachWelt(Layout.HelicopterTarget.CenterCm));
	const bool bZieleGetrennt =
		!VolumenUeberschneiden(GarageArrivalVolume, PedestrianArrivalVolume)
		&& !VolumenUeberschneiden(GarageArrivalVolume, HelipadArrivalVolume)
		&& !VolumenUeberschneiden(PedestrianArrivalVolume, HelipadArrivalVolume);

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Ankunftsprobe: Auto %s (%s), Fuss %s (%s), Heli %s (%s). ")
		TEXT("Schwelle Garage %.0f cm, Portal %.0f cm. ")
		TEXT("Volumen getroffen: Auto %s, Fuss %s, Heli %s; getrennt: %s."),
		Wagenweg.bFrei ? TEXT("FREI") : TEXT("BLOCKIERT"), *Wagenweg.Woran,
		Fussweg.bFrei ? TEXT("FREI") : TEXT("BLOCKIERT"), *Fussweg.Woran,
		Heliweg.bFrei ? TEXT("FREI") : TEXT("BLOCKIERT"), *Heliweg.Woran,
		StufeGarageCm, StufePortalCm,
		bGarageTrifftVolumen ? TEXT("ja") : TEXT("nein"),
		bFussTrifftVolumen ? TEXT("ja") : TEXT("nein"),
		bHeliTrifftVolumen ? TEXT("ja") : TEXT("nein"),
		bZieleGetrennt ? TEXT("ja") : TEXT("nein"));

	// Das blockierende Bauteil in TURMKOORDINATEN - "StaticMeshComponent_448"
	// ist eine Nummer, erst die oertliche Lage sagt, welcher Quader das ist.
	FString BlockerName = TEXT("-");
	FVector BlockerMitte = FVector::ZeroVector;
	FVector BlockerGroesse = FVector::ZeroVector;
	if (FussBlocker)
	{
		BlockerName = GetNameSafe(FussBlocker);
		BlockerMitte = Drehung.UnrotateVector(FussBlocker->GetComponentLocation() - Fuss);
		// Die Bauteile sind skalierte 100-cm-Wuerfel; die Skalierung IST die
		// Kantenlaenge in Zentimetern.
		BlockerGroesse = FussBlocker->GetComponentScale() * 100.0;
	}

	const FString Pfad = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("ankunftsprobe.json");
	const auto JaNein = [](bool b) { return b ? TEXT("true") : TEXT("false"); };
	const FString AufsetzhoeheJson = bPadGefunden
		? FString::Printf(TEXT("%.0f"), PadHoeheCm)
		: FString(TEXT("null"));

	// Ein Treffer, den es nicht gab, wird NICHT als Null gemeldet.
	//
	// `auto_treffer` und `fuss_blocker` standen bei freiem Weg mit lauter
	// Nullen in der Tabelle - vier Messwerte, die keine waren. Null ist eine
	// Hoehe; "nicht gemessen" ist keine. Darum jetzt `null`.
	const FString AutoTrefferJson = Wagenweg.bFrei
		? FString(TEXT("null"))
		: FString::Printf(
			TEXT("{\"z_cm\": %.0f, \"quader_unterkante_cm\": %.0f, ")
			TEXT("\"belag_z_cm\": %.0f, \"belag\": \"%s\"}"),
			AutoTrefferWeltZCm, AutoQuaderUnterkanteCm, AutoBelagZCm, *AutoBelagName);
	const FString FussBlockerJson = FussBlocker
		? FString::Printf(
			TEXT("{\"name\": \"%s\", \"lokal_mitte\": [%.0f, %.0f, %.0f], ")
			TEXT("\"lokal_groesse\": [%.0f, %.0f, %.0f]}"),
			*BlockerName, BlockerMitte.X, BlockerMitte.Y, BlockerMitte.Z,
			BlockerGroesse.X, BlockerGroesse.Y, BlockerGroesse.Z)
		: FString(TEXT("null"));

	const FString Inhalt = FString::Printf(
		TEXT("{\n \"auto\": { \"frei\": %s, \"weg_cm\": %.0f, \"woran\": \"%s\" },\n")
		TEXT(" \"fuss\": { \"frei\": %s, \"weg_cm\": %.0f, \"woran\": \"%s\" },\n")
		TEXT(" \"heli\": { \"frei\": %s, \"aufsetzhoehe_cm\": %s, \"woran\": \"%s\" },\n")
		// OHNE DIE BEZUGSLINIE sind alle x_lokal der beiden Profile nicht zu
		// lesen: "x 1887" heisst erst etwas, wenn die Fassade bekannt ist.
		TEXT(" \"fassade_x_lokal\": %.0f,\n")
		TEXT(" \"schwelle_garage_cm\": %.0f,\n \"schwelle_portal_cm\": %.0f,\n")
		TEXT(" \"schwelle_belag\": { \"garage\": \"%s\", \"portal\": \"%s\" },\n")
		TEXT(" \"volumen_getroffen\": { \"auto\": %s, \"fuss\": %s, \"heli\": %s },\n")
		TEXT(" \"volumen_getrennt\": %s,\n")
		TEXT(" \"fuss_ende_welt\": [%.0f, %.0f, %.0f],\n")
		TEXT(" \"portal_volumen\": {\"mitte\": [%.0f, %.0f, %.0f], \"halb\": [%.0f, %.0f, %.0f]},\n")
		TEXT(" \"fuss_in_volumenkoordinaten\": [%.0f, %.0f, %.0f],\n")
		TEXT(" \"auto_profil\": [\n  %s\n ],\n")
		TEXT(" \"groesster_absatz_im_fussweg\": {\"cm\": %.0f, \"bei_x_lokal\": %.0f, ")
		TEXT("\"im_durchgang\": %s},\n")
		TEXT(" \"fuss_profil\": [\n  %s\n ],\n")
		TEXT(" \"fuss_blocker\": %s,\n")
		TEXT(" \"auto_treffer\": %s,\n")
		TEXT(" \"gelaende_ring\": { \"gelaendepunkte\": %d, \"von_punkten\": %d, ")
		TEXT("\"min_cm\": %.0f, \"max_cm\": %.0f, \"mittel_cm\": %.0f },\n")
		TEXT(" \"gelaende_schnitt\": [%s]\n}\n"),
		JaNein(Wagenweg.bFrei), Wagenweg.WegCm, *Wagenweg.Woran,
		JaNein(Fussweg.bFrei), Fussweg.WegCm, *Fussweg.Woran,
		JaNein(Heliweg.bFrei), *AufsetzhoeheJson, *Heliweg.Woran,
		Half,
		StufeGarageCm, StufePortalCm,
		*SchwelleBelagGarage, *SchwelleBelagPortal,
		JaNein(bGarageTrifftVolumen), JaNein(bFussTrifftVolumen), JaNein(bHeliTrifftVolumen),
		JaNein(bZieleGetrennt),
		FussEnde.X, FussEnde.Y, FussEnde.Z,
		PortalWeltMitte.X, PortalWeltMitte.Y, PortalWeltMitte.Z,
		PortalHalb.X, PortalHalb.Y, PortalHalb.Z,
		FussImVolumen.X, FussImVolumen.Y, FussImVolumen.Z,
		*AutoProfilJson,
		GroessterAbsatzCm, AbsatzBeiXCm, JaNein(bAbsatzImDurchgang),
		*FussProfilJson,
		*FussBlockerJson,
		*AutoTrefferJson,
		GelaendePunkte, RingPunkteGesamt,
		GelaendeMinCm, GelaendeMaxCm, GelaendeMittelCm, *GelaendeSchnitt);
	FFileHelper::SaveStringToFile(Inhalt, *Pfad);
}
