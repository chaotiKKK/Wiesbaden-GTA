// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"

#include "Vehicles/WiesbadenBugTankPawn.h"

#include "Vehicles/WiesbadenBugTankTeile.h"

/**
 * Nachweis der verdrahteten statischen Insektenteile.
 *
 * Die Darstellung sind 15 starre Teile, kein Skelett: der Unreal-5.8-Import
 * setzt die Knochenorientierung eines Blender-Rigs um (Beleg
 * Saved/Logs/wb_test_bugtankrig5.log), deshalb zerlegt
 * Blender/bugtank/export_bugtank_teile.py das Mesh. Der Test misst daher an
 * den Spitzenpunkten aus WiesbadenBugTankTeile.h - geometrisch dieselben
 * Stellen, die vorher die Knochenenden waren (Sohle, Antennenkolben), nur
 * direkt am Messpunkt statt ueber den Bonespace.
 *
 * Die Kugel bleibt unveraendert (r = 56 cm, Snap 58 cm); das Modell ragt mit
 * Bein- und Fuehlerspitzen 8 bis 20 cm ueber sie hinaus (Blender-Pruefbericht).
 * Daraus folgt die Entscheidung, die hier gemessen wird - nicht die Kugel
 * aendern, sondern die Gliedmassen:
 *
 *  1) Auf der Standflaeche bleiben Beine und Fuehler AUSGEFAHREN. Der Pawn
 *     steht 58 cm ueber der Flaeche, die Fusssohle liegt in Ruhe bei -55 cm,
 *     also 3 cm ueber dem Boden: die Fuesse stehen auf der Flaeche, nicht
 *     darin. Jede Knochenpose wird einzeln gemessen (Ruhelage und Lauf).
 *  2) Nur wenn die vorhandene Oberflaechenabfrage eine ANDERE Flaeche trifft
 *     als die Standflaeche - der Kafer laeuft in eine Wand oder an eine
 *     Decke - werden die Gliedmassen eingeklappt, und dann liegen alle
 *     Spitzen innerhalb der Kugel.
 *
 * Wand- und Deckenuebergang laufen mit demselben Boden/Wand/Decke-Aufbau wie
 * WiesbadenReal.Vehicles.BugTank.Movement, damit die Kugel-Kollision
 * unveraendert bleibt und nur die Darstellung neu ist.
 */
namespace BugTankRigTest
{
	/** Reihenfolge wie im Pawn: Body, je Bein Ober/Unter fuer L1..L3 und R1..R3. */
	const FBugTankTeil* const BeinOber[6] = {
		&BugTankTeile::BeinL1_Ober, &BugTankTeile::BeinL2_Ober, &BugTankTeile::BeinL3_Ober,
		&BugTankTeile::BeinR1_Ober, &BugTankTeile::BeinR2_Ober, &BugTankTeile::BeinR3_Ober };
	const FBugTankTeil* const BeinUnter[6] = {
		&BugTankTeile::BeinL1_Unter, &BugTankTeile::BeinL2_Unter, &BugTankTeile::BeinL3_Unter,
		&BugTankTeile::BeinR1_Unter, &BugTankTeile::BeinR2_Unter, &BugTankTeile::BeinR3_Unter };
	const FBugTankTeil* const Antennen[2] = { &BugTankTeile::AntenneL, &BugTankTeile::AntenneR };

	/** Kugel des Pawns; unveraendert 56 cm. */
	const float KugelR = 56.0f;
	const float Toleranz = 1.5f;
	/** Fusssohle in Ruhelage (Asset). Der Pawn steht 58 cm ueber der Flaeche. */
	const float SohleRuhe = -55.0f;
	/** Spitzen duerfen durch die Schrittbewegung etwas tiefer stehen als die Sohle. */
	const float FussToleranz = 3.0f;
	/** Ab diesem Einklappgrad gilt die Kette als voll eingeklappt. */
	const float FaltFenster = 0.8f;

