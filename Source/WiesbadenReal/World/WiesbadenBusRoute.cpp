// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusRoute.h"

#include "World/WiesbadenBusInterior.h"
#include "World/WiesbadenRailTransport.h"
#include "World/WiesbadenCitySubsystem.h"
#include "GIS/WiesbadenTrafficLights.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "GIS/RoadNetworkTypes.h"
#include "GIS/GeoCoordinateConverter.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Audio/WiesbadenAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "ProceduralMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstance.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "EngineUtils.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbBus, Log, All);

AWiesbadenBusRoute::AWiesbadenBusRoute()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	RideAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("RideAnchor"));
	RideAnchor->SetupAttachment(Root);

	// Innenraum des Fahrgasts: haengt am Anker und folgt damit dem mitgefahrenen
	// Bus. Ausserhalb der Mitfahrt unsichtbar - der Bus bleibt von aussen der
	// geschlossene Kasten. Ohne Kollision: der Fahrgast ist angeheftet, und die
	// Sitze/Stangen duerfen ihn nicht aus dem Wagen schieben.
	InteriorMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BusInterior"));
	InteriorMesh->SetupAttachment(RideAnchor);
	InteriorMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	InteriorMesh->SetCastShadow(false);
	InteriorMesh->SetVisibility(false);
}

WiesbadenBusInterior::FSpec AWiesbadenBusRoute::InteriorSpec() const
{
	WiesbadenBusInterior::FSpec Spec;
	Spec.LengthCm = BusLengthCm;
	Spec.HalfWidthCm = BusHalfWidthCm;
	// Echte Wagenhoehe aus der Mesh-Box (Pivot kann innerhalb liegen); Vorgabe
	// nur, falls das Mesh fehlt.
	Spec.HeightCm = (MeshTopCm - MeshBottomCm > 100.0) ? (MeshTopCm - MeshBottomCm) : 225.0;
	return Spec;
}

void AWiesbadenBusRoute::LoadLine()
{
	// Die Datei oeffnet allein World/WiesbadenBusLineFile; Bus-Actor und
	// Haltestellenmonitor derselben Linie bekommen von dort DASSELBE geparste
	// Objekt (gleiche Adresse, einmal gelesen). Hier wird nur angewendet, was
	// dieser Actor daraus uebernimmt.
	if (!Converter)
	{
		UE_LOG(LogWbBus, Warning, TEXT("Bus: kein Georeferenz-Konverter - %s nicht gelesen."), *LineFile);
		return;
	}
	LineRoute = WiesbadenBusLineFile::ReadLine(LineFile, *Converter);
	if (!LineRoute->File.bLoaded) { return; }
	ApplyLineConfig(LineRoute->File);
}

void AWiesbadenBusRoute::ApplyLineConfig(const WiesbadenBusLineFile::FLineFile& F)
{
	// Liniennummer (Ansagen-Ordner, Wagen-Nummernkreis, Diagnose).
	LineRef = F.Ref;

	// Takt aus dem OSM-`interval` (headway_seconds). Der Wert bestimmt, wie viele
	// Wagen noetig sind; fehlt er, bleibt der Wunschtakt der Eigenschaft.
	ServiceHeadwaySeconds = (F.HeadwaySeconds > 0.0)
		? F.HeadwaySeconds
		: ((HeadwaySeconds > 0.0f) ? (double)HeadwaySeconds : 1200.0);

	// Wendezeit an den Endpunkten: aus der Datei, sonst die Eigenschaft.
	if (F.TerminusDwellSeconds >= 0.0)
	{
		TerminusDwellSeconds = (float)F.TerminusDwellSeconds;
	}

	// Zielschilder: Namen aus der Datei, damit eine neue Linie nur Daten braucht.
	if (!F.Blinds.Forward.IsEmpty() || !F.Blinds.Backward.IsEmpty() || !F.Blinds.Line.IsEmpty())
	{
		BlindDir = F.Blinds.Dir;
		BlindNameForward = F.Blinds.Forward;
		BlindNameBackward = F.Blinds.Backward;
		BlindNameLine = F.Blinds.Line;
	}
	UE_LOG(LogWbBus, Log,
		TEXT("Bus-Linie %s: Takt %.0f s, Wendezeit %.0f s, Schilder %s / %s / %s."),
		*LineRef, ServiceHeadwaySeconds, TerminusDwellSeconds,
		*BlindNameForward, *BlindNameBackward, *BlindNameLine);
}

void AWiesbadenBusRoute::LoadBlinds()
{
	auto Load = [this](const FString& Name) -> UMaterialInterface*
	{
		const FString Path = FString::Printf(TEXT("%s/%s.%s"), *BlindDir, *Name, *Name);
		return LoadObject<UMaterialInterface>(nullptr, *Path);
	};
	BlindMatForward = Load(BlindNameForward);
	BlindMatBackward = Load(BlindNameBackward);
	BlindMatLine = Load(BlindNameLine);
	UE_LOG(LogWbBus, Log, TEXT("Bus-Blind-Assets (Linie %s): %s=%d %s=%d %s=%d"),
		*LineRef, *BlindNameForward, BlindMatForward ? 1 : 0,
		*BlindNameBackward, BlindMatBackward ? 1 : 0, *BlindNameLine, BlindMatLine ? 1 : 0);
}

int32 AWiesbadenBusRoute::FirstVehicleNumber() const
{
	// Liniennummer aus "3"/"6": das Hundertfache davon ist der Nummernkreis der
	// Linie (601.., 301..). Alles, was keine einstellige Zahl ist (leer, "S6",
	// "RB10"), faellt auf 1 zurueck - eine zweistellige Linie wuerde sonst in den
	// Nummernkreis der naechsten Linie laufen.
	const int32 Num = FCString::Atoi(*LineRef);
	return (Num >= 1 && Num <= 9) ? (Num * 100 + 1) : 1;
}

void AWiesbadenBusRoute::BuildServiceFleet()
{
	WiesbadenBusLine::FServiceConfig Cfg;
	Cfg.CruiseSpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	Cfg.StopDwellSeconds = StopDwellSeconds;
	Cfg.TerminusDwellSeconds = TerminusDwellSeconds;
	Cfg.HeadwaySeconds = ServiceHeadwaySeconds;
	Cfg.MaxBuses = FMath::Max(NumBuses, 2);
	Cfg.FirstVehicleId = FirstVehicleNumber();

	double Cycle = 0.0;
	WiesbadenBusLine::BuildFleet(LineRoute->Route, Cfg, Cycle, Fleet);
	if (Cycle > 0.0) { CycleSeconds = Cycle; }

	// Der Pool wird so gross wie die Flotte (der Dienst braucht genau so viele
	// Wagen); -WbBusCount kann ihn zum Vergleich groesser machen.
	if (Fleet.Num() > 0)
	{
		NumBuses = FMath::Max(NumBuses, Fleet.Num());
	}
	UE_LOG(LogWbBus, Log,
		TEXT("Bus-Linie %s: Dauerbetrieb, %.0f km Strecke, Umlauf %.0f min, Abstand %.0f min -> %d Wagen (Wendezeit %.0f min je Ende)."),
		*LineRef, LineRoute->Route.TotalLengthCm / 100000.0, CycleSeconds / 60.0,
		CycleSeconds / (double)FMath::Max(Fleet.Num(), 1) / 60.0,
		Fleet.Num(), TerminusDwellSeconds / 60.0);
	for (const WiesbadenBusLine::FBusVehicle& V : Fleet)
	{
		UE_LOG(LogWbBus, Log, TEXT("  Wagen %d (Linie %s): Abfahrt bei +%.0f s, danach je Umlauf."),
			V.Id, *LineRef, V.PhaseSeconds);
	}
}

void AWiesbadenBusRoute::LoadSchedule()
{
	// Auch die Fahrplandatei liest der gemeinsame Leser (einmal je Fassung).
	Schedule = WiesbadenBusLineFile::ReadSchedule(ScheduleFile);
	UE_LOG(LogWbBus, Log, TEXT("Bus-Fahrplan %s: %d Abfahrten/Tag, Start %.1f Uhr."),
		*ScheduleFile, Schedule->DepartureSeconds.Num(), ServiceStartHour);
}

