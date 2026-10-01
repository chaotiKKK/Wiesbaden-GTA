// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerInput.h"
#include "InputKeyEventArgs.h"

#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenChaosCar.h"
#include "Vehicles/WiesbadenBugTankPawn.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "UI/WiesbadenVehicleHUD.h"
#include "World/BuildingCollisionSpawnerComponent.h"

/**
 * Zwei-Fahrzeug-Verdrahtungsnachweis der Steuernaht (Spec-Schritt 6).
 *
 * Steuert BEIDE Fahrzeuge - den kinematischen AWiesbadenCar und den Chaos-
 * physikalischen AWiesbadenChaosCar - ausschliesslich ueber IWiesbadenVehicle-
 * Control an und prueft, dass die Naht Steuerung UND Readouts korrekt durch-
 * reicht: aktivieren/abschalten (Lebenszyklus) sowie die HUD-/Diagnose-Readouts
 * (Tempo, Gang, Drehzahl + Skala, Licht, Kamera).
 *
 * Bewusst OHNE echte Fahrt: die tatsaechliche Laengsdynamik (Tempo>20,
 * Kursaenderung>15) weist der Rauchtest ueber WbDrive an der echten Fahrphysik
 * nach - fuer den Kaefer heute, fuer den ChaosCar nach dem Bauch-Kollisions-Fix.
 * Dieser Unit-Test sichert die NAHT selbst ab (kein Editor, kein Physik-Schritt):
 * dass beide Klassen das Interface erfuellen und ihre Werte durchreichen.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVehicleControlSeamTest,
	"WiesbadenReal.Vehicles.ControlSeam",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVehicleControlSeamTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	// Prueft den kompletten Naht-Vertrag an EINEM Fahrzeug ueber das Interface -
	// identisch fuer Kaefer und ChaosCar, denn genau das ist der Sinn der Naht.
	auto CheckSeam = [this](IWiesbadenVehicleControl* Ctrl, const TCHAR* Name)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s: implementiert IWiesbadenVehicleControl"), Name), Ctrl))
		{
			return;
		}

		// -- Steuerungs-Lebenszyklus: aktivieren / abschalten -----------------
		TestFalse(*FString::Printf(TEXT("%s: anfangs keine externe Steuerung"), Name),
			Ctrl->IsExternalControlActive());

		FWiesbadenCarControl Cmd;
		Cmd.Throttle = 1.0f;
		Cmd.Steering = 0.5f;
		Cmd.bHandbrake = false;
		Ctrl->SetExternalControl(Cmd);
		TestTrue(*FString::Printf(TEXT("%s: SetExternalControl aktiviert die Naht"), Name),
			Ctrl->IsExternalControlActive());

		// -- Readouts durchgereicht (plausibel, kein Absturz) -----------------
		const float Idle = Ctrl->GetEngineIdleRpm();
		const float MaxRpm = Ctrl->GetEngineMaxRpm();
		TestTrue(*FString::Printf(TEXT("%s: Leerlaufdrehzahl > 0 (Skala verdrahtet)"), Name), Idle > 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: Hoechstdrehzahl > Leerlauf"), Name), MaxRpm > Idle);
		TestTrue(*FString::Printf(TEXT("%s: Motordrehzahl endlich und >= 0"), Name),
			FMath::IsFinite(Ctrl->GetEngineRpm()) && Ctrl->GetEngineRpm() >= 0.0f);
		TestTrue(*FString::Printf(TEXT("%s: Tempo endlich"), Name),
			FMath::IsFinite(Ctrl->GetSpeedKmh()));
		TestNotNull(*FString::Printf(TEXT("%s: Lichtanlage fuer Kontrollleuchten vorhanden"), Name),
			Ctrl->GetLights());

		const int32 Gear = Ctrl->GetGear();
		TestTrue(*FString::Printf(TEXT("%s: Gang im plausiblen Bereich (-1..10)"), Name),
			Gear >= -1 && Gear <= 10);

		// GetCameraMode nur aufrufen - der Wert ist der Follow-Default, es geht um
		// die Durchreichung (kein Absturz, gueltiger Enum).
		const EWiesbadenVehicleCameraMode Cam = Ctrl->GetCameraMode();
		TestTrue(*FString::Printf(TEXT("%s: Kameramodus ist ein gueltiger Wert"), Name),
			Cam == EWiesbadenVehicleCameraMode::Follow
			|| Cam == EWiesbadenVehicleCameraMode::Orbit
			|| Cam == EWiesbadenVehicleCameraMode::Cockpit);

		// -- Abschalten gibt die Steuerung frei -------------------------------
		Ctrl->ClearExternalControl();
		TestFalse(*FString::Printf(TEXT("%s: ClearExternalControl gibt die Steuerung frei"), Name),
			Ctrl->IsExternalControlActive());
	};

	// -- Kaefer (kinematisch) ------------------------------------------------
	AWiesbadenCar* Car = World->SpawnActor<AWiesbadenCar>(
		FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("Kaefer gespawnt"), Car);
	CheckSeam(Cast<IWiesbadenVehicleControl>(Car), TEXT("Kaefer"));
	// Familien-Wurzel: derselbe Pawn ist auch IWiesbadenExternalControl (EIN
	// Zugriffspfad fuer Autopilot/Harness ueber alle Fahrzeuge).
	TestNotNull(TEXT("Kaefer castet auf die Familien-Wurzel IWiesbadenExternalControl"),
		Cast<IWiesbadenExternalControl>(Car));

	// -- ChaosCar (echte Fahrzeugphysik) - reiner Verdrahtungsnachweis -------
	AWiesbadenChaosCar* Chaos = World->SpawnActor<AWiesbadenChaosCar>(
		FVector(5000.0, 0.0, 100.0), FRotator::ZeroRotator);
	TestNotNull(TEXT("ChaosCar gespawnt"), Chaos);
	CheckSeam(Cast<IWiesbadenVehicleControl>(Chaos), TEXT("ChaosCar"));
	TestNotNull(TEXT("ChaosCar castet auf die Familien-Wurzel IWiesbadenExternalControl"),
		Cast<IWiesbadenExternalControl>(Chaos));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankMovementTest,
	"WiesbadenReal.Vehicles.BugTank.Movement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankMovementTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* FloorCollision = NewObject<UBoxComponent>(Floor, TEXT("BugTankTestFloor"));
	Floor->SetRootComponent(FloorCollision);
	FloorCollision->SetBoxExtent(FVector(5000.0f, 5000.0f, 10.0f));
	FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
	FloorCollision->RegisterComponent();

	AActor* Wall = World->SpawnActor<AActor>();
	UBoxComponent* WallCollision = NewObject<UBoxComponent>(Wall, TEXT("BugTankTestWall"));
	Wall->SetRootComponent(WallCollision);
	WallCollision->SetBoxExtent(FVector(10.0f, 5000.0f, 1500.0f));
	WallCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	WallCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Wall->SetActorLocation(FVector(310.0f, 0.0f, 1500.0f));
	WallCollision->RegisterComponent();

	AActor* Ceiling = World->SpawnActor<AActor>();
	UBoxComponent* CeilingCollision = NewObject<UBoxComponent>(Ceiling, TEXT("BugTankTestCeiling"));
	Ceiling->SetRootComponent(CeilingCollision);
	CeilingCollision->SetBoxExtent(FVector(2000.0f, 5000.0f, 10.0f));
	CeilingCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	CeilingCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Ceiling->SetActorLocation(FVector(1600.0f, 0.0f, 310.0f));
	CeilingCollision->RegisterComponent();
	FHitResult FloorTrace;
	TestTrue(TEXT("Testboden ist per Visibility-LineTrace erreichbar"),
		World->LineTraceSingleByChannel(FloorTrace, FVector(0.0f, 0.0f, 58.0f),
			FVector(0.0f, 0.0f, -107.0f), ECC_Visibility));

	AWiesbadenBugTankPawn* BugTank = World->SpawnActor<AWiesbadenBugTankPawn>(
		FVector(0.0f, 0.0f, 12.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("BugTank gespawnt"), BugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	BugTank->DispatchBeginPlay();
	TestTrue(TEXT("Spawn aus der bisherigen Pawn-Hoehe wird vor Bewegung auf die Bodenflaeche gesetzt"),
		FMath::IsNearlyEqual(BugTank->GetActorLocation().Z, 58.0f, 0.1f));
	TestTrue(TEXT("BugTank meldet echten Bodenkontakt nach dem Surface-Trace"),
		BugTank->HasSurfaceContact());

	AWiesbadenBugTankPawn* UnsupportedBugTank = World->SpawnActor<AWiesbadenBugTankPawn>(
		FVector(10000.0f, 10000.0f, 1000.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("BugTank ohne erreichbare Flaeche gespawnt"), UnsupportedBugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	UnsupportedBugTank->DispatchBeginPlay();
	TestFalse(TEXT("fehlende Geometrie wird nicht als Oberflaechenkontakt gewertet"),
		UnsupportedBugTank->HasSurfaceContact());

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("PlayerController gespawnt"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	Controller->InitInputSystem();
	Controller->Possess(BugTank);
	TestTrue(TEXT("PlayerController besitzt den BugTank"), BugTank->GetController() == Controller);

	const FVector Start = BugTank->GetActorLocation();
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
	}
	TestTrue(TEXT("Leere Eingabe laesst den BugTank auf dem Boden stehen"),
		BugTank->GetActorLocation().Equals(Start, 0.1f));
	TestTrue(TEXT("Leere Eingabe behaelt den Boden-Kontaktstatus"),
		BugTank->HasSurfaceContact());
	FHitResult SweepHit;
	const bool bDirectSweepMoved = BugTank->SetActorLocation(
		Start + FVector(10.0f, 0.0f, 0.0f), true, &SweepHit);
	TestTrue(TEXT("Sweep bewegt an der getrackten Flaeche ohne Startpenetration"),
		bDirectSweepMoved && !SweepHit.bStartPenetrating);
	BugTank->SetActorLocation(Start, false);

	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
	TestTrue(TEXT("W wird vom besitzenden Controller gehalten"),
		Controller->IsInputKeyDown(EKeys::W));
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
	}
	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);

	const float ForwardDistance = FVector::DotProduct(
		BugTank->GetActorLocation() - Start, FVector::ForwardVector);
	TestTrue(TEXT("W bewegt den BugTank vorwaerts ueber den Boden"), ForwardDistance > 100.0f);

	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	bool bReachedWall = false;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
		if (BugTank->GetActorUpVector().X < -0.8f)
		{
			bReachedWall = true;
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("BugTank richtet sich an der Wand aus (Up=%s, Position=%s)"),
		*BugTank->GetActorUpVector().ToString(), *BugTank->GetActorLocation().ToString()),
		bReachedWall);
	TestTrue(TEXT("Wanduebergang endet mit echtem Surface-Kontakt"), BugTank->HasSurfaceContact());

	bool bReachedCeiling = false;
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
		if (BugTank->GetActorUpVector().Z < -0.8f)
		{
			bReachedCeiling = true;
			break;
		}
	}
	TestTrue(FString::Printf(TEXT("BugTank erreicht die Decke (Up=%s, Position=%s)"),
		*BugTank->GetActorUpVector().ToString(), *BugTank->GetActorLocation().ToString()),
		bReachedCeiling);
	TestTrue(TEXT("Deckenuebergang endet mit echtem Surface-Kontakt"), BugTank->HasSurfaceContact());
	const FVector CeilingStart = BugTank->GetActorLocation();
	for (int32 Frame = 0; Frame < 30; ++Frame)
	{
		Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
	}
	Controller->PlayerInput->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
	TestTrue(FString::Printf(TEXT("BugTank bewegt sich auf der Decke von der Wand fort (Up=%s, Position=%s)"),
		*BugTank->GetActorUpVector().ToString(), *BugTank->GetActorLocation().ToString()),
		BugTank->GetActorUpVector().Z < -0.8f
		&& FVector::DotProduct(BugTank->GetActorLocation() - CeilingStart, -FVector::ForwardVector) > 10.0f);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankTransitionBannerTest,
	"WiesbadenReal.Vehicles.BugTank.TransitionBanner",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankTransitionBannerTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AWiesbadenBugTankPawn* BugTank = World->SpawnActor<AWiesbadenBugTankPawn>();
	AWiesbadenCar* Car = World->SpawnActor<AWiesbadenCar>();
	APawn* FootPawn = World->SpawnActor<APawn>();
	TestEqual(TEXT("Leerer Pawn bleibt ohne Wechseltext"),
		AWiesbadenVehicleHUD::ResolveVehicleTransitionBanner(nullptr), FString());
	TestEqual(TEXT("BugTank wird beim Einsteigen als BugTank erkannt"),
		AWiesbadenVehicleHUD::ResolveVehicleTransitionBanner(BugTank), FString(TEXT("Eingestiegen: BugTank")));
	TestEqual(TEXT("Steuernaht-Fahrzeug behaelt den Fahrzeugtext"),
		AWiesbadenVehicleHUD::ResolveVehicleTransitionBanner(Car), FString(TEXT("Eingestiegen: Fahrzeug")));
	TestEqual(TEXT("Unbekannter Pawn wird als zu Fuss klassifiziert"),
		AWiesbadenVehicleHUD::ResolveVehicleTransitionBanner(FootPawn), FString(TEXT("Ausgestiegen - zu Fuss")));

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankObliqueWallContactTest,
	"WiesbadenReal.Vehicles.BugTank.ObliqueWallContact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankObliqueWallContactTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld*/ false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* FloorCollision = NewObject<UBoxComponent>(Floor, TEXT("ObliqueTestFloor"));
	Floor->SetRootComponent(FloorCollision);
	FloorCollision->SetBoxExtent(FVector(5000.0f, 5000.0f, 10.0f));
	FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FloorCollision->SetCollisionObjectType(ECC_WorldStatic);
	FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
	FloorCollision->RegisterComponent();

	AActor* Facade = World->SpawnActor<AActor>();
	UBoxComponent* FacadeCollision = NewObject<UBoxComponent>(Facade, TEXT("ObliqueTestFacade"));
	Facade->SetRootComponent(FacadeCollision);
	FacadeCollision->SetBoxExtent(FVector(10.0f, 5000.0f, 1500.0f));
	FacadeCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FacadeCollision->SetCollisionObjectType(ECC_WorldStatic);
	FacadeCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Facade->SetActorLocation(FVector(310.0f, 0.0f, 1500.0f));
	FacadeCollision->RegisterComponent();

	AWiesbadenBugTankPawn* BugTank = World->SpawnActor<AWiesbadenBugTankPawn>(
		FVector(0.0f, 0.0f, 12.0f), FRotator(0.0f, -40.0f, 0.0f));
	if (!TestNotNull(TEXT("BugTank gespawnt"), BugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	BugTank->DispatchBeginPlay();

	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("PlayerController gespawnt"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	Controller->InitInputSystem();
	Controller->Possess(BugTank);
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
	}
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);

	TestTrue(FString::Printf(TEXT("Schraeg angefahrene Fassade wird Oberflaechenkontakt (Up=%s, Position=%s)"),
		*BugTank->GetActorUpVector().ToString(), *BugTank->GetActorLocation().ToString()),
		BugTank->GetActorUpVector().X < -0.8f && BugTank->HasSurfaceContact());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankRoadToFacadeBottomEdgeTest,
	"WiesbadenReal.Vehicles.BugTank.RoadToFacadeBottomEdge",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankRoadToFacadeBottomEdgeTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Floor = World->SpawnActor<AActor>();
	UBoxComponent* FloorCollision = NewObject<UBoxComponent>(Floor, TEXT("BugTankProxyTestFloor"));
	Floor->SetRootComponent(FloorCollision);
	FloorCollision->SetBoxExtent(FVector(5000.0f, 5000.0f, 10.0f));
	FloorCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	FloorCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Floor->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
	FloorCollision->RegisterComponent();

	AActor* CityActor = World->SpawnActor<AActor>();
	USceneComponent* CityRoot = NewObject<USceneComponent>(CityActor, TEXT("BugTankProxyTestRoot"));
	CityActor->SetRootComponent(CityRoot);
	CityRoot->RegisterComponent();
	UBuildingCollisionSpawnerComponent* Spawner = NewObject<UBuildingCollisionSpawnerComponent>(CityActor);
	Spawner->SetupAttachment(CityRoot);
	Spawner->BodyCount = 1;
	Spawner->RegisterComponent();

	FGeneratedBuilding Building;
	Building.SourceId = 44;
	Building.BuildingName = TEXT("Neugasse test facade");
	Building.Address = TEXT("Neugasse 44");
	Building.Centroid = FVector(310.0f, 0.0f, 1500.0f);
	Building.Bounds = FBox(FVector(300.0f, -5000.0f, 0.0f), FVector(320.0f, 5000.0f, 3000.0f));
	Building.FootprintCenterCm = FVector2D(310.0f, 0.0f);
	Building.FootprintExtentCm = FVector2D(10.0f, 5000.0f);
	Building.FootprintYawDegrees = 0.0f;
	Spawner->SetBuildings({ Building });
	Spawner->UpdateAround(FVector(0.0f, 0.0f, 58.0f));

	TArray<UBoxComponent*> BuildingBodies;
	CityActor->GetComponents<UBoxComponent>(BuildingBodies);
	UBoxComponent* BuildingBody = nullptr;
	for (UBoxComponent* Candidate : BuildingBodies)
	{
		if (Candidate && Candidate->ComponentHasTag(UBuildingCollisionSpawnerComponent::BuildingBodyTag))
		{
			BuildingBody = Candidate;
			break;
		}
	}
	if (!TestNotNull(TEXT("City spawner erzeugt den markierten Gebaeudekoerper"), BuildingBody))
	{
		World->DestroyWorld(false);
		return false;
	}

	TestEqual(TEXT("Gebaeudekoerper ist WorldStatic"), BuildingBody->GetCollisionObjectType(), ECC_WorldStatic);
	TestEqual(TEXT("Gebaeudekoerper blockiert Visibility-Traces"),
		BuildingBody->GetCollisionResponseToChannel(ECC_Visibility), ECR_Block);
	TestEqual(TEXT("Gebaeudekoerper blockiert Pawn-Bewegungssweeps"),
		BuildingBody->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	TestEqual(TEXT("Gebaeudekoerper bleibt query-only"),
		BuildingBody->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);

	const FVector Start(0.0f, 0.0f, 58.0f);
	FHitResult StaticObjectHit;
	FCollisionObjectQueryParams StaticObjects;
	StaticObjects.AddObjectTypesToQuery(ECC_WorldStatic);
	TestTrue(TEXT("Stadt-Diagnose-Objektabfrage trifft den Gebaeudekoerper"),
		World->LineTraceSingleByObjectType(StaticObjectHit, Start, FVector(600.0f, 0.0f, 58.0f), StaticObjects));
	TestTrue(TEXT("Objektabfrage trifft genau den markierten Stadtkasten"),
		StaticObjectHit.GetComponent() == BuildingBody);

	FHitResult VisibilityHit;
	TestTrue(TEXT("Visibility-Surface-Trace trifft denselben Gebaeudekoerper"),
		World->LineTraceSingleByChannel(VisibilityHit, Start, FVector(600.0f, 0.0f, 58.0f), ECC_Visibility));
	TestTrue(TEXT("Visibility-Treffer und Stadt-Diagnosetreffer sind dieselbe Komponente"),
		VisibilityHit.GetComponent() == StaticObjectHit.GetComponent());

	FHitResult PawnSweepHit;
	FCollisionQueryParams SweepParams(SCENE_QUERY_STAT(BugTankBuildingProxyRegression), false);
	TestTrue(TEXT("Pawn-Sphere-Sweep trifft den Gebaeudekoerper"),
		World->SweepSingleByChannel(PawnSweepHit, Start, FVector(600.0f, 0.0f, 58.0f),
			FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(56.0f), SweepParams));
	TestTrue(TEXT("Bewegungssweep und beide Line-Traces treffen dieselbe Komponente"),
		PawnSweepHit.GetComponent() == BuildingBody);

	AWiesbadenBugTankPawn* BugTank = World->SpawnActor<AWiesbadenBugTankPawn>(
		FVector(0.0f, 0.0f, 12.0f), FRotator(0.0f, -40.0f, 0.0f));
	if (!TestNotNull(TEXT("BugTank auf der Neugasse-Testspur gespawnt"), BugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	BugTank->DispatchBeginPlay();
	TestTrue(TEXT("BugTank startet auf der Straßenstütze"),
		BugTank->HasSurfaceContact() && BugTank->GetSurfaceHitComponent() == FloorCollision);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("PlayerController gespawnt"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	Controller->InitInputSystem();
	Controller->Possess(BugTank);
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	for (int32 Frame = 0; Frame < 90; ++Frame)
	{
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
	}
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
	TestTrue(FString::Printf(TEXT("BugTank bindet an die echte Stadt-Proxy-Fassade (Up=%s, Position=%s)"),
		*BugTank->GetActorUpVector().ToString(), *BugTank->GetActorLocation().ToString()),
		BugTank->GetActorUpVector().X < -0.8f && BugTank->HasSurfaceContact());
	TestTrue(TEXT("BugTank wechselt von der Straßenstütze zur gepoolten Fassadenbox"),
		BugTank->GetSurfaceHitComponent() == BuildingBody);
	FHitResult BugTankSurfaceHit;
	const FVector SurfaceTraceStart = BugTank->GetActorLocation();
	TestTrue(TEXT("BugTanks normaler Flächentrace trifft den aktiven Stadt-Proxy"),
		World->LineTraceSingleByChannel(BugTankSurfaceHit, SurfaceTraceStart,
			SurfaceTraceStart - BugTank->GetActorUpVector() * 165.0f, ECC_Visibility));
	TestTrue(TEXT("Pawn-Sweep, Stadttrace und BugTank-Flächentrace benennen dieselbe Komponente"),
		BugTankSurfaceHit.GetComponent() == PawnSweepHit.GetComponent()
			&& BugTankSurfaceHit.GetComponent() == StaticObjectHit.GetComponent());

	// The movement sphere still overlaps the facade when its center is just
	// below the generated box's lower bound; centerline traces alone lose it.
	FloorCollision->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	TestEqual(TEXT("Straßenstütze bleibt für Pawn-Kollision aktiv"),
		FloorCollision->GetCollisionResponseToChannel(ECC_Pawn), ECR_Block);
	FVector BelowFacadeBase = BugTank->GetActorLocation();
	BelowFacadeBase.Z = Building.Bounds.Min.Z - 5.5f;
	BugTank->SetActorLocation(BelowFacadeBase, false, nullptr, ETeleportType::TeleportPhysics);
	FHitResult CenterlineMiss;
	TestFalse(TEXT("Mittelpunkt-Trace verfehlt die Fassadenbox knapp unter ihrer Unterkante"),
		World->LineTraceSingleByChannel(CenterlineMiss, BelowFacadeBase,
			BelowFacadeBase - BugTank->GetActorUpVector() * 165.0f, ECC_Visibility));
	BugTank->Tick(1.0f / 30.0f);
	TestTrue(TEXT("BugTank haelt an der Unterkante Kontakt, solange seine Bewegungskugel die Fassade noch beruehrt"),
		BugTank->HasSurfaceContact() && BugTank->GetSurfaceHitComponent() == BuildingBody);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBugTankRoadContactMovementTest,
	"WiesbadenReal.Vehicles.BugTank.RoadContactMovement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FBugTankRoadContactMovementTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, /*bInformEngineOfWorld=*/false);
	if (!TestNotNull(TEXT("Test-Welt erstellt"), World))
	{
		return false;
	}

	AActor* Ground = World->SpawnActor<AActor>();
	UBoxComponent* GroundCollision = NewObject<UBoxComponent>(Ground, TEXT("BugTankGround"));
	Ground->SetRootComponent(GroundCollision);
	GroundCollision->SetBoxExtent(FVector(2000.0f, 2000.0f, 10.0f));
	GroundCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	GroundCollision->SetCollisionResponseToAllChannels(ECR_Block);
	Ground->SetActorLocation(FVector(0.0f, 0.0f, -10.0f));
	Ground->SetActorRotation(FRotator(2.0f, 0.0f, 0.0f));
	GroundCollision->RegisterComponent();

	// Model the observed two surfaces: the centerline support trace finds the
	// lower road bed while the wide movement sphere overlaps a slightly higher
	// collision shoulder that does not cover the ray's exact XY location.
	AActor* RoadCollisionActor = World->SpawnActor<AActor>();
	UBoxComponent* RoadCollision = NewObject<UBoxComponent>(RoadCollisionActor, TEXT("RoadCollisionStaticMesh"));
	RoadCollisionActor->SetRootComponent(RoadCollision);
	RoadCollision->SetBoxExtent(FVector(1000.0f, 1000.0f, 8.0f));
	RoadCollision->SetCollisionObjectType(ECC_WorldDynamic);
	RoadCollision->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	RoadCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	RoadCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	RoadCollisionActor->SetActorLocation(FVector(0.0f, 0.0f, -4.2f));
	RoadCollisionActor->SetActorRotation(FRotator(-35.0f, 0.0f, 0.0f));
	RoadCollision->RegisterComponent();

	AWiesbadenBugTankPawn* BugTank = World->SpawnActor<AWiesbadenBugTankPawn>(
		FVector(0.0f, 0.0f, 12.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("BugTank gespawnt"), BugTank))
	{
		World->DestroyWorld(false);
		return false;
	}
	BugTank->DispatchBeginPlay();
	const FVector SupportedStart = BugTank->GetActorLocation();
	TestTrue(TEXT("Flächentrace hält den BugTank auf dem darunterliegenden Boden"),
		BugTank->HasSurfaceContact() && FMath::IsNearlyEqual(SupportedStart.Z, 58.0f, 0.5f));

	// Match the Neugasse triangle seam: the sphere starts 1.7 cm into a slanted
	// road facet. Chaos reports a t=0 start penetration with a tilted normal.
	const FVector ReproStart = SupportedStart;
	FHitResult ReproHit;
	FCollisionQueryParams ReproParams(SCENE_QUERY_STAT(BugTankRoadContactRegression), false, BugTank);
	const FVector ReproEnd = ReproStart + FVector(17.3f, 0.0f, -0.6f);
	TestTrue(TEXT("BugTank-Sphere trifft den Strassenkörper beim Abwärtssnap"),
		World->SweepSingleByChannel(ReproHit, ReproStart, ReproEnd, FQuat::Identity,
			ECC_Pawn, FCollisionShape::MakeSphere(56.0f), ReproParams));
	TestTrue(FString::Printf(TEXT("Zeit-Null-Treffer gehört zum Straßenkörper (hit=%d component=%s road=%s t=%.4f startPen=%d)"),
		ReproHit.bBlockingHit ? 1 : 0,
		ReproHit.GetComponent() ? *ReproHit.GetComponent()->GetName() : TEXT("none"),
		*RoadCollision->GetName(), ReproHit.Time, ReproHit.bStartPenetrating ? 1 : 0),
		ReproHit.GetComponent() == RoadCollision && ReproHit.bBlockingHit && ReproHit.Time <= 0.005f
			&& ReproHit.bStartPenetrating && !ReproHit.IsValidBlockingHit()
			&& FVector::DotProduct(ReproHit.ImpactNormal.GetSafeNormal(), FVector::UpVector) < 0.9f);
	APlayerController* Controller = World->SpawnActor<APlayerController>();
	if (!TestNotNull(TEXT("PlayerController gespawnt"), Controller))
	{
		World->DestroyWorld(false);
		return false;
	}
	Controller->InitInputSystem();
	Controller->Possess(BugTank);
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Pressed, 1.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
	BugTank->Tick(1.0f / 30.0f);
	TestTrue(TEXT("First forward step slides along the contacted road surface"),
		BugTank->GetActorLocation().X - SupportedStart.X > 5.0f);
	for (int32 Frame = 1; Frame < 90; ++Frame)
	{
		Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Repeat, 1.0f));
		Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);
		BugTank->Tick(1.0f / 30.0f);
	}
	Controller->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W, IE_Released, 0.0f));
	Controller->PlayerInput->ProcessInputStack(TArray<UInputComponent*>(), 1.0f / 30.0f, false);

	TestTrue(FString::Printf(TEXT("BugTank fährt trotz t=0-Straßenkontakt weiter (Start=%s Ende=%s)"),
		*SupportedStart.ToString(), *BugTank->GetActorLocation().ToString()),
		BugTank->GetActorLocation().X - SupportedStart.X > 1000.0f);
	TestTrue(TEXT("BugTank behält während der Fahrt Straßenkontakt"), BugTank->HasSurfaceContact());

	World->DestroyWorld(false);
	return true;
}
