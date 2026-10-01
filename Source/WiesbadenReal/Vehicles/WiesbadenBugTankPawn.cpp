// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenBugTankPawn.h"

#include "Vehicles/WiesbadenBugTankTeile.h"

#include "WiesbadenReal.h"

#include "Camera/CameraComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CollisionShape.h"
#include "Camera/CameraActor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "UObject/ConstructorHelpers.h"
#if UE_BUILD_DEVELOPMENT
#include "World/BuildingCollisionSpawnerComponent.h"
#include "World/WiesbadenCityActor.h"
#include "World/WiesbadenCitySubsystem.h"
#endif

namespace BugTankRig
{
	/**
	 * Die 15 Teile in der Reihenfolge, in der sie als Komponenten angelegt
	 * werden. Das ist zugleich die Reihenfolge der Tabelle BugTankTeile: Index
	 * 0 ist der Koerper, 1..12 die sechs Beine (Ober, Unter) je Reihe und
	 * Seite, 13 und 14 die beiden Antennen.
	 *
	 * Die Reihenfolge ist wichtig und wird geprueft: der Import legt die Teile
	 * nach Typ sortiert in Unterordner, sie muessen flach liegen (Log
	 * wb_import_bugtank_teile.log, Zeilen "Verschoben" und "Benannt"). Fehlt
	 * ein Mesh, bleibt die Komponente leer und das faellt sonst erst beim
	 * Rendern auf - WiesbadenReal.Vehicles.BugTank.Rig misst es ueber
	 * GetGeladeneTeile().
	 */
	const FBugTankTeil* const Teile[] = {
		&BugTankTeile::Body,
		&BugTankTeile::BeinL1_Ober, &BugTankTeile::BeinL1_Unter,
		&BugTankTeile::BeinL2_Ober, &BugTankTeile::BeinL2_Unter,
		&BugTankTeile::BeinL3_Ober, &BugTankTeile::BeinL3_Unter,
		&BugTankTeile::BeinR1_Ober, &BugTankTeile::BeinR1_Unter,
		&BugTankTeile::BeinR2_Ober, &BugTankTeile::BeinR2_Unter,
		&BugTankTeile::BeinR3_Ober, &BugTankTeile::BeinR3_Unter,
		&BugTankTeile::AntenneL, &BugTankTeile::AntenneR };
	const int32 TeilAnzahl = UE_ARRAY_COUNT(Teile);

	/** Index des Oberteils von Bein B (0..5, L1..L3 dann R1..R3). */
	inline int32 OberIndex(int32 Bein) { return 1 + Bein * 2; }
	/** Index des Unterteils von Bein B. */
	inline int32 UnterIndex(int32 Bein) { return 2 + Bein * 2; }

	/**
	 * Laedt ein Insekt-Teil per ConstructorHelpers; nullptr, wenn es fehlt.
	 *
	 * Als eigene Funktion, nicht im Konstruktor: FObjectFinder laedt in seinem
	 * eigenen Konstruktor und ist nicht zuweisbar - ein `Finder = FObjectFinder(...)`
	 * im Konstruktor koennte den Pfad kopieren, ohne das Mesh zu laden, und der
	 * Kaeferschaender bliebe dann lautlos leer.
	 */
	UStaticMesh* LadeInsektTeil(const TCHAR* TeilName)
	{
		const FString Pfad = FString::Printf(
			TEXT("/Game/Vehicles/BugTank/SM_Insekt_%s.SM_Insekt_%s"), TeilName, TeilName);
		ConstructorHelpers::FObjectFinder<UStaticMesh> Finder(*Pfad);
		return Finder.Succeeded() ? Finder.Object : nullptr;
	}

	/**
	 * Zielpunkt einer Antenne, wenn eine fremde Fläche in Reichweite ist.
	 *
	 * GEMESSEN am 30.09.2026 in der Asset-Ruhelage (Komponentenraum, cm):
	 * Antennenbasis (51, ±7.5, 12.5), Kettenlänge 29.7 cm - ausgefahren steht
	 * die Spitze bei 70.2 cm, die Kolben Spitze bei 74.7 cm vom Ursprung und
	 * damit 14 bis 19 cm über der Kugel (r = 56). Eingeklappt zeigt der
	 * Zielpunkt nach vorn-unten statt weit nach vorn-aussen; die Spitze
	 * landet dann bei rund 44 cm, innerhalb der Kugel. Beide Zustände misst
	 * der Test WiesbadenReal.Vehicles.BugTank.Antenna.
	 */
	const FVector AntenneZielNahe(34.0f, 16.0f, -26.0f);
}