bool AWiesbadenBusRoute::ResolveGround(double X, double Y, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBusGround), true);
	Params.AddIgnoredActor(this);
	if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, 1000000.0), FVector(X, Y, -200000.0),
		ECC_WorldStatic, Params) && !Hit.bStartPenetrating)
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AWiesbadenBusRoute::BeginPlay()
{
	Super::BeginPlay();
	CitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	// Erst die Georeferenz, dann die Datei: der gemeinsame Leser braucht sie fuer
	// die Weltkoordinaten (und liest die Datei nur EINMAL fuer alle Aufrufer).
	Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();
	LoadLine();
	LoadSchedule();
	BusMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Vehicles/Bus/SM_Bus.SM_Bus"));
	if (BusMesh)
	{
		const FBox LocalBox = BusMesh->GetBoundingBox();
		const FVector Size = LocalBox.GetSize();
		const double Longest = FMath::Max3(Size.X, Size.Y, Size.Z);
		MeshScale = (Longest > 1.0) ? (BusLengthCm / Longest) : 1.0;
		// Der glTF-Pivot liegt in der Mesh-MITTE (convert_bus.py: origin BOUNDS),
		// nicht an der Unterkante. Ohne Korrektur haengt der halbe Bus unter der
		// Fahrbahn. Unterste Ecke der skalierten+ausgerichteten Box bestimmen und
		// den Bus spaeter genau um diesen Betrag anheben -> Raeder auf der Strasse.
		double MinZ = TNumericLimits<double>::Max();
		double MaxZ = -TNumericLimits<double>::Max();
		for (int32 ci = 0; ci < 8; ++ci)
		{
			const FVector Corner(
				(ci & 1) ? LocalBox.Max.X : LocalBox.Min.X,
				(ci & 2) ? LocalBox.Max.Y : LocalBox.Min.Y,
				(ci & 4) ? LocalBox.Max.Z : LocalBox.Min.Z);
			MinZ = FMath::Min(MinZ, MeshOrient.RotateVector(Corner * MeshScale).Z);
			MaxZ = FMath::Max(MaxZ, MeshOrient.RotateVector(Corner * MeshScale).Z);
		}
		MeshBottomCm = -MinZ;
		MeshTopCm = MaxZ;
		UE_LOG(LogWbBus, Log, TEXT("Bus-Box lokal Min.Z=%.2f Max.Z=%.2f, Scale %.3f -> Unterkante %.0f cm, Oberkante %.0f cm ueber Pivot, Hoehe %.0f cm."),
			LocalBox.Min.Z, LocalBox.Max.Z, MeshScale, MeshBottomCm, MeshTopCm, MeshBottomCm + MeshTopCm);

		// Aussenmaterial EINMAL beim Laden nachsehen: die glTF-Import-Instanzen
		// hatten als Elternmaterial das Default-Material des Importers aus dem
		// ENGINE-Inhalt - ohne Nanite-Flag zeichnet Unreal den Nanite-Bus damit
		// grau statt mit seinen Texturen. Genau das waren die "verschwundenen
		// Texturen". Der Fix (Tools/fix_bus_materials.cmd) haengt die Plaetze auf
		// einen Projekt-Master; diese Zeile belegt, womit das Spiel wirklich
		// rendert (Tools/verify_bus_materials.cmd prueft dasselbe an den Assets).
		int32 Slots = 0, WithTex = 0, EngineBase = 0;
		FString FirstBase;
		for (const FStaticMaterial& Slot : BusMesh->GetStaticMaterials())
		{
			UMaterialInterface* Mat = Slot.MaterialInterface;
			if (!Mat) { continue; }
			++Slots;
			if (UMaterial* Base = Mat->GetMaterial())
			{
				if (FirstBase.IsEmpty()) { FirstBase = Base->GetPathName(); }
				if (!Base->GetPathName().StartsWith(TEXT("/Game/"))) { ++EngineBase; }
			}
			if (const UMaterialInstance* MI = Cast<UMaterialInstance>(Mat))
			{
				UTexture* Tex = nullptr;
				if (MI->GetTextureParameterValue(TEXT("BaseColorTexture"), Tex) && Tex)
				{
					++WithTex;
				}
			}
		}
		UE_LOG(LogWbBus, Log,
			TEXT("Bus-Aussenmaterial: %d Plaetze, %d mit BaseColor-Textur, %d auf Engine-Inhalt; Basis %s"),
			Slots, WithTex, EngineBase, *FirstBase);
		if (EngineBase > 0)
		{
			UE_LOG(LogWbBus, Warning, TEXT("Bus-Aussenmaterial: %d von %d Plaetzen haengen an Engine-Inhalt "
				"(kein Nanite-Flag) - der Bus rendert dort grau. Tools/fix_bus_materials.cmd laufen lassen."),
				EngineBase, Slots);
		}
	}
	// Zielanzeige (Blind): authentische Punktmatrix-Texturen (Bernstein-LEDs) auf
	// unlit Quads (Engine-Plane). Front/Seite je Fahrtrichtung, Heck = Liniennummer.
	// WELCHE Schilder: aus der Liniendatei (`blinds`), siehe LoadBlinds.
	BlindMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	LoadBlinds();

	// Dauerbetrieb oder echter Fahrplan? Kommandozeile schlaegt die Eigenschaft.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbBusSchedule"))) { bContinuousService = false; }
	if (FParse::Param(FCommandLine::Get(), TEXT("WbBusFleet"))) { bContinuousService = true; }

	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	CycleSeconds = WiesbadenBusLine::RoundTripSeconds(LineRoute->Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
	int32 N = FMath::Max(NumBuses, 1);
	int32 BusOv = 0;
	// -WbBusCount ueberschreibt den Pool bewusst (Vergleichslauf mit mehr Slots).
	const bool bCountOverride = FParse::Value(FCommandLine::Get(), TEXT("WbBusCount="), BusOv) && BusOv > 0;
	if (bCountOverride) { N = BusOv; }
	if (bContinuousService)
	{
		BuildServiceFleet();
		// Im Dauerbetrieb ist der Pool GENAU die Flotte: alles darueber waere ein
		// Bus-Mesh, das jeden Tick versteckt wird (die Vorgabe NumBuses=12 liess
		// so 12 Meshes bauen, von denen nur 6 fuhren).
		N = bCountOverride ? FMath::Max(N, Fleet.Num()) : FMath::Max(Fleet.Num(), 2);
	}

	// Ein Blind-Quad (Engine-Plane 100x100) mit Material + Groesse. Ausrichtung
	// setzt PlaceBusAt je Flaeche via MakeFromZY(Normal, Up). Die Engine-Plane
	// rendert die Textur von aussen um 180 Grad gedreht (per Ecken-Marker-Foto
	// verifiziert: Quell-Oben-Links landet unten-rechts) -> negative X- UND Y-Skala
	// dreht das Quad um 180 Grad zurueck, damit die Blind-Textur korrekt steht.
	auto MakeBlindQuad = [this](UMaterialInterface* Mat, float WidthCm, float HeightCm) -> UStaticMeshComponent*
	{
		if (!BlindMesh) { return nullptr; }
		UStaticMeshComponent* Q = NewObject<UStaticMeshComponent>(this);
		Q->SetStaticMesh(BlindMesh);
		Q->SetupAttachment(Root);
		Q->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Q->SetCastShadow(false);
		if (Mat) { Q->SetMaterial(0, Mat); }
		Q->RegisterComponent();
		Q->SetWorldScale3D(FVector(-WidthCm / 100.0, -HeightCm / 100.0, 1.0));
		Q->SetVisibility(false);
		return Q;
	};

	for (int32 k = 0; k < N; ++k)
	{
		UStaticMeshComponent* Bus = NewObject<UStaticMeshComponent>(this);
		Bus->SetStaticMesh(BusMesh);
		Bus->SetupAttachment(Root);
		Bus->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Bus->RegisterComponent();
		Bus->SetWorldScale3D(FVector(MeshScale));
		Bus->SetVisibility(false);
		Buses.Add(Bus);

		// Punktmatrix-Blinds: Front + rechte Seite (Liniennummer + Ziel, Material je
		// Fahrtrichtung), Heck nur die Liniennummer. Front/Seite starten mit dem
		// Mainz-Material; PlaceBusAt setzt es je Richtung um.
		BlindFront.Add(MakeBlindQuad(BlindMatForward, BlindWidthCm, BlindHeightCm));
		BlindSide.Add(MakeBlindQuad(BlindMatForward, BlindWidthCm, BlindHeightCm));
		BlindRear.Add(MakeBlindQuad(BlindMatLine, RearBlindSizeCm, RearBlindSizeCm));
		BlindForward.Add(-1);
	}
	SlotWorldPos.Init(FVector::ZeroVector, Buses.Num());
	// Spurseite je Wagen (+1 = rechts der Fahrtrichtung): wird je Tick aus den
	// Spurdaten geprueft und nur bei deutlichem Vorsprung gewechselt.
	SlotLaneSide.Init(1.0, Buses.Num());
	SlotState.Init(0, Buses.Num());
	SlotVehicleId.Init(-1, Buses.Num());

	AuditLastArcCm.Init(-1.0, Buses.Num());
	SetupAnnouncements();
	BuildInteriorMesh();

	bReady = (LineRoute.IsValid() && LineRoute->WorldPath.Num() >= 2
		&& LineRoute->Route.StopArcCm.Num() >= 2 && BusMesh != nullptr && CycleSeconds > 0.0);
	UE_LOG(LogWbBus, Log, TEXT("Bus-Linie %s bereit=%d: %d Busse (%s), Route %.0f m, Umlauf %.0f s, MeshScale %.3f, Unterkante %.0f cm."),
		*LineRef, bReady ? 1 : 0, Buses.Num(),
		bContinuousService ? TEXT("Dauerbetrieb") : TEXT("Fahrplan"),
		LineRoute.IsValid() ? LineRoute->Route.TotalLengthCm / 100.0 : 0.0,
		CycleSeconds, MeshScale, MeshBottomCm);

	bLogDiag = FParse::Param(FCommandLine::Get(), TEXT("WbBusLog"));
	bGroundAudit = FParse::Param(FCommandLine::Get(), TEXT("WbBusGroundAudit"));
	if (FParse::Value(FCommandLine::Get(), TEXT("WbBusLogWagon="), LogWagonId) && LogWagonId > 0)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus-Diagnose: Umlauf-Protokoll fuer Wagen %d (Linie %s)."),
			LogWagonId, *LineRef);
	}
	FParse::Value(FCommandLine::Get(), TEXT("WbBusClock="), ServiceClockOffsetSeconds);
	if (ServiceClockOffsetSeconds != 0.0)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus-Diagnose: Dienstzeit startet um %.0f s versetzt (%.1f min)."),
			ServiceClockOffsetSeconds, ServiceClockOffsetSeconds / 60.0);
	}
	FParse::Value(FCommandLine::Get(), TEXT("WbBusParkStop="), ParkStop);
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbBusRide="), DevRideAfterSeconds))
	{
		DevRideAfterSeconds = -1.0f;
	}
	else if (DevRideAfterSeconds >= 0.0f)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus-Diagnose: -WbBusRide steigt nach %.0f s selbst ein."),
			DevRideAfterSeconds);
	}
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbBusRideExit="), DevRideExitAfterSeconds))
	{
		DevRideExitAfterSeconds = -1.0f;
	}
	else if (DevRideExitAfterSeconds >= 0.0f)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus-Diagnose: -WbBusRideExit steigt nach %.0f s Mitfahrt wieder aus."),
			DevRideExitAfterSeconds);
	}
	if (FParse::Value(FCommandLine::Get(), TEXT("WbBusRideWagon="), DevRideWagonId) && DevRideWagonId > 0)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus-Diagnose: -WbBusRideWagon steigt nur in Wagen %d ein (Linie %s)."),
			DevRideWagonId, *LineRef);
	}
	bAnnounceDiag = FParse::Param(FCommandLine::Get(), TEXT("WbBusAnnounceTest"));
	if (ParkStop >= 0)
	{
		UE_LOG(LogWbBus, Log, TEXT("Bus-PARK-Diagnose: Halt %d - je ein Bus beider Richtungen steht dort (kein Fahrbetrieb)."), ParkStop);
	}
	if (bLogDiag)
	{
		for (int32 i = 0; i < LineRoute->Route.StopArcCm.Num(); ++i)
		{
			FVector Pos, Tangent;
			if (WiesbadenRailTransport::SamplePolyline(LineRoute->WorldPath, LineRoute->ArcCm, LineRoute->Route.StopArcCm[i], Pos, Tangent))
			{
				UE_LOG(LogWbBus, Log, TEXT("Bus-Halte %2d: Bogen %.0f m -> Welt X=%.0f Y=%.0f"),
					i, LineRoute->Route.StopArcCm[i] / 100.0, Pos.X, Pos.Y);
			}
		}
	}
}

void AWiesbadenBusRoute::HideBusSlot(int32 k)
{
	if (Buses.IsValidIndex(k) && Buses[k]) { Buses[k]->SetVisibility(false); }
	if (BlindFront.IsValidIndex(k) && BlindFront[k]) { BlindFront[k]->SetVisibility(false); }
	if (BlindSide.IsValidIndex(k) && BlindSide[k]) { BlindSide[k]->SetVisibility(false); }
	if (BlindRear.IsValidIndex(k) && BlindRear[k]) { BlindRear[k]->SetVisibility(false); }
	if (SlotState.IsValidIndex(k)) { SlotState[k] = 0; }
}

void AWiesbadenBusRoute::BuildGates()
{
	Gates.Reset();
	if (!CitySubsystem || LineRoute->WorldPath.Num() < 2) { return; }
	const TArray<FWiesbadenTrafficLight>& Lights = CitySubsystem->TrafficLightSystem.Lights;
	if (Lights.Num() == 0) { return; }   // Ampelsystem noch nicht initialisiert -> spaeter erneut
	const double MatchSq = (double)RedGateMatchCm * (double)RedGateMatchCm;
	for (int32 li = 0; li < Lights.Num(); ++li)
	{
		const FVector L = Lights[li].Location;
		double Best = TNumericLimits<double>::Max();
		int32 BestIdx = INDEX_NONE;
		for (int32 i = 0; i < LineRoute->WorldPath.Num(); ++i)
		{
			const double D = FVector2D::DistSquared(FVector2D(LineRoute->WorldPath[i].X, LineRoute->WorldPath[i].Y), FVector2D(L.X, L.Y));
			if (D < Best) { Best = D; BestIdx = i; }
		}
		if (BestIdx != INDEX_NONE && Best <= MatchSq)
		{
			FBusGate G; G.ArcCm = LineRoute->ArcCm[BestIdx]; G.LightIndex = li;
			Gates.Add(G);
		}
	}
	Gates.Sort([](const FBusGate& A, const FBusGate& B) { return A.ArcCm < B.ArcCm; });
	bGatesBuilt = true;
	UE_LOG(LogWbBus, Log, TEXT("Bus: %d Ampeln auf der Linie 6 als Halte-Gates erkannt (von %d im Netz)."),
		Gates.Num(), Lights.Num());
}

bool AWiesbadenBusRoute::RedGateAhead(double InArcCm, const FVector& Dir, bool bForward, double& OutStopArcCm) const
{
	if (!CitySubsystem || Gates.Num() == 0) { return false; }
	// naechstes Gate in Fahrtrichtung innerhalb des Prueffensters.
	int32 BestGate = INDEX_NONE;
	double BestDelta = (double)RedApproachCm;
	for (int32 gi = 0; gi < Gates.Num(); ++gi)
	{
		const double Delta = bForward ? (Gates[gi].ArcCm - InArcCm) : (InArcCm - Gates[gi].ArcCm);
		if (Delta > 0.0 && Delta < BestDelta) { BestDelta = Delta; BestGate = gi; }
	}
	if (BestGate == INDEX_NONE) { return false; }
	// Anfahrts-Achse aus der Peilung - ueber DIE Fassung des Ampelsystems, nicht
	// ueber eine eigene Kopie der Regel. Der Bus faehrt geradeaus, liest also die
	// Geradeaus-Gruppe seiner Achse; seit es Abbiegephasen gibt, ist die
	// Gruppennummer nicht mehr gleich der Achse.
	const double BearingDeg = FMath::RadiansToDegrees(FMath::Atan2(Dir.Y, Dir.X));
	const int32 Group = FWiesbadenTrafficLightSystem::GroupForApproach(
		FWiesbadenTrafficLightSystem::AxisForBearing(BearingDeg), /*bLeftTurn=*/false);
	const ESignalAspect A = CitySubsystem->TrafficLightSystem.GetGroupAspect(Gates[BestGate].LightIndex, Group);
	if (A == ESignalAspect::Green) { return false; }
	OutStopArcCm = bForward ? (Gates[BestGate].ArcCm - RedStopMarginCm)
	                        : (Gates[BestGate].ArcCm + RedStopMarginCm);
	return true;
}

