// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenSebboHq.h"

#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "CollisionQueryParams.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbSebboHq, Log, All);

namespace
{
	/** Materialpfade je Werkstoff - dieselben Stadt-Materialien wie die Wahrzeichen. */
	const TCHAR* MaterialPath(EHqMaterial Material)
	{
		switch (Material)
		{
		case EHqMaterial::Glass:   return TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate");
		case EHqMaterial::Metal:   return TEXT("/Game/Materials/City/M_WbLmSlate.M_WbLmSlate");
		case EHqMaterial::Marking: return TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite");
		default:                   return TEXT("/Game/Materials/City/M_WbLmWhite.M_WbLmWhite");
		}
	}
}

AWiesbadenSebboHq::AWiesbadenSebboHq()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenSebboHq::BeginPlay()
{
	Super::BeginPlay();

	CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

	Materials.SetNum(static_cast<int32>(EHqMaterial::MAX));
	for (int32 i = 0; i < Materials.Num(); ++i)
	{
		Materials[i] = LoadObject<UMaterialInterface>(
			nullptr, MaterialPath(static_cast<EHqMaterial>(i)));
	}

	UGeoCoordinateConverter* Own = NewObject<UGeoCoordinateConverter>(this);
	Own->InitializeWithWiesbadenOrigin();
	Converter = Own;
}

bool AWiesbadenSebboHq::ResolveGround(const FVector& WorldXY, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbSebboHqGround), true);
	Params.AddIgnoredActor(this);
	const FVector Start(WorldXY.X, WorldXY.Y, 100000.0);
	const FVector End(WorldXY.X, WorldXY.Y, -20000.0);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params)
		&& !Hit.bStartPenetrating)
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AWiesbadenSebboHq::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (bBuilt)
	{
		// Treppenprobe NACH dem Bauen, nicht im selben Bild (siehe
		// SecondsSinceBuild): die Kollisionskoerper brauchen einen Takt.
		if (!bProbed && SecondsSinceBuild >= 0.0f)
		{
			SecondsSinceBuild += DeltaSeconds;
			if (SecondsSinceBuild >= 2.0f)
			{
				bProbed = true;
				ProbeStaircase();
				SetActorTickEnabled(false);
			}
		}
		return;
	}
	if (!Converter)
	{
		return;
	}

	FGeoCoordinate Coord;
	Coord.Latitude = PlotLatitude;
	Coord.Longitude = PlotLongitude;
	Coord.Height = 0.0;
	const FVector Ground = Converter->GeoToUnrealGround(Coord);

	double Z = 0.0;
	if (!ResolveGround(Ground, Z))
	{
		return;     // Zelle noch nicht gestreamt - naechster Tick
	}

	Build(FVector(Ground.X, Ground.Y, Z), FRotator(0.0, HeadingDegrees, 0.0));
	bBuilt = true;

	// -WbTreppenProbe: nach dem Bauen einmal die Treppe hochsteigen. Nur ein
	// Messwerkzeug - ohne den Schalter tickt der Actor gar nicht weiter.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbTreppenProbe")))
	{
		SecondsSinceBuild = 0.0f;
	}
	else
	{
		SetActorTickEnabled(false);
	}
}

void AWiesbadenSebboHq::Build(const FVector& BaseWorld, const FRotator& BaseYaw)
{
	BuiltBase = BaseWorld;

	TArray<FHqPart> Teile;
	SebboHq::BuildShell(Dimensions, Teile);
	SebboHq::BuildVerticalCore(Dimensions, Teile);

	for (const FHqPart& Teil : Teile)
	{
		UStaticMesh* Mesh = Teil.Primitive == EHqPrimitive::Cylinder ? CylinderMesh : CubeMesh;
		if (!Mesh)
		{
			continue;
		}
		UStaticMeshComponent* Komponente = NewObject<UStaticMeshComponent>(this);
		Komponente->SetStaticMesh(Mesh);
		Komponente->SetupAttachment(Root);
		// MIT Kollision, anders als die Wahrzeichen: auf diesem Dach soll der
		// Helikopter aufsetzen und der Spieler herumlaufen koennen.
		Komponente->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Komponente->RegisterComponent();
		Komponente->SetWorldLocation(BaseWorld + BaseYaw.RotateVector(Teil.CenterCm));
		Komponente->SetWorldRotation(BaseYaw);
		// Engine-Cube und -Cylinder sind 100 cm gross und um den Ursprung
		// zentriert - die Skalierung ist darum schlicht Groesse/100.
		Komponente->SetWorldScale3D(Teil.SizeCm / 100.0);
		if (Materials.IsValidIndex(static_cast<int32>(Teil.Material)))
		{
			if (UMaterialInterface* Material = Materials[static_cast<int32>(Teil.Material)])
			{
				Komponente->SetMaterial(0, Material);
			}
		}
		Parts.Add(Komponente);
	}

	UE_LOG(LogWbSebboHq, Log,
		TEXT("Sebbo-Hauptsitz gebaut bei (%.0f, %.0f, %.0f): %d Bauteile, %d Geschosse, ")
		TEXT("%.0f m hoch, Landeplatz auf %.0f m."),
		BaseWorld.X, BaseWorld.Y, BaseWorld.Z, Parts.Num(), Dimensions.FloorCount,
		SebboHq::GetRoofHeightCm(Dimensions) / 100.0,
		SebboHq::GetHelipadHeightCm(Dimensions) / 100.0);
}

namespace
{
	/**
	 * Ein Schritt mit der Kapsel der Spielfigur - dieselbe Regel wie
	 * AWiesbadenFootPawn::TryStep: anheben, vorwaerts, absetzen. Nur wenn alle
	 * drei Teilstuecke frei sind, war es eine Stufe und keine Wand.
	 */
	bool KapselSchritt(const UWorld* World, const AActor* /*Selbst*/,
		const FVector& Von, const FVector& Richtung, double Weite,
		double MaxStufeCm, FVector& OutNach, FString& OutGrund)
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
			return false;
		}

		const FVector Vor = Hoch + Richtung.GetSafeNormal() * Weite;
		if (World->SweepSingleByChannel(Treffer, Hoch, Vor, FQuat::Identity,
			ECC_Pawn, Kapsel, Params) && Versperrt(Treffer))
		{
			OutGrund = TEXT("eine Wand quer im Weg");
			return false;
		}

		// Absetzen: bis zu einer vollen Stufe nach unten suchen.
		const FVector Tief = Vor - FVector(0.0, 0.0, MaxStufeCm * 2.0);
		if (World->SweepSingleByChannel(Treffer, Vor, Tief, FQuat::Identity,
			ECC_Pawn, Kapsel, Params))
		{
			OutNach = Treffer.bStartPenetrating ? Vor : Treffer.Location;
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