AWiesbadenBugTankPawn::AWiesbadenBugTankPawn()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionRoot = CreateDefaultSubobject<USphereComponent>(TEXT("BugTankCollision"));
	SetRootComponent(CollisionRoot);
	// Der Surface-Snap liegt bei 58 cm. Zwei Zentimeter Luft verhindern, dass
	// Sweeps an einer exakt tangentialen Flaeche als Startpenetration blockieren.
	CollisionRoot->InitSphereRadius(56.0f);
	CollisionRoot->SetCollisionProfileName(TEXT("Pawn"));
	CollisionRoot->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	CollisionRoot->SetSimulatePhysics(false);
	CollisionRoot->SetGenerateOverlapEvents(false);

	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("BugTankVisuals"));
	VisualRoot->SetupAttachment(CollisionRoot);

	// Darstellung: die 15 statischen Teile des Blender-Assets ersetzen die
	// bisherigen Engine-Grundkörper (Import: Tools/import_bugtank_teile.cmd).
	//
	// Warum kein Skeletal-Mesh: der Unreal-5.8-Import setzt die
	// Knochenorientierung eines Blender-Rigs um und übernimmt nur die
	// Knochenlänge, FBX wie glTF (Beleg Saved/Logs/wb_test_bugtankrig5.log,
	// Ruhepose 0/0/0.622 statt 38/30/-39). Blender/bugtank/export_bugtank_teile.py
	// zerlegt das Mesh darum in starre Teile mit denselben Gelenkpunkten.
	//
	// Der Versatz -Gelenk ist der Kern: die Komponente sitzt im Ruhelage-
	// Drehpunkt, das Mesh relativ um die Gegenstelle verschoben. Eine Drehung
	// der Komponente um ihren Ursprung ist damit genau die Drehung um den
	// Gelenkpunkt - ohne dass die Geometrie verrutscht.
	TeilKomponenten.Reserve(BugTankRig::TeilAnzahl);
	TeilDrehpunkte.Reserve(BugTankRig::TeilAnzahl);
	TeilAssets.Reserve(BugTankRig::TeilAnzahl);
	for (int32 Index = 0; Index < BugTankRig::TeilAnzahl; ++Index)
	{
		const FBugTankTeil& Teil = *BugTankRig::Teile[Index];
		const FName KomponentenName(*FString::Printf(TEXT("BugTankTeil_%s"), Teil.Name));
		UStaticMeshComponent* Komponente = CreateDefaultSubobject<UStaticMeshComponent>(KomponentenName);
		const FName PivotName(*FString::Printf(TEXT("BugTankGelenk_%s"), Teil.Name));
		USceneComponent* Pivot = CreateDefaultSubobject<USceneComponent>(PivotName);
		const bool bUnterbein = Index >= 2 && Index <= 12 && Index % 2 == 0;
		if (bUnterbein)
		{
			// Pivot am Knie folgt der Huefte; Mesh bleibt im Koerperraum.
			Pivot->SetupAttachment(TeilDrehpunkte[Index - 1]);
			Pivot->SetRelativeLocation(Teil.Gelenk - BugTankRig::Teile[Index - 1]->Gelenk);
		}
		else
		{
			Pivot->SetupAttachment(VisualRoot);
			Pivot->SetRelativeLocation(Teil.Gelenk);
		}
		TeilDrehpunkte.Add(Pivot);
		Komponente->SetupAttachment(Pivot);
		Komponente->SetRelativeLocation(-Teil.Gelenk);
		Komponente->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Komponente->SetGenerateOverlapEvents(false);

		// FObjectFinder je Teil: der Pfad muss flach liegen, Interchange legt
		// die Teile sonst nach Typ sortiert in Unterordner.
		if (UStaticMesh* Mesh = BugTankRig::LadeInsektTeil(Teil.Name))
		{
			Komponente->SetStaticMesh(Mesh);
			// Harte Referenz: sonst findet der Cooker das Mesh nicht.
			TeilAssets.Add(Mesh);
		}
		else
		{
			UE_LOG(LogWbVehicles, Warning,
				TEXT("BugTank: Mesh /Game/Vehicles/BugTank/SM_Insekt_%s fehlt - der Kafer bleibt "
					"unvollstaendig. Kollision und Bewegung sind davon nicht betroffen. "
					"Import: Tools/import_bugtank_teile.cmd"), Teil.Name);
		}
		TeilKomponenten.Add(Komponente);
	}

	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("BugTankCameraBoom"));
	CameraBoom->SetupAttachment(CollisionRoot);
	CameraBoom->TargetArmLength = 520.0f;
	CameraBoom->SetRelativeLocation(FVector(-35.0f, 0.0f, 110.0f));
	CameraBoom->bUsePawnControlRotation = false;
	CameraBoom->bDoCollisionTest = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("BugTankCamera"));
	Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);

	AutoPossessPlayer = EAutoReceiveInput::Disabled;
}

void AWiesbadenBugTankPawn::BeginPlay()
{
	Super::BeginPlay();
	SurfaceUp = GetActorUpVector();

	// Teile sofort aufbauen: Gelenkpunkte und Einklappachsen sind die
	// Voraussetzung fuer jede Beinpose, und der Logeintrag sagt beim Start
	// sofort Bescheid, ob alle 15 Meshes wirklich geladen wurden.
	TeileVorbereiten();

	// The pawn is spawned at the previous pawn's location, which can already
	// overlap the ground. Establish the surface offset before the first swept move.
	FVector SurfacePoint;
	FVector SurfaceNormal;
	UPrimitiveComponent* HitComponent = nullptr;
	float HitDistance = 0.0f;
	bHasSurfaceContact = FindSurface(0.0f, SurfacePoint, SurfaceNormal, HitComponent, HitDistance);
	if (bHasSurfaceContact)
	{
		SurfaceHitComponent = HitComponent;
		SurfaceHitDistance = HitDistance;
		SurfaceHitPoint = SurfacePoint;
		SurfaceUp = SurfaceNormal;
		const FVector Forward = FVector::VectorPlaneProject(GetActorForwardVector(), SurfaceUp).GetSafeNormal();
		if (!Forward.IsNearlyZero())
		{
			SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, SurfaceUp).ToQuat());
		}
		SetActorLocation(SurfacePoint + SurfaceUp * 58.0f, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

bool AWiesbadenBugTankPawn::FindSurface(float ForwardInput, FVector& OutPoint, FVector& OutNormal,
	UPrimitiveComponent*& OutComponent, float& OutDistance) const
{
	OutComponent = nullptr;
	OutDistance = 0.0f;
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const FVector Origin = GetActorLocation();
	const FVector Forward = GetActorForwardVector();
	const FVector Right = GetActorRightVector();
	const FVector Candidates[] = {
		-SurfaceUp,
		Forward - SurfaceUp * 0.25f,
		-Forward - SurfaceUp * 0.25f,
		Right - SurfaceUp * 0.25f,
		-Right - SurfaceUp * 0.25f,
		SurfaceUp
	};
	const UPrimitiveComponent* PreviousContactComponent = SurfaceHitComponent.Get();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BugTankSurface), false, this);
	float BestDistanceSq = TNumericLimits<float>::Max();
	float BestTraceDistance = 0.0f;
	UPrimitiveComponent* BestComponent = nullptr;
	int32 BestCandidateIndex = INDEX_NONE;
	bool bFound = false;
#if UE_BUILD_DEVELOPMENT
	const bool bLogSurfaceProbe =
		FParse::Param(FCommandLine::Get(), TEXT("WbBugTankProbe"));
	FHitResult ProbeHits[UE_ARRAY_COUNT(Candidates)];
	bool bProbeHit[UE_ARRAY_COUNT(Candidates)] = {};
	const UBuildingCollisionSpawnerComponent* BuildingCollision = nullptr;
	if (bLogSurfaceProbe)
	{
		if (const UWiesbadenCitySubsystem* City = World->GetSubsystem<UWiesbadenCitySubsystem>())
		{
			if (const AWiesbadenCityActor* CityActor = City->GetCityActor())
			{
				BuildingCollision = CityActor->FindComponentByClass<UBuildingCollisionSpawnerComponent>();
			}
		}
	}
#endif
	for (int32 CandidateIndex = 0; CandidateIndex < UE_ARRAY_COUNT(Candidates); ++CandidateIndex)
	{
		const FVector& RawDirection = Candidates[CandidateIndex];
		const FVector Direction = RawDirection.GetSafeNormal();
		FHitResult Hit;
		const FVector End = Origin + Direction * 165.0f;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Origin, End, ECC_Visibility, Params);
#if UE_BUILD_DEVELOPMENT
		if (bLogSurfaceProbe)
		{
			ProbeHits[CandidateIndex] = Hit;
			bProbeHit[CandidateIndex] = bHit;
		}
#endif
		if (bHit)
		{
			float Score = FVector::DistSquared(Origin, Hit.ImpactPoint);
			const bool bForwardTransition = CandidateIndex == 1 && ForwardInput > 0.1f;
			const bool bBackwardTransition = CandidateIndex == 2 && ForwardInput < -0.1f;
			if ((bForwardTransition || bBackwardTransition)
				&& FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal(), SurfaceUp) < 0.65f)
			{
				// An einer anderen Flaeche vor dem Kaefer darf der Treffer leicht
				// gewinnen; ohne Eingabe bleibt die aktuelle Flaeche stabil.
				Score *= 0.55f;
			}
			if (Score < BestDistanceSq)
			{
				BestDistanceSq = Score;
				OutPoint = Hit.ImpactPoint;
				OutNormal = Hit.ImpactNormal.GetSafeNormal();
				BestComponent = Hit.GetComponent();
				BestTraceDistance = Hit.Distance;
				BestCandidateIndex = CandidateIndex;
				bFound = true;
			}
		}
	}
	bool bUsedSphereRecovery = false;
	int32 SphereRecoveryCandidate = INDEX_NONE;
	FHitResult SphereRecoveryHit;
	if (!bFound && IsValid(PreviousContactComponent) && CollisionRoot)
	{
		const float Radius = CollisionRoot->GetScaledSphereRadius();
		for (int32 CandidateIndex = 0; CandidateIndex < UE_ARRAY_COUNT(Candidates); ++CandidateIndex)
		{
			const FVector Direction = Candidates[CandidateIndex].GetSafeNormal();
			const FVector End = Origin + Direction * 165.0f;
			FHitResult Hit;
			if (!World->SweepSingleByChannel(Hit, Origin, End, FQuat::Identity,
				ECC_Visibility, FCollisionShape::MakeSphere(Radius), Params))
			{
				continue;
			}
			if (Hit.GetComponent() != PreviousContactComponent)
			{
				continue;
			}

			float Score = FVector::DistSquared(Origin, Hit.ImpactPoint);
			const bool bForwardTransition = CandidateIndex == 1 && ForwardInput > 0.1f;
			const bool bBackwardTransition = CandidateIndex == 2 && ForwardInput < -0.1f;
			if ((bForwardTransition || bBackwardTransition)
				&& FVector::DotProduct(Hit.ImpactNormal.GetSafeNormal(), SurfaceUp) < 0.65f)
			{
				Score *= 0.55f;
			}
			if (Score < BestDistanceSq)
			{
				BestDistanceSq = Score;
				OutPoint = Hit.ImpactPoint;
				OutNormal = Hit.ImpactNormal.GetSafeNormal();
				BestComponent = Hit.GetComponent();
				BestTraceDistance = Hit.Distance;
				BestCandidateIndex = CandidateIndex;
				SphereRecoveryCandidate = CandidateIndex;
				SphereRecoveryHit = Hit;
				bFound = true;
				bUsedSphereRecovery = true;
			}
		}
	}