WiesbadenBusLine::FBusState AWiesbadenBusRoute::ComputeHeldState(int64 VehicleId, double RawElapsed,
	float DeltaSeconds, bool bPeriodic, bool& bOutFinished)
{
	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	// Rotlicht-Haltezeit je WAGEN. Im Dauerbetrieb ist sie auf die Wendezeit
	// begrenzt (sonst wuerde ein Wagen ueber Stunden immer spaeter) und laeuft mit
	// dem Umlauf um.
	const double HoldMax = bPeriodic ? FMath::Max(TerminusDwellSeconds - 120.0, 0.0) : 1.0e9;
	const double Held = FMath::Min(HoldByVehicle.FindRef((int32)VehicleId), HoldMax);
	double Eff = RawElapsed - Held;
	if (bPeriodic)
	{
		Eff = FMath::Fmod(Eff, CycleSeconds);
		if (Eff < 0.0) { Eff += CycleSeconds; }
		bOutFinished = false;
	}
	else
	{
		bOutFinished = (Eff >= CycleSeconds);
	}
	WiesbadenBusLine::FBusState St = WiesbadenBusLine::EvaluateRoundTrip(
		Eff, LineRoute->Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
	if (!bStopAtRed || Gates.Num() == 0 || St.bDwelling || bOutFinished) { return St; }
	FVector Pos, Tangent;
	if (!WiesbadenRailTransport::SamplePolyline(LineRoute->WorldPath, LineRoute->ArcCm, St.ArcLengthCm, Pos, Tangent)) { return St; }
	FVector Dir = Tangent.GetSafeNormal();
	if (!St.bForward) { Dir = -Dir; }
	double StopArc = 0.0;
	if (RedGateAhead(St.ArcLengthCm, Dir, St.bForward, StopArc))
	{
		const bool bPast = St.bForward ? (St.ArcLengthCm >= StopArc) : (St.ArcLengthCm <= StopArc);
		if (bPast)
		{
			St.ArcLengthCm = StopArc;                            // an der Haltelinie klemmen
			// Zeit verlieren -> bei Gruen fluessig weiter
			HoldByVehicle.Add((int32)VehicleId, FMath::Min(Held + (double)DeltaSeconds, HoldMax));
		}
	}
	return St;
}

void AWiesbadenBusRoute::PlaceBusAt(int32 k, const WiesbadenBusLine::FBusState& St, bool bLogThisTick)
{
	UStaticMeshComponent* Bus = Buses.IsValidIndex(k) ? Buses[k] : nullptr;
	if (!Bus) { return; }
	FVector Pos, Tangent;
	if (!WiesbadenRailTransport::SamplePolyline(LineRoute->WorldPath, LineRoute->ArcCm, St.ArcLengthCm, Pos, Tangent))
	{
		HideBusSlot(k);
		return;
	}
	// Fahrtrichtung (fuer die Rueckfahrt gespiegelt) bestimmt die rechte Seite.
	FVector Dir = Tangent.GetSafeNormal();
	if (!St.bForward) { Dir = -Dir; }
	// Rechtsverkehr: jeder Bus faehrt LaneOffsetCm rechts seiner Fahrtrichtung,
	// so begegnen sich Gegenrichtungs-Busse nebeneinander statt durcheinander.
	// ACHTUNG Achsen: die Geo->Welt-Projektion bildet Ost~+X, SUED~+Y ab (Nord=-Y),
	// also gegenueber ENU gespiegelt. Die rechte Hand der Fahrtrichtung ist damit
	// (-Dir.Y, Dir.X) (nicht (Dir.Y,-Dir.X) - das waere Linksverkehr). Boden an der
	// versetzten Position tasten (Strassenquerneigung/Streaming).
	const FVector RightDir = FVector(-Dir.Y, Dir.X, 0.0).GetSafeNormal();	// WELCHE Seite ist unsere Fahrspur? Der feste Rechtsversatz unterstellt, dass
	// die Richtung des OSM-Wegs unsere Fahrtrichtung ist. Das stimmt nicht ueberall:
	// Wo sie gegen unsere Fahrt zeigt, setzt der Versatz den Bus auf die Gegenspur -
	// im Spiel sichtbar als "beide Richtungen in derselben Spur". Deshalb die Seite
	// aus den Spurdaten messen (FRoadLane.Centerline zeigt in Fahrtrichtung) und nur
	// bei deutlichem Vorsprung wechseln (sonst springt der Bus an Kreuzungen).
	double LaneSide = SlotLaneSide.IsValidIndex(k) ? SlotLaneSide[k] : 1.0;
	double RightDist = -1.0, LeftDist = -1.0;
	if (LanePt.Num() > 0)
	{
		const double Probe = (double)LaneSideProbeCm;
		RightDist = AlignedLaneDistanceCm(Pos.X + RightDir.X * Probe, Pos.Y + RightDir.Y * Probe, Dir, Probe);
		LeftDist = AlignedLaneDistanceCm(Pos.X - RightDir.X * Probe, Pos.Y - RightDir.Y * Probe, Dir, Probe);
		if (LeftDist + (double)LaneSideHysteresisCm < RightDist) { LaneSide = -1.0; }
		else if (RightDist + (double)LaneSideHysteresisCm < LeftDist) { LaneSide = 1.0; }
		if (SlotLaneSide.IsValidIndex(k)) { SlotLaneSide[k] = LaneSide; }
	}
	// Haltebucht: an der Halte weiter zum Bordstein ausscheren (weich ein/aus).
	// Beides auf DERSELBEN Seite: die Bucht liegt am Gehweg unserer Fahrspur.
	const double Bay = WiesbadenBusLine::BayFactor(St.ArcLengthCm, St.bDwelling, LineRoute->Route.StopArcCm, BayZoneCm);
	const double SideOffsetCm = LaneSide * (LaneOffsetCm + Bay * BayDepthCm);
	const double FinalX = Pos.X + RightDir.X * SideOffsetCm;
	const double FinalY = Pos.Y + RightDir.Y * SideOffsetCm;
	double TraceZ = 0.0;
	if (!ResolveGround(FinalX, FinalY, TraceZ))
	{
		HideBusSlot(k);
		if (bLogThisTick)
		{
			UE_LOG(LogWbBus, Log, TEXT("Linie %s Wagen %d: X=%.0f Y=%.0f Bogen %.0f m %s - Boden nicht gestreamt (unsichtbar)"),
				*LineRef, WagonId(k), FinalX, FinalY, St.ArcLengthCm / 100.0,
				St.bDwelling ? TEXT("VERWEILT") : TEXT("faehrt"));
		}
		return;
	}
	// FAHRBAHN: die gebackene Karte traegt keine Fahrbahn-Kollision (gemessen: in
	// JEDER Probe beider Linien trifft der vertikale Strahl nur das Landscape -
	// s. -WbBusGroundAudit). Der Bus stand damit auf dem Gelaende, das unter dem
	// Belag liegt: im Mittel 37 cm, an der Rheinbruecke bis 5,7 m (dort ist der
	// Boden unter der Bruecke der Fluss). Die Fahrbahnhoehe liefert deshalb das
	// Strassennetz - DIESELBE Sollbahn, auf der die Verkehrs-Simulation faehrt.
	// Liegt eine Spur in Reichweite, gilt sie; nur ohne Spur bleibt der Trace.
	double LaneZ = 0.0;
	double RejectedDevCm = 0.0;
	const bool bLane = RoadSurfaceZ(FinalX, FinalY, RoadReachCm, TraceZ, LaneZ,
		bGroundAudit ? &RejectedDevCm : nullptr);
	const double GroundZ = bLane ? LaneZ : TraceZ;
	if (bGroundAudit) { AuditGround(k, St.ArcLengthCm, FinalX, FinalY, TraceZ, bLane, LaneZ, RejectedDevCm); }
	const double BusZ = GroundZ + MeshBottomCm + BusLiftCm;
	const FQuat Q = Dir.Rotation().Quaternion() * MeshOrient.Quaternion();
	Bus->SetWorldLocationAndRotation(FVector(FinalX, FinalY, BusZ), Q);
	Bus->SetVisibility(true);
	if (SlotWorldPos.IsValidIndex(k)) { SlotWorldPos[k] = FVector(FinalX, FinalY, BusZ); }
	if (SlotState.IsValidIndex(k)) { SlotState[k] = St.bDwelling ? 2 : 1; }

	// Punktmatrix-Blinds: Front (Fahrtrichtung), rechte Seite (nahe vorderer Tuer),
	// Heck (entgegen der Fahrtrichtung). Jeder Quad blickt mit seiner +Z-Flaeche
	// nach aussen und wird 3 cm proud gesetzt, damit er nicht im Blech steckt.
	const FVector Up = FVector::UpVector;
	const double BlindZ = GroundZ + BlindZAboveGroundCm;
	auto PlaceQuad = [&Up](UStaticMeshComponent* Q, const FVector& Pos, const FVector& Normal)
	{
		if (!Q) { return; }
		Q->SetWorldLocationAndRotation(Pos + Normal * 3.0,
			FRotationMatrix::MakeFromZY(Normal, Up).Rotator());
		Q->SetVisibility(true);
	};

	// Material von Front + Seite je Fahrtrichtung (nur bei Wechsel umsetzen).
	const int8 WantFwd = St.bForward ? 1 : 0;
	if (BlindForward.IsValidIndex(k) && BlindForward[k] != WantFwd)
	{
		UMaterialInterface* M = St.bForward ? BlindMatForward : BlindMatBackward;
		if (M && BlindFront.IsValidIndex(k) && BlindFront[k]) { BlindFront[k]->SetMaterial(0, M); }
		if (M && BlindSide.IsValidIndex(k) && BlindSide[k]) { BlindSide[k]->SetMaterial(0, M); }
		BlindForward[k] = WantFwd;
	}

	const double EndCm = BusLengthCm * BlindEndFrac;
	if (BlindFront.IsValidIndex(k))
	{
		PlaceQuad(BlindFront[k], FVector(FinalX + Dir.X * EndCm, FinalY + Dir.Y * EndCm, BlindZ), Dir);
	}
	if (BlindSide.IsValidIndex(k))
	{
		const double SideCm = BusLengthCm * BlindSideForwardFrac;
		const FVector SidePos(
			FinalX + Dir.X * SideCm + RightDir.X * BusHalfWidthCm,
			FinalY + Dir.Y * SideCm + RightDir.Y * BusHalfWidthCm,
			BlindZ);
		PlaceQuad(BlindSide[k], SidePos, RightDir);
	}
	if (BlindRear.IsValidIndex(k))
	{
		PlaceQuad(BlindRear[k], FVector(FinalX - Dir.X * EndCm, FinalY - Dir.Y * EndCm, BlindZ), -Dir);
	}

	if (bLogThisTick)
	{
		const bool bEndpunkt = LineRoute->Route.StopArcCm.Num() > 0
			&& St.ArcLengthCm > LineRoute->Route.StopArcCm.Last() - 50.0;
		if (St.bDwelling)
		{
			UE_LOG(LogWbBus, Log, TEXT("Linie %s Wagen %d: X=%.0f Y=%.0f Z=%.0f Bogen %.0f m VERWEILT noch %.0f s von %.0f s | Richtung %s%s SICHTBAR"),
				*LineRef, WagonId(k), FinalX, FinalY, BusZ, St.ArcLengthCm / 100.0,
				St.DwellRemainingSeconds, St.DwellTotalSeconds,
				St.bForward ? TEXT("hin") : TEXT("zurueck"),
				bEndpunkt ? TEXT(" (Endpunkt)") : TEXT(""));
		}
		else
		{
			UE_LOG(LogWbBus, Log, TEXT("Linie %s Wagen %d: X=%.0f Y=%.0f Z=%.0f Bogen %.0f m faehrt | Richtung %s%s SICHTBAR"),
				*LineRef, WagonId(k), FinalX, FinalY, BusZ, St.ArcLengthCm / 100.0,
				St.bForward ? TEXT("hin") : TEXT("zurueck"),
				bEndpunkt ? TEXT(" (Endpunkt)") : TEXT(""));
		}
	}

	// Umlauf-Protokoll EINES Wagens (-WbBusLogWagon=601): je ~2 s eine Zeile
	// mit Bogenlaenge, Unterkante, Bodenhoehe (Fahrbahn UND Gelaende-Trace),
	// Zustand und Mitfahrt - aus EINEM Lauf ueber den ganzen Umlauf laesst
	// sich damit Wendezeit, Fahrbahnhoehe an jedem Punkt und die Mitfahrt
	// belegen, ohne die Zeilen aller uebrigen Wagen mitzulesen. Eigener Takt,
	// damit -WbBusLogWagon allein reicht (ohne -WbBusLog).
	{
		const bool bEndpunkt = LineRoute->Route.StopArcCm.Num() > 0
			&& St.ArcLengthCm > LineRoute->Route.StopArcCm.Last() - 50.0;
		if (bLogWagonTick && WagonId(k) == LogWagonId)
		{
			const TCHAR* EndFlag =
				(bEndpunkt ? TEXT(" (Endpunkt fern)")
					: (St.ArcLengthCm < 50.0 ? TEXT(" (Endpunkt Start)") : TEXT("")));
			UE_LOG(LogWbBus, Log,
				TEXT("Umlauf Wagen %d (%s): t=%.0f s Bogen %.1f von %.1f m | Unterkante %.2f m | Grundlage %s (Gelaende-Trace %.2f m, Abweichung %+.0f cm) | %s%s | Richtung %s | Spurseite %s (Abstand rechts %.0f / links %.0f cm) | Mitfahrt %s | X=%.0f Y=%.0f"),
				WagonId(k), *LineRef,
				GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0,
				St.ArcLengthCm / 100.0, LineRoute->Route.TotalLengthCm / 100.0,
				BusZ / 100.0,
				bLane ? TEXT("Fahrbahn aus dem Strassennetz") : TEXT("Gelaende-Trace (keine brauchbare Spur)"),
				TraceZ / 100.0, TraceZ - GroundZ,
				St.bDwelling
					? *FString::Printf(TEXT("VERWEILT noch %.0f s von %.0f s"),
						St.DwellRemainingSeconds, St.DwellTotalSeconds)
					: TEXT("faehrt"),
				EndFlag,
				St.bForward ? TEXT("hin") : TEXT("zurueck"),
				LaneSide > 0.0 ? TEXT("rechts der Fahrtrichtung") : TEXT("links der Fahrtrichtung"),
				RightDist, LeftDist,
				(RideSession.IsRiding() && RiddenSlot == k) ? TEXT("aktiv") : TEXT("nein"),
				FinalX, FinalY);
		}
	}
}

void AWiesbadenBusRoute::BuildLaneIndex()
{
	bLaneIndexBuilt = true;
	// Der WorldBuilder traegt das Strassennetz (nicht-transiente UPROPERTY der
	// gebackenen Karte) - ohne gebackene Stadt gibt es keinen und der Bus bleibt
	// beim Gelaende-Trace.
	AWiesbadenWorldBuilder* Builder = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(GetWorld()); It; ++It)
	{
		if (!It->RoadNetwork.IsEmpty()) { Builder = *It; break; }
	}
	if (!Builder) { return; }
	LanePt.Reserve(300000);
	for (const FRoadLane& Lane : Builder->RoadNetwork.Lanes)
	{
		const TArray<FVector>& Pts = Lane.Centerline;
		for (int32 i = 0; i < Pts.Num(); ++i)
		{
			const int32 Idx = LanePt.Add(Pts[i]);
			// Folgepunkt auf DERSELBEN Spur: die Hoehe wird entlang der Spur
			// interpoliert, sonst stuende der Bus an langen Geraden zwischen zwei
			// Stuetzpunkten um die Steigung daneben.
			LaneNext.Add((i + 1 < Pts.Num()) ? Idx + 1 : INDEX_NONE);
			LaneCells.FindOrAdd(LaneCellKey(Pts[i].X, Pts[i].Y)).Add(Idx);
		}
	}
	UE_LOG(LogWbBus, Log, TEXT("Bus-Fahrbahn: %d Spur-Stuetzpunkte indexiert (%d Zellen)."),
		LanePt.Num(), LaneCells.Num());
}