	/**
	 * Weltort eines Teilpunkts, im lokalen Raum des Pawns.
	 *
	 * Punkt = Drehpunkt der Komponente + gedrehte Gelenk-Stelle. Genau so
	 * rechnet der Pawn auch, nur dass der Test den Umweg ueber die Komponente
	 * geht: erst Komponentenlage, dann Drehung, dann Spitzenversatz. Misst
	 * also die tatsaechlich aufgebaute Hierarchie, nicht einen Tablewert.
	 */
	FVector SpitzeOrt(AWiesbadenBugTankPawn& BugTank, const FBugTankTeil& Teil)
	{
		const UStaticMeshComponent* Komponente = BugTank.GetInsektTeil(Teil);
		if (!Komponente)
		{
			return FVector::ZeroVector;
		}
		return BugTank.GetActorTransform().InverseTransformPosition(
			Komponente->GetComponentTransform().TransformPosition(Teil.Spitze));
	}

	/** Wie SpitzeOrt, nur als Abstand: der Kugel- und Spitzennachweis. */
	float SpitzeAbstand(AWiesbadenBugTankPawn& BugTank, const FBugTankTeil& Teil)
	{
		return SpitzeOrt(BugTank, Teil).Size();
	}

	float GroessterAbstand(AWiesbadenBugTankPawn& BugTank, const FBugTankTeil& Teil)
	{
		return SpitzeAbstand(BugTank, Teil);
	}

	/** Groesster Abstand ueber alle Bein- und Fuehlerspitzen. */
	float GroessteSpitze(AWiesbadenBugTankPawn& BugTank)
	{
		float Max = 0.0f;
		for (int32 Bein = 0; Bein < 6; ++Bein)
		{
			Max = FMath::Max(Max, GroessterAbstand(BugTank, *BeinUnter[Bein]));
		}
		for (int32 Antenne = 0; Antenne < 2; ++Antenne)
		{
			Max = FMath::Max(Max, GroessterAbstand(BugTank, *Antennen[Antenne]));
		}
		return Max;
	}

	/** Groesster Abstand der sechs Fussspitzen. */
	float GroessterFussabstand(AWiesbadenBugTankPawn& BugTank)
	{
		float Max = 0.0f;
		for (int32 Bein = 0; Bein < 6; ++Bein)
		{
			Max = FMath::Max(Max, GroessterAbstand(BugTank, *BeinUnter[Bein]));
		}
		return Max;
	}

	/** Groesster Abstand der beiden Fuehlerspitzen. */
	float GroessterAntennenabstand(AWiesbadenBugTankPawn& BugTank)
	{
		float Max = 0.0f;
		for (int32 Antenne = 0; Antenne < 2; ++Antenne)
		{
			Max = FMath::Max(Max, GroessterAbstand(BugTank, *Antennen[Antenne]));
		}
		return Max;
	}

	/** Niedrigste Fussspitze im lokalen Raum: die Fuesse duerfen nicht in die Flaeche ragen. */
	float TiefsterFuss(AWiesbadenBugTankPawn& BugTank)
	{
		float Min = TNumericLimits<float>::Max();
		for (int32 Bein = 0; Bein < 6; ++Bein)
		{
			Min = FMath::Min(Min, SpitzeOrt(BugTank, *BeinUnter[Bein]).Z);
		}
		return Min;
	}

	/** Protokollzeile: jede Gliedmasse mit Ruhelage, Pose und Abstand. */
	FString GliedmassenProtokoll(AWiesbadenBugTankPawn& BugTank)
	{
		FString Text;
		for (int32 Bein = 0; Bein < 6; ++Bein)
		{
			const FVector Ort = SpitzeOrt(BugTank, *BeinUnter[Bein]);
			Text += FString::Printf(TEXT("%c  Bein%d Ober Gelenk %s Spitze %s"), 10,
				Bein + 1,
				*BeinOber[Bein]->Gelenk.ToString(), *BeinOber[Bein]->Spitze.ToString());
			Text += FString::Printf(TEXT("%c  Bein%d Unter Gelenk %s Sohle Ref %s -> Pose %s (%.1f cm)"), 10,
				Bein + 1, *BeinUnter[Bein]->Gelenk.ToString(),
				*BeinUnter[Bein]->Spitze.ToString(), *Ort.ToString(), Ort.Size());
		}
		for (int32 Antenne = 0; Antenne < 2; ++Antenne)
		{
			const FVector Ort = SpitzeOrt(BugTank, *Antennen[Antenne]);
			Text += FString::Printf(TEXT("%c  Antenne%d Gelenk %s Kolben Ref %s -> Pose %s (%.1f cm)"), 10,
				Antenne + 1, *Antennen[Antenne]->Gelenk.ToString(),
				*Antennen[Antenne]->Spitze.ToString(), *Ort.ToString(), Ort.Size());
		}
		return Text;
	}

