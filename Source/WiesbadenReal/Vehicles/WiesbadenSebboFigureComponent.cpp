// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenSebboFigureComponent.h"

#include "WiesbadenReal.h"

#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimNodeBase.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "BonePose.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/CommandLine.h"
#include "TwoBoneIK.h"

// -- Mischer ------------------------------------------------------------------

void FWbSebboMixer::Start(EWbSebboMove Move, bool bLoop, float PlayRate, float InBlendSeconds, float StartTime)
{
	for (FWbSebboLayer& Layer : Layers)
	{
		Layer.StartWeight = Layer.Weight;
	}

	// Laeuft die Bewegung noch (im Ausblenden), blendet sie von dort wieder
	// ein und behaelt ihre Abspielstelle - Gehen/Rennen im Wechsel springt
	// sonst jedes Mal an den Anfang des Zyklus.
	FWbSebboLayer Layer;
	const int32 Existing = Layers.IndexOfByPredicate([Move](const FWbSebboLayer& L) { return L.Move == Move; });
	if (Existing != INDEX_NONE)
	{
		Layer = Layers[Existing];
		Layers.RemoveAt(Existing);
		if (!bLoop)
		{
			Layer.Time = StartTime;   // Einmalbewegung beginnt immer von vorn
		}
	}
	else
	{
		Layer.Move = Move;
		Layer.Time = StartTime;
	}
	Layer.bLoop = bLoop;
	Layer.PlayRate = PlayRate;
	Layers.Add(Layer);

	// Zu viele Spuren: die aeltesten fallen weg, der Rest wird so skaliert,
	// dass die Gewichte wieder 1 ergeben.
	if (Layers.Num() > MaxLayers)
	{
		Layers.RemoveAt(0, Layers.Num() - MaxLayers);
		float Sum = 0.0f;
		for (const FWbSebboLayer& L : Layers) { Sum += L.StartWeight; }
		for (FWbSebboLayer& L : Layers)
		{
			L.StartWeight = Sum > KINDA_SMALL_NUMBER ? L.StartWeight / Sum : 0.0f;
			L.Weight = L.StartWeight;
		}
	}

	AlphaStart = Layers.Last().StartWeight;
	Alpha = AlphaStart;
	BlendSeconds = InBlendSeconds;
	if (InBlendSeconds <= 0.0f || Layers.Num() == 1)
	{
		// Hart: nur noch diese Spur.
		FWbSebboLayer Only = Layers.Last();
		Only.Weight = Only.StartWeight = 1.0f;
		Layers.Reset();
		Layers.Add(Only);
		Alpha = AlphaStart = 1.0f;
	}
}

void FWbSebboMixer::Advance(float DeltaSeconds, TFunctionRef<float(EWbSebboMove)> ClipLength)
{
	for (FWbSebboLayer& Layer : Layers)
	{
		const float Length = ClipLength(Layer.Move);
		Layer.Time += DeltaSeconds * Layer.PlayRate;
		if (Length > KINDA_SMALL_NUMBER)
		{
			Layer.Time = Layer.bLoop
				? Layer.Time - Length * FMath::FloorToFloat(Layer.Time / Length)
				: FMath::Clamp(Layer.Time, 0.0f, Length);   // Einmal: im letzten Bild stehen
		}
	}

	if (Alpha < 1.0f)
	{
		Alpha = BlendSeconds > KINDA_SMALL_NUMBER ? FMath::Min(1.0f, Alpha + DeltaSeconds / BlendSeconds) : 1.0f;
	}
	if (Layers.Num() == 0)
	{
		return;
	}
	if (Alpha >= 1.0f)
	{
		FWbSebboLayer Only = Layers.Last();
		Only.Weight = 1.0f;
		Layers.Reset();
		Layers.Add(Only);
		return;
	}
	// Die gewollte Spur steigt linear, die anderen fallen im selben Verhaeltnis.
	const float Rest = AlphaStart < 1.0f ? (1.0f - Alpha) / (1.0f - AlphaStart) : 0.0f;
	for (int32 I = 0; I + 1 < Layers.Num(); ++I)
	{
		Layers[I].Weight = Layers[I].StartWeight * Rest;
	}
	Layers.Last().Weight = Alpha;
}