bool AWiesbadenBusRoute::RoadSurfaceZ(double X, double Y, double MaxDistCm, double HintZ,
	double& OutZ, double* OutRejectedDevCm)
{
	if (!bLaneIndexBuilt) { BuildLaneIndex(); }
	if (LanePt.Num() == 0) { return false; }

	const int64 CX = (int64)FMath::FloorToDouble(X / 20000.0);
	const int64 CY = (int64)FMath::FloorToDouble(Y / 20000.0);
	// Fehlermass: waagerechter Abstand PLUS Hoehenfehler. Eine Spur auf einer Rampe
	// 8 m neben dem Bus ist zwar die naechste im Plan, aber 40 m zu tief - mit der
	// Hoehe im Fehlermass gewinnt die Spur, auf der der Bus wirklich steht.
	const double ReachSq = MaxDistCm * MaxDistCm;
	double BestCost = ReachSq + MaxLaneDeviationCm * MaxLaneDeviationCm;
	double BestZ = 0.0;
	bool bFound = false;
	for (int64 dx = -1; dx <= 1; ++dx)
	{
		for (int64 dy = -1; dy <= 1; ++dy)
		{
			const TArray<int32>* Cell = LaneCells.Find((CX + dx) * 1000003LL ^ (CY + dy));
			if (!Cell) { continue; }
			for (const int32 Idx : *Cell)
			{
				if (!LanePt.IsValidIndex(Idx)) { continue; }
				const FVector& A = LanePt[Idx];
				const bool bSeg = LaneNext.IsValidIndex(Idx) && LaneNext[Idx] != INDEX_NONE;
				const FVector& B = bSeg ? LanePt[LaneNext[Idx]] : A;
				const double SX = B.X - A.X;
				const double SY = B.Y - A.Y;
				const double L2 = SX * SX + SY * SY;
				const double t = (L2 > 1.0) ? FMath::Clamp(((X - A.X) * SX + (Y - A.Y) * SY) / L2, 0.0, 1.0) : 0.0;
				const double PX = A.X + SX * t - X;
				const double PY = A.Y + SY * t - Y;
				const double D2 = PX * PX + PY * PY;
				if (D2 > ReachSq) { continue; }
				const double Z = A.Z + (B.Z - A.Z) * t;
				const double DZ = Z - HintZ;
				const double Cost = D2 + DZ * DZ;
				if (Cost < BestCost)
				{
					BestCost = Cost;
					BestZ = Z;
					bFound = true;
				}
			}
		}
	}
	// Auch die beste Spur muss zur Bodenhoehe passen: sonst liegen nur Spuren
	// fremder Ebenen in Reichweite (Damm, Bruecke, Rampe) - dann ist der
	// Gelaende-Trace die ehrlichere Hoehe.
	if (!bFound) { return false; }
	const double DevCm = FMath::Abs(BestZ - HintZ);
	if (OutRejectedDevCm) { *OutRejectedDevCm = DevCm; }
	if (DevCm > MaxLaneDeviationCm) { return false; }
	OutZ = BestZ;
	return true;
}

double AWiesbadenBusRoute::AlignedLaneDistanceCm(double X, double Y, const FVector& TravelDir, double MaxDistCm) const
{
	// Dieselbe Zellenabfrage wie RoadSurfaceZ, nur mit einer zweiten Bedingung:
	// das Spursegment muss in UNSERE Fahrtrichtung zeigen. Eine Spur, die in die
	// Gegenrichtung zeigt, hilft fuer die Frage "auf welcher Seite liegt meine
	// Fahrspur?" nicht - sie ist genau die Gegenspur.
	if (LanePt.Num() == 0) { return MaxDistCm; }
	const double ReachSq = MaxDistCm * MaxDistCm;
	double BestSq = ReachSq;
	for (double dx = -20000.0; dx <= 20000.0; dx += 20000.0)
	{
		for (double dy = -20000.0; dy <= 20000.0; dy += 20000.0)
		{
			const TArray<int32>* Cell = LaneCells.Find(LaneCellKey(X + dx, Y + dy));
			if (!Cell) { continue; }
			for (const int32 Idx : *Cell)
			{
				if (!LanePt.IsValidIndex(Idx)) { continue; }
				const FVector& A = LanePt[Idx];
				const bool bSeg = LaneNext.IsValidIndex(Idx) && LaneNext[Idx] != INDEX_NONE;
				if (!bSeg) { continue; }                     // Einzelpunkt hat keine Richtung
				const FVector& B = LanePt[LaneNext[Idx]];
				const double SX = B.X - A.X;
				const double SY = B.Y - A.Y;
				const double L2 = SX * SX + SY * SY;
				if (L2 <= 1.0) { continue; }
				const double Len = FMath::Sqrt(L2);
				if ((SX * TravelDir.X + SY * TravelDir.Y) / Len < 0.5) { continue; }   // Gegenrichtung
				const double t = FMath::Clamp(((X - A.X) * SX + (Y - A.Y) * SY) / L2, 0.0, 1.0);
				const double PX = A.X + SX * t - X;
				const double PY = A.Y + SY * t - Y;
				const double D2 = PX * PX + PY * PY;
				if (D2 < BestSq) { BestSq = D2; }
			}
		}
	}
	return FMath::Sqrt(BestSq);
}

void AWiesbadenBusRoute::NoteDeviation(double DevCm, const FString& Text)
{
	// Nur die schlimmsten fuenf behalten: die Datei soll den Ort nennen, nicht mit
	// tausenden gleichfoermigen Zeilen die Auffaelligkeiten verstecken.
	if (AuditWorst.Num() >= 5 && FMath::Abs(DevCm) <= FMath::Abs(AuditWorst.Last().DevCm)) { return; }
	AuditWorst.Add(FWorstDev{DevCm, Text});
	AuditWorst.Sort([](const FWorstDev& A, const FWorstDev& B)
		{ return FMath::Abs(A.DevCm) > FMath::Abs(B.DevCm); });
	if (AuditWorst.Num() > 5) { AuditWorst.SetNum(5); }
}

void AWiesbadenBusRoute::AuditGround(int32 Slot, double AtArcCm, double X, double Y,
	double TraceZ, bool bLane, double LaneZ, double RejectedDevCm)
{
	if (!AuditLastArcCm.IsValidIndex(Slot))
	{
		AuditLastArcCm.Init(-1.0, Slot + 1);
	}
	if (AuditLastArcCm[Slot] >= 0.0 && FMath::Abs(AtArcCm - AuditLastArcCm[Slot]) < GroundAuditStepCm)
	{
		return;
	}
	AuditLastArcCm[Slot] = AtArcCm;
	// Wo genau lag die Spur wie weit neben der Bodenhoehe? Ohne diese Zeile sind die
	// Summen unten nur Zahlen ohne Ort - genau daran fehlte der Beleg.
	if (bLane)
	{
		NoteDeviation(TraceZ - LaneZ, FString::Printf(
			TEXT("Spur <-> Boden %+.0f cm; Bogen %.0f m (X %.0f Y %.0f) Wagen %d, Boden-Trace %.2f m, Spur %.2f m"),
			TraceZ - LaneZ, AtArcCm / 100.0, X, Y, WagonId(Slot), TraceZ / 100.0, LaneZ / 100.0));
	}
	else
	{
		// Keine brauchbare Spur in Reichweite: der Bus steht auf dem Gelaende. Der
		// Abstand der verworfenen Spur nennt das Ausmass der Fehlmessung.
		++AuditFallbacks;
		AuditFallbackMaxCm = FMath::Max(AuditFallbackMaxCm, RejectedDevCm);
		NoteDeviation(RejectedDevCm, FString::Printf(
			TEXT("keine Spur brauchbar (beste %+.0f cm daneben); Bogen %.0f m (X %.0f Y %.0f) Wagen %d, Boden-Trace %.2f m"),
			RejectedDevCm, AtArcCm / 100.0, X, Y, WagonId(Slot), TraceZ / 100.0));
	}

	UWorld* World = GetWorld();
	if (!World) { return; }
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBusGroundAudit), true);
	Params.AddIgnoredActor(this);

	// (1) WELCHE Flaeche traegt den Bus? Genau dieser Strahl bestimmt die Hoehe.
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, FVector(X, Y, TraceZ + 400.0),
		FVector(X, Y, TraceZ - 5.0), ECC_WorldStatic, Params))
	{
		if (AuditNotes.Num() < 8)
		{
			AuditNotes.Add(FString::Printf(
				TEXT("Bogen %.0f m (X %.0f Y %.0f): ueber dem gemeldeten Boden %.0f cm keine Flaeche."),
				AtArcCm / 100.0, X, Y, TraceZ));
		}
		return;
	}
	++AuditSamples;
	AuditArcMinCm = (AuditSamples == 1) ? AtArcCm : FMath::Min(AuditArcMinCm, AtArcCm);
	AuditArcMaxCm = (AuditSamples == 1) ? AtArcCm : FMath::Max(AuditArcMaxCm, AtArcCm);
	if (bLane) { ++AuditPlacedOnLane; } else { ++AuditPlacedOnTrace; }
	const FString Name = Hit.GetComponent() ? Hit.GetComponent()->GetName() : FString(TEXT("?"));
	AuditSurfaces.FindOrAdd(Name)++;
	const bool bRoad = Name.Contains(TEXT("Road"));
	if (bRoad) { ++AuditOnRoad; }
	else if (Name.Contains(TEXT("Landscape"))) { ++AuditOnTerrain; }
	else { ++AuditOnOther; }
	// Belegt, dass der SICHTBARE Belag (Fahrbahn-Kollision der Stadt) dieselbe
	// Hoehe hat wie die Spur des Strassennetzes, auf die der Bus gestellt wird.
	if (bRoad && bLane)
	{
		const double D = FMath::Abs(Hit.Location.Z - LaneZ);
		++AuditRoadHits;
		AuditRoadVsLaneSumCm += D;
		AuditRoadVsLaneMaxCm = FMath::Max(AuditRoadVsLaneMaxCm, D);
		// Auch dieser Abstand soll 0 sein (sichtbarer Belag = Spur). Wo er gross
		// wird, gehoert die Stelle mit Bogenlaenge in den Bericht.
		if (D > 100.0)
		{
			NoteDeviation(D, FString::Printf(
				TEXT("Belag <-> Spur %.0f cm; Bogen %.0f m (X %.0f Y %.0f) Wagen %d, Belag %.2f m, Spur %.2f m"),
				D, AtArcCm / 100.0, X, Y, WagonId(Slot), Hit.Location.Z / 100.0, LaneZ / 100.0));
		}
	}

	// (2) Was liegt darunter? Die Fahrbahn ist ein eigener Mesh ueber dem
	// Gelaende (Fahrbahnversatz); liegt direkt darunter nichts, steht der Bus
	// auf dem Gelaende selbst statt auf dem Belag.
	FHitResult Below;
	if (World->LineTraceSingleByChannel(Below, FVector(X, Y, TraceZ - 5.0),
		FVector(X, Y, TraceZ - 20000.0), ECC_WorldStatic, Params))
	{
		const double Gap = TraceZ - Below.Location.Z;
		++AuditNested;
		if (AuditNested == 1 || Gap < AuditNestedMinCm) { AuditNestedMinCm = Gap; }
		AuditNestedMaxCm = FMath::Max(AuditNestedMaxCm, Gap);
	}

	// (3) Abstand zur FAHRBAHN des Strassennetzes: das ist die Sollbahn, auf der
	// die Verkehrs-Simulation faehrt. Wo keine Kollision der Stadt liegt (siehe
	// (1)), ist erst dieser Wert die Fahrbahnhoehe.
	if (bLane)
	{
		const double Delta = TraceZ - LaneZ;
		++AuditLaneSamples;
		AuditLaneSumCm += Delta;
		if (AuditLaneSamples == 1 || Delta < AuditLaneMinCm) { AuditLaneMinCm = Delta; }
		AuditLaneMaxCm = FMath::Max(AuditLaneMaxCm, Delta);
		if (FMath::Abs(Delta) <= 5.0) { ++AuditLaneOn; }
		else if (Delta < 0.0) { ++AuditLaneBelow; }
	}

	if (!bRoad && AuditNotes.Num() < 10)
	{
		AuditNotes.Add(FString::Printf(
			TEXT("Bogen %.0f m (X %.0f Y %.0f): Raeder auf %s (Boden %.0f cm), nicht auf der Fahrbahn."),
			AtArcCm / 100.0, X, Y, *Name, TraceZ));
	}
}

void AWiesbadenBusRoute::WriteGroundAudit()
{
	FString Text;
	Text += FString::Printf(TEXT("Boden/Fahrbahn-Pruefung Linie %s (Probe alle %.0f m)\n"),
		*LineRef, GroundAuditStepCm / 100.0);
	Text += FString::Printf(TEXT("Proben: %d (Bogen %.0f..%.0f m)\n"),
		AuditSamples, AuditArcMinCm / 100.0, AuditArcMaxCm / 100.0);
	Text += FString::Printf(TEXT("Aufgesetzt auf: %d Proben Fahrbahn (Strassennetz), %d Gelaende-Trace\n"),
		AuditPlacedOnLane, AuditPlacedOnTrace);
	Text += FString::Printf(
		TEXT("Fahrbahn-Kollision der Stadt getroffen: %d Proben; Abstand Belag <-> Spur Mittel %.0f, max %.0f cm\n"),
		AuditRoadHits, AuditRoadHits > 0 ? AuditRoadVsLaneSumCm / (double)AuditRoadHits : 0.0,
		AuditRoadVsLaneMaxCm);
	Text += FString::Printf(TEXT("Unter den Raedern: %d Fahrbahn, %d Gelaende (unter dem Belag), %d anderes\n"),
		AuditOnRoad, AuditOnTerrain, AuditOnOther);
	Text += FString::Printf(TEXT("Flaeche unter der obersten: %d von %d Proben, Abstand %.0f..%.0f cm\n"),
		AuditNested, AuditSamples, AuditNestedMinCm, AuditNestedMaxCm);
	Text += FString::Printf(
		TEXT("Fahrbahn des Strassennetzes: %d von %d Proben in Reichweite (%.0f m); ")
		TEXT("Unterkante darunter %d, auf der Fahrbahn %d; Abstand min %.0f, Mittel %.0f, max %.0f cm\n"),
		AuditLaneSamples, AuditSamples, RoadReachCm / 100.0, AuditLaneBelow, AuditLaneOn,
		AuditLaneMinCm,
		AuditLaneSamples > 0 ? AuditLaneSumCm / (double)AuditLaneSamples : 0.0,
		AuditLaneMaxCm);
	Text += FString::Printf(
		TEXT("Ohne brauchbare Spur (Gelaende-Trace): %d Proben; groesster verworfener Abstand %.0f cm\n"),
		AuditFallbacks, AuditFallbackMaxCm);
	Text += TEXT("Groesste Spur/Boden-Abweichungen (Ort der Fehlmessung):\n");
	for (const FWorstDev& W : AuditWorst)
	{
		Text += FString::Printf(TEXT("  %+.0f cm  %s\n"), W.DevCm, *W.Text);
	}
	Text += TEXT("Getroffene Flaechen:\n");
	for (const TPair<FString, int32>& It : AuditSurfaces)
	{
		Text += FString::Printf(TEXT("  %-32s %d\n"), *It.Key, It.Value);
	}
	if (AuditNotes.Num() > 0)
	{
		Text += TEXT("Auffaelligkeiten:\n");
		for (const FString& N : AuditNotes) { Text += TEXT("  ") + N + TEXT("\n"); }
	}
	// Dieselbe Zusammenfassung zusaetzlich ins Log: der Beleg soll im Logauszug
	// lesbar sein und nicht nur in der Datei.
	UE_LOG(LogWbBus, Log,
		TEXT("Boden-Audit Linie %s: %d Proben (Bogen %.0f..%.0f m) | aufgesetzt %d x Strassennetz, %d x Gelaende | Fahrbahn-Kollision getroffen %d x, Abstand zur Spur Mittel %.0f max %.0f cm | Abstand Unterkante<->Fahrbahn min %.0f Mittel %.0f max %.0f cm"),
		*LineRef, AuditSamples, AuditArcMinCm / 100.0, AuditArcMaxCm / 100.0,
		AuditPlacedOnLane, AuditPlacedOnTrace, AuditRoadHits,
		AuditRoadHits > 0 ? AuditRoadVsLaneSumCm / (double)AuditRoadHits : 0.0, AuditRoadVsLaneMaxCm,
		AuditLaneMinCm,
		AuditLaneSamples > 0 ? AuditLaneSumCm / (double)AuditLaneSamples : 0.0, AuditLaneMaxCm);
	for (const FWorstDev& W : AuditWorst)
	{
		UE_LOG(LogWbBus, Log, TEXT("Boden-Audit Linie %s: %s"), *LineRef, *W.Text);
	}

	// Je Linie eine eigene Datei: beide Linien fahren im selben Lauf, und ohne
	// die Liniennummer in der Datei haette der zweite Actor den ersten
	// ueberschrieben (gemessen wurde dann nur eine Linie).
	FFileHelper::SaveStringToFile(Text,
		*(FPaths::ProjectSavedDir() / FString::Printf(TEXT("Diagnose/bus_ground_audit_line%s.txt"), *LineRef)));
}

void AWiesbadenBusRoute::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady) { return; }
	const double WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const int32 N = Buses.Num();
	if (N <= 0) { return; }
	bool bLogThisTick = false;
	if (bLogDiag)
	{
		LogAccum += DeltaSeconds;
		if (LogAccum >= 2.0) { LogAccum = 0.0; bLogThisTick = true; }
	}
	// Umlauf-Protokoll eines Wagens laeuft im EIGENEN 2-s-Takt: -WbBusLogWagon
	// allein soll genau eine Zeile je 2 s liefern, nicht die aller Wagen.
	bLogWagonTick = false;
	if (LogWagonId > 0)
	{
		LogWagonAccum += DeltaSeconds;
		if (LogWagonAccum >= 2.0) { LogWagonAccum = 0.0; bLogWagonTick = true; }
	}
	if (bGroundAudit)
	{
		AuditWriteAccum += DeltaSeconds;
		if (AuditWriteAccum >= 30.0) { AuditWriteAccum = 0.0; WriteGroundAudit(); }
	}

	// Ampeln als Halte-Gates sammeln, sobald das (nur auf gebackenen Karten
	// initialisierte) Ampelsystem bereitsteht; nur in den ersten Sekunden versuchen.
	if (bStopAtRed && !bGatesBuilt && WorldTime < 20.0) { BuildGates(); }

	// Mitfahren: der Slot des Fahrgast-Busses ist GEPINNT - weder die Fahrplan-
	// Slotvergabe noch der Parkbetrieb darf ihn wegtauschen oder verstecken.
	const bool bRiding = RideSession.IsRiding() && Buses.IsValidIndex(RiddenSlot);

	// PARK-Diagnose: je ein haltender Bus beider Richtungen an Halt N, sonst nichts.
	// KEIN fruehzeitiges return mehr: Anker, Mitfahrt und Dev-Mitfahrt laufen auch
	// im Parkbetrieb weiter, sonst liesse sich an einem geparkten Bus nicht
	// einsteigen (und die Bild-Automation braucht genau diesen Fall).
	const bool bParkMode = ParkStop >= 0 && LineRoute->Route.StopArcCm.IsValidIndex(ParkStop);
	if (bParkMode)
	{
		for (int32 k = 0; k < N; ++k) { if (!bRiding || k != RiddenSlot) { HideBusSlot(k); } }
		WiesbadenBusLine::FBusState St;
		St.ArcLengthCm = LineRoute->Route.StopArcCm[ParkStop];
		St.bDwelling = true;
		St.bForward = true;  PlaceBusAt(0, St, false);
		if (N > 1) { St.bForward = false; PlaceBusAt(1, St, false); }
	}

	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;

	if (!bParkMode && bContinuousService && Fleet.Num() > 0)
	{
		// DAUERBETRIEB (Standard): jeder Wagen der Flotte hat seine feste Nummer und
		// faehrt seinen Umlauf ununterbrochen - Terminus, Wendezeit, zurueck, wieder
		// Wendezeit. Slot k gehoert Wagen Fleet[k], das aendert sich nie.
		const double ServiceSeconds = (double)ServiceStartHour * 3600.0 + WorldTime
			+ ServiceClockOffsetSeconds;
		for (int32 k = 0; k < N; ++k)
		{
			if (k >= Fleet.Num())
			{
				// Pool groesser als die Flotte (-WbBusCount): hier faehrt niemand.
				if (!bRiding || k != RiddenSlot) { HideBusSlot(k); }
				if (SlotVehicleId.IsValidIndex(k)) { SlotVehicleId[k] = -1; }
				continue;
			}
			const WiesbadenBusLine::FBusVehicle& V = Fleet[k];
			if (SlotVehicleId.IsValidIndex(k)) { SlotVehicleId[k] = V.Id; }

			// Umlaufzeit umlaufen: ein Wagen vor seiner Abfahrt ist im Ruecklauf des
			// vorigen Umlaufs (nicht geparkt) - sonst stuenden alle noch nicht
			// abgefahrenen Wagen auf dem Anfangspunkt uebereinander.
			double Local = FMath::Fmod(ServiceSeconds - V.PhaseSeconds, CycleSeconds);
			if (Local < 0.0) { Local += CycleSeconds; }
			bool bFinished = false;
			const WiesbadenBusLine::FBusState St = ComputeHeldState(V.Id, Local, DeltaSeconds, true, bFinished);
			PlaceBusAt(k, St, bLogThisTick);
			if (bAnnounceDiag && !bRiding && k == 0) { UpdateStopAnnouncement(St); }
			if (bRiding && k == RiddenSlot) { UpdateStopAnnouncement(St); }
		}
		if (bLogThisTick)
		{
			int32 Driving = 0;
			for (int32 k = 0; k < N; ++k) { if (SlotState.IsValidIndex(k) && SlotState[k] != 0) { ++Driving; } }
			const int32 Hour = ((int32)(ServiceSeconds / 3600.0)) % 24;
			const int32 Min = ((int32)(ServiceSeconds / 60.0)) % 60;
			UE_LOG(LogWbBus, Log,
				TEXT("Linie %s: Dienstzeit %02d:%02d, %d Wagen im Umlauf (%.0f min), %d Ampel-Gates."),
				*LineRef, Hour, Min, Driving, CycleSeconds / 60.0, Gates.Num());
		}
	}
	else if (!bParkMode && Schedule->DepartureSeconds.Num() >= 2)
	{
		// FAHRPLAN-MODUS (-WbBusSchedule): jeder Kurs faehrt zur echten ESWE-Abfahrtsminute ab
		// Nordfriedhof; Dichte schwankt mit dem Takt. Rotlicht-Halt je Kurs (Held).
		const double ServiceSeconds = (double)ServiceStartHour * 3600.0 + WorldTime
			+ ServiceClockOffsetSeconds;
		const double Window = CycleSeconds + 2400.0;   // Spielraum fuer rotlicht-verspaetete Kurse
		TArray<WiesbadenBusLine::FBusRun> Runs;
		WiesbadenBusLine::ActiveRuns(ServiceSeconds, *Schedule, Window, Runs);
		for (int32 k = 0; k < N; ++k) { HideBusSlot(k); }
		TArray<bool> Used; Used.Init(false, N);
		TSet<int64> ActiveKeys;
		int32 Driving = 0;
		// Mitfahren: den Slot des Fahrgast-Busses reservieren, damit die Slotvergabe
		// ihn nicht wegtauscht/versteckt (bRiding ist oben gesetzt).
		bool bRiddenSeen = false;
		if (bRiding) { Used[RiddenSlot] = true; }
		for (const WiesbadenBusLine::FBusRun& R : Runs)
		{
			bool bFinished = false;
			const WiesbadenBusLine::FBusState St = ComputeHeldState(R.Index, R.Elapsed, DeltaSeconds, false, bFinished);
			if (bFinished) { continue; }   // Rundfahrt (evtl. verspaetet) beendet
			ActiveKeys.Add(R.Index);
			int32 Slot;
			if (bRiding && (int32)R.Index == RiddenVehicleId)
			{
				Slot = RiddenSlot;      // gepinnt: der Fahrgast bleibt an diesem Bus
				bRiddenSeen = true;
				UpdateStopAnnouncement(St);   // naechste Halte ansagen (nur fuer den Fahrgast)
			}
			else
			{
				// Stabile Slot-Zuordnung ueber den fortlaufenden Kurs-Index; bei Kollision
				// (Pool < gleichzeitige Kurse) den naechsten freien Slot.
				Slot = (int32)(((R.Index % N) + N) % N);
				if (Used[Slot])
				{
					Slot = INDEX_NONE;
					for (int32 j = 0; j < N; ++j) { if (!Used[j]) { Slot = j; break; } }
					if (Slot == INDEX_NONE) { continue; }
				}
			}
			Used[Slot] = true;
			if (SlotVehicleId.IsValidIndex(Slot)) { SlotVehicleId[Slot] = (int32)R.Index; }
			PlaceBusAt(Slot, St, bLogThisTick);
			if (bAnnounceDiag && !bRiding && Driving == 0) { UpdateStopAnnouncement(St); }
			++Driving;
		}
		// Fahrgast-Kurs nicht mehr aktiv (Rundfahrt beendet) -> automatisch absetzen.
		if (bRiding && !bRiddenSeen) { ToggleBoarding(); }
		// Haltezeiten nicht mehr aktiver Kurse aufraeumen (kein Leck).
		for (auto It = HoldByVehicle.CreateIterator(); It; ++It)
		{
			if (!ActiveKeys.Contains(It.Key())) { It.RemoveCurrent(); }
		}
		if (bLogThisTick)
		{
			const int32 Hour = ((int32)(ServiceSeconds / 3600.0)) % 24;
			const int32 Min = ((int32)(ServiceSeconds / 60.0)) % 60;
			UE_LOG(LogWbBus, Log, TEXT("Fahrplan: Dienstzeit %02d:%02d, %d Busse unterwegs (%d Ampel-Gates)."),
				Hour, Min, Driving, Gates.Num());
		}
	}
	else if (!bParkMode)
	{
		// Rueckfall ohne Fahrplan: gleichverteilte Busse (ohne Rotlicht-Halt).
		for (int32 k = 0; k < N; ++k)
		{
			const double Offset = (CycleSeconds * k) / FMath::Max(N, 1);
			const WiesbadenBusLine::FBusState St = WiesbadenBusLine::EvaluateRoundTrip(
				WorldTime + Offset, LineRoute->Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds);
			if (SlotVehicleId.IsValidIndex(k)) { SlotVehicleId[k] = FirstVehicleNumber() + k; }
			PlaceBusAt(k, St, bLogThisTick);
		}
	}

	// Mitfahren: der unskalierte Anker folgt der Pose des Fahrgast-Busses (Fahrgast +
	// Kamera haengen am Anker); danach die Einstiegstaste abfragen.
	//
	// FAHRT-RICHTUNG, NICHT MESH-ROTATION: Die Komponente traegt Dir*MeshOrient -
	// MeshOrient dreht das glTF-Mesh in den Fahrzeugkasten (Yaw -90). Wer die
	// Komponentenrotation uebernimmt, bekommt ein Bezugssystem, in dem die
	// Wagenlaengsachse auf +Y liegt; die Cockpit-Offsets des Fahrgasts (X nach vorn)
	// zeigten dann 90 Grad quer - die Kamera stand NEBEN dem Bus in der Luft. Genau
	// das war der gemeldete Fehler "mit dem Bus in die Luft teleportiert". Darum die
	// Mesh-Drehung wieder herausrechnen: Anker +X = Fahrtrichtung, +Y = rechts,
	// +Z = oben - dasselbe Bezugssystem wie WiesbadenBusInterior.
	if (RideSession.IsRiding() && RideAnchor && Buses.IsValidIndex(RiddenSlot) && Buses[RiddenSlot])
	{
		const FQuat Heading = Buses[RiddenSlot]->GetComponentQuat() * MeshOrient.Quaternion().Inverse();
		RideAnchor->SetWorldLocationAndRotation(Buses[RiddenSlot]->GetComponentLocation(), Heading);
	}
	UpdateRiding(DeltaSeconds);
	UpdateDevRide(WorldTime, DeltaSeconds);
}

void AWiesbadenBusRoute::LogRideDiag()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!World || !PC || !PC->PlayerCameraManager) { return; }

	const FVector Eye = PC->PlayerCameraManager->GetCameraLocation();
	const FRotator View = PC->PlayerCameraManager->GetCameraRotation();

	// Anker-Yaw gegen Blick-Yaw: beide muessen zusammenfallen (Anker +X = Fahrt-
	// richtung). Genau hier sass der gemeldete Fehler - der Anker trug die ROTATION
	// DES MESHES (MeshOrient des glTF-Imports, 90 Grad quer), und damit stand die
	// Kamera quer neben dem Bus in der Luft.
	const double AnchorYaw = RideAnchor ? RideAnchor->GetForwardVector().Rotation().Yaw : 0.0;

	// Wo sitzt das Auge im WAGEN? Erwartet wird vorne rechts: rund +220 cm in
	// Fahrtrichtung, +73 cm nach rechts. Falsch waeren z. B. (73, 220) - dann
	// liegt das Auge quer im Wagen (vertauschte Achsen).
	const FQuat Heading = RideAnchor ? RideAnchor->GetComponentQuat() : FQuat::Identity;
	FString Seat;
	if (RideSession.IsRiding() && Buses.IsValidIndex(RiddenSlot) && Buses[RiddenSlot])
	{
		const FVector Rel = Heading.Inverse().RotateVector(
			Eye - Buses[RiddenSlot]->GetComponentLocation());
		Seat = FString::Printf(TEXT("Auge im Wagen X=%.0f (vorn) Y=%.0f (rechts) Z=%.0f"), Rel.X, Rel.Y, Rel.Z);
	}

	UE_LOG(LogWbBus, Log,
		TEXT("Mitfahrt Linie %s Wagen %d: Auge (%.0f,%.0f,%.0f) Welt; Anker-Yaw %.1f, Blick-Yaw %.1f (Versatz %.1f); %s; Innenraum sichtbar=%d; Spielerfigur versteckt=%d."),
		*LineRef, RiddenVehicleId, Eye.X, Eye.Y, Eye.Z, AnchorYaw, View.Yaw,
		(double)FRotator::NormalizeAxis(View.Yaw - AnchorYaw), *Seat,
		(InteriorMesh && InteriorMesh->IsVisible()) ? 1 : 0,
		(Pawn && Pawn->IsHidden()) ? 1 : 0);

	// Vier Strahlen aus dem Blickfeld NENNEN, was die Kamera sieht. Am gemeldeten
	// Fehler war genau das die offene Frage: das Bild zeigt eine Flaeche, aber
	// nicht, wem sie gehoert. Die Figur wird ignoriert und der Startpunkt liegt
	// vor ihrer Kapsel (sonst trifft jeder Strahl die eigene Kapsel bei 0 cm).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbBusRideDiag), true);
	if (Pawn) { Params.AddIgnoredActor(Pawn); }
	const FVector Start = Eye + View.Vector() * 60.0;
	static const TCHAR* Names[] = { TEXT("geradeaus"), TEXT("links"), TEXT("rechts"), TEXT("unten") };
	const FVector Dirs[] = {
		View.Vector(),
		View.RotateVector(FRotator(0.0, -35.0, 0.0).Vector()),
		View.RotateVector(FRotator(0.0, 35.0, 0.0).Vector()),
		View.RotateVector(FRotator(-35.0, 0.0, 0.0).Vector()),
	};
	for (int32 i = 0; i < 4; ++i)
	{
		FHitResult Hit;
		const bool bHit = World->LineTraceSingleByChannel(Hit, Start, Start + Dirs[i] * 4000.0,
			ECC_Visibility, Params);
		UE_LOG(LogWbBus, Log, TEXT("Bus-Mitfahrt %s: %s in %.0f cm."), Names[i],
			(bHit && Hit.GetComponent()) ? *Hit.GetComponent()->GetName() : TEXT("nichts"),
			bHit ? (double)(Hit.Location - Eye).Size() : 0.0);
	}
}