#if UE_BUILD_DEVELOPMENT
	if (bLogSurfaceProbe)
	{
		for (int32 CandidateIndex = 0; CandidateIndex < UE_ARRAY_COUNT(Candidates); ++CandidateIndex)
		{
			const FHitResult& Hit = ProbeHits[CandidateIndex];
			const AActor* HitActor = bProbeHit[CandidateIndex] ? Hit.GetActor() : nullptr;
			const UPrimitiveComponent* HitComponent = bProbeHit[CandidateIndex] ? Hit.GetComponent() : nullptr;
			const FVector End = Origin + Candidates[CandidateIndex].GetSafeNormal() * 165.0f;
			const FString PoolBoxes = BuildingCollision
				? BuildingCollision->DescribeProbeTrace(
					Origin, End, HitComponent, PreviousContactComponent)
				: TEXT("unavailable");
			UE_LOG(LogWbVehicles, Log,
				TEXT("WbBugTankSurfaceTrace t=%.3f candidate=%d hit=%d selected=%d actor=%s component=%s normal=(%.3f,%.3f,%.3f) start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) trace_distance_cm=%.1f hit_distance_cm=%.1f building_pool_boxes={%s}"),
				World->GetTimeSeconds(), CandidateIndex, bProbeHit[CandidateIndex] ? 1 : 0,
				BestCandidateIndex == CandidateIndex && !bUsedSphereRecovery ? 1 : 0,
				HitActor ? *HitActor->GetPathName() : TEXT("None"),
				HitComponent ? *HitComponent->GetPathName() : TEXT("None"),
				Hit.ImpactNormal.X, Hit.ImpactNormal.Y, Hit.ImpactNormal.Z,
				Origin.X, Origin.Y, Origin.Z, End.X, End.Y, End.Z,
				FVector::Distance(Origin, End), bProbeHit[CandidateIndex] ? Hit.Distance : -1.0f,
				*PoolBoxes);
		}
		if (bUsedSphereRecovery)
		{
			const FVector End = Origin + Candidates[SphereRecoveryCandidate].GetSafeNormal() * 165.0f;
			const UPrimitiveComponent* RecoveredComponent = SphereRecoveryHit.GetComponent();
			const FString PoolBoxes = BuildingCollision
				? BuildingCollision->DescribeProbeTrace(
					Origin, End, RecoveredComponent, PreviousContactComponent)
				: TEXT("unavailable");
			UE_LOG(LogWbVehicles, Log,
				TEXT("WbBugTankSurfaceRecovery t=%.3f candidate=%d actor=%s component=%s normal=(%.3f,%.3f,%.3f) start=(%.1f,%.1f,%.1f) end=(%.1f,%.1f,%.1f) sphere_radius_cm=%.1f sweep_distance_cm=%.1f building_pool_boxes={%s}"),
				World->GetTimeSeconds(), SphereRecoveryCandidate,
				SphereRecoveryHit.GetActor() ? *SphereRecoveryHit.GetActor()->GetPathName() : TEXT("None"),
				RecoveredComponent ? *RecoveredComponent->GetPathName() : TEXT("None"),
				SphereRecoveryHit.ImpactNormal.X, SphereRecoveryHit.ImpactNormal.Y, SphereRecoveryHit.ImpactNormal.Z,
				Origin.X, Origin.Y, Origin.Z, End.X, End.Y, End.Z,
				CollisionRoot->GetScaledSphereRadius(), SphereRecoveryHit.Distance, *PoolBoxes);
		}
	}
#endif
	if (bFound)
	{
		OutComponent = BestComponent;
		OutDistance = BestTraceDistance;
	}
	return bFound;
}

void AWiesbadenBugTankPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || DeltaSeconds <= 0.0f)
	{
		return;
	}

	const float ForwardInput = (PC->IsInputKeyDown(EKeys::W) ? 1.0f : 0.0f)
		- (PC->IsInputKeyDown(EKeys::S) ? 1.0f : 0.0f);
	const float TurnInput = (PC->IsInputKeyDown(EKeys::D) ? 1.0f : 0.0f)
		- (PC->IsInputKeyDown(EKeys::A) ? 1.0f : 0.0f);
	const float Speed = WalkSpeedCmS * (PC->IsInputKeyDown(EKeys::LeftShift) ? 1.35f : 1.0f);
	const FQuat Turn( SurfaceUp, FMath::DegreesToRadians(TurnInput * TurnRateDegS * DeltaSeconds));
	FVector Forward = Turn.RotateVector(GetActorForwardVector());
	Forward = FVector::VectorPlaneProject(Forward, SurfaceUp).GetSafeNormal();
	if (!Forward.IsNearlyZero())
	{
		const FQuat Aligned(FRotationMatrix::MakeFromXZ(Forward, SurfaceUp));
		SetActorRotation(Aligned);
	}

	FVector SurfacePoint;
	FVector SurfaceNormal;
	UPrimitiveComponent* HitComponent = nullptr;
	float HitDistance = 0.0f;
	const bool bHasSurface = FindSurface(ForwardInput, SurfacePoint, SurfaceNormal, HitComponent, HitDistance);
	bHasSurfaceContact = bHasSurface;
	SurfaceHitComponent = bHasSurface ? HitComponent : nullptr;
	SurfaceHitDistance = bHasSurface ? HitDistance : 0.0f;
	SurfaceHitPoint = bHasSurface ? SurfacePoint : FVector::ZeroVector;
	if (bHasSurface)
	{
		// Fremde Fläche: der Treffer zeigt nicht auf die aktuelle Standfläche,
		// sondern auf eine Wand oder Decke, in die der Käfer gerade läuft.
		// Beine und Fühler ragen 8 bis 20 cm über die Kugel hinaus (Blender-
		// Prüfbericht) und würden da hineinragen - genau dann werden sie
		// eingeklappt (UpdatePose). Dieselbe Abfrage wie für Laufen und
		// Ausrichtung, keine zweite Tracesuche.
		bFremdeFlaeche = FVector::DotProduct(SurfaceNormal, SurfaceUp) < 0.65f;
		SurfaceUp = FMath::VInterpNormalRotationTo(SurfaceUp, SurfaceNormal, DeltaSeconds, 260.0f).GetSafeNormal();
		const FQuat Aligned(FRotationMatrix::MakeFromXZ(
			FVector::VectorPlaneProject(GetActorForwardVector(), SurfaceUp).GetSafeNormal(), SurfaceUp));
		SetActorRotation(FMath::QInterpTo(GetActorQuat(), Aligned, DeltaSeconds, 7.0f));
		MoveVelocity = FVector::VectorPlaneProject(MoveVelocity, SurfaceUp);
		const FVector Desired = GetActorLocation() + Forward * ForwardInput * Speed * DeltaSeconds;
		const FVector Snapped = FMath::VInterpTo(Desired,
			SurfacePoint + SurfaceNormal * 58.0f, DeltaSeconds, 9.0f);
		FHitResult MoveHit;
		SetActorLocation(Snapped, true, &MoveHit, ETeleportType::None);
		UPrimitiveComponent* BlockingComponent = MoveHit.GetComponent();
		if (MoveHit.bBlockingHit && BlockingComponent
			&& BlockingComponent->GetCollisionObjectType() == ECC_WorldDynamic
			&& BlockingComponent->GetFName() == FName(TEXT("RoadCollisionStaticMesh"))
			&& FVector::DotProduct(MoveHit.ImpactNormal.GetSafeNormal(), SurfaceUp) > 0.65f)
		{
			const FVector Slide = FVector::VectorPlaneProject(Snapped - GetActorLocation(), SurfaceUp);
			if (!Slide.IsNearlyZero())
			{
				// The support trace still sees the road; ignore only its blocking sweep during this tangent retry.
				CollisionRoot->IgnoreComponentWhenMoving(BlockingComponent, true);
				SetActorLocation(GetActorLocation() + Slide, true, nullptr, ETeleportType::None);
				CollisionRoot->IgnoreComponentWhenMoving(BlockingComponent, false);
			}
		}
	}
	else
	{
		// Frei im Fall: nichts zum Durchdringen, die Gliedmaßen bleiben
		// ausgefahren.
		bFremdeFlaeche = false;
		MoveVelocity -= SurfaceUp * GravityCmS2 * DeltaSeconds;
		SetActorLocation(GetActorLocation() + Forward * ForwardInput * Speed * DeltaSeconds
			+ MoveVelocity * DeltaSeconds, true);
	}

	UpdatePose(DeltaSeconds, FMath::Abs(ForwardInput));
}