float FWbSebboMixer::WeightOf(EWbSebboMove Move) const
{
	float Weight = 0.0f;
	for (const FWbSebboLayer& Layer : Layers)
	{
		Weight += Layer.Move == Move ? Layer.Weight : 0.0f;
	}
	return Weight;
}

// -- Anim-Instanz --------------------------------------------------------------

namespace
{
	/** Tastet die gemeldeten Clips ab und mischt sie nach Gewicht. */
	struct FWbSebboAnimProxy : public FAnimInstanceProxy
	{
		explicit FWbSebboAnimProxy(UAnimInstance* InAnimInstance) : FAnimInstanceProxy(InAnimInstance) {}

		virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override
		{
			FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);
			// Spielstrang: Kopie fuer das Abtasten auf dem Anim-Strang.
			const UWiesbadenSebboAnimInstance* Instance = CastChecked<UWiesbadenSebboAnimInstance>(InAnimInstance);
			Samples = Instance->GetSamples();
			FootIk = Instance->GetFootIk();
		}

		/**
		 * Fuss-IK auf die gemischte Pose: Becken um PelvisOffset, dann jedes
		 * Bein per Zwei-Knochen-IK mit dem Fussgelenk auf (animierte Lage +
		 * FootOffset), Knie nach vorn, der Fuss in die Bodenneigung gekippt.
		 */
		void ApplyFootIk(FPoseContext& Output) const
		{
			const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
			const auto Index = [&Bones](const TCHAR* Name)
			{
				const int32 Mesh = Bones.GetPoseBoneIndexForBoneName(FName(Name));
				return Mesh == INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE)
					: Bones.MakeCompactPoseIndex(FMeshPoseBoneIndex(Mesh));
			};
			const FCompactPoseBoneIndex Pelvis = Index(TEXT("pelvis"));
			const FCompactPoseBoneIndex Thigh[2] = { Index(TEXT("thigh_l")), Index(TEXT("thigh_r")) };
			const FCompactPoseBoneIndex Calf[2] = { Index(TEXT("calf_l")), Index(TEXT("calf_r")) };
			const FCompactPoseBoneIndex Foot[2] = { Index(TEXT("foot_l")), Index(TEXT("foot_r")) };
			if (!Pelvis.IsValid() || !Thigh[0].IsValid() || !Thigh[1].IsValid() || !Calf[0].IsValid()
				|| !Calf[1].IsValid() || !Foot[0].IsValid() || !Foot[1].IsValid())
			{
				return;
			}

			const double Alpha = FootIk.Alpha;
			FCSPose<FCompactPose> Pose;
			Pose.InitPose(Output.Pose);

			// Die animierte Fusslage VOR dem Becken: der Boden ist gegen sie gemessen.
			const FVector Animated[2] = {
				Pose.GetComponentSpaceTransform(Foot[0]).GetLocation(),
				Pose.GetComponentSpaceTransform(Foot[1]).GetLocation() };

			if (!FootIk.PelvisOffset.IsNearlyZero(0.01))
			{
				FTransform Hips = Pose.GetComponentSpaceTransform(Pelvis);
				Hips.AddToTranslation(FootIk.PelvisOffset * Alpha);
				const FBoneTransform Moved[] = { FBoneTransform(Pelvis, Hips) };
				Pose.SafeSetCSBoneTransforms(Moved);
			}

			TArray<FBoneTransform, TInlineAllocator<6>> Legs;
			for (int32 I = 0; I < 2; ++I)
			{
				FTransform Hip = Pose.GetComponentSpaceTransform(Thigh[I]);
				FTransform Knee = Pose.GetComponentSpaceTransform(Calf[I]);
				FTransform Ankle = Pose.GetComponentSpaceTransform(Foot[I]);
				const FQuat AnkleRotation = Ankle.GetRotation();
				const FVector Target = Animated[I] + FootIk.FootOffset[I] * Alpha;
				// Beugeebene: das Knie zeigt nach vorn (ein fast gestrecktes Bein
				// liefert sonst keine eindeutige Ebene).
				const FVector KneeHint = Knee.GetLocation() + FootIk.Forward * 60.0;
				AnimationCore::SolveTwoBoneIK(Hip, Knee, Ankle, KneeHint, Target, false, 1.0, 1.0);

				// Fuss in die Bodenneigung, hoechstens 30 Grad.
				FVector Axis;
				float Angle = 0.0f;
				FQuat::FindBetweenNormals(FootIk.Up, FootIk.GroundNormal[I]).ToAxisAndAngle(Axis, Angle);
				Angle = FMath::Min(Angle, FMath::DegreesToRadians(30.0f)) * static_cast<float>(Alpha);
				Ankle.SetRotation(FQuat(Axis, Angle) * AnkleRotation);

				Legs.Add(FBoneTransform(Thigh[I], Hip));
				Legs.Add(FBoneTransform(Calf[I], Knee));
				Legs.Add(FBoneTransform(Foot[I], Ankle));
			}
			Legs.Sort(FCompareBoneTransformIndex());
			Pose.SafeSetCSBoneTransforms(Legs);
			FCSPose<FCompactPose>::ConvertComponentPosesToLocalPoses(MoveTemp(Pose), Output.Pose);
		}

		virtual bool Evaluate(FPoseContext& Output) override
		{
			float Summed = 0.0f;
			for (const FWbSebboPoseSample& Sample : Samples)
			{
				if (!Sample.Sequence || Sample.Weight <= KINDA_SMALL_NUMBER)
				{
					continue;
				}
				const FAnimExtractContext Extract(static_cast<double>(Sample.Time), false, {}, Sample.bLoop);
				if (Summed <= 0.0f)
				{
					FAnimationPoseData Base(Output);
					Sample.Sequence->GetAnimationPose(Base, Extract);
					Summed = Sample.Weight;
					continue;
				}
				FPoseContext Other(this);
				FAnimationPoseData OtherData(Other);
				Sample.Sequence->GetAnimationPose(OtherData, Extract);
				FAnimationPoseData Blended(Output);
				FAnimationRuntime::BlendTwoPosesTogetherInPlace(Blended, OtherData, Summed / (Summed + Sample.Weight));
				Summed += Sample.Weight;
			}
			if (Summed <= 0.0f)
			{
				return false;   // nichts gemeldet: Ruhepose
			}
			if (FootIk.Alpha > 0.01f)
			{
				ApplyFootIk(Output);
			}
			return true;
		}

		TArray<FWbSebboPoseSample> Samples;
		FWbFootIkPose FootIk;
	};
}

FAnimInstanceProxy* UWiesbadenSebboAnimInstance::CreateAnimInstanceProxy()
{
	return new FWbSebboAnimProxy(this);
}

// -- Figur ---------------------------------------------------------------------

bool UWiesbadenSebboFigureComponent::SetupFigure(float InJumpAirSeconds)
{
	USkeletalMesh* Skeletal = LoadObject<USkeletalMesh>(
		nullptr, TEXT("/Game/Assets/People/Sebbo/Meshes/SK_Sebbo.SK_Sebbo"));
	if (!Skeletal)
	{
		return false;
	}

	Moves.SetNum(static_cast<int32>(EWbSebboMove::Count));
	FString Fehlend;
	int32 Geladen = 0;
	for (int32 I = 0; I < Moves.Num(); ++I)
	{
		const FString Name = MoveName(static_cast<EWbSebboMove>(I));
		Moves[I] = LoadObject<UAnimSequence>(nullptr, *FString::Printf(
			TEXT("/Game/Assets/People/Sebbo/Animations/A_Sebbo_%s.A_Sebbo_%s"), *Name, *Name));
		if (Moves[I]) { ++Geladen; } else { Fehlend += Name + TEXT(" "); }
	}

	// Ohne Stehen, Gehen, Rennen und Springen lohnt die Figur nicht: ein
	// Skelett, das reglos in T-Haltung ueber die Strasse gleitet, waere
	// schlechter als das statische Modell. Die uebrigen sind Zugaben.
	if (!HasMove(EWbSebboMove::Idle) || !HasMove(EWbSebboMove::Walk)
		|| !HasMove(EWbSebboMove::Run) || !HasMove(EWbSebboMove::Jump))
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerfigur: SK_Sebbo ohne Grundbewegungen (es fehlen: %s) - statisches Modell."), *Fehlend);
		Moves.Reset();
		return false;
	}

	SetSkeletalMesh(Skeletal);
	// Eigene Anim-Instanz statt PlayAnimation: sie mischt die Clips, die der
	// Mischer meldet - weiche Uebergaenge statt hartem Umschalten.
	SetAnimationMode(EAnimationMode::AnimationBlueprint);
	SetAnimInstanceClass(UWiesbadenSebboAnimInstance::StaticClass());
	if (!Cast<UWiesbadenSebboAnimInstance>(GetAnimInstance()))
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Spielerfigur: Misch-Instanz nicht angelegt (Komponente noch nicht registriert?) - die Figur bleibt in Ruhepose, bis sie es ist."));
	}
	JumpAirSeconds = InJumpAirSeconds;
	bReady = true;
	if (FParse::Param(FCommandLine::Get(), TEXT("WbOhneFussIk")))
	{
		bFootIk = false;
		UE_LOG(LogWbVehicles, Warning, TEXT("-WbOhneFussIk: Spielerfigur ohne Fuss-IK (nur zum Messen)."));
	}
	Mixer = FWbSebboMixer();
	CurrentMove = EWbSebboMove::Count;
	PlayMove(EWbSebboMove::Idle, true, 1.0f);
	AdvanceMixer(0.0f);
	UE_LOG(LogWbVehicles, Log,
		TEXT("Spielerfigur: SK_Sebbo (Tripo, 61 Knochen) mit %d von %d Bewegungen%s%s."),
		Geladen, Moves.Num(), Fehlend.IsEmpty() ? TEXT("") : TEXT(", es fehlen: "), *Fehlend);
	return true;
}

bool UWiesbadenSebboFigureComponent::HasMove(EWbSebboMove Move) const
{
	return Moves.IsValidIndex(static_cast<int32>(Move)) && Moves[static_cast<int32>(Move)] != nullptr;
}

float UWiesbadenSebboFigureComponent::MoveLength(EWbSebboMove Move) const
{
	return HasMove(Move) ? Moves[static_cast<int32>(Move)]->GetPlayLength() : 0.0f;
}

void UWiesbadenSebboFigureComponent::PlayOneShot(EWbSebboMove Move, float Seconds)
{
	if (!bReady || !HasMove(Move))
	{
		return;
	}
	// Einmalig, keine Schleife. CurrentMove auf "keine", damit danach die
	// passende Dauerbewegung NEU startet - sonst bliebe die Figur im letzten
	// Bild stehen.
	OneShotMove = Move;
	OneShotRemaining = FMath::Max(Seconds, 0.1f);
	const FWbSebboLayer* Before = Mixer.Top();
	Mixer.Start(Move, false, MoveLength(Move) / OneShotRemaining,
		BlendSecondsFor(Before ? Before->Move : EWbSebboMove::Count, Move));
	CurrentMove = EWbSebboMove::Count;
}

void UWiesbadenSebboFigureComponent::CancelOneShot(EWbSebboMove Move)
{
	if (OneShotMove == Move)
	{
		OneShotRemaining = 0.0f;
		OneShotMove = EWbSebboMove::Count;
	}
}

void UWiesbadenSebboFigureComponent::Animate(float DeltaSeconds, const FWbFigureInput& Input)
{
	if (!bReady || DeltaSeconds <= KINDA_SMALL_NUMBER)
	{
		return;
	}
	ChooseAndPlay(DeltaSeconds, Input);
	UpdateFootIk(DeltaSeconds, Input);
	AdvanceMixer(DeltaSeconds);
}

FWbFootIkOffsets UWiesbadenSebboFigureComponent::ComputeFootIk(float CenterGroundZ, const bool bHit[2],
	const float FootGroundZ[2], float MaxUpCm, float MaxDownCm)
{
	FWbFootIkOffsets Out;
	for (int32 I = 0; I < 2; ++I)
	{
		const float Height = FootGroundZ[I] - CenterGroundZ;
		// Weiter als erlaubt: eine Kante (Fuss ueber dem Abgrund) oder eine
		// Wand, kein Boden - der Fuss bleibt, wo die Animation ihn hat.
		Out.FootCm[I] = (bHit[I] && Height >= -MaxDownCm && Height <= MaxUpCm) ? Height : 0.0f;
	}
	Out.PelvisCm = FMath::Min(Out.FootCm[0], Out.FootCm[1]);
	return Out;
}

void UWiesbadenSebboFigureComponent::UpdateFootIk(float DeltaSeconds, const FWbFigureInput& Input)
{
	UWorld* World = GetWorld();
	// In der Luft und auf dem Wagen gibt es keinen Boden unter den Fuessen.
	const bool bWant = bFootIk && World && !Input.bAirborne && !Input.bRiding
		&& CurrentMove != EWbSebboMove::Jump && CurrentMove != EWbSebboMove::Surf;

	// Boden unter der Figur (dort steht die Kapsel) und unter jedem Fuss -
	// Fusslage aus dem letzten Bild; senkrecht, also zaehlt nur X/Y.
	const FVector Root = GetComponentLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbFussIk), false, GetOwner());
	const auto Ground = [&](const FVector& At, float& OutZ, FVector& OutNormal)
	{
		FHitResult Hit;
		const FVector Start(At.X, At.Y, Root.Z + FootIkMaxUpCm + 40.0f);
		const FVector End(At.X, At.Y, Root.Z - FootIkMaxDownCm - 40.0f);
		if (!World || !World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, Params))
		{
			return false;
		}
		OutZ = Hit.ImpactPoint.Z;
		OutNormal = Hit.ImpactNormal;
		return true;
	};
	static const FName FootBones[2] = { TEXT("foot_l"), TEXT("foot_r") };
	float CenterZ = 0.0f;
	FVector CenterNormal = FVector::UpVector;
	const bool bCenter = Ground(Root, CenterZ, CenterNormal);
	bool bHit[2] = { false, false };
	float FootZ[2] = { 0.0f, 0.0f };
	FVector Normal[2] = { FVector::UpVector, FVector::UpVector };
	for (int32 I = 0; I < 2; ++I)
	{
		const FVector Ankle = GetSocketLocation(FootBones[I]);
		bHit[I] = Ground(Ankle, FootZ[I], Normal[I]);
		FootAboveGroundCm[I] = bHit[I] ? Ankle.Z - FootZ[I] : -1.0f;
	}

	const bool bActive = bWant && bCenter;
	const FWbFootIkOffsets Target = bActive
		? ComputeFootIk(CenterZ, bHit, FootZ, FootIkMaxUpCm, FootIkMaxDownCm) : FWbFootIkOffsets();
	// Geglaettet: ein Fuss, der ueber eine Stufenkante gleitet, springt sonst.
	constexpr float Follow = 14.0f;
	FootIkNow.PelvisCm = FMath::FInterpTo(FootIkNow.PelvisCm, Target.PelvisCm, DeltaSeconds, Follow);
	for (int32 I = 0; I < 2; ++I)
	{
		FootIkNow.FootCm[I] = FMath::FInterpTo(FootIkNow.FootCm[I], Target.FootCm[I], DeltaSeconds, Follow);
		const FVector Wanted = (bActive && bHit[I]) ? Normal[I] : FVector::UpVector;
		FootIkNormal[I] = FMath::VInterpTo(FootIkNormal[I], Wanted, DeltaSeconds, Follow).GetSafeNormal();
	}
	FootIkAlpha = FMath::FInterpTo(FootIkAlpha, bActive ? 1.0f : 0.0f, DeltaSeconds, 10.0f);

	if (UWiesbadenSebboAnimInstance* Instance = Cast<UWiesbadenSebboAnimInstance>(GetAnimInstance()))
	{
		const FTransform& ToComponent = GetComponentTransform();
		FWbFootIkPose Pose;
		Pose.Alpha = FootIkAlpha;
		Pose.PelvisOffset = ToComponent.InverseTransformVector(FVector(0.0, 0.0, FootIkNow.PelvisCm));
		for (int32 I = 0; I < 2; ++I)
		{
			Pose.FootOffset[I] = ToComponent.InverseTransformVector(FVector(0.0, 0.0, FootIkNow.FootCm[I]));
			Pose.GroundNormal[I] = ToComponent.InverseTransformVectorNoScale(FootIkNormal[I]);
		}
		Pose.Up = ToComponent.InverseTransformVectorNoScale(FVector::UpVector);
		Pose.Forward = ToComponent.InverseTransformVectorNoScale(
			GetOwner() ? GetOwner()->GetActorForwardVector() : GetForwardVector());
		Instance->SetFootIk(Pose);
	}
}

void UWiesbadenSebboFigureComponent::AdvanceMixer(float DeltaSeconds)
{
	Mixer.Advance(DeltaSeconds, [this](EWbSebboMove Move) { return MoveLength(Move); });
	if (UWiesbadenSebboAnimInstance* Instance = Cast<UWiesbadenSebboAnimInstance>(GetAnimInstance()))
	{
		TArray<FWbSebboPoseSample> Samples;
		Samples.Reserve(Mixer.Layers.Num());
		for (const FWbSebboLayer& Layer : Mixer.Layers)
		{
			if (HasMove(Layer.Move))
			{
				FWbSebboPoseSample& Sample = Samples.AddDefaulted_GetRef();
				Sample.Sequence = Moves[static_cast<int32>(Layer.Move)];
				Sample.Time = Layer.Time;
				Sample.bLoop = Layer.bLoop;
				Sample.Weight = Layer.Weight;
			}
		}
		Instance->SetSamples(MoveTemp(Samples));
	}
}

void UWiesbadenSebboFigureComponent::ChooseAndPlay(float DeltaSeconds, const FWbFigureInput& Input)
{
	// Drehrate aus der Blickrichtung, geglaettet: die Maus liefert je Bild
	// sprunghafte Werte, und ohne Glaettung flackerte Turn/Idle.
	const float RawRate = bHasPreviousYaw
		? FMath::FindDeltaAngleDegrees(PreviousYaw, Input.YawDeg) / DeltaSeconds : 0.0f;
	PreviousYaw = Input.YawDeg;
	bHasPreviousYaw = true;
	YawRateDegS = FMath::Lerp(YawRateDegS, RawRate, FMath::Clamp(DeltaSeconds / 0.15f, 0.0f, 1.0f));

	// Ein Bild ohne Vorankommen (Bordsteinkante, Stufe) ist kein Anhalten:
	// sonst sprang die Figur mitten im Rennen fuer ein Bild ins Idle, und der
	// Laufzyklus begann danach von vorn. Erst 0,2 s ohne Vorankommen gelten
	// als Stehen.
	float SpeedMps = Input.SpeedMps;
	if (SpeedMps > 0.4f)
	{
		StopSeconds = 0.0f;
		LastMovingSpeedMps = SpeedMps;
	}
	else
	{
		StopSeconds += DeltaSeconds;
		if (StopSeconds < 0.2f && (CurrentMove == EWbSebboMove::Walk || CurrentMove == EWbSebboMove::Run
			|| CurrentMove == EWbSebboMove::CrouchWalk))
		{
			SpeedMps = LastMovingSpeedMps;
		}
	}

	const bool bStill = SpeedMps <= 0.4f && FMath::Abs(YawRateDegS) < 25.0f && !Input.bAirborne
		&& !Input.bCrouching;
	StillSeconds = bStill ? StillSeconds + DeltaSeconds : 0.0f;

	// Trefferreaktion bei jedem Abfall der Gesundheit. Heute kennt das Spiel
	// noch keinen Schaden am Spieler (Gesundheit steigt nur ueber Pickups) -
	// die Bewegung steht bereit, sobald es einen gibt.
	if (LastHealthPoints >= 0.0f && Input.HealthPoints < LastHealthPoints - 0.5f)
	{
		PlayOneShot(EWbSebboMove::Hit, MoveLength(EWbSebboMove::Hit));
	}
	LastHealthPoints = Input.HealthPoints;

	// Waehrend Tritt oder Treffer nichts ueberschreiben.
	if (OneShotRemaining > 0.0f)
	{
		OneShotRemaining = FMath::Max(0.0f, OneShotRemaining - DeltaSeconds);
		return;
	}
	OneShotMove = EWbSebboMove::Count;

	FWbSebboMoveState State;
	State.SpeedMps = SpeedMps;
	State.YawRateDegS = YawRateDegS;
	State.StillSeconds = StillSeconds;
	State.bAirborne = Input.bAirborne;
	State.bRiding = Input.bRiding;
	State.bCrouching = Input.bCrouching;
	State.Previous = CurrentMove;
	EWbSebboMove Move = ChooseMove(State, RunFromMps,
		MoveLength(EWbSebboMove::Swagger), MoveLength(EWbSebboMove::Call));
	if (!HasMove(Move))
	{
		Move = EWbSebboMove::Idle;   // Zugabe fehlt: stehen statt T-Haltung
	}

	switch (Move)
	{
	case EWbSebboMove::Walk:
	case EWbSebboMove::CrouchWalk:   // gebaut auf den Fussbahnen von Walk: gleicher Schritt
		// Schritttakt ans Tempo: sonst glitten die Fuesse ueber den Asphalt.
		PlayMove(Move, true, FMath::Clamp(SpeedMps / WalkAnimSpeedMps, 0.5f, 2.0f));
		break;
	case EWbSebboMove::Run:
		PlayMove(Move, true, FMath::Clamp(SpeedMps / RunAnimSpeedMps, 0.6f, 1.6f));
		break;
	case EWbSebboMove::Jump:
		// Einmal je Luftphase. Die Aufnahme (2,2 s) zeigt Absprung, Flug und
		// Landung; die Flugzeit bekommt 0,4 s fuer Absprung und Landung dazu.
		PlayMove(Move, false, FMath::Clamp(MoveLength(Move) / (JumpAirSeconds + 0.4f), 1.0f, 2.5f));
		break;
	default:
		PlayMove(Move, true, 1.0f);
		break;
	}
}

void UWiesbadenSebboFigureComponent::PlayMove(EWbSebboMove Move, bool bLoop, float PlayRate)
{
	if (!HasMove(Move))
	{
		return;
	}
	if (Move != CurrentMove)
	{
		// Gangzyklen uebernehmen die Schrittphase des bisherigen Clips.
		const FWbSebboLayer* Before = Mixer.Top();
		const EWbSebboMove From = Before ? Before->Move : EWbSebboMove::Count;
		float StartTime = 0.0f;
		if (Before && MoveLength(From) > KINDA_SMALL_NUMBER)
		{
			const float Phase = GaitStartFor(From, Before->Time / MoveLength(From), Move);
			if (Phase >= 0.0f)
			{
				StartTime = Phase * MoveLength(Move);
			}
		}
		Mixer.Start(Move, bLoop, PlayRate, BlendSecondsFor(From, Move), StartTime);
		CurrentMove = Move;
	}
	else if (FWbSebboLayer* Top = Mixer.Top())
	{
		Top->PlayRate = PlayRate;
	}
}

float UWiesbadenSebboFigureComponent::BlendSecondsFor(EWbSebboMove From, EWbSebboMove To)
{
	using EM = EWbSebboMove;
	if (From == To || From == EM::Count)
	{
		return 0.0f;
	}
	if (To == EM::Jump)
	{
		return 0.1f;
	}
	if (From == EM::Jump)
	{
		return 0.15f;
	}
	if (To == EM::Kick || To == EM::Hit)
	{
		return 0.08f;
	}
	if (From == EM::Kick || From == EM::Hit)
	{
		return 0.2f;
	}
	if ((From == EM::Walk && To == EM::Run) || (From == EM::Run && To == EM::Walk))
	{
		return 0.25f;
	}
	if (From == EM::Swagger || From == EM::Call || To == EM::Swagger || To == EM::Call)
	{
		return 0.35f;
	}
	return 0.2f;
}

float UWiesbadenSebboFigureComponent::GaitStartFor(EWbSebboMove From, float FromNormalizedTime, EWbSebboMove To)
{
	// Gemessen am exportierten Skelett (SK_Sebbo.fbx, Abstand foot_l - foot_r
	// entlang der Blickrichtung, .planning/sebbo-blend/fussphase.py): jeder
	// Clip enthaelt ZWEI Gangzyklen; der linke Fuss liegt am weitesten vorn
	// bei Walk 0,384/0,884 (Bild 22,5 und 50,5 von 56), Run 0,167/0,667
	// (Bild 6 und 21 von 30). CrouchWalk ist auf den Fussbahnen von Walk gebaut.
	struct FGait { float Cycles; float LeftFront; };
	const auto GaitOf = [](EWbSebboMove Move, FGait& Out)
	{
		switch (Move)
		{
		case EWbSebboMove::Walk:
		case EWbSebboMove::CrouchWalk: Out = { 2.0f, 0.384f }; return true;
		case EWbSebboMove::Run:        Out = { 2.0f, 0.167f }; return true;
		default:                       return false;
		}
	};
	FGait A, B;
	if (!GaitOf(From, A) || !GaitOf(To, B))
	{
		return -1.0f;
	}
	// Schrittphase (0 = links vorn) UND welcher der Zyklen im Clip gerade
	// laeuft - so bleibt Gehen -> Duckgehen (gleiche Fussbahnen) bildgenau.
	const float Steps = (FromNormalizedTime - A.LeftFront) * A.Cycles;
	const float Cycle = FMath::FloorToFloat(Steps);
	const float Phase = Steps - Cycle;
	const float Copy = FMath::Fmod(FMath::Fmod(Cycle, B.Cycles) + B.Cycles, B.Cycles);
	return FMath::Frac(B.LeftFront + (Copy + Phase) / B.Cycles);
}

EWbSebboMove UWiesbadenSebboFigureComponent::ChooseMove(const FWbSebboMoveState& State, float RunFromMps,
	float SwaggerSeconds, float CallSeconds)
{
	if (State.bRiding)
	{
		return EWbSebboMove::Surf;
	}
	if (State.bAirborne)
	{
		return EWbSebboMove::Jump;
	}
	if (State.bCrouching)
	{
		return State.SpeedMps > 0.4f ? EWbSebboMove::CrouchWalk : EWbSebboMove::CrouchIdle;
	}
	if (State.SpeedMps > RunFromMps)
	{
		return EWbSebboMove::Run;
	}
	if (State.SpeedMps > 0.4f)
	{
		return EWbSebboMove::Walk;
	}
	// Drehen im Stand, mit Hysterese: an ab 60 Grad/s, aus unter 25.
	const float TurnFrom = State.Previous == EWbSebboMove::Turn ? 25.0f : 60.0f;
	if (FMath::Abs(State.YawRateDegS) >= TurnFrom)
	{
		return EWbSebboMove::Turn;
	}

	// Stehen: Idle, ab 10 s einmal Stolzieren, ab 30 s das Telefonat, dann
	// von vorn.
	constexpr float SwaggerAt = 10.0f;
	constexpr float CallAt = 30.0f;
	const float Period = CallAt + FMath::Max(CallSeconds, 0.0f);
	const float T = FMath::Fmod(FMath::Max(State.StillSeconds, 0.0f), Period);
	if (T >= SwaggerAt && T < SwaggerAt + SwaggerSeconds)
	{
		return EWbSebboMove::Swagger;
	}
	if (T >= CallAt && CallSeconds > 0.0f)
	{
		return EWbSebboMove::Call;
	}
	return EWbSebboMove::Idle;
}

FString UWiesbadenSebboFigureComponent::MoveName(EWbSebboMove Move)
{
	return StaticEnum<EWbSebboMove>()->GetNameStringByValue(static_cast<int64>(Move));
}