	AActor* Flaeche(UWorld* World, const FVector& Groesse, const FVector& Ort, const TCHAR* Name)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		UBoxComponent* Box = NewObject<UBoxComponent>(Actor, Name);
		Actor->SetRootComponent(Box);
		Box->SetBoxExtent(Groesse);
		Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Actor->SetActorLocation(Ort);
		Box->RegisterComponent();
		return Actor;
	}

	/** Setzt den BugTank auf eine Fläche und stellt einen Controller bereit. */
	AWiesbadenBugTankPawn* SpawnBugTank(UWorld* World, APlayerController*& OutPC,
		const FVector& Ort, const TCHAR* Name)
	{
		AWiesbadenBugTankPawn* BugTank = World->SpawnActor<AWiesbadenBugTankPawn>(
			Ort, FRotator::ZeroRotator);
		if (!BugTank)
		{
			return nullptr;
		}
		BugTank->DispatchBeginPlay();
		OutPC = World->SpawnActor<APlayerController>();
		OutPC->InitInputSystem();
		OutPC->Possess(BugTank);
		return BugTank;
	}

	/**
	 * Treibt den Pawn mit gedrückter W-Taste und hält die Pose frisch.
	 *
	 * Reihenfolge wie im Movement-Test: Eingabe, dann ProcessInputStack, dann
	 * Tick - der Pawn liest die Tastenlage im Tick.
	 */
	void Laufen(AWiesbadenBugTankPawn* BugTank, APlayerController* PC, int32 Frames,
		TFunction<void()> ProTick = nullptr)
	{
		for (int32 Schritt = 0; Schritt < Frames; ++Schritt)
		{
			PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
			PC->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
			BugTank->Tick(1.0f / 30.0f);
			if (ProTick)
			{
				ProTick();
			}
		}
	}

	void Loslassen(AWiesbadenBugTankPawn* BugTank, APlayerController* PC)
	{
		PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
		PC->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
	}

	/** Ruhe auf der Stelle: nur Tick, keine Eingabe. */
	void Stand(AWiesbadenBugTankPawn* BugTank, int32 Frames)
	{
		for (int32 Frame = 0; Frame < Frames; ++Frame)
		{
			BugTank->Tick(1.0f / 30.0f);
		}
	}

	/**
	 * Messfenster des Einklappens: nur Frames zaehlen, in denen die
	 * Gliedmassen tatsaechlich eingeklappt sind. Genau in diesen Frames
	 * muessen alle Spitzen innerhalb der Kugel liegen.
	 */
	struct FKnickfenster
	{
		bool bVorhanden = false;
		float MaxFalt = 0.0f;
		float MaxSpitze = 0.0f;
		int32 Frames = 0;

		void Messen(AWiesbadenBugTankPawn* BugTank)
		{
			const float Falt = FMath::Min(BugTank->GetLegFold(), BugTank->GetAntennaFold());
			if (Falt < FaltFenster)
			{
				return;
			}
			bVorhanden = true;
			++Frames;
			MaxFalt = FMath::Max(MaxFalt, Falt);
			MaxSpitze = FMath::Max(MaxSpitze, GroessteSpitze(*BugTank));
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankRigTest,
	"WiesbadenReal.Vehicles.BugTank.Rig",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankRigTest::RunTest(const FString& Parameters)
{
	using namespace BugTankRigTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	Flaeche(World, FVector(5000.0f, 5000.0f, 10.0f), FVector(0.0f, 0.0f, -10.0f),
		TEXT("BugTankRigTestBoden"));
	// Wand und Decke fuer den Flaechenwechsel, Aufbau wie im Movement-Test.
	Flaeche(World, FVector(10.0f, 5000.0f, 1500.0f), FVector(310.0f, 0.0f, 1500.0f),
		TEXT("BugTankRigTestWand"));
	Flaeche(World, FVector(2000.0f, 5000.0f, 10.0f), FVector(1600.0f, 0.0f, 310.0f),
		TEXT("BugTankRigTestDecke"));

	APlayerController* PC = nullptr;
	AWiesbadenBugTankPawn* BugTank = SpawnBugTank(World, PC, FVector(0.0f, 0.0f, 12.0f),
		TEXT("BugTankRigSpawn"));
	if (!TestNotNull(TEXT("BugTank gespawnt und besessen"), BugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	TestTrue(TEXT("Bodenkontakt nach dem Surface-Trace"), BugTank->HasSurfaceContact());

	// -- 1) Teile, Kugel und Gelenkpunkte -------------------------------
	const USphereComponent* Kugel = Cast<USphereComponent>(BugTank->GetRootComponent());
	TestNotNull(TEXT("Kugel-Kollision ist weiterhin eine Sphere"), Kugel);
	TestEqual(TEXT("Kugel-Kollision bleibt r = 56 cm"), Kugel ? Kugel->GetScaledSphereRadius() : 0.0f,
		56.0f);

	// Alle 15 Teile muessen als Komponente UND als Mesh dastehen: eine leere
	// Komponente faellt sonst erst beim Rendern auf (Beleg: die Bounds im
	// Importlog sind je Asset, nicht je Komponente).
	TestEqual(TEXT("alle 15 Teile liegen als Mesh vor"), BugTank->GetGeladeneTeile(), 15);
	int32 FehlendeTeile = 0;
	for (int32 Bein = 0; Bein < 6; ++Bein)
	{
		for (const FBugTankTeil* Teil : { BeinOber[Bein], BeinUnter[Bein] })
		{
			if (!BugTank->GetInsektTeil(*Teil)
				|| !BugTank->GetInsektTeil(*Teil)->GetStaticMesh())
			{
				++FehlendeTeile;
			}
		}
	}
	for (const FBugTankTeil* Antenne : Antennen)
	{
		if (!BugTank->GetInsektTeil(*Antenne)
			|| !BugTank->GetInsektTeil(*Antenne)->GetStaticMesh())
		{
			++FehlendeTeile;
		}
	}
	TestEqual(TEXT("die 14 Gliedmassteile sind als Komponenten mit Mesh vorhanden"),
		FehlendeTeile, 0);

	// Der Drehpunkt sitzt genau am Gelenk: der Versatz der Komponente muss
	// das Gegenteil des Gelenks sein, sonst verrutscht die Geometrie beim
	// Drehen. Ohne diese Pruefung waere ein falscher Versatz nur daran zu
	// erkennen, dass die Beine beim Laufen wegfliegen.
	int32 VersatzFehler = 0;
	for (int32 Bein = 0; Bein < 6; ++Bein)
	{
		for (const FBugTankTeil* Teil : { BeinOber[Bein], BeinUnter[Bein] })
		{
			const UStaticMeshComponent* Komponente = BugTank->GetInsektTeil(*Teil);
			if (!Komponente || !Komponente->GetRelativeLocation().Equals(-Teil->Gelenk, 0.01f))
			{
				++VersatzFehler;
			}
		}
	}
	TestEqual(TEXT("jeder Teil sitzt mit dem Versatz -Gelenk in seinem Drehpunkt"),
		VersatzFehler, 0);

	// -- 2) Ruhelage auf der Standflaeche: ausgefahren, Fuesse auf der Flaeche --
	// 60 Bilder ohne Eingabe. Erwartet wird NICHT eingeklappt: der Pawn steht
	// 58 cm ueber dem Boden, die Sohle liegt in Ruhe bei -55 cm.
	Stand(BugTank, 60);
	AddInfo(FString::Printf(TEXT("Ruhelage auf dem Boden (LegFold %.2f, AntennaFold %.2f, "
		"Surface-Abstand %.1f cm, %d Teile):%s"),
		BugTank->GetLegFold(), BugTank->GetAntennaFold(), BugTank->GetSurfaceHitDistance(),
		BugTank->GetGeladeneTeile(), *GliedmassenProtokoll(*BugTank)));
	TestFalse(TEXT("auf der Standflaeche wird nicht eingeklappt"), BugTank->GetFremdeFlaeche());
	TestTrue(FString::Printf(TEXT("Beine bleiben ausgefahren (LegFold %.2f)"),
		BugTank->GetLegFold()), BugTank->GetLegFold() < 0.5f);
	TestTrue(FString::Printf(TEXT("Fuehler bleiben ausgefahren (AntennaFold %.2f)"),
		BugTank->GetAntennaFold()), BugTank->GetAntennaFold() < 0.5f);

	const float RuhelageFuss = GroessterFussabstand(*BugTank);
	const float RuhelageAntenne = GroessterAntennenabstand(*BugTank);
	TestTrue(FString::Printf(
		TEXT("Ruhelage: groesster Fussabstand %.1f cm (Kugel %.1f cm, das Modell ragt 8 bis 20 cm "
			"heraus)"), RuhelageFuss, KugelR), RuhelageFuss > KugelR && RuhelageFuss <= 76.0f);
	TestTrue(FString::Printf(
		TEXT("Ruhelage: groesster Fuehlerabstand %.1f cm (Kugel %.1f cm)"), RuhelageAntenne, KugelR),
		RuhelageAntenne > KugelR && RuhelageAntenne <= 81.0f);
	const float RuhelageTief = TiefsterFuss(*BugTank);
	TestTrue(FString::Printf(
		TEXT("die Fuesse stehen auf der Flaeche, nicht darin: tiefster Fussknoten %.1f cm "
			"(Sohle %.1f cm, Grenze %.1f cm)"),
		RuhelageTief, SohleRuhe, SohleRuhe - FussToleranz),
		RuhelageTief >= SohleRuhe - FussToleranz);

	// Die sechs Fuesse stehen nicht auf derselben Stelle: das beweist, dass
	// jede Kette einzeln gepostet wird (Ruhelage: Reihen 54 cm auseinander).
	const float VorderHinten = FVector::Dist(SpitzeOrt(*BugTank, *BeinUnter[0]),
		SpitzeOrt(*BugTank, *BeinUnter[2]));
	TestTrue(FString::Printf(TEXT("Vorder- und Hinterfuss stehen nicht uebereinander (%.1f cm)"),
		VorderHinten), VorderHinten > 5.0f);
	const float LinksRechts = FVector::Dist(SpitzeOrt(*BugTank, *BeinUnter[0]),
		SpitzeOrt(*BugTank, *BeinUnter[3]));
	TestTrue(FString::Printf(TEXT("linkes und rechtes Vorderbein stehen %.1f cm auseinander"),
		LinksRechts), LinksRechts > 10.0f);

	// -- 3) Laufen: alle sechs Beine bewegen sich, der Kafer laeuft vorwaerts --
	const FVector StartOrt = BugTank->GetActorLocation();
	FVector Start[6];
	for (int32 Bein = 0; Bein < 6; ++Bein)
	{
		Start[Bein] = SpitzeOrt(*BugTank, *BeinUnter[Bein]);
	}
	float Weg[6] = { 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
	PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	Laufen(BugTank, PC, 12, [&]() {
		for (int32 Bein = 0; Bein < 6; ++Bein)
		{
			Weg[Bein] = FMath::Max(Weg[Bein],
				FVector::Dist(SpitzeOrt(*BugTank, *BeinUnter[Bein]), Start[Bein]));
		}
		TestTrue(TEXT("kein Fuss durchdringt die Standflaeche im Lauf"),
			TiefsterFuss(*BugTank) >= -58.0f);
	});
	const float LaufTief = TiefsterFuss(*BugTank);
	AddInfo(FString::Printf(TEXT("nach 40 Laufbildern: Weg je Bein %.1f/%.1f/%.1f/%.1f/%.1f/%.1f cm, "
		"tiefster Fussknoten %.1f cm, Ort %s"),
		Weg[0], Weg[1], Weg[2], Weg[3], Weg[4], Weg[5], LaufTief,
		*BugTank->GetActorLocation().ToString()));

	int32 Unbewegte = 0;
	for (int32 Bein = 0; Bein < 6; ++Bein)
	{
		if (Weg[Bein] < 1.0f)
		{
			++Unbewegte;
		}
	}
	TestEqual(TEXT("alle sechs Beine werden beim Laufen bewegt (kein unbewegtes Bein)"),
		Unbewegte, 0);
	TestTrue(FString::Printf(TEXT("beim Laufen bleibt der tiefste Fussknoten bei %.1f cm "
		"(Grenze %.1f cm)"), LaufTief, SohleRuhe - FussToleranz),
		LaufTief >= SohleRuhe - FussToleranz);
	TestTrue(FString::Printf(TEXT("W bewegt den BugTank vorwaerts (%.1f cm)"),
		FVector::Dist(BugTank->GetActorLocation(), StartOrt)),
		FVector::Dist(BugTank->GetActorLocation(), StartOrt) > 20.0f);

	// -- 4) Wand: fremde Flaeche -> Einklappen, Spitzen in der Kugel -----
	FKnickfenster Wand;
	bool bWand = false;
	int32 NachWand = 0;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		if (bWand && ++NachWand > 40)
		{
			break;
		}
		Laufen(BugTank, PC, 1);
		if (BugTank->GetActorUpVector().X < -0.8f)
		{
			bWand = true;
		}
		Wand.Messen(BugTank);
	}
	TestTrue(FString::Printf(TEXT("Wandkontakt erreicht (Up=%s)"),
		*BugTank->GetActorUpVector().ToString()), bWand);
	TestTrue(TEXT("Wandkontakt ist ein echter Surface-Kontakt"), BugTank->HasSurfaceContact());
	TestTrue(FString::Printf(
		TEXT("an der Wand wurde eingeklappt: %d Bilder ueber Faltgrad %.2f, groesste Spitze "
			"darin %.1f cm (Kugel %.1f cm)"),
		Wand.Frames, Wand.MaxFalt, Wand.MaxSpitze, KugelR),
		Wand.bVorhanden && Wand.MaxFalt > 0.5f && Wand.MaxSpitze <= KugelR + Toleranz);

	// -- 5) Decke: derselbe Nachweis auf der Deckenflaeche ---------------
	FKnickfenster Decke;
	bool bDecke = false;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Laufen(BugTank, PC, 1);
		if (BugTank->GetActorUpVector().Z < -0.8f)
		{
			bDecke = true;
		}
		Decke.Messen(BugTank);
		if (bDecke && Decke.bVorhanden && BugTank->GetLegFold() < 0.5f)
		{
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("Deckenkontakt erreicht (Up=%s)"),
		*BugTank->GetActorUpVector().ToString()), bDecke);
	TestTrue(TEXT("Deckenkontakt ist ein echter Surface-Kontakt"), BugTank->HasSurfaceContact());
	TestTrue(FString::Printf(
		TEXT("unter der Decke wurde eingeklappt: %d Bilder ueber Faltgrad %.2f, groesste Spitze "
			"darin %.1f cm (Kugel %.1f cm)"),
		Decke.Frames, Decke.MaxFalt, Decke.MaxSpitze, KugelR),
		Decke.bVorhanden && Decke.MaxFalt > 0.5f && Decke.MaxSpitze <= KugelR + Toleranz);

	// -- 6) Auf der Decke steht er wieder ausgefahren ---------------------
	Loslassen(BugTank, PC);
	Stand(BugTank, 60);
	AddInfo(FString::Printf(TEXT("auf der Decke in Ruhe (LegFold %.2f, AntennaFold %.2f):%s"),
		BugTank->GetLegFold(), BugTank->GetAntennaFold(), *GliedmassenProtokoll(*BugTank)));
	TestTrue(FString::Printf(TEXT("auf der Decke sind die Gliedmassen wieder ausgefahren "
		"(LegFold %.2f)"), BugTank->GetLegFold()), BugTank->GetLegFold() < 0.5f);
	TestTrue(FString::Printf(TEXT("auf der Decke stehen die Fuesse auf der Flaeche (tiefster "
		"Knoten %.1f cm, Grenze %.1f cm)"), TiefsterFuss(*BugTank), SohleRuhe - FussToleranz),
		TiefsterFuss(*BugTank) >= SohleRuhe - FussToleranz);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankAntennaSurfaceTest,
	"WiesbadenReal.Vehicles.BugTank.Antenna",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankAntennaSurfaceTest::RunTest(const FString& Parameters)
{
	using namespace BugTankRigTest;
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	Flaeche(World, FVector(6000.0f, 6000.0f, 10.0f), FVector(0.0f, 0.0f, -10.0f),
		TEXT("BugTankAntennaBoden"));
	Flaeche(World, FVector(10.0f, 5000.0f, 1500.0f), FVector(310.0f, 0.0f, 1500.0f),
		TEXT("BugTankAntennaWand"));
	Flaeche(World, FVector(2000.0f, 5000.0f, 10.0f), FVector(1600.0f, 0.0f, 310.0f),
		TEXT("BugTankAntennaDecke"));

	APlayerController* PC = nullptr;
	AWiesbadenBugTankPawn* BugTank = SpawnBugTank(World, PC, FVector(0.0f, 0.0f, 12.0f),
		TEXT("BugTankAntennaSpawn"));
	if (!TestNotNull(TEXT("BugTank gespawnt"), BugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	if (!TestNotNull(TEXT("linke Antenne als Komponente vorhanden"),
		BugTank->GetInsektTeil(*Antennen[0]))
		|| !TestNotNull(TEXT("rechte Antenne als Komponente vorhanden"),
			BugTank->GetInsektTeil(*Antennen[1])))
	{
		World->DestroyWorld(false);
		return false;
	}
	const FBugTankTeil& Spitze = *Antennen[0];
	const FBugTankTeil& SpitzeRechts = *Antennen[1];

	// -- 1) Ruhelage: ausgefahren, Antennen ueber dem Boden --------------
	Stand(BugTank, 60);
	const float RuhelageSpitze = GroessterAbstand(*BugTank, Spitze);
	AddInfo(FString::Printf(TEXT("Antenne in Ruhelage: Kolben %.1f cm vom Ursprung (Ruhelage %.1f "
		"cm), Faltgrad %.2f"), RuhelageSpitze, Spitze.Spitze.Size(),
		BugTank->GetAntennaFold()));
	TestTrue(FString::Printf(TEXT("die ausgefahrene Fuehlerspitze ragt %.1f cm ueber die Kugel "
		"heraus (dokumentiertes Mass 14 bis 19 cm)"), RuhelageSpitze - KugelR),
		RuhelageSpitze > KugelR + 10.0f);
	TestTrue(TEXT("die Antennen sind auf der Standflaeche ausgefahren"),
		BugTank->GetAntennaFold() < 0.5f);

	// -- 2) Tasten beim Vorwaerislaufen -----------------------------------
	PC->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	float MinZ = TNumericLimits<float>::Max();
	float MaxZ = -TNumericLimits<float>::Max();
	float MaxZRechts = -TNumericLimits<float>::Max();
	float MinZRechts = TNumericLimits<float>::Max();
	for (int32 Frame = 0; Frame < 12; ++Frame)
	{
		Laufen(BugTank, PC, 1);
		const FVector Ort = SpitzeOrt(*BugTank, Spitze);
		const FVector OrtRechts = SpitzeOrt(*BugTank, SpitzeRechts);
		MinZ = FMath::Min(MinZ, Ort.Z);
		MaxZ = FMath::Max(MaxZ, Ort.Z);
		MinZRechts = FMath::Min(MinZRechts, OrtRechts.Z);
		MaxZRechts = FMath::Max(MaxZRechts, OrtRechts.Z);
	}
	TestTrue(FString::Printf(
		TEXT("die Antenne tastet beim Laufen sichtbar: Hoehenweg %.1f cm (von %.1f bis %.1f)"),
		MaxZ - MinZ, MinZ, MaxZ), (MaxZ - MinZ) > 2.0f);
	TestTrue(FString::Printf(
		TEXT("die rechte Antenne tastet versetzt: Hoehenweg %.1f cm (von %.1f bis %.1f)"),
		MaxZRechts - MinZRechts, MinZRechts, MaxZRechts), (MaxZRechts - MinZRechts) > 2.0f);
	TestTrue(FString::Printf(
		TEXT("beim Tasten bleibt die Fuehlerspitze ueber der Flaeche (tiefster Punkt %.1f cm, "
			"Grenze %.1f cm)"), FMath::Min(MinZ, MinZRechts), SohleRuhe - FussToleranz),
		FMath::Min(MinZ, MinZRechts) >= SohleRuhe - FussToleranz);

	// -- 3) Reaktion auf eine nahe Flaeche: die Wand ------------------------
	FKnickfenster Wand;
	bool bWand = false;
	int32 NachWand = 0;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		if (bWand && ++NachWand > 40)
		{
			break;
		}
		Laufen(BugTank, PC, 1);
		if (BugTank->GetActorUpVector().X < -0.8f)
		{
			bWand = true;
		}
		Wand.Messen(BugTank);
	}
	TestTrue(FString::Printf(TEXT("Wandkontakt fuer die Antennenreaktion erreicht (Up=%s)"),
		*BugTank->GetActorUpVector().ToString()), bWand);
	TestTrue(FString::Printf(
		TEXT("an der Wand klappt die Antenne ein: %d Bilder ueber Faltgrad %.2f, groesste Spitze "
			"darin %.1f cm (Kugel %.1f cm, ausgefahren %.1f cm)"),
		Wand.Frames, Wand.MaxFalt, Wand.MaxSpitze, KugelR, RuhelageSpitze),
		Wand.bVorhanden && Wand.MaxFalt > 0.5f && Wand.MaxSpitze <= KugelR + Toleranz);
	TestTrue(FString::Printf(TEXT("die eingeklappte Antenne ist um %.1f cm kuerzer als die "
		"ausgefahrene"), RuhelageSpitze - Wand.MaxSpitze),
		RuhelageSpitze - Wand.MaxSpitze > 10.0f);

	// -- 4) Ohne Oberflaeche laeuft sie ausgefahren ------------------------
	APlayerController* FreiPC = nullptr;
	AWiesbadenBugTankPawn* Frei = SpawnBugTank(World, FreiPC, FVector(-4000.0f, -4000.0f, 4000.0f),
		TEXT("BugTankOhneFlaeche"));
	if (TestNotNull(TEXT("zweiter BugTank ohne Flaeche gespawnt"), Frei))
	{
		TestFalse(TEXT("ohne erreichbare Geometrie gibt es keinen Oberflaechenkontakt"),
			Frei->HasSurfaceContact());
		Stand(Frei, 60);
		const float AbstandFrei = GroessterAbstand(*Frei, Spitze);
		TestTrue(FString::Printf(
			TEXT("ohne Flaeche ist die Antenne ausgefahren: %.1f cm, auf der Flaeche an der "
				"Wand eingeklappt %.1f cm"), AbstandFrei, Wand.MaxSpitze),
			AbstandFrei > Wand.MaxSpitze + 10.0f);
	}

	// -- 5) Nach dem Uebergang wieder ausgefahren --------------------------
	Loslassen(BugTank, PC);
	Stand(BugTank, 60);
	const float NachUebergang = GroessterAbstand(*BugTank, Spitze);
	AddInfo(FString::Printf(TEXT("Antenne nach dem Wanduebergang: Spitze %.1f cm, Faltgrad %.2f"),
		NachUebergang, BugTank->GetAntennaFold()));
	TestTrue(FString::Printf(TEXT("nach dem Uebergang ist die Antenne wieder ausgefahren "
		"(%.1f cm, Faltgrad %.2f)"), NachUebergang, BugTank->GetAntennaFold()),
		BugTank->GetAntennaFold() < 0.5f && NachUebergang > RuhelageSpitze - 5.0f);

	World->DestroyWorld(false);
	return true;
}