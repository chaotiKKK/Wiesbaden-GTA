// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.
//
// Dennos Arbeitstag im Laden: Denno (sie) mit Skelett laeuft hektisch zwischen
// Cafe und Friseur hin und her - fegt, wischt Tische, raeumt auf, schneidet
// Friseurgaesten die Haare und bringt Cafegaesten ihre Tasse. Beim Annehmen
// eines Lieferauftrags reicht sie das Paket an der Cafetuer und zwinkert.
// Die Planung (Wege, Plaetze, Vorrang) ist datenrein in WiesbadenDennoWork;
// hier wird sie ausgefuehrt: bewegen, Bewegungen spielen, Requisiten fuehren.

#include "World/WiesbadenDennoShop.h"

#include "World/WiesbadenDeliveryCustomer.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInterface.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbDennoLife, Log, All);

namespace
{
	const TCHAR* DennoMesh = TEXT("/Game/Assets/People/Denno/Meshes/SK_Denno.SK_Denno");
	const TCHAR* DennoAnimPaths[static_cast<int32>(EDennoAnim::Count)] = {
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Idle.A_Denno_Idle"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Walk.A_Denno_Walk"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_WalkCarry.A_Denno_WalkCarry"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Sweep.A_Denno_Sweep"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Wipe.A_Denno_Wipe"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Tidy.A_Denno_Tidy"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_CutHair.A_Denno_CutHair"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Serve.A_Denno_Serve"),
		TEXT("/Game/Assets/People/Denno/Animations/A_Denno_Handover.A_Denno_Handover"),
	};
	/** Tischplatte der Bistrotische (build_denno_shop.py: 0,72-0,76 m). */
	constexpr double TableTopCm = 76.0;
	/** Der Schrittzyklus jeder Kundenfigur ist fuer 1,30 m/s gebaut (build_customer_figure.py). */
	constexpr double GuestWalkAnimSpeedCmS = WiesbadenDennoDelivery::CustomerWalkAnimSpeedCmS;
	/** Ein Friseurgast steht nach dem Haarschnitt noch kurz auf (s). */
	constexpr double SalonAfterCutSeconds = 2.0;
	/** Gaeste kommen eher ins Cafe als zum Friseur. */
	constexpr float SalonGuestShare = 0.4f;

	const TCHAR* TaskName(EDennoTask Task)
	{
		switch (Task)
		{
		case EDennoTask::Sweep: return TEXT("fegt");
		case EDennoTask::Wipe: return TEXT("wischt einen Tisch");
		case EDennoTask::Tidy: return TEXT("raeumt auf");
		case EDennoTask::CutHair: return TEXT("schneidet Haare");
		case EDennoTask::Fetch: return TEXT("holt eine Bestellung");
		case EDennoTask::Serve: return TEXT("serviert");
		case EDennoTask::Handover: return TEXT("bringt das Lieferpaket");
		default: return TEXT("steht");
		}
	}

	/** Gierwinkel (Grad) einer Laden-lokalen Richtung. */
	double YawOf(const FVector2D& Dir)
	{
		return FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	}
}

const TCHAR* AWiesbadenDennoShop::DennoMeshPath()
{
	return DennoMesh;
}

const TCHAR* AWiesbadenDennoShop::DennoAnimPath(EDennoAnim Anim)
{
	return DennoAnimPaths[FMath::Clamp(static_cast<int32>(Anim), 0, static_cast<int32>(EDennoAnim::Count) - 1)];
}

bool AWiesbadenDennoShop::CreateLife()
{
	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, DennoMeshPath());
	DennoAnims.SetNumZeroed(static_cast<int32>(EDennoAnim::Count));
	int32 Missing = Mesh ? 0 : 1;
	for (int32 I = 0; I < DennoAnims.Num(); ++I)
	{
		DennoAnims[I] = LoadObject<UAnimSequence>(nullptr, DennoAnimPath(static_cast<EDennoAnim>(I)));
		Missing += DennoAnims[I] ? 0 : 1;
	}
	// Nur vollstaendig: eine Denno, die in Grundhaltung durch den Laden
	// gleitet, waere schlechter als die atmende Figur ohne Skelett.
	if (Missing > 0)
	{
		UE_LOG(LogWbDennoLife, Warning,
			TEXT("Denno mit Skelett unvollstaendig (%d Teile fehlen, Tools/import_tripo_figure.py WB_FIGUR=Denno) - statische Figur."),
			Missing);
		return false;
	}
	DennoSkel = NewObject<USkeletalMeshComponent>(this, TEXT("DennoSkel"));
	DennoSkel->SetupAttachment(Root);
	DennoSkel->SetSkeletalMeshAsset(Mesh);
	DennoSkel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	DennoSkel->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	DennoSkel->RegisterComponent();

	const FDennoSpot Rest = WiesbadenDennoWork::RestSpot();
	DennoPos = Rest.Pos;
	DennoYaw = Rest.YawDeg;
	DennoNode = Rest.Node;
	DennoSkel->SetRelativeLocationAndRotation(FVector(DennoPos, 0.0), FRotator(0.0, DennoYaw, 0.0));
	PlayDenno(EDennoAnim::Idle, true);
	LifeRandom.Initialize(0x0D3770);

	PropBroomStick = AddProp(TEXT("PropBroomStick"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("M_Denno_Oak"));
	PropBroomHead = AddProp(TEXT("PropBroomHead"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("M_Denno_Anthracite"));
	PropTray = AddProp(TEXT("PropTray"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("M_Denno_Chrome"));
	PropTrayCup = AddProp(TEXT("PropTrayCup"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("M_Denno_White"));
	PropPackage = AddProp(TEXT("PropPackage"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("M_Denno_Oak"));
	PropScissors = AddProp(TEXT("PropScissors"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("M_Denno_Chrome"));
	PropCloth = AddProp(TEXT("PropCloth"), TEXT("/Engine/BasicShapes/Cube.Cube"), TEXT("M_Denno_Teal"));

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	NextGuestSeconds = Now + 4.0;   // der erste Gast kommt bald - der Laden soll nicht leer wirken
	UE_LOG(LogWbDennoLife, Log, TEXT("Denno mit Skelett und %d Bewegungen; %d Kundenfiguren als Gaeste."),
		DennoAnims.Num(), GetCustomerFigures().Num());
	return true;
}

const TArray<FWbCustomerFigure>& AWiesbadenDennoShop::GetCustomerFigures()
{
	if (!bCustomerFiguresLoaded)
	{
		bCustomerFiguresLoaded = true;
		CustomerFigures = WiesbadenCustomerFigures::LoadAll();
	}
	return CustomerFigures;
}

UStaticMeshComponent* AWiesbadenDennoShop::AddProp(FName Name, const TCHAR* ShapePath, const TCHAR* MaterialName)
{
	UStaticMeshComponent* Prop = NewObject<UStaticMeshComponent>(this, Name);
	Prop->SetupAttachment(Root);
	Prop->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, ShapePath));
	const FString MaterialPath = FString::Printf(TEXT("/Game/Buildings/DennoShop/Materials/%s.%s"), MaterialName, MaterialName);
	if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath))
	{
		Prop->SetMaterial(0, Material);
	}
	Prop->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Prop->SetVisibility(false);
	Prop->RegisterComponent();
	return Prop;
}

void AWiesbadenDennoShop::PlayDenno(EDennoAnim Anim, bool bLoop, float Rate)
{
	UAnimSequence* Seq = DennoAnims.IsValidIndex(static_cast<int32>(Anim)) ? DennoAnims[static_cast<int32>(Anim)] : nullptr;
	if (!DennoSkel || !Seq)
	{
		return;
	}
	if (PlayingAnim != Anim || bPlayingLoop != bLoop || !bLoop)
	{
		DennoSkel->PlayAnimation(Seq, bLoop);
		PlayingAnim = Anim;
		bPlayingLoop = bLoop;
	}
	DennoSkel->SetPlayRate(Rate);
}

FVector AWiesbadenDennoShop::ShopToWorld(const FVector2D& Local, double Z) const
{
	return GetActorTransform().TransformPosition(FVector(Local, Z));
}

FVector AWiesbadenDennoShop::HandCentre(bool bRight) const
{
	const FVector Wrist = DennoSkel->GetBoneLocation(bRight ? TEXT("Hand_R") : TEXT("Hand_L"));
	const FVector Elbow = DennoSkel->GetBoneLocation(bRight ? TEXT("LowerArm_R") : TEXT("LowerArm_L"));
	return Wrist + (Wrist - Elbow).GetSafeNormal() * 8.0;
}

void AWiesbadenDennoShop::QueueHandover()
{
	if (!DennoSkel)
	{
		return;
	}
	bPendingHandover = true;
	// Bei der Arbeit sofort los; im Gehen am naechsten Wegpunkt.
	if (!bWalking)
	{
		NextTask();
	}
}

TArray<FDennoGuestView> AWiesbadenDennoShop::GuestViews() const
{
	TArray<FDennoGuestView> Views;
	for (const FDennoGuest& Guest : Guests)
	{
		FDennoGuestView View;
		View.Seat = Guest.Seat;
		View.bSeated = Guest.Phase == FDennoGuest::EPhase::Seated;
		View.bServed = Guest.bServed;
		Views.Add(View);
	}
	return Views;
}

FDennoGuest* AWiesbadenDennoShop::GuestAtSeat(int32 Seat)
{
	return Guests.FindByPredicate([Seat](const FDennoGuest& G) { return G.Seat == Seat; });
}

void AWiesbadenDennoShop::NextTask()
{
	if (bPendingHandover)
	{
		bPendingHandover = false;
		FDennoTaskPick Pick;
		Pick.Task = EDennoTask::Handover;
		Pick.Spot = WiesbadenDennoWork::HandoverSpot();
		StartTask(Pick);
		return;
	}
	int32 SpotIndex = INDEX_NONE;
	const FDennoTaskPick Pick = WiesbadenDennoWork::PickTask(GuestViews(), LastSpotIndex, LifeRandom, SpotIndex);
	if (SpotIndex != INDEX_NONE)
	{
		LastSpotIndex = SpotIndex;
	}
	StartTask(Pick);
}

void AWiesbadenDennoShop::StartTask(const FDennoTaskPick& Pick)
{
	CurrentPick = Pick;
	TaskElapsed = 0.0;
	bReleased = false;
	UE_LOG(LogWbDennoLife, Log, TEXT("Denno %s (%.0f, %.0f)."), TaskName(Pick.Task), Pick.Spot.Pos.X, Pick.Spot.Pos.Y);
	WalkTo(Pick.Spot);
}

void AWiesbadenDennoShop::WalkTo(const FDennoSpot& Spot)
{
	// Immer ueber das Wegenetz: erst zurueck zum eigenen Knoten, dann die
	// Knotenkette, zuletzt gerade auf den Platz - jede dieser Strecken ist
	// gegen die Moebel geprueft (WiesbadenReal.World.DennoWork.Paths).
	WalkPath.Reset();
	const FVector2D NodePos = WiesbadenDennoWork::Nodes()[DennoNode];
	if (FVector2D::Distance(DennoPos, NodePos) > 5.0)
	{
		WalkPath.Add(NodePos);
	}
	WalkPath.Append(WiesbadenDennoWork::PathFromNode(DennoNode, Spot.Node, { Spot.Pos }));
	WalkIndex = 0;
	bWalking = true;
	const bool bCarry = CurrentPick.Task == EDennoTask::Serve || CurrentPick.Task == EDennoTask::Handover;
	PlayDenno(bCarry ? EDennoAnim::WalkCarry : EDennoAnim::Walk, true,
		static_cast<float>(WiesbadenDennoWork::WalkSpeedCmS / WiesbadenDennoWork::WalkAnimSpeedCmS));
}

void AWiesbadenDennoShop::TickLife(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	if (!World || !DennoSkel)
	{
		return;
	}
	if (CurrentPick.Task == EDennoTask::Idle && !bWalking)
	{
		NextTask();
	}

	double TargetYaw = DennoYaw;
	if (bWalking)
	{
		double Step = WiesbadenDennoWork::WalkSpeedCmS * DeltaSeconds;
		while (Step > 0.0 && WalkPath.IsValidIndex(WalkIndex))
		{
			const FVector2D ToTarget = WalkPath[WalkIndex] - DennoPos;
			const double Left = ToTarget.Size();
			if (Left > 1.0)
			{
				TargetYaw = YawOf(ToTarget);
			}
			if (Left > Step)
			{
				DennoPos += ToTarget / Left * Step;
				break;
			}
			DennoPos = WalkPath[WalkIndex];
			Step -= Left;
			++WalkIndex;
			// An einem Knoten angekommen? Dann ist er der neue Ausgangspunkt -
			// und eine vorgemerkte Uebergabe darf hier unterbrechen.
			const int32 NodeHere = WiesbadenDennoWork::Nodes().IndexOfByPredicate(
				[this](const FVector2D& N) { return FVector2D::Distance(N, DennoPos) < 1.0; });
			if (NodeHere != INDEX_NONE)
			{
				DennoNode = NodeHere;
				if (bPendingHandover && CurrentPick.Task != EDennoTask::Handover)
				{
					NextTask();
					return;
				}
			}
		}
		if (!WalkPath.IsValidIndex(WalkIndex))
		{
			// Am Platz: arbeiten.
			bWalking = false;
			DennoNode = CurrentPick.Spot.Node;
			TaskElapsed = 0.0;
			PlayDenno(WiesbadenDennoWork::WorkAnim(CurrentPick.Task), !WiesbadenDennoWork::IsOneShot(CurrentPick.Task));
		}
	}
	else
	{
		TaskElapsed += DeltaSeconds;
		TargetYaw = CurrentPick.Spot.YawDeg;
		if (CurrentPick.Task == EDennoTask::Handover)
		{
			// Zum Spieler drehen, wenn er vor dem Laden steht, sonst zur Strasse.
			const APlayerController* PC = World->GetFirstPlayerController();
			if (const APawn* Player = PC ? PC->GetPawn() : nullptr)
			{
				const FVector Local = GetActorTransform().InverseTransformPosition(Player->GetActorLocation());
				const FVector2D To = FVector2D(Local.X, Local.Y) - DennoPos;
				if (To.Size() < 2000.0 && Local.Y < 0.0)
				{
					TargetYaw = YawOf(To);
				}
			}
		}
		const UAnimSequence* Seq = DennoAnims[static_cast<int32>(WiesbadenDennoWork::WorkAnim(CurrentPick.Task))];
		const double OneShotLength = Seq ? Seq->GetPlayLength() : 1.0;
		switch (CurrentPick.Task)
		{
		case EDennoTask::Serve:
			if (!bReleased && TaskElapsed >= WiesbadenDennoWork::ServeReleaseSeconds)
			{
				bReleased = true;
				if (FDennoGuest* Guest = GuestAtSeat(CurrentPick.Seat))
				{
					Guest->bServed = true;
					Guest->Timer = 0.0;
					Guest->Linger = LifeRandom.FRandRange(WiesbadenDennoWork::CafeLingerMinSeconds,
						WiesbadenDennoWork::CafeLingerMaxSeconds);
					if (!Guest->Cup)
					{
						Guest->Cup = AddProp(NAME_None, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"), TEXT("M_Denno_White"));
						GuestComponents.Add(Guest->Cup);
					}
					const FVector2D CupPos = WiesbadenDennoWork::Seats()[CurrentPick.Seat].CupPos;
					Guest->Cup->SetRelativeLocation(FVector(CupPos, TableTopCm + 4.5));
					Guest->Cup->SetRelativeScale3D(FVector(0.08, 0.08, 0.09));
					Guest->Cup->SetVisibility(true);
					UE_LOG(LogWbDennoLife, Log, TEXT("Denno serviert an Tisch %d."), CurrentPick.Seat + 1);
				}
			}
			if (TaskElapsed >= OneShotLength)
			{
				NextTask();
			}
			break;
		case EDennoTask::Handover:
			if (!bReleased && TaskElapsed >= WiesbadenDennoWork::HandoverReleaseSeconds)
			{
				bReleased = true;
				UE_LOG(LogWbDennoLife, Log, TEXT("Denno reicht das Lieferpaket und zwinkert."));
			}
			if (TaskElapsed >= OneShotLength)
			{
				NextTask();
			}
			break;
		case EDennoTask::Fetch:
			if (TaskElapsed >= CurrentPick.Seconds)
			{
				// Bestellung geholt: an den Tisch bringen - sitzt der Gast noch.
				const FDennoGuest* Guest = GuestAtSeat(CurrentPick.Seat);
				if (Guest && Guest->Phase == FDennoGuest::EPhase::Seated && !Guest->bServed)
				{
					FDennoTaskPick Serve;
					Serve.Task = EDennoTask::Serve;
					Serve.Seat = CurrentPick.Seat;
					Serve.Spot = WiesbadenDennoWork::Seats()[CurrentPick.Seat].Work;
					StartTask(Serve);
				}
				else
				{
					NextTask();
				}
			}
			break;
		case EDennoTask::CutHair:
			if (TaskElapsed >= CurrentPick.Seconds)
			{
				if (FDennoGuest* Guest = GuestAtSeat(CurrentPick.Seat))
				{
					Guest->bServed = true;
					Guest->Timer = 0.0;
					UE_LOG(LogWbDennoLife, Log, TEXT("Denno ist mit dem Haarschnitt fertig."));
				}
				NextTask();
			}
			break;
		default:
			if (TaskElapsed >= CurrentPick.Seconds)
			{
				NextTask();
			}
			break;
		}
	}
	DennoYaw = FMath::FixedTurn(static_cast<float>(DennoYaw), static_cast<float>(TargetYaw),
		static_cast<float>(WiesbadenDennoWork::TurnRateDegS * DeltaSeconds));
	DennoSkel->SetRelativeLocationAndRotation(FVector(DennoPos, 0.0), FRotator(0.0, DennoYaw, 0.0));
	UpdateProps();
	TickGuests(DeltaSeconds);
}

void AWiesbadenDennoShop::UpdateProps()
{
	const bool bWorking = !bWalking;
	const EDennoTask Task = CurrentPick.Task;
	const bool bBroom = Task == EDennoTask::Sweep && bWorking;
	const bool bCloth = Task == EDennoTask::Wipe && bWorking;
	const bool bScissors = Task == EDennoTask::CutHair && bWorking;
	const bool bTray = Task == EDennoTask::Serve;
	const bool bTrayCup = bTray && !bReleased;
	const bool bPackage = Task == EDennoTask::Handover && !bReleased;
	PropBroomStick->SetVisibility(bBroom);
	PropBroomHead->SetVisibility(bBroom);
	PropCloth->SetVisibility(bCloth);
	PropScissors->SetVisibility(bScissors);
	PropTray->SetVisibility(bTray);
	PropTrayCup->SetVisibility(bTrayCup);
	PropPackage->SetVisibility(bPackage);
	if (!(bBroom || bCloth || bScissors || bTray || bPackage))
	{
		return;
	}
	// Requisiten folgen den Haenden der AKTUELLEN Pose (Knochen in Weltlage) -
	// so passen sie zu jeder Bewegung, ohne Sockel im Skelett.
	const FVector Left = HandCentre(false);
	const FVector Right = HandCentre(true);
	const FRotator Facing(0.0, DennoSkel->GetComponentRotation().Yaw, 0.0);
	if (bBroom)
	{
		// Stiel von der oberen (linken) durch die untere (rechte) Hand bis zum Boden.
		FVector Dir = (Right - Left).GetSafeNormal();
		if (Dir.Z > -0.3)
		{
			Dir = (Dir + FVector(0.0, 0.0, -1.0)).GetSafeNormal();
		}
		const double FloorZ = GetActorLocation().Z + 4.0;
		const FVector Top = Left - Dir * 12.0;
		const double Length = FMath::Clamp((Top.Z - FloorZ) / -Dir.Z, 60.0, 160.0);
		const FVector Bottom = Top + Dir * Length;
		PropBroomStick->SetWorldLocationAndRotation((Top + Bottom) * 0.5, FRotationMatrix::MakeFromZ(Dir).Rotator());
		PropBroomStick->SetWorldScale3D(FVector(0.028, 0.028, Length / 100.0));
		PropBroomHead->SetWorldLocationAndRotation(Bottom + FVector(0.0, 0.0, 2.0),
			FRotationMatrix::MakeFromXZ(DennoSkel->GetRightVector(), FVector::UpVector).Rotator());
		PropBroomHead->SetWorldScale3D(FVector(0.34, 0.07, 0.05));
	}
	if (bCloth)
	{
		PropCloth->SetWorldLocationAndRotation(Right - FVector(0.0, 0.0, 4.0), Facing);
		PropCloth->SetWorldScale3D(FVector(0.18, 0.14, 0.012));
	}
	if (bScissors)
	{
		const FVector HandDir = (Right - DennoSkel->GetBoneLocation(TEXT("Hand_R"))).GetSafeNormal();
		PropScissors->SetWorldLocationAndRotation(Right, FRotationMatrix::MakeFromZ(HandDir).Rotator());
		PropScissors->SetWorldScale3D(FVector(0.015, 0.035, 0.13));
	}
	const FVector Middle = (Left + Right) * 0.5;
	if (bTray)
	{
		PropTray->SetWorldLocationAndRotation(Middle + FVector(0.0, 0.0, 4.0), Facing);
		PropTray->SetWorldScale3D(FVector(0.36, 0.36, 0.02));
		PropTrayCup->SetWorldLocationAndRotation(Middle + FVector(0.0, 0.0, 9.5), Facing);
		PropTrayCup->SetWorldScale3D(FVector(0.08, 0.08, 0.09));
	}
	if (bPackage)
	{
		PropPackage->SetWorldLocationAndRotation(Middle + FVector(0.0, 0.0, 10.0), Facing);
		PropPackage->SetWorldScale3D(FVector(0.26, 0.36, 0.18));   // quer zwischen den Haenden
	}
}

void AWiesbadenDennoShop::SpawnGuest(bool bSalon)
{
	const int32 Seat = WiesbadenDennoWork::PickFreeSeat(GuestViews(), bSalon, LifeRandom);
	const TArray<FWbCustomerFigure>& Figures = GetCustomerFigures();
	if (Seat == INDEX_NONE || Figures.IsEmpty())
	{
		return;
	}
	// Nicht zweimal dieselbe Person hintereinander und moeglichst keine, die
	// schon im Laden ist.
	TArray<int32> Avoid = { LastGuestFigure };
	for (const FDennoGuest& Other : Guests)
	{
		Avoid.Add(Other.Figure);
	}
	const int32 FigureIndex = WiesbadenCustomerFigures::PickFigure(Figures.Num(), Avoid, LifeRandom.GetUnsignedInt());
	LastGuestFigure = FigureIndex;
	const FWbCustomerFigure& Look = Figures[FigureIndex];
	const FDennoSeat& S = WiesbadenDennoWork::Seats()[Seat];
	FDennoGuest Guest;
	Guest.Seat = Seat;
	Guest.Pos = WiesbadenDennoWork::DoorOutside(bSalon);
	const int32 Door = WiesbadenDennoWork::DoorNode(bSalon);
	Guest.Path = { WiesbadenDennoWork::Nodes()[Door] };
	Guest.Path.Append(WiesbadenDennoWork::PathFromNode(Door, S.Node, { S.Approach, S.Pos }));
	Guest.Yaw = 90.0;   // ins Haus
	Guest.Figure = FigureIndex;
	Guest.Mesh = NewObject<USkeletalMeshComponent>(this);
	Guest.Mesh->SetupAttachment(Root);
	Guest.Mesh->SetSkeletalMeshAsset(Look.Mesh);
	Guest.Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Guest.Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
	Guest.Mesh->RegisterComponent();
	Guest.Mesh->PlayAnimation(Look.Anim(ECustomerAnim::Walk), true);
	Guest.Mesh->SetPlayRate(static_cast<float>(WiesbadenDennoWork::GuestWalkSpeedCmS / GuestWalkAnimSpeedCmS));
	Guest.Mesh->SetRelativeLocationAndRotation(FVector(Guest.Pos, 0.0), FRotator(0.0, Guest.Yaw, 0.0));
	GuestComponents.Add(Guest.Mesh);
	Guests.Add(Guest);
	UE_LOG(LogWbDennoLife, Log, TEXT("Ein Gast (%s) kommt %s (Platz %d)."), *Look.Name,
		bSalon ? TEXT("zum Friseur") : TEXT("ins Cafe"), Seat + 1);
}

void AWiesbadenDennoShop::TickGuests(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	const APlayerController* PC = World->GetFirstPlayerController();
	const APawn* Player = PC ? PC->GetPawn() : nullptr;
	const bool bNear = Player && FVector::Dist2D(Player->GetActorLocation(), GetActorLocation()) < GuestRangeCm;
	if (bNear && Now >= NextGuestSeconds)
	{
		NextGuestSeconds = Now + LifeRandom.FRandRange(WiesbadenDennoWork::GuestArrivalMinSeconds,
			WiesbadenDennoWork::GuestArrivalMaxSeconds);
		int32 Cafe = 0, Salon = 0;
		for (const FDennoGuest& G : Guests)
		{
			(WiesbadenDennoWork::Seats()[G.Seat].bSalon ? Salon : Cafe) += 1;
		}
		const bool bWantSalon = LifeRandom.FRand() < SalonGuestShare;
		const bool bSalonFree = Salon < WiesbadenDennoWork::MaxSalonGuests;
		const bool bCafeFree = Cafe < WiesbadenDennoWork::MaxCafeGuests;
		if ((bWantSalon && bSalonFree) || (!bCafeFree && bSalonFree))
		{
			SpawnGuest(true);
		}
		else if (bCafeFree)
		{
			SpawnGuest(false);
		}
	}

	for (int32 I = Guests.Num() - 1; I >= 0; --I)
	{
		FDennoGuest& G = Guests[I];
		const FDennoSeat& Seat = WiesbadenDennoWork::Seats()[G.Seat];
		if (G.Phase == FDennoGuest::EPhase::Seated)
		{
			G.Timer += DeltaSeconds;
			const bool bDone = G.bServed && G.Timer >= (Seat.bSalon ? SalonAfterCutSeconds : G.Linger);
			const bool bGaveUp = !G.bServed && G.Timer >= WiesbadenDennoWork::GuestPatienceSeconds;
			if (bDone || bGaveUp)
			{
				// Aufstehen und gehen: ueber den Zugang zurueck zur Tuer.
				G.Phase = FDennoGuest::EPhase::Leaving;
				G.Path = { Seat.Approach };
				G.Path.Append(WiesbadenDennoWork::PathFromNode(Seat.Node, WiesbadenDennoWork::DoorNode(Seat.bSalon),
					{ WiesbadenDennoWork::DoorOutside(Seat.bSalon) }));
				G.PathIndex = 0;
				G.Mesh->PlayAnimation(CustomerFigures[G.Figure].Anim(ECustomerAnim::Walk), true);
				G.Mesh->SetPlayRate(static_cast<float>(WiesbadenDennoWork::GuestWalkSpeedCmS / GuestWalkAnimSpeedCmS));
				if (G.Cup)
				{
					G.Cup->SetVisibility(false);
				}
				UE_LOG(LogWbDennoLife, Log, TEXT("Gast von Platz %d geht%s."), G.Seat + 1,
					bGaveUp ? TEXT(" unbedient") : TEXT(""));
			}
			continue;
		}
		// Gehen (herein oder hinaus).
		double Step = WiesbadenDennoWork::GuestWalkSpeedCmS * DeltaSeconds;
		double TargetYaw = G.Yaw;
		while (Step > 0.0 && G.Path.IsValidIndex(G.PathIndex))
		{
			const FVector2D To = G.Path[G.PathIndex] - G.Pos;
			const double Left = To.Size();
			if (Left > 1.0)
			{
				TargetYaw = YawOf(To);
			}
			if (Left > Step)
			{
				G.Pos += To / Left * Step;
				break;
			}
			G.Pos = G.Path[G.PathIndex++];
			Step -= Left;
		}
		G.Yaw = FMath::FixedTurn(static_cast<float>(G.Yaw), static_cast<float>(TargetYaw), 360.0f * DeltaSeconds);
		double Lift = 0.0;
		if (!G.Path.IsValidIndex(G.PathIndex))
		{
			if (G.Phase == FDennoGuest::EPhase::Leaving)
			{
				G.Mesh->DestroyComponent();
				if (G.Cup)
				{
					G.Cup->DestroyComponent();
				}
				GuestComponents.Remove(G.Mesh);
				GuestComponents.Remove(G.Cup);
				Guests.RemoveAt(I);
				continue;
			}
			// Am Stuhl: hinsetzen, Blick zum Tisch bzw. zum Spiegel.
			G.Phase = FDennoGuest::EPhase::Seated;
			G.Timer = 0.0;
			G.Yaw = Seat.YawDeg;
			Lift = Seat.LiftCm;
			G.Mesh->PlayAnimation(CustomerFigures[G.Figure].Anim(ECustomerAnim::Sit), true);
			G.Mesh->SetPlayRate(1.0f);
			UE_LOG(LogWbDennoLife, Log, TEXT("Gast sitzt auf Platz %d."), G.Seat + 1);
		}
		G.Mesh->SetRelativeLocationAndRotation(FVector(G.Pos, Lift), FRotator(0.0, G.Yaw, 0.0));
	}
}