void AWiesbadenBusRoute::BuildInteriorMesh()
{
	if (!InteriorMesh) { return; }

	const WiesbadenBusInterior::FSpec Spec = InteriorSpec();

	// Je Flaechenart ein Mesh-Abschnitt mit eigener Innenraum-Textur. Das
	// Material multipliziert die Textur mit der Vertexfarbe (Tools/import_bus_interior.py):
	// die Textur liefert die Oberflaeche, die Vertexfarbe die Farbe.
	//
	// Fehlt ein Material (Innenraum-Content noch nicht importiert), faellt genau
	// dieser Abschnitt auf das Vertexfarben-Material zurueck - der Innenraum
	// bleibt sichtbar, und das Log nennt das fehlende Material beim Namen.
	UMaterialInterface* const Fallback = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Materials/City/M_WbVertexFarbe.M_WbVertexFarbe"));

	InteriorMesh->ClearAllMeshSections();

	int32 Boxes = 0;
	int32 Corners = 0;
	int32 Missing = 0;
	FString Sections;

	for (int32 Index = 0; Index < WiesbadenBusInterior::TileCount; ++Index)
	{
		const WiesbadenBusInterior::ETile Tile = (WiesbadenBusInterior::ETile)Index;
		TArray<FVector> Vertices;
		TArray<int32> Triangles;
		TArray<FVector> Normals;
		TArray<FVector2D> UV0;
		TArray<FLinearColor> Colours;
		WiesbadenBusInterior::BuildSection(Spec, Tile, Vertices, Triangles, Normals, UV0, Colours);
		if (Vertices.Num() == 0) { continue; }

		InteriorMesh->CreateMeshSection_LinearColor(Index, Vertices, Triangles, Normals, UV0, Colours,
			TArray<FProcMeshTangent>(), /*bCreateCollision=*/false);

		UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr,
			*WiesbadenBusInterior::TileMaterialPath(Tile));
		InteriorMesh->SetMaterial(Index, Mat ? Mat : Fallback);

		// Groesse der Textur mitloggen: eine Textur, die nur als Name existiert,
		// waere sonst nicht von einer echten zu unterscheiden.
		// GetImportedSize() statt GetSizeX(): die Groesse steckt im Quellbild,
		// GetSizeX() liest die Plattformdaten und liefert 0, solange die Textur
		// noch nicht aufgebaut ist - eine Zeile "0x0" im Log sieht dann wie ein
		// kaputtes Asset aus, ist aber nur zu frueh gemessen.
		int32 TexX = 0;
		int32 TexY = 0;
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(TEXT("%s/%s.%s"),
			WiesbadenBusInterior::AssetDir, WiesbadenBusInterior::TileTextureName(Tile),
			WiesbadenBusInterior::TileTextureName(Tile))))
		{
			const FIntPoint Imported = Tex->GetImportedSize();
			TexX = Imported.X;
			TexY = Imported.Y;
		}

		// UV-Grenzen des Abschnitts: sie belegen, dass das Mesh Kachelkoordinaten
		// traegt (eine Flaeche von 8 m bekommt UV bis ~4,3) und nicht je Flaeche
		// dieselben (0,0)-(1,1) - sonst laege dieselbe Textur auf jeder Flaeche
		// anders verzerrt.
		FVector2D UVMin(TNumericLimits<double>::Max(), TNumericLimits<double>::Max());
		FVector2D UVMax(TNumericLimits<double>::Lowest(), TNumericLimits<double>::Lowest());
		for (const FVector2D& UV : UV0)
		{
			UVMin.X = FMath::Min(UVMin.X, UV.X);
			UVMin.Y = FMath::Min(UVMin.Y, UV.Y);
			UVMax.X = FMath::Max(UVMax.X, UV.X);
			UVMax.Y = FMath::Max(UVMax.Y, UV.Y);
		}

		if (!Mat) { ++Missing; }
		Sections += FString::Printf(TEXT("%s %s(%dx%d, UV %.2f..%.2f/%.2f..%.2f) "),
			Mat ? TEXT("ok") : TEXT("FEHLT"), WiesbadenBusInterior::TileTextureName(Tile),
			TexX, TexY, UVMin.X, UVMax.X, UVMin.Y, UVMax.Y);
		Boxes += Vertices.Num() / 24;
		Corners += Vertices.Num();
	}

	InteriorMesh->SetVisibility(false);
	const FVector Eye = WiesbadenBusInterior::PassengerEyeCm(Spec);
	UE_LOG(LogWbBus, Log, TEXT("Bus-Innenraum: %d Kaesten, %d Ecken in %d Abschnitten; Auge X=%.0f Y=%.0f Z=%.0f cm; %s"),
		Boxes, Corners, InteriorMesh->GetNumSections(), Eye.X, Eye.Y, Eye.Z, *Sections);
	if (Missing > 0)
	{
		UE_LOG(LogWbBus, Warning, TEXT("Bus-Innenraum: %d von %d Materialien fehlen (%s) - "
			"Tools/make_bus_interior_textures.py und Tools/import_bus_interior.cmd laufen lassen."),
			Missing, WiesbadenBusInterior::TileCount, WiesbadenBusInterior::AssetDir);
	}
}

void AWiesbadenBusRoute::ShowInterior(bool bShow)
{
	if (InteriorMesh) { InteriorMesh->SetVisibility(bShow); }
}

void AWiesbadenBusRoute::UpdateDevRide(double WorldTime, float DeltaSeconds)
{
	// Automatisches AUSSTEIGEN (-WbBusRideExit=<Sekunden>): ohne Tastendruck
	// laesst sich der Ausstieg in einem automatischen Lauf nicht belegen - und
	// genau er muss Figur, Innenraum und Aussenhaut wieder zurueckholen.
	if (RideSession.IsRiding() && DevRideExitAfterSeconds >= 0.0f && !bDevRideExitDone)
	{
		DevRideRiddenSeconds += DeltaSeconds;
		if (DevRideRiddenSeconds >= (double)DevRideExitAfterSeconds)
		{
			bDevRideExitDone = true;
			UE_LOG(LogWbBus, Log, TEXT("Bus-Diagnose: nach %.0f s Mitfahrt wieder aussteigen."),
				DevRideRiddenSeconds);
			ToggleBoarding();
		}
	}

	// Entwicklungshilfe -WbBusRide=<Sekunden> (siehe Header): in automatischen
	// Laeufen kommt kein Tastendruck an, der Beleg braucht aber ein Bild AUS dem
	// fahrenden Bus. Vorbild ist -WbMitfahr an der Nerobergbahn.
	if (bDevRideDone || DevRideAfterSeconds < 0.0f || RideSession.IsRiding()) { return; }
	if (WorldTime < (double)DevRideAfterSeconds) { return; }

	// Nur einmal je Sekunde versuchen: der naechste Bus muss erst an einer Halte
	// stehen, sonst waere der Fahrgast sofort wieder weg.
	DevRideRetryAccum += DeltaSeconds;
	if (DevRideRetryAccum < 1.0) { return; }
	DevRideRetryAccum = 0.0;

	int32 Best = INDEX_NONE;
	int32 Moving = INDEX_NONE;
	int32 Wanted = INDEX_NONE;
	for (int32 k = 0; k < SlotState.Num(); ++k)
	{
		if (!Buses.IsValidIndex(k) || !Buses[k]) { continue; }
		// -WbBusRideWagon: nur dieser Wagen kommt als Einstieg in Frage; er wird
		// aber auch dann verfolgt, wenn er gerade faehrt oder ausserhalb des
		// geladenen Bereichs liegt (Slot merken), sonst wartet die Automatik am
		// falschen Ort auf einen Halt, den es dort nie gibt.
		if (DevRideWagonId > 0 && WagonId(k) != DevRideWagonId) { continue; }
		if (DevRideWagonId > 0) { Wanted = k; }
		if (SlotState[k] == 2) { Best = k; break; }
		if (SlotState[k] == 1 && Moving == INDEX_NONE) { Moving = k; }
	}

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	// NUR zu Fuss: sass der Spieler noch im Auto, zoege die Automatik das Auto
	// quer durch die Stadt (mit -WbZuFuss steigt er vorher aus).
	const AWiesbadenFootPawn* FootPawn = Cast<AWiesbadenFootPawn>(Pawn);
	if (!FootPawn) { return; }
	// Schon unterwegs (der andere Linien-Actor war schneller): nicht noch einmal
	// zum naechsten Bus teleportieren - das riss den Fahrgast aus dem Bus.
	if (FootPawn->IsRiding()) { return; }

	if (Best == INDEX_NONE)
	{
		// Kein HALTENDER Bus in Sicht: Busse ausserhalb des geladenen Bereichs
		// werden versteckt und koennen deshalb auch nicht "halten". Darum dem
		// naechsten fahrenden Bus nachziehen - das streamt seinen Abschnitt, und
		// beim naechsten Halt greift der naechste Versuch.
		const int32 Follow = (Moving != INDEX_NONE) ? Moving : Wanted;
		if (Follow != INDEX_NONE)
		{
			Pawn->SetActorLocation(Buses[Follow]->GetComponentLocation() + FVector(0.0, 0.0, 300.0));
		}
		return;
	}

	bDevRideDone = true;
	// Neben den Bus setzen: der Einstieg sucht ueber die Naehe, und ein
	// automatischer Lauf kann nicht hinlaufen.
	Pawn->SetActorLocation(Buses[Best]->GetComponentLocation() + FVector(0.0, 0.0, 50.0));
	ToggleBoarding();
}

void AWiesbadenBusRoute::UpdateRiding(float DeltaSeconds)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) { return; }
	const bool bDown = PC->IsInputKeyDown(EKeys::E);
	if (bDown && !bBoardKeyHeld) { ToggleBoarding(); }
	bBoardKeyHeld = bDown;

	// Alle 5 s melden, was die Kamera gerade sieht (siehe LogRideDiag).
	if (RideSession.IsRiding())
	{
		RideDiagAccum += DeltaSeconds;
		if (RideDiagAccum >= 5.0f)
		{
			RideDiagAccum = 0.0f;
			LogRideDiag();
		}
	}
	else
	{
		RideDiagAccum = 0.0f;
	}
}

void AWiesbadenBusRoute::CreatePassengerCamera()
{
	if (PassengerCamera || !RideSession.GetPassenger() || !RideAnchor) { return; }
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC) { return; }
	PassengerCamera = NewObject<UWiesbadenVehicleCameraComponent>(this, TEXT("BusPassengerCamera"));
	PassengerCamera->SetupAttachment(RideAnchor);   // unskalierter Anker -> Offsets stimmen
	const WiesbadenBusInterior::FSpec Spec = InteriorSpec();
	PassengerCamera->CameraOffset = FVector(0.0f, 0.0f, 140.0f);
	PassengerCamera->FollowArmLength = 950.0f;
	PassengerCamera->ZoomMinArmLength = 120.0f;
	PassengerCamera->ZoomMaxArmLength = 1600.0f;
	PassengerCamera->bLevelHorizon = true;
	PassengerCamera->FollowPitchOffset = -12.0f;   // Rueckfall, falls Follow
	// Fahrgastplatz aus dem Innenraum-Modell (Fensterplatz vorne rechts, sitzend).
	// Bewusst KEINE zweite Zahlenliste: sonst sitzt die Kamera in Wand oder Sitz,
	// sobald sich die Kabine aendert.
	const FVector Eye = WiesbadenBusInterior::PassengerEyeCm(Spec);
	PassengerCamera->CockpitOffset = FVector(Eye.X, Eye.Y, Eye.Z);
	// Leicht nach unten: die Sitzreihen und die Strasse vor dem Wagen.
	PassengerCamera->CockpitPitch = -3.0f;
	PassengerCamera->RegisterComponent();
	PassengerCamera->ActivateExternalView(PC, RideAnchor, RideSession.GetPassenger());
	PassengerCamera->SetCameraMode(EWiesbadenVehicleCameraMode::Cockpit);
}

void AWiesbadenBusRoute::DestroyPassengerCamera()
{
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (APawn* PassengerPawn = RideSession.GetPassenger())
		{
			PC->SetViewTarget(PassengerPawn);
		}
	}
	if (PassengerCamera)
	{
		PassengerCamera->DeactivateExternalView();
		PassengerCamera->DestroyComponent();
		PassengerCamera = nullptr;
	}
}