FVector AWiesbadenBugTankPawn::EinklappZiel(const FVector& RefKnie)
{
	// Eingeklappt liegt der Fuss dicht unter dem Koerper: 10 % der
	// Knie-Seitenlage und 20 cm unter dem Ursprung. GEMESSEN am Asset
	// (Blender-Rig, 1 Einheit = 1 cm): Knie (33, 28, 23), Abstand
	// Knie->Fussknoten 62.2 cm, Fusssohle in Ruhe bei -55 cm. Mit diesem
	// Ziel landet der Fussknoten bei rund 23 cm und die Sohle bei 34 cm vom
	// Ursprung - beide weit innerhalb der Kugel (r = 56). Der daraus
	// berechnete Faltwinkel betraegt 47 Grad.
	return FVector(RefKnie.X * 0.10f, RefKnie.Y * 0.10f, -20.0f);
}

UStaticMeshComponent* AWiesbadenBugTankPawn::GetInsektTeil(const FBugTankTeil& Teil) const
{
	for (int32 Index = 0; Index < BugTankRig::TeilAnzahl && Index < TeilKomponenten.Num(); ++Index)
	{
		if (Teil.Name && FCString::Strcmp(BugTankRig::Teile[Index]->Name, Teil.Name) == 0)
		{
			return TeilKomponenten[Index];
		}
	}
	return nullptr;
}

int32 AWiesbadenBugTankPawn::GetGeladeneTeile() const
{
	int32 Zahl = 0;
	for (const TObjectPtr<UStaticMeshComponent>& Komponente : TeilKomponenten)
	{
		if (Komponente && Komponente->GetStaticMesh())
		{
			++Zahl;
		}
	}
	return Zahl;
}

bool AWiesbadenBugTankPawn::TeileVorbereiten()
{
	if (bTeileBereit)
	{
		return true;
	}
	if (TeilKomponenten.Num() != BugTankRig::TeilAnzahl)
	{
		return false;
	}

	// Ruhelage steht, solange nichts gepostet wurde.
	for (USceneComponent* Komponente : TeilDrehpunkte)
	{
		if (Komponente)
		{
			Komponente->SetRelativeRotation(FQuat::Identity);
		}
	}

	// Gelenk (Drehpunkt), Fussspitze (Messpunkt) und Einklappachse je Kette -
	// dieselbe Rechnung wie zuvor, jetzt auf den Tabellenwerten statt auf
	// Ref-Pose-Matrizen. GEMESSEN am Blender-Asset (1 Einheit = 1 cm):
	// Knie (33, 28, 23), Spitze (39.4, 30.4, -55.1), Abstand Knie->Sohle
	// 62.2 cm; der Einklappwinkel betraegt 47 Grad.
	for (int32 B = 0; B < 6; ++B)
	{
		FGliedmasseKette& Kette = Beinketten[B];
		const FBugTankTeil& Ober = *BugTankRig::Teile[BugTankRig::OberIndex(B)];
		const FBugTankTeil& Unter = *BugTankRig::Teile[BugTankRig::UnterIndex(B)];
		Kette.Ober = BugTankRig::OberIndex(B);
		Kette.Unter = BugTankRig::UnterIndex(B);
		Kette.SpitzeIndex = Kette.Unter;
		Kette.RefKnie = Unter.Gelenk;
		Kette.RefFuss = Unter.Spitze;
		Kette.Ziel = EinklappZiel(Kette.RefKnie);
		const FVector Von = (Kette.RefFuss - Kette.RefKnie).GetSafeNormal();
		const FVector Nach = (Kette.Ziel - Kette.RefKnie).GetSafeNormal();
		const FVector Achse = FVector::CrossProduct(Von, Nach);
		if (Achse.SizeSquared() > 1.0e-6f)
		{
			// Achse senkrecht zur Ebene aus Bein und Ziel: dreht das untere
			// Bein genau auf das Ziel, ohne die Huefte zu verschieben.
			Kette.Faltaehse = Achse.GetSafeNormal();
			Kette.Faltwinkel = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector::DotProduct(Von, Nach), -1.0f, 1.0f)));
		}
		else
		{
			Kette.Faltaehse = FVector::ZeroVector;
			Kette.Faltwinkel = 0.0f;
		}
	}
	for (int32 A = 0; A < 2; ++A)
	{
		FGliedmasseKette& Kette = Antennenketten[A];
		const FBugTankTeil& Antenne = *BugTankRig::Teile[13 + A];
		Kette.Ober = 13 + A;
		Kette.Unter = 13 + A;
		Kette.SpitzeIndex = 13 + A;
		Kette.RefKnie = Antenne.Gelenk;
		Kette.RefFuss = Antenne.Spitze;
		// Ausgefahren zeigen die Fuehler nach vorn-aussen (Ziel weit weg, der
		// Faltwinkel waere dann fast 180 Grad und die Kette laeuft nach hinten
		// weg). Der Nah-Zielpunkt traegt nur den Einklapp-Anteil.
		Kette.Ziel = FVector(BugTankRig::AntenneZielNahe.X,
			(A == 0 ? BugTankRig::AntenneZielNahe.Y : -BugTankRig::AntenneZielNahe.Y),
			BugTankRig::AntenneZielNahe.Z);
		const FVector Von = (Kette.RefFuss - Kette.RefKnie).GetSafeNormal();
		const FVector Nach = (Kette.Ziel - Kette.RefKnie).GetSafeNormal();
		const FVector Achse = FVector::CrossProduct(Von, Nach);
		if (Achse.SizeSquared() > 1.0e-6f)
		{
			Kette.Faltaehse = Achse.GetSafeNormal();
			Kette.Faltwinkel = FMath::RadiansToDegrees(FMath::Acos(
				FMath::Clamp(FVector::DotProduct(Von, Nach), -1.0f, 1.0f)));
		}
		else
		{
			Kette.Faltaehse = FVector::ZeroVector;
			Kette.Faltwinkel = 0.0f;
		}
	}

	// Faltwinkel als Beleg: so weit muessen die Gliedmassen drehen, damit
	// die Spitzen in die Kugel r = 56 zurueckkommen.
	float BeinWinkelMin = 1.0e9f;
	float BeinWinkelMax = 0.0f;
	for (int32 B = 0; B < 6; ++B)
	{
		BeinWinkelMin = FMath::Min(BeinWinkelMin, Beinketten[B].Faltwinkel);
		BeinWinkelMax = FMath::Max(BeinWinkelMax, Beinketten[B].Faltwinkel);
	}
	float AntenneWinkelMin = 1.0e9f;
	float AntenneWinkelMax = 0.0f;
	for (int32 A = 0; A < 2; ++A)
	{
		AntenneWinkelMin = FMath::Min(AntenneWinkelMin, Antennenketten[A].Faltwinkel);
		AntenneWinkelMax = FMath::Max(AntenneWinkelMax, Antennenketten[A].Faltwinkel);
	}

	bTeileBereit = true;
	UE_LOG(LogWbVehicles, Log,
		TEXT("BugTank: %d statische Teile geladen (%d Meshes), 6 Beinketten, 2 Antennenketten, "
			"Einklappwinkel Bein %.0f-%.0f Grad, Fuehler %.0f-%.0f Grad."),
		BugTankRig::TeilAnzahl, GetGeladeneTeile(),
		BeinWinkelMin, BeinWinkelMax, AntenneWinkelMin, AntenneWinkelMax);
	return true;
}

void AWiesbadenBugTankPawn::TeilDrehen(int32 Index, const FQuat& Drehung)
{
	if (!TeilDrehpunkte.IsValidIndex(Index))
	{
		return;
	}
	if (USceneComponent* Komponente = TeilDrehpunkte[Index])
	{
		Komponente->SetRelativeRotation(Drehung);
	}
}

void AWiesbadenBugTankPawn::UpdatePose(float DeltaSeconds, float SpeedFraction)
{
	if (!TeileVorbereiten())
	{
		return;
	}

	// Einklappgrad aus der VORHANDENEN Oberflaechenabfrage. Keine zweite
	// Tracesuche: dieselbe Abfrage, die Laufen, Wand- und Deckenkontakt
	// entscheidet, liefert auch ihre Normale. Zeigt der Treffer nicht auf
	// die Standflaeche (Dot < 0.65), laeuft der Kafer in eine Wand oder an
	// eine Decke - dann wuerden Beine und Fuehler, die 8 bis 20 cm ueber die
	// Kugel hinausragen, in die Flaeche stossen, und werden hereingeklappt.
	// Auf der Standflaeche bleiben sie ausgefahren: die Fuesse stehen bei
	// Ruhelage 3 cm ueber der Flaeche (Snap 58, Sohle -55), nicht darin.
	// Der Normalenwechsel dauert nur wenige Frames. Sofort einklappen,
	// dann kurz halten: eine langsame Einblendung verpasst den Uebergang.
	FoldHoldRemaining = bFremdeFlaeche ? 0.3f
		: FMath::Max(0.0f, FoldHoldRemaining - FMath::Max(0.0f, DeltaSeconds));
	if (FoldHoldRemaining > 0.0f)
	{
		LegFold = 1.0f;
		AntennaFold = 1.0f;
	}
	else
	{
		LegFold = FMath::FInterpTo(LegFold, 0.0f, DeltaSeconds, 5.0f);
		AntennaFold = FMath::FInterpTo(AntennaFold, 0.0f, DeltaSeconds, 4.0f);
	}

	UpdateLegs(DeltaSeconds, SpeedFraction);
	UpdateAntennae(DeltaSeconds, SpeedFraction);
}

void AWiesbadenBugTankPawn::UpdateLegs(float DeltaSeconds, float SpeedFraction)
{
	LegPhase += DeltaSeconds * (5.0f + SpeedFraction * 8.0f);
	if (!TeileVorbereiten())
	{
		return;
	}
	const float Falt = LegFold;
	for (int32 B = 0; B < 6; ++B)
	{
		const FGliedmasseKette& Kette = Beinketten[B];
		// Phase unveraendert aus der bisherigen Beinanimation: drei Reihen
		// je Seite, diagonale Versetzung. So laeuft der Kafer im selben
		// Rhythmus wie vorher, nur jetzt auf zwei Teile statt auf fuenf Knochen.
		const int32 Reihe = B % 3;
		const bool bLinks = B < 3;
		const float Seite = bLinks ? 1.0f : -1.0f;
		const float Phase = LegPhase + ((Reihe % 2) ? PI : 0.0f) + (bLinks ? PI : 0.0f);
		const float Gang = FMath::Clamp(SpeedFraction, 0.0f, 1.0f) * (1.0f - Falt);
		const float S = FMath::Sin(Phase) * Gang;
		const float C = FMath::Cos(Phase) * Gang;

		// Oberteil (Coxa + Femur): Gieren (Z) schwingt das Bein vor/zurueck,
		// Nicken (Y) hebt es an. Die Winkel sind die der früheren
		// Knochenkette (16 Grad Gieren, 6 Grad Nicken) - beim Drehen des
		// Oberteils um die Coxa wandert die Spitze des Unterteils mit.
		const FQuat OberDrehung = FQuat(FVector::UpVector, FMath::DegreesToRadians(-Seite * 16.0f * S))
			* FQuat(FVector::RightVector, FMath::DegreesToRadians(6.0f * C));
		TeilDrehen(Kette.Ober, OberDrehung);

		// Unterteil (Tibia + Tarsus): Einklappen (Achse aus TeileVorbereiten),
		// Gegenschwung und Nachschlag. Die Drehungen addieren sich auf die
		// Oberdrehung, damit das Bein als Kette von zwei Gliedern laeuft.
		const FQuat Falten = Kette.Faltaehse.IsNearlyZero()
			? FQuat::Identity
			: FQuat(Kette.Faltaehse, FMath::DegreesToRadians(Kette.Faltwinkel * Falt));
		const FQuat UnterDrehung = FQuat(FVector::RightVector, FMath::DegreesToRadians(-14.0f * C))
			* Falten * FQuat(FVector::RightVector, FMath::DegreesToRadians(16.0f * C - 6.0f * S));
		TeilDrehen(Kette.Unter, UnterDrehung);
	}
}

void AWiesbadenBugTankPawn::UpdateAntennae(float DeltaSeconds, float SpeedFraction)
{
	if (!TeileVorbereiten())
	{
		return;
	}
	AntennaPhase += DeltaSeconds * (2.4f + SpeedFraction * 7.0f);
	const float Falt = AntennaFold;

	// Seitlicher Versatz der getroffenen Flaeche aus derselben Abfrage: liegt
	// die Flaeche links vom Kafer, taucht die linke Antenne tiefer. Keine
	// eigene Tracesuche, nur eine Projektion des bereits bekannten Treffers.
	const float Seitlich = bHasSurfaceContact
		? FVector::DotProduct(SurfaceHitPoint - GetActorLocation(), GetActorRightVector()) / 60.0f
		: 0.0f;

	for (int32 A = 0; A < 2; ++A)
	{
		const FGliedmasseKette& Kette = Antennenketten[A];
		const float Seite = (A == 0) ? 1.0f : -1.0f;
		const float SeiteGewicht = FMath::Clamp(0.5f + 0.5f * Seite * Seitlich, 0.0f, 1.0f);

		// Tasten: in 18 % der Zykluszeit schnell nach unten, danach langsam
		// zurueck - dieselbe Formel wie in der Blender-Referenz.
		const float T = FMath::Fmod(AntennaPhase + (A == 1 ? 0.21f : 0.0f), 1.0f);
		const float Tasten = (T < 0.18f)
			? (T / 0.18f)
			: (1.0f - (T - 0.18f) / 0.82f);
		const float Tiefe = 0.40f * Tasten * (1.0f - Falt)
			* (0.35f + 0.65f * SpeedFraction) * SeiteGewicht;

		// Die Antenne ist ein einziges starres Teil (alle vier Segmente
		// zusammen, export_bugtank_teile.py), also EIN Drehwinkel statt
		// einer Kette:
		//   Falten   - Einklappen, die Antenne zeigt nach vorn-unten statt
		//              weit nach vorn-aussen (Achse aus TeileVorbereiten)
		//   Seiten   - der seitliche Schwenk
		//   Tiefe*   - Tasten nach unten (Nicken um die Rechtsachse); 0.4 und
		//              0.6 waren auf zwei Gelenke verteilt, zusammen 1.0
		//   Zucken   - schnelles Zucken nach aussen
		const FQuat Falten = Kette.Faltaehse.IsNearlyZero()
			? FQuat::Identity
			: FQuat(Kette.Faltaehse, FMath::DegreesToRadians(Kette.Faltwinkel * Falt));
		const FQuat Drehung = Falten
			* FQuat(FVector::UpVector, FMath::DegreesToRadians(10.0f * (1.0f - Falt)
				* FMath::Sin(AntennaPhase * 2.0f * PI)))
			* FQuat(FVector::RightVector, Tiefe)
			* FQuat(FVector::UpVector, FMath::DegreesToRadians(8.0f * (1.0f - Falt)
				* FMath::Sin(AntennaPhase * 2.0f * PI + 1.0f)));
		TeilDrehen(Kette.Ober, Drehung);
	}
}
