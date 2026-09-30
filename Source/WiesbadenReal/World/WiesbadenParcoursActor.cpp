// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenParcoursActor.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/ConfigCacheIni.h"
#include "UObject/ConstructorHelpers.h"

#include "UI/WiesbadenVehicleHUD.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "WiesbadenReal.h"

TWeakObjectPtr<AWiesbadenParcours> AWiesbadenParcours::Zuletzt;

namespace
{
	// Leitkegel: 40 cm Fuss, 70 cm hoch (BasicShapes-Kegel ist 1 m, Drehpunkt mittig).
	const FVector KegelSkala(0.4, 0.4, 0.7);
	constexpr float KegelHalbeHoeheCm = 35.0f;
	constexpr float StartAbstandCm = 1000.0f;   // Startlinie 10 m vor dem Wagen
}

AWiesbadenParcours::AWiesbadenParcours()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Wurzel"));
	// Harte Referenz im Konstruktor: so kocht der Kegel mit ins Paket.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Kegelform(TEXT("/Engine/BasicShapes/Cone.Cone"));
	KegelMesh = Kegelform.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Leuchtfarbe(
		TEXT("/Game/Materials/City/M_WbLeitkegel.M_WbLeitkegel"));
	KegelLeuchtMaterial = Leuchtfarbe.Object;
}

AWiesbadenParcours* AWiesbadenParcours::Aktiver(const UWorld* World)
{
	AWiesbadenParcours* P = Zuletzt.Get();
	return (P && P->GetWorld() == World) ? P : nullptr;
}

void AWiesbadenParcours::Starten(APawn* Fahrzeug, int32 FahrerModus, bool bRegen)
{
	Wagen = Fahrzeug;
	bMitFahrer = FahrerModus != 0;
	bFahrerFehler = FahrerModus == 2;
	bRegenVerlangt = bRegen;
	bWarteAufNaesseGemeldet = false;
	bAufgebaut = false;
	RuhePos = Fahrzeug ? Fahrzeug->GetActorLocation() : FVector::ZeroVector;
	RuheGrip = BelagsGripVon(Fahrzeug);
	RuheSekunden = 0.0f;
	Zuletzt = this;
}

void AWiesbadenParcours::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (IWiesbadenVehicleControl* Ctrl = Cast<IWiesbadenVehicleControl>(Wagen.Get()))
	{
		if (bSteuertWagen)
		{
			Ctrl->ClearExternalControl();
			bSteuertWagen = false;
		}
	}
	Super::EndPlay(EndPlayReason);
}

FVector2D AWiesbadenParcours::InsParcours(const FVector& Welt) const
{
	const float Rad = FMath::DegreesToRadians(KursGrad);
	const FVector2D Vor(FMath::Cos(Rad), FMath::Sin(Rad));
	const FVector2D Rechts(-FMath::Sin(Rad), FMath::Cos(Rad));
	const FVector2D D = FVector2D(Welt.X, Welt.Y) - Ursprung;
	return FVector2D(FVector2D::DotProduct(D, Vor), FVector2D::DotProduct(D, Rechts));
}

FVector AWiesbadenParcours::InDieWelt(const FVector2D& Lokal) const
{
	const float Rad = FMath::DegreesToRadians(KursGrad);
	const FVector2D Vor(FMath::Cos(Rad), FMath::Sin(Rad));
	const FVector2D Rechts(-FMath::Sin(Rad), FMath::Cos(Rad));
	const FVector2D W = Ursprung + Vor * Lokal.X + Rechts * Lokal.Y;
	return FVector(W.X, W.Y, BodenBasisZ);
}

bool AWiesbadenParcours::BodenZ(const FVector2D& Lokal, float& OutZ) const
{
	const FVector W = InDieWelt(Lokal);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbParcoursBoden), false, this);
	if (Wagen.IsValid())
	{
		Params.AddIgnoredActor(Wagen.Get());
	}
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, W + FVector(0, 0, 3000), W - FVector(0, 0, 3000), ECC_Visibility, Params))
	{
		OutZ = static_cast<float>(Hit.ImpactPoint.Z);
		return true;
	}
	return false;
}

void AWiesbadenParcours::Aufbauen()
{
	APawn* Pawn = Wagen.Get();
	if (!Pawn || !KegelMesh)
	{
		return;
	}
	for (UStaticMeshComponent* Alt : Kegel)
	{
		if (Alt)
		{
			Alt->DestroyComponent();
		}
	}
	Kegel.Reset();

	// Strecke in Blickrichtung des Wagens, Startlinie 10 m voraus.
	KursGrad = static_cast<float>(Pawn->GetActorRotation().Yaw);
	const FVector PawnPos = Pawn->GetActorLocation();
	const float Rad = FMath::DegreesToRadians(KursGrad);
	Ursprung = FVector2D(PawnPos.X, PawnPos.Y) + FVector2D(FMath::Cos(Rad), FMath::Sin(Rad)) * StartAbstandCm;
	BodenBasisZ = static_cast<float>(PawnPos.Z);
	float Z = 0.0f;
	if (BodenZ(InsParcours(PawnPos), Z))
	{
		BodenBasisZ = Z;
	}
	SetActorLocation(InDieWelt(FVector2D::ZeroVector));

	Bewertung = FWbParcoursBewertung();
	Fahrer = FWbParcoursFahrer();
	Fahrer.bFehlerMachen = bFahrerFehler;
	if (const AWiesbadenCar* Car = Cast<AWiesbadenCar>(Pawn))
	{
		Fahrer.MaxLenkGrad = Car->VehiclePhysics.MaxSteerAngleDeg;
		Fahrer.LenkAbfallMS = Car->VehiclePhysics.SteerFalloffSpeedMetersPerS;
	}

	if (!KegelMaterial)
	{
		// Leitkegel sind fluoreszierend: mit dem reinen Grundmaterial standen sie
		// im Gegenlicht schwarz da und waren nachts unsichtbar, mit dem Lampenglas
		// in der Sonne milchweiss (feste Grundfarbe) - Clips vom 29.09.2026.
		// M_WbLeitkegel (Tools/create_leitkegel_material.py) nimmt Grundfarbe und
		// Leuchten aus derselben Farbe; fehlt es, bleibt das Grundmaterial.
		// Verkehrsorange; mit mehr Gruen oder Leuchten ueberstrahlt es in der Sonne gelb.
		const FLinearColor Orange(1.0f, 0.16f, 0.01f);
		if (KegelLeuchtMaterial)
		{
			KegelMaterial = UMaterialInstanceDynamic::Create(KegelLeuchtMaterial, this);
			KegelMaterial->SetVectorParameterValue(TEXT("Farbe"), Orange);
			KegelMaterial->SetScalarParameterValue(TEXT("Glow"), 0.25f);
		}
		else
		{
			KegelMaterial = UMaterialInstanceDynamic::Create(KegelMesh->GetMaterial(0), this);
			KegelMaterial->SetVectorParameterValue(TEXT("Color"), Orange);
		}
	}

	int32 BodenTreffer = 0;
	const TArray<FVector2D> Lagen = Bewertung.GetLayout().AlleKegel();
	for (const FVector2D& L : Lagen)
	{
		float KegelBoden = BodenBasisZ;
		BodenTreffer += BodenZ(L, KegelBoden) ? 1 : 0;
		UStaticMeshComponent* K = NewObject<UStaticMeshComponent>(this);
		K->SetMobility(EComponentMobility::Movable);
		K->SetStaticMesh(KegelMesh);
		K->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		K->SetGenerateOverlapEvents(false);
		if (KegelMaterial)
		{
			K->SetMaterial(0, KegelMaterial);
		}
		K->SetupAttachment(RootComponent);
		K->RegisterComponent();
		FVector W = InDieWelt(L);
		W.Z = KegelBoden + KegelHalbeHoeheCm;
		K->SetWorldLocationAndRotation(W, FRotator(0.0f, KursGrad, 0.0f));
		K->SetWorldScale3D(KegelSkala);
		Kegel.Add(K);
	}
	bAufgebaut = true;
	bHatLetztePos = false;
	TaktSekunden = 0.0f;
	NachZielSekunden = -1.0f;
	UE_LOG(LogWbCore, Log,
		TEXT("Parcours: aufgebaut - Startlinie (%.0f, %.0f, %.0f), Kurs %.0f Grad, %d Kegel, Boden gefunden %d/%d, Fahrer %d."),
		Ursprung.X, Ursprung.Y, BodenBasisZ, KursGrad, Kegel.Num(), BodenTreffer, Lagen.Num(), bMitFahrer ? (bFahrerFehler ? 2 : 1) : 0);
	Melden(TEXT("Durchs Starttor - Kegel 1 links umfahren"));
}

float AWiesbadenParcours::BelagsGripVon(const APawn* Pawn)
{
	const AWiesbadenCar* Car = Cast<AWiesbadenCar>(Pawn);
	return Car ? Car->VehiclePhysics.SurfaceGripScale : 1.0f;
}

void AWiesbadenParcours::KegelUmwerfen(int32 Index)
{
	if (!Kegel.IsValidIndex(Index) || !Kegel[Index] || !Wagen.IsValid())
	{
		return;
	}
	// Umgekippt in Fahrtrichtung des Wagens, ein Stueck weitergeschoben.
	const FRotator Fahrt(0.0f, Wagen->GetActorRotation().Yaw, 0.0f);
	FVector W = Kegel[Index]->GetComponentLocation() + Fahrt.Vector() * 60.0f;
	W.Z -= KegelHalbeHoeheCm - 20.0f;
	Kegel[Index]->SetWorldLocationAndRotation(W, FRotator(-90.0f, Fahrt.Yaw, 0.0f));
}

void AWiesbadenParcours::Melden(const FString& Text)
{
	LetzteMeldung = Text;
	LetzteMeldungAlter = 0.0f;
	UE_LOG(LogWbCore, Log, TEXT("Parcours: %s (t=%.1f s, %s)"),
		*Text, Bewertung.GetFahrzeit(), WbParcoursAbschnittName(Bewertung.GetAbschnitt()));
}

void AWiesbadenParcours::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APawn* Pawn = Wagen.Get();
	if (!Pawn)
	{
		return;
	}
	IWiesbadenVehicleControl* Ctrl = Cast<IWiesbadenVehicleControl>(Pawn);
	const FVector Pos = Pawn->GetActorLocation();
	LetzteMeldungAlter += DeltaSeconds;

	if (!bAufgebaut)
	{
		// Ruhig = Wagen UND Belag: blendet der Regen gerade ein (8 s), startete
		// die Runde halb trocken und zaehlte trocken - gemessen mit
		// -WbWeather=Rain, Aufbau nach 2 s: 47,6 s Regenfahrt, trocken gewertet.
		const float Grip = BelagsGripVon(Pawn);
		if (FVector::Dist(Pos, RuhePos) < 20.0f && FMath::Abs(Grip - RuheGrip) < 0.01f)
		{
			RuheSekunden += DeltaSeconds;
		}
		else
		{
			RuheSekunden = 0.0f;
			RuhePos = Pos;
			RuheGrip = Grip;
		}
		// Regen verlangt: erst aufbauen, wenn die Strasse nass ist.
		const bool bNass = Grip <= FWbParcoursBewertung::RegenGripBis;
		if (RuheSekunden >= 1.0f && bRegenVerlangt && !bNass && !bWarteAufNaesseGemeldet)
		{
			bWarteAufNaesseGemeldet = true;
			Melden(TEXT("Warte auf nasse Strasse"));
		}
		if (RuheSekunden >= 1.0f && (!bRegenVerlangt || bNass))
		{
			Aufbauen();
		}
		return;
	}

	FWbParcoursProbe P;
	P.PosCm = InsParcours(Pos);
	P.KursGrad = FMath::FindDeltaAngleDegrees(KursGrad, static_cast<float>(Pawn->GetActorRotation().Yaw));
	P.Kmh = Ctrl ? FMath::Abs(Ctrl->GetSpeedKmh()) : 0.0f;
	P.bHandbremse = Ctrl && Ctrl->IsHandbrakeApplied();
	P.DtSekunden = DeltaSeconds;
	P.BelagsGrip = BelagsGripVon(Pawn);

	// Vor dem Start VERSETZT (-WbGoto, WbTeleport - ein Sprung in einem Bild):
	// an der neuen Stelle neu aufbauen. Wegfahren vor dem Start ist erlaubt;
	// frueher zaehlte schon "200 m weg", und der Parcours landete an der
	// naechsten roten Ampel quer auf der Strasse (Review PR #27).
	const bool bVersetzt = bHatLetztePos && FVector::Dist2D(Pos, LetztePos) > 5000.0f;
	LetztePos = Pos;
	bHatLetztePos = true;
	if (Bewertung.GetAbschnitt() == EWbParcoursAbschnitt::Bereit && bVersetzt)
	{
		bAufgebaut = false;
		RuheSekunden = 0.0f;
		RuhePos = Pos;
		return;
	}

	const bool bWarFertig = Bewertung.IstFertig();
	Bewertung.Schritt(P);
	for (const int32 I : Bewertung.HoleNeuUmgefahrene())
	{
		KegelUmwerfen(I);
	}
	for (const FString& M : Bewertung.HoleNeueMeldungen())
	{
		Melden(M);
	}
	if (!bWarFertig && Bewertung.IstFertig())
	{
		AmZiel();
	}

	if (bMitFahrer && Ctrl)
	{
		if (!Bewertung.IstFertig())
		{
			const FWbParcoursSteuerung S = Fahrer.Steuern(P, Bewertung.GetAbschnitt());
			FWiesbadenCarControl C;
			C.Throttle = S.Gas;
			C.Brake = S.Bremse;
			C.Steering = S.Lenkung;
			C.bHandbrake = S.bHandbremse;
			Ctrl->SetExternalControl(C);
			bSteuertWagen = true;
		}
		else if (NachZielSekunden < 3.0f)
		{
			FWiesbadenCarControl C;
			C.Brake = 1.0f;
			Ctrl->SetExternalControl(C);
			bSteuertWagen = true;
		}
		else if (bSteuertWagen)
		{
			// EINMAL loesen - danach gehoert die Steuerung wieder anderen
			// (Tastatur, WbDrive, Bus). Vorher loeste er jede fremde je Bild.
			Ctrl->ClearExternalControl();
			bSteuertWagen = false;
		}

		// Fahrer-Takt im Log: so laesst sich ein Nachweislauf ohne Bild verfolgen.
		TaktSekunden += DeltaSeconds;
		if (TaktSekunden >= 1.0f && !Bewertung.IstFertig())
		{
			TaktSekunden = 0.0f;
			UE_LOG(LogWbCore, Log, TEXT("Parcours-Takt: %s t=%.1f x=%.0f y=%.0f Kurs=%.0f v=%.0f Handbremse=%d"),
				WbParcoursAbschnittName(Bewertung.GetAbschnitt()), Bewertung.GetFahrzeit(),
				P.PosCm.X, P.PosCm.Y, P.KursGrad, P.Kmh, P.bHandbremse ? 1 : 0);
		}
	}
	if (Bewertung.IstFertig())
	{
		NachZielSekunden += DeltaSeconds;
	}
}

void AWiesbadenParcours::AmZiel()
{
	NachZielSekunden = 0.0f;
	const FWbParcoursErgebnis E = Bewertung.GetErgebnis();

	// Bestzeit je Variante in den Spieler-Einstellungen. Nur eigene Runden:
	// der Nachweis-Fahrer (WbParcours 1/2) liest, traegt aber nie ein.
	FConfigFile* Ini = GConfig ? GConfig->FindConfigFile(GGameUserSettingsIni) : nullptr;
	BestzeitVorher = Ini ? FWbParcoursBestzeit::Lesen(*Ini, E.bRegen) : 0.0f;
	bNeueBestzeit = false;
	if (E.bImZiel && !bMitFahrer && Ini && FWbParcoursBestzeit::Eintragen(*Ini, E.bRegen, E.GesamtSekunden))
	{
		bNeueBestzeit = true;
		GConfig->Flush(false, GGameUserSettingsIni);
	}

	UE_LOG(LogWbCore, Log,
		TEXT("Parcours-Ergebnis: %s, %s (Belagsgrip max %.2f), Fahrzeit %.1f s, Strafe %.0f s, Gesamt %.1f s, Kegel %d, Tore %d, Linie %.0f km/h, Box %.1f m, Handbremse %d, Wendezone verfehlt %d, Sauberkeit %d, Medaille %s, Bestzeit vorher %.1f s%s."),
		E.bImZiel ? TEXT("im Ziel") : TEXT("abgebrochen"), E.bRegen ? TEXT("Regen") : TEXT("trocken"),
		E.BelagsGripMax, E.FahrzeitSekunden, E.StrafSekunden, E.GesamtSekunden,
		E.KegelGetroffen, E.TorFehler, E.KmhAnBremslinie, E.StoppAbweichungM, E.bHandbremseGenutzt ? 1 : 0,
		E.bWendezoneVerfehlt ? 1 : 0, E.Sauberkeit, E.Medaille.IsEmpty() ? TEXT("-") : *E.Medaille, BestzeitVorher,
		bNeueBestzeit ? TEXT(", NEUE BESTZEIT") : (bMitFahrer ? TEXT(", Fahrer-Lauf traegt nicht ein") : TEXT("")));

	const APawn* Pawn = Wagen.Get();
	const APlayerController* PC = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (AWiesbadenVehicleHUD* HUD = PC ? Cast<AWiesbadenVehicleHUD>(PC->GetHUD()) : nullptr)
	{
		HUD->ShowTransientHint(E.bImZiel
			? FString::Printf(TEXT("Parcours%s: %.1f s - Sauberkeit %d %% - %s%s"), E.bRegen ? TEXT(" (Regen)") : TEXT(""),
				E.GesamtSekunden, E.Sauberkeit, *E.Medaille, bNeueBestzeit ? TEXT(" - neue Bestzeit!") : TEXT(""))
			: FString(TEXT("Parcours abgebrochen (Zeitlimit)")));
	}
}

bool AWiesbadenParcours::HudSichtbar() const
{
	return bAufgebaut && NachZielSekunden < 20.0f;   // -1 = noch nicht im Ziel
}

FString AWiesbadenParcours::HudTitel() const
{
	const FWbParcoursErgebnis E = Bewertung.GetErgebnis();
	const TCHAR* Variante = E.bRegen ? TEXT("Parcours (Regen)") : TEXT("Parcours");
	if (E.bImZiel)
	{
		return FString::Printf(TEXT("%s - Ziel: %s"), Variante, *E.Medaille);
	}
	return FString::Printf(TEXT("%s - %s"), Variante, WbParcoursAbschnittName(Bewertung.GetAbschnitt()));
}

FString AWiesbadenParcours::HudZeile() const
{
	const FWbParcoursErgebnis E = Bewertung.GetErgebnis();
	if (Bewertung.IstFertig())
	{
		const FString Bestzeit = bNeueBestzeit ? FString(TEXT("  -  neue Bestzeit!"))
			: (BestzeitVorher > 0.0f ? FString::Printf(TEXT("  -  Bestzeit %.1f s"), BestzeitVorher) : FString());
		return FString::Printf(TEXT("Gesamt %.1f s (Fahrt %.1f + Strafe %.0f)  -  Sauberkeit %d %%%s"),
			E.GesamtSekunden, E.FahrzeitSekunden, E.StrafSekunden, E.Sauberkeit, *Bestzeit);
	}
	return FString::Printf(TEXT("Zeit %.1f s  -  Strafe +%.0f s  -  Kegel %d"),
		E.FahrzeitSekunden, E.StrafSekunden, E.KegelGetroffen);
}

FString AWiesbadenParcours::HudHinweis() const
{
	if (LetzteMeldungAlter < 3.0f && !LetzteMeldung.IsEmpty())
	{
		return LetzteMeldung;
	}
	switch (Bewertung.GetAbschnitt())
	{
	case EWbParcoursAbschnitt::Bereit:   return TEXT("Durchs Starttor - Kegel 1 links umfahren");
	case EWbParcoursAbschnitt::Slalom:   return TEXT("Slalom: abwechselnd links und rechts vorbei");
	case EWbParcoursAbschnitt::Bremsen:  return TEXT("Mit 40 km/h ueber die Bremslinie, in der Box halten");
	case EWbParcoursAbschnitt::Wende:    return TEXT("In der Wendezone mit Handbremse (Leertaste) wenden");
	case EWbParcoursAbschnitt::Rueckweg: return TEXT("Zurueck durchs Ziel");
	default:                             return FString();
	}
}