void AWiesbadenBusRoute::ToggleBoarding()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn || !RideAnchor) { return; }

	// Aussteigen: neben dem Bus absetzen und die Fusssteuerung freigeben.
	if (RideSession.IsRiding())
	{
		APawn* Passenger = RideSession.GetPassenger();
		if (!RideSession.BeginExiting() || !Passenger) { return; }
		Passenger->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		const FVector Side = RideAnchor->GetRightVector() * 320.0f + FVector(0, 0, 60.0f);
		Passenger->SetActorLocation(RideAnchor->GetComponentLocation() + Side);
		if (AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Passenger)) { Foot->SetRiding(false); }
		// Figur wieder zeigen (sie wurde beim Einsteigen ausgeblendet).
		Passenger->SetActorHiddenInGame(false);
		ShowInterior(false);
		// Aussenhaut und Zielschilder kamen fuer den Fahrgast ausgeblendet zurueck -
		// ohne diesen Gegenpart bliebe der Bus fuer den Spieler durchsichtig.
		if (PassengerCamera)
		{
			if (Buses.IsValidIndex(RiddenSlot))
			{
				PassengerCamera->RemoveCockpitHiddenMesh(Buses[RiddenSlot]);
			}
			for (TArray<UStaticMeshComponent*>* Quads : { &BlindFront, &BlindSide, &BlindRear })
			{
				if (Quads->IsValidIndex(RiddenSlot))
				{
					PassengerCamera->RemoveCockpitHiddenMesh((*Quads)[RiddenSlot]);
				}
			}
		}
		DestroyPassengerCamera();
		RideSession.CompleteExit();
		RiddenSlot = INDEX_NONE;
		RiddenVehicleId = -1;
		StopAnnouncement();   // laufende Ansage stoppen + Ducking beenden
		UE_LOG(LogWbBus, Log,
			TEXT("Bus-Linie %s: Fahrgast ausgestiegen (%.0f cm neben dem Bus; Figur sichtbar=%d, Innenraum sichtbar=%d)."),
			*LineRef, 320.0, (Passenger && !Passenger->IsHidden()) ? 1 : 0,
			(InteriorMesh && InteriorMesh->IsVisible()) ? 1 : 0);
		return;
	}

	// Einsteigen: nur der Spieler ZU FUSS, nur nahe an einem an der Halte STEHENDEN Bus.
	AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(Pawn);
	if (!Foot) { return; }
	// ZWEI Linien laufen im selben Level, beide Actors sehen denselben
	// Tastendruck. Ohne diese Frage haetten sich BEIDE denselben Spieler
	// angehaengt (jeder mit eigenem Anker) - im Log stand dann "eingestiegen"
	// fuer Linie 3 UND Linie 6, der Fahrgast hing am zuletzt eingestiegenen Bus,
	// und der andere Actor rechnete mit einem Anker, der nicht mehr sein Bus war.
	if (Foot->IsRiding()) { return; }
	const FVector P = Pawn->GetActorLocation();
	int32 Best = INDEX_NONE;
	double BestD = (double)BoardRangeCm;
	for (int32 k = 0; k < SlotState.Num(); ++k)
	{
		if (SlotState[k] != 2 || !SlotWorldPos.IsValidIndex(k)) { continue; }   // nur haltende Busse
		const double D = FVector::Dist(P, SlotWorldPos[k]);
		if (D < BestD) { BestD = D; Best = k; }
	}
	UStaticMeshComponent* Car = (Best != INDEX_NONE && Buses.IsValidIndex(Best)) ? Buses[Best] : nullptr;
	if (!Car) { return; }
	if (!RideSession.BeginBoarding(Pawn, Best)) { return; }
	RideAnchor->SetWorldLocationAndRotation(Car->GetComponentLocation(),
		Car->GetComponentQuat() * MeshOrient.Quaternion().Inverse());
	Foot->SetRiding(true);
	Pawn->AttachToComponent(RideAnchor, FAttachmentTransformRules::KeepWorldTransform);
	if (!RideSession.ConfirmRiding())
	{
		Pawn->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Foot->SetRiding(false);
		RideSession.Reset();
		return;
	}
	RiddenSlot = Best;
	RiddenVehicleId = SlotVehicleId.IsValidIndex(Best) ? SlotVehicleId[Best] : -1;
	LastAnnouncedStop = INDEX_NONE;   // die erste naechste Halte gleich ansagen
	CreatePassengerCamera();

	// Innenraum zeigen und die eigene Aussenhaut samt Zielschildern fuer den
	// Fahrgast ausblenden. Der Bus ist ein reines Aussen-Modell: ohne diese beiden
	// Schritte sitzt die Kamera in einer geschlossenen Huelle - man sieht nichts
	// vom Bus und scheint mit ihm in der Luft zu schweben.
	ShowInterior(true);
	if (PassengerCamera)
	{
		PassengerCamera->AddCockpitHiddenMesh(Car);
		for (TArray<UStaticMeshComponent*>* Quads : { &BlindFront, &BlindSide, &BlindRear })
		{
			if (Quads->IsValidIndex(Best)) { PassengerCamera->AddCockpitHiddenMesh((*Quads)[Best]); }
		}
	}

	// Die eigene Spielfigur ausblenden: sie steht aufrecht im Wagen, die
	// Fahrgastkamera sitzt aber auf SITZHOEHE in ihrem Brustkorb - ohne diesen
	// Schritt fuellt die eigene Kleidung das ganze Bild (das Bild der ersten
	// Fassung zeigte genau das: eine Flaeche, kein Bus, keine Stadt).
	Pawn->SetActorHiddenInGame(true);

	// Standplatz im Wagen: Fusboden + halbe Kapsel. Die frueheren 160 cm ueber der
	// Bus-Wurzel standen ueber dem Dach (der Wagen ist nur 2,25 m hoch) - die Figur
	// schwebte also im Freien ueber dem Bus.
	double StandHalfCm = 90.0;   // Rueckfall: Vorgabe-Kapsel der Figur (40/90)
	if (const AWiesbadenFootPawn* FootPawn = Cast<AWiesbadenFootPawn>(Pawn))
	{
		if (const UCapsuleComponent* Capsule = FootPawn->Capsule)
		{
			StandHalfCm = Capsule->GetScaledCapsuleHalfHeight();
		}
	}
	Pawn->SetActorLocation(RideAnchor->GetComponentTransform().TransformPosition(
		WiesbadenBusInterior::PassengerSeatCm(InteriorSpec()) + FVector(0.0, 0.0, StandHalfCm)));
	UE_LOG(LogWbBus, Log, TEXT("Bus-Linie %s: Fahrgast eingestiegen (Slot %d, Wagen %d)."),
		*LineRef, Best, RiddenVehicleId);
}

void AWiesbadenBusRoute::SetupAnnouncements()
{
	// Ein 2D-"Cabin-PA"-AudioComponent (nicht raeumlich - der Fahrgast sitzt drin)
	// ueber den Voice-Bus des Mischpults. Fehlt das Mix-Asset, bleibt der Klang
	// ungeroutet (kein Fehler); die Ansagen spielen trotzdem.
	AnnounceAudio = NewObject<UAudioComponent>(this, TEXT("BusAnnounceAudio"));
	if (AnnounceAudio)
	{
		AnnounceAudio->SetupAttachment(Root);
		AnnounceAudio->bAutoActivate = false;
		AnnounceAudio->bAllowSpatialization = false;
		AnnounceAudio->SoundClassOverride = UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::Voice);
		AnnounceAudio->RegisterComponent();
	}

	// Vorgerenderte TTS-Wellen je Halte. WELCHE Welle zu welcher Halte gehoert,
	// steht in Data/Raw/Bus/announce_line<ref>.json (Tools/build_announce_index.py):
	// Zuordnung ueber den HALTENAMEN, nicht ueber die Reihenfolge.
	//
	// Warum ueber den Namen: die Wellen hiessen frueher A_00..A_18 nach ihrer
	// Position in der damals halben Linie-6-Liste. Seit die Linie 6 bis
	// Mainz-Gonsenheim durchgebaut ist, hat sie 40 Halte - eine Zuordnung nach
	// Index wuerde den Text von "Adlerstrasse" an der 5. Halte abspielen, wo jetzt
	// eine andere liegt. Nach Name zugeordnet bleibt jede Ansage an ihrem Halt,
	// und Halte ohne Welle bleiben einfach still.
	const int32 NumStops = LineRoute->Route.StopArcCm.Num();
	AnnounceWaves.Reset();
	AnnounceWaves.SetNum(NumStops);

	TMap<FString, FString> ByName;   // Haltename -> Objektpfad der Welle
	const FString IndexPath = FPaths::ProjectDir() / TEXT("Data/Raw/Bus")
		/ FString::Printf(TEXT("announce_line%s.json"), *LineRef);
	FString IndexJson;
	if (FFileHelper::LoadFileToString(IndexJson, *IndexPath))
	{
		TSharedPtr<FJsonObject> Obj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(IndexJson);
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (FJsonSerializer::Deserialize(Reader, Obj) && Obj.IsValid()
			&& Obj->TryGetArrayField(TEXT("stops"), Arr) && Arr)
		{
			for (const TSharedPtr<FJsonValue>& V : *Arr)
			{
				const TSharedPtr<FJsonObject>* E = nullptr;
				FString Name, Asset;
				if (V.IsValid() && V->TryGetObject(E) && E && E->IsValid()
					&& (*E)->TryGetStringField(TEXT("name"), Name)
					&& (*E)->TryGetStringField(TEXT("asset"), Asset) && !Asset.IsEmpty())
				{
					ByName.Add(Name, Asset);
				}
			}
		}
	}

	int32 Loaded = 0;
	for (int32 i = 0; i < NumStops; ++i)
	{
		const FString* Path = LineRoute->File.StopNames.IsValidIndex(i)
			? ByName.Find(LineRoute->File.StopNames[i]) : nullptr;
		if (!Path)
		{
			// Rueckfall auf die alte Ablage (/Game/Audio/Bus/Announce/A_<i>) - nur
			// wenn es gar keine Zuordnungsdatei gibt.
			if (ByName.Num() == 0)
			{
				const FString ObjPath = FString::Printf(TEXT("/Game/Audio/Bus/Announce/A_%02d.A_%02d"), i, i);
				USoundBase* Wave = LoadObject<USoundBase>(nullptr, *ObjPath);
				AnnounceWaves[i] = Wave;
				if (Wave) { ++Loaded; }
			}
			continue;
		}
		USoundBase* Wave = LoadObject<USoundBase>(nullptr, **Path);
		AnnounceWaves[i] = Wave;
		if (Wave) { ++Loaded; }
	}
	UE_LOG(LogWbBus, Log,
		TEXT("Bus-Ansagen Linie %s: %d/%d Halte mit Welle (%d Namen in %s), Voice-Bus=%d."),
		*LineRef, Loaded, NumStops, ByName.Num(),
		ByName.Num() > 0 ? TEXT("Zuordnungsdatei") : TEXT("keiner Zuordnungsdatei"),
		(AnnounceAudio && AnnounceAudio->SoundClassOverride) ? 1 : 0);
}

int32 AWiesbadenBusRoute::NextStopIndex(const WiesbadenBusLine::FBusState& St) const
{
	// Datenreine Kernlogik in WiesbadenBusLine (unit-getestet BusLine.NextStop).
	return WiesbadenBusLine::NextStopIndex(St.ArcLengthCm, St.bForward, LineRoute->Route.StopArcCm);
}

void AWiesbadenBusRoute::UpdateStopAnnouncement(const WiesbadenBusLine::FBusState& St)
{
	const int32 Next = NextStopIndex(St);
	// Nur beim WECHSEL der naechsten Halte ansagen (einmal je Uebergang), nicht je Tick.
	if (Next == INDEX_NONE || Next == LastAnnouncedStop) { return; }
	LastAnnouncedStop = Next;
	PlayStopAnnouncement(Next);
}

void AWiesbadenBusRoute::PlayStopAnnouncement(int32 StopIndex)
{
	if (!AnnounceAudio || !AnnounceWaves.IsValidIndex(StopIndex)) { return; }
	USoundBase* Wave = AnnounceWaves[StopIndex].Get();
	if (!Wave) { return; }   // fehlende Welle -> stille Halte, kein Ducking

	AnnounceAudio->SetSound(Wave);
	AnnounceAudio->Play();
	SetAudioDucking(true);   // Musik + Ambiente ueber das Mischpult absenken

	// Ducking nach der Ansagedauer sicher wieder aufheben - per Timer, unabhaengig
	// vom OnAudioFinished-Delegat (das bei unterbrochenen Ansagen ausbleiben kann).
	if (UWorld* W = GetWorld())
	{
		const float Dur = FMath::Max(Wave->GetDuration(), 0.5f) + 0.35f;
		W->GetTimerManager().ClearTimer(DuckTimer);
		W->GetTimerManager().SetTimer(DuckTimer, this, &AWiesbadenBusRoute::EndDucking, Dur, false);
	}

	UE_LOG(LogWbBus, Log, TEXT("Bus-Ansage: Naechster Halt %s (Halt %d, spielt=%d, %.1f s)."),
		LineRoute->File.StopNames.IsValidIndex(StopIndex) ? *LineRoute->File.StopNames[StopIndex] : TEXT("?"), StopIndex,
		AnnounceAudio->IsPlaying() ? 1 : 0, Wave->GetDuration());
}

void AWiesbadenBusRoute::EndDucking()
{
	SetAudioDucking(false);
}

void AWiesbadenBusRoute::StopAnnouncement()
{
	if (AnnounceAudio) { AnnounceAudio->Stop(); }
	if (UWorld* W = GetWorld()) { W->GetTimerManager().ClearTimer(DuckTimer); }
	SetAudioDucking(false);
	LastAnnouncedStop = INDEX_NONE;
}

void AWiesbadenBusRoute::SetAudioDucking(bool bActive)
{
	if (UWorld* W = GetWorld())
	{
		if (UGameInstance* GI = W->GetGameInstance())
		{
			if (UWiesbadenAudioSubsystem* Audio = GI->GetSubsystem<UWiesbadenAudioSubsystem>())
			{
				Audio->SetDuckingActive(bActive);
			}
		}
	}
}