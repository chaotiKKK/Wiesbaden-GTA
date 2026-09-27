// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Vehicles/WiesbadenHeliLightRig.h"

#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbHeliLight, Log, All);

namespace
{
	/**
	 * Sitze der Leuchtenkoerper und des Geraets, in MODELL-Koordinaten (cm).
	 *
	 * Herkunft: Vermessung des importierten FBX mit echten Vertexdaten
	 * (Tools/ka52_fbxlage.py, Saved/Diagnose/ka52/fbxlage.txt):
	 *   Nav_Green    X -444..-426  Y 41..59  Z 151..169
	 *   Nav_Red      X  426..444   Y 41..59  Z 151..169
	 *   Strobe_White X   -9..9     Y 811..829 Z 281..299
	 *   Rumpf        X -438..433   Y -580..826  Z 0..295
	 *
	 * Daraus folgt: Gruen liegt bei Modell-X = -435 (Backbord), Rot bei +435
	 * (Steuerbord), das Stroboskop ganz hinten oben (Y = +820, Z = 290), und
	 * die NASE ist bei Modell-Y = -580. Die Sitze sind hier mit einem
	 * kleinen Versatz nach oben gesetzt, damit die Leuchte nicht im
	 * Leuchtenkoerper steckt.
	 */
	const FVector SitzGruen(-435.0, 50.0, 175.0);
	const FVector SitzRot(435.0, 50.0, 175.0);
	const FVector SitzStrobe(0.0, 820.0, 300.0);

	/** Suchscheinwerfer: an der Nase, links und rechts unter dem Kanzelrand. */
	const FVector SitzSuchlicht(-430.0, -190.0, 150.0);
	const FVector SuchlichtVersatz(0.0f, 190.0f, 0.0f);

	/** Landlicht: unter dem Rumpf, knapp hinter der Nase. */
	const FVector SitzLandlicht(-300.0, 0.0f, 0.0f);

	/**
	 * Parameter, unter denen die importierten Leuchtenmaterialien ihren
	 * Leuchtstoff tragen koennen.
	 *
	 * Die Materialien M_Nav_Green/M_Nav_Red/M_Strobe_White sind mit einem
	 * Fremdexporter gebaut; welche Namen sie verwenden, steht hier in einer
	 * Liste, statt im Code angenommen zu werden. Fehlt der Name, bleibt der
	 * Koerper sichtbar, aber ohne Aufblitzen - das Aufblitzen sitzt dann
	 * allein im Punktlicht, was immer noch deutlich ist.
	 */
	const TCHAR* LeuchtstoffParameter[] = {
		TEXT("StrobeStrength"), TEXT("EmissiveStrength"), TEXT("Emissive"),
		TEXT("Glow"), TEXT("Intensity")
	};
}

UWiesbadenHeliLightRig::UWiesbadenHeliLightRig()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f;

	// Alles haengt an einem Knoten, der den Modellraum traegt. Der Pawn
	// gibt ihm Massstab und Drehung - damit kann die Montage nicht
	// auseinanderlaufen (zwei Zahlen fuer dieselbe Sache sind der Fehler,
	// den dieser Aufbau gerade verhindert).
	ModelSpace = CreateDefaultSubobject<USceneComponent>(TEXT("Modellraum"));
	ModelSpace->SetupAttachment(this);
	USceneComponent* const Modellraum = ModelSpace;

	auto koerper = [&](const TCHAR* Name, const TCHAR* Asset) -> UStaticMeshComponent*
	{
		UStaticMeshComponent* Comp = CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Comp->SetupAttachment(Modellraum);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetGenerateOverlapEvents(false);
		Comp->CastShadow = false;
		static ConstructorHelpers::FObjectFinder<UStaticMesh> Found(Asset);
		if (Found.Succeeded())
		{
			Comp->SetStaticMesh(Found.Object);
		}
		return Comp;
	};

	auto punktlicht = [&](const TCHAR* Name, const FVector& Sitz,
		const FLinearColor& Farbe) -> UPointLightComponent*
	{
		UPointLightComponent* Comp = CreateDefaultSubobject<UPointLightComponent>(Name);
		Comp->SetupAttachment(Modellraum);
		Comp->SetRelativeLocation(Sitz);
		Comp->SetLightColor(Farbe);
		Comp->SetIntensity(NavLightIntensity);
		Comp->SetAttenuationRadius(NavLightAttenuationRadius);
		Comp->SetCastShadows(false);
		Comp->SetVisibility(false);   // erst BeginPlay schaltet sie ein
		return Comp;
	};

	// -- Leuchtenkoerper des Modells ---------------------------------------
	NavBodyRed = koerper(TEXT("NavBodyRot"), TEXT("/Game/Vehicles/Ka52/Nav_Red.Nav_Red"));
	NavBodyRed->SetRelativeLocation(SitzRot);
	NavBodyGreen = koerper(TEXT("NavBodyGruen"), TEXT("/Game/Vehicles/Ka52/Nav_Green.Nav_Green"));
	NavBodyGreen->SetRelativeLocation(SitzGruen);
	NavBodyStrobe = koerper(TEXT("NavBodyStrobe"), TEXT("/Game/Vehicles/Ka52/Strobe_White.Strobe_White"));
	NavBodyStrobe->SetRelativeLocation(SitzStrobe);

	// -- Punktlichter dazu -------------------------------------------------
	// Rot = Backbord, Gruen = Steuerbord. Die Farben sind die Luftfahrtfarben,
	// nicht "sieht hübsch aus": ein rotes Licht links erkennt man im
	// Dunkeln an einem halben Meter Abstand, ein anders nicht.
	NavLightRed = punktlicht(TEXT("NavLightRot"), SitzRot, FLinearColor(1.0f, 0.04f, 0.02f));
	NavLightGreen = punktlicht(TEXT("NavLightGruen"), SitzGruen, FLinearColor(0.04f, 1.0f, 0.18f));

	StrobeLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StrobeHell"));
	StrobeLight->SetupAttachment(Modellraum);
	StrobeLight->SetRelativeLocation(SitzStrobe);
	StrobeLight->SetLightColor(FLinearColor(1.0f, 1.0f, 1.0f));
	StrobeLight->SetIntensity(0.0f);
	StrobeLight->SetAttenuationRadius(4200.0f);
	StrobeLight->SetCastShadows(false);

	// -- Landlicht ----------------------------------------------------------
	LandingLight = CreateDefaultSubobject<USpotLightComponent>(TEXT("Landlicht"));
	LandingLight->SetupAttachment(Modellraum);
	LandingLight->SetRelativeLocation(SitzLandlicht);
	// Ein Licht strahlt entlang seiner lokalen X-Achse. -90 Grad Nickung
	// zeigt sie nach unten, im Modellraum bleibt sie unten, weil der
	// Modellraum nur eine Gierdrehung traegt.
	LandingLight->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));
	LandingLight->SetLightColor(FLinearColor(1.0f, 0.97f, 0.88f));
	LandingLight->SetIntensity(140000.0f);
	LandingLight->SetAttenuationRadius(7000.0f);
	LandingLight->SetInnerConeAngle(14.0f);
	LandingLight->SetOuterConeAngle(38.0f);
	LandingLight->SetCastShadows(true);
	LandingLight->SetVisibility(false);

	// -- Zwei Suchscheinwerfer ---------------------------------------------
	auto suchlicht = [&](const TCHAR* Name, const FVector& Sitz)
	{
		USpotLightComponent* Comp = CreateDefaultSubobject<USpotLightComponent>(Name);
		Comp->SetupAttachment(Modellraum);
		Comp->SetRelativeLocation(Sitz);
		// Basis: -90 Grad Gier strahlt entlang Modell--Y, das ist nach vorn
		// (die Nase liegt bei Modell--Y). Die Zielrichtung kommt als
		// Zusatznickung und -gierung dazu.
		Comp->SetRelativeRotation(FRotator(WantedPitch, -90.0f + WantedYaw, 0.0f));
		Comp->SetLightColor(FLinearColor(1.0f, 0.98f, 0.92f));
		Comp->SetIntensity(SearchlightRange * 62.0f);
		Comp->SetAttenuationRadius(SearchlightRange);
		Comp->SetInnerConeAngle(FMath::Max(2.0f, SearchlightConeAngle * 0.45f));
		Comp->SetOuterConeAngle(SearchlightConeAngle);
		Comp->SetCastShadows(true);
		Comp->SetVisibility(false);
		return Comp;
	};
	SearchlightA = suchlicht(TEXT("SuchscheinwerferA"), SitzSuchlicht - SuchlichtVersatz);
	SearchlightB = suchlicht(TEXT("SuchscheinwerferB"), SitzSuchlicht + SuchlichtVersatz);
}

void UWiesbadenHeliLightRig::SetModelTransform(float InScale, const FRotator& InYaw)
{
	if (!ModelSpace)
	{
		return;
	}
	ModelSpace->SetRelativeScale3D(FVector(InScale));
	ModelSpace->SetRelativeRotation(InYaw);
	ModelSpace->SetRelativeLocation(FVector::ZeroVector);
}

void UWiesbadenHeliLightRig::BeginPlay()
{
	Super::BeginPlay();

	// Leuchtstoff-Materialinstanzen: nur wenn das Material den Parameter
	// wirklich fuehrt (siehe LeuchtstoffParameter). Der Versuch ist
	// absichtlich defensiv - die Materialien stammen aus einem Fremdimport.
	auto mid = [](UStaticMeshComponent* Comp) -> UMaterialInstanceDynamic*
	{
		if (!Comp || Comp->GetNumMaterials() == 0)
		{
			return nullptr;
		}
		UMaterialInterface* Basis = Comp->GetMaterial(0);
		return Basis ? UMaterialInstanceDynamic::Create(Basis, Comp) : nullptr;
	};
	NavBodyRedMID = mid(NavBodyRed);
	NavBodyGreenMID = mid(NavBodyGreen);
	NavBodyStrobeMID = mid(NavBodyStrobe);

	if (NavLightRed)  { NavLightRed->SetVisibility(bAllLightsEnabled); }
	if (NavLightGreen) { NavLightGreen->SetVisibility(bAllLightsEnabled); }

	// Der Stroboskop-Takt startet ausserhalb des Blitzes, sonst leuchtet er
	// in der ersten Sekunde schon und der Rhythmus wirkt zufallsartig.
	StrobeTimer = StrobePeriod * 0.65f;

	UE_LOG(LogWbHeliLight, Log,
		TEXT("Ka52-Licht: Rot %s, Gruen %s, Strobe %s, Suchscheinwerfer %d, Reichweite %.0f cm."),
		NavLightRed ? TEXT("da") : TEXT("fehlt"),
		NavLightGreen ? TEXT("da") : TEXT("fehlt"),
		StrobeLight ? TEXT("da") : TEXT("fehlt"),
		(SearchlightA ? 1 : 0) + (SearchlightB ? 1 : 0),
		SearchlightRange);
}

void UWiesbadenHeliLightRig::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// -- Stroboskop ---------------------------------------------------------
	// Ein Stroboskop ist ein kurzer, harter Blitz mit Pause dazwischen. Ein
	// weiches Ein- und Ausblenden sieht nach Lampe aus, nicht nach
	// Kollisionswarnung. Deshalb wird die Helligkeit hart umgeschaltet.
	if (bStrobeEnabled && bAllLightsEnabled)
	{
		StrobeTimer += DeltaTime;
		if (StrobeTimer >= StrobePeriod)
		{
			StrobeTimer -= StrobePeriod;
			bStrobeFlashOn = !bStrobeFlashOn;
		}
		if (bStrobeFlashOn && StrobeTimer > StrobeFlash)
		{
			// Blitz zu Ende, bevor der naechste Periode beginnt.
			bStrobeFlashOn = false;
		}
	}
	else
	{
		bStrobeFlashOn = false;
	}

	if (StrobeLight)
	{
		StrobeLight->SetIntensity(bStrobeFlashOn ? 26000.0f : 0.0f);
		StrobeLight->SetVisibility(bStrobeFlashOn);
	}
	// Der Koerper selbst blitzt mit, wenn das Material einen Parameter fuehrt.
	for (UMaterialInstanceDynamic* M : { NavBodyStrobeMID })
	{
		if (!M)
		{
			continue;
		}
		for (const TCHAR* Name : LeuchtstoffParameter)
		{
			M->SetScalarParameterValue(FName(Name), bStrobeFlashOn ? 14.0f : 0.6f);
		}
	}

	// -- Suchscheinwerfer folgen der Zielrichtung --------------------------
	// Nicht springen, sondern fuehren: ein Scheinwerfer, der ruckartig
	// schwenkt, sieht aus wie ein kaputter Servo.
	SearchlightYaw = FMath::FInterpTo(SearchlightYaw, WantedYaw, DeltaTime, SearchlightAimResponse);
	SearchlightPitch = FMath::FInterpTo(SearchlightPitch, WantedPitch, DeltaTime, SearchlightAimResponse);
	if (SearchlightA)
	{
		SearchlightA->SetRelativeRotation(FRotator(SearchlightPitch, -90.0f + SearchlightYaw, 0.0f));
	}
	if (SearchlightB)
	{
		SearchlightB->SetRelativeRotation(FRotator(SearchlightPitch, -90.0f + SearchlightYaw, 0.0f));
	}
}

void UWiesbadenHeliLightRig::SetSearchlights(bool bOn)
{
	bSearchlightsOn = bOn && bAllLightsEnabled;
	if (SearchlightA) { SearchlightA->SetVisibility(bSearchlightsOn); }
	if (SearchlightB) { SearchlightB->SetVisibility(bSearchlightsOn); }
	UE_LOG(LogWbHeliLight, Log, TEXT("Ka52-Suchscheinwerfer %s."),
		bSearchlightsOn ? TEXT("an") : TEXT("aus"));
}

void UWiesbadenHeliLightRig::SetSearchlightTarget(const FVector& Weltziel)
{
	if (!SearchlightA)
	{
		return;
	}
	const FVector Von = SearchlightA->GetComponentLocation();
	FVector Richtung = Weltziel - Von;
	if (Richtung.SizeSquared() < 1.0f)
	{
		return;
	}
	Richtung = Richtung.GetSafeNormal();

	// Aus der Actor-Welt in den Modellraum: ModelSpace traegt die
	// Gierdrehung, und die Schwenkgrenzen unten sind Modellwinkel. Ohne
	// diese Umrechnung wuerde der Kegel bei jeder Kurve stehen bleiben.
	if (ModelSpace)
	{
		Richtung = ModelSpace->GetRelativeRotation().UnrotateVector(Richtung);
	}

	// Bezug ist die Nase (Modell--Y), denn der Kegel steht in Ruhelage auf
	// FRotator(0, -90, 0). Gegenprobe: Richtung (0,-1,0) liefert 0 Grad.
	const float YawGrad = FMath::RadiansToDegrees(FMath::Atan2(Richtung.X, -Richtung.Y));
	const float PitchGrad = FMath::RadiansToDegrees(
		FMath::Asin(FMath::Clamp(Richtung.Z, -1.0f, 1.0f)));

	WantedYaw = FMath::Clamp(YawGrad, -60.0f, 60.0f);
	WantedPitch = FMath::Clamp(PitchGrad, -51.0f, 39.0f) - 6.0f;
}

void UWiesbadenHeliLightRig::SetSearchlightAim(float Horizontal, float Vertical)
{
	// -1..1 auf die Schwenkgrenzen. 60 Grad seitlich und 45 Grad hoeher
	// decken den brauchbaren Anflugwinkel ab, ohne dass man den Scheinwerfer
	// in den Rumpf drehen kann.
	WantedYaw = FMath::Clamp(Horizontal, -1.0f, 1.0f) * 60.0f;
	WantedPitch = FMath::Clamp(Vertical, -1.0f, 1.0f) * 45.0f - 6.0f;
}

void UWiesbadenHeliLightRig::SetStrobeEnabled(bool bOn)
{
	bStrobeEnabled = bOn;
	if (!bOn)
	{
		bStrobeFlashOn = false;
		if (StrobeLight)
		{
			StrobeLight->SetIntensity(0.0f);
			StrobeLight->SetVisibility(false);
		}
	}
}

void UWiesbadenHeliLightRig::SetLandingLight(bool bOn)
{
	bLandingLightOn = bOn && bAllLightsEnabled;
	if (LandingLight)
	{
		LandingLight->SetVisibility(bLandingLightOn);
	}
}

void UWiesbadenHeliLightRig::SetAllLightsEnabled(bool bOn)
{
	bAllLightsEnabled = bOn;
	if (NavLightRed)  { NavLightRed->SetVisibility(bOn); }
	if (NavLightGreen) { NavLightGreen->SetVisibility(bOn); }
	if (NavBodyRed)   { NavBodyRed->SetVisibility(bOn); }
	if (NavBodyGreen) { NavBodyGreen->SetVisibility(bOn); }
	if (NavBodyStrobe) { NavBodyStrobe->SetVisibility(bOn); }
	if (LandingLight) { LandingLight->SetVisibility(bOn && bLandingLightOn); }
	bSearchlightsOn = bSearchlightsOn && bOn;
	if (SearchlightA) { SearchlightA->SetVisibility(bSearchlightsOn); }
	if (SearchlightB) { SearchlightB->SetVisibility(bSearchlightsOn); }
	if (!bOn)
	{
		bStrobeFlashOn = false;
	}
}
