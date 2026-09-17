// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenBusStopMonitor.h"

#include "World/WiesbadenRailTransport.h"
#include "GIS/GeoCoordinateConverter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbMonitor, Log, All);

AWiesbadenBusStopMonitor::AWiesbadenBusStopMonitor()
{
	PrimaryActorTick.bCanEverTick = true;
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
}

void AWiesbadenBusStopMonitor::LoadLine()
{
	// Die Datei oeffnet der gemeinsame Leser; der Bus-Actor derselben Linie haelt
	// danach DASSELBE Objekt. Hier steht nur, was der Monitor daraus uebernimmt -
	// so koennen Anzeige und Busse nicht auseinanderlaufen.
	if (!Converter) { return; }
	LineRoute = WiesbadenBusLineFile::ReadLine(LineFile, *Converter);
	const WiesbadenBusLineFile::FLineFile& F = LineRoute->File;
	if (!F.bLoaded) { return; }

	// Liniennummer, Zieltext, Dienstwerte und die Halte fuer die Saeulen stehen in
	// der Liniendatei, damit der Monitor fuer eine zweite Linie nur eine andere
	// Datei braucht. Die Eigenschaften am Actor sind die Vorgabe, wenn ein Feld in
	// der Datei fehlt.
	if (!F.Ref.IsEmpty()) { LineRef = F.Ref; }
	// Zieltext aus der Datei, auch wenn am Actor eine Vorgabe steht: sonst stuende
	// auf der einen Tafel der Dateiname des Endpunkts ('Mainz Gonsenheim Wildpark')
	// und auf der gegenueberliegenden der kurze Schildtext - zwei Schreibweisen
	// fuer denselben Endpunkt.
	if (!F.Destination.IsEmpty()) { Destination = F.Destination; }
	if (F.HeadwaySeconds > 0.0) { HeadwaySeconds = (float)F.HeadwaySeconds; }
	if (F.TerminusDwellSeconds >= 0.0) { TerminusDwellSeconds = (float)F.TerminusDwellSeconds; }
	// Halte fuer die Saeulen: NAMEN aus der Datei. Sie kommen bewusst aus dem JSON
	// und nicht aus einem C++-Literal - die Namen tragen Umlaute, und eine
	// Quelldatei ohne BOM liest MSVC in der lokalen Codepage (dann steht
	// "Duerer" statt "Duererplatz" im Vergleich und die Halte wird nicht gefunden).
	if (MonitorStopNames.Num() == 0)
	{
		MonitorStopNames = F.MonitorStops;
	}
}

void AWiesbadenBusStopMonitor::LoadSchedule()
{
	// Auch der Fahrplan kommt aus dem einen Leser (einmal je Datei).
	Schedule = WiesbadenBusLineFile::ReadSchedule(ScheduleFile);
}

bool AWiesbadenBusStopMonitor::ResolveGround(double X, double Y, double& OutZ) const
{
	UWorld* World = GetWorld();
	if (!World) { return false; }
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbMonitorGround), true);
	Params.AddIgnoredActor(this);
	if (World->LineTraceSingleByChannel(Hit, FVector(X, Y, 1000000.0), FVector(X, Y, -200000.0),
		ECC_WorldStatic, Params) && !Hit.bStartPenetrating)
	{
		OutZ = Hit.Location.Z;
		return true;
	}
	return false;
}

void AWiesbadenBusStopMonitor::BeginPlay()
{
	Super::BeginPlay();
	// Erst die Georeferenz, dann die Linie: der gemeinsame Leser projiziert die
	// Datei damit in die Weltkoordinaten der gebackenen Karte.
	Converter = NewObject<UGeoCoordinateConverter>(this);
	Converter->InitializeWithWiesbadenOrigin();
	LoadLine();
	LoadSchedule();

	PoleMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	PanelMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	PoleMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/DFI/M_WbDfiPole.M_WbDfiPole"));
	PanelMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Props/DFI/M_WbDfiPanel.M_WbDfiPanel"));

	// Dienst wie bei den Bussen aufsetzen: dieselben Wagen, dieselben Zeiten.
	// Kommandozeile schlaegt die Eigenschaft (wie beim Bus, fuer Vergleichslaeufe).
	if (FParse::Param(FCommandLine::Get(), TEXT("WbBusSchedule"))) { bContinuousService = false; }
	if (FParse::Param(FCommandLine::Get(), TEXT("WbBusFleet"))) { bContinuousService = true; }
	if (bContinuousService)
	{
		WiesbadenBusLine::FServiceConfig Cfg;
		Cfg.CruiseSpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
		Cfg.StopDwellSeconds = StopDwellSeconds;
		Cfg.TerminusDwellSeconds = TerminusDwellSeconds;
		Cfg.HeadwaySeconds = (HeadwaySeconds > 0.0f) ? HeadwaySeconds : 1200.0;
		Cfg.MaxBuses = FMath::Max(MaxBuses, 2);
		WiesbadenBusLine::BuildFleet(LineRoute->Route, Cfg, CycleSeconds, Fleet);
	}

	bReady = (LineRoute->WorldPath.Num() >= 2 && LineRoute->Route.StopArcCm.Num() >= 2 && PoleMesh && PanelMesh);
	if (bReady) { BuildMonitors(); }
	UE_LOG(LogWbMonitor, Log,
		TEXT("Abfahrtsmonitor Linie %s bereit=%d: %d Saeulen von %d gewuenschten, %d Halten, %d Wagen (Umlauf %.0f min)."),
		*LineRef, bReady ? 1 : 0, Monitors.Num(),
		MonitorStopNames.Num() > 0 ? MonitorStopNames.Num() : 5,
		LineRoute->Route.StopArcCm.Num(), Fleet.Num(), CycleSeconds / 60.0);
}

void AWiesbadenBusStopMonitor::BuildMonitors()
{
	const double SpeedCmS = FMath::Max(SpeedKmh, 1.0f) * 100000.0 / 3600.0;
	// Die gewuenschten Halten ueber ihren NAMEN suchen (Vorgabe: die fuenf aus dem
	// Linie-6-Pilot). Namen statt Indizes, weil ein Index nach jedem Ausbau der
	// Linie auf eine andere Halte zeigt.
	const TArray<FString>& Wanted = MonitorStopNames;
	if (Wanted.Num() == 0)
	{
		UE_LOG(LogWbMonitor, Warning,
			TEXT("Abfahrtsmonitor Linie %s: keine Halte gewaehlt (monitor_stops in %s fehlt) - keine Saeulen."),
			*LineRef, *LineFile);
		return;
	}
	TArray<int32> Indices;
	for (const FString& Want : Wanted)
	{
		const int32 Found = LineRoute->File.StopNames.IndexOfByKey(Want);
		if (Found == INDEX_NONE)
		{
			UE_LOG(LogWbMonitor, Warning,
				TEXT("Abfahrtsmonitor Linie %s: Halte '%s' gibt es in %s nicht - keine Saeule."),
				*LineRef, *Want, *LineFile);
			continue;
		}
		Indices.Add(Found);
	}
	// ZWEI Saeulen je Halte, je Strassenseite eine Richtung: wer in die eine
	// Richtung faehrt, wartet auf der einen Seite; die Gegenseite braucht ihre
	// eigene Tafel (mit dem Ziel und der Durchfahrtszeit IHRER Richtung).
	BuildMonitorsForSide(true, Indices, Wanted, SpeedCmS);
	BuildMonitorsForSide(false, Indices, Wanted, SpeedCmS);
	UE_LOG(LogWbMonitor, Log, TEXT("Abfahrtsmonitor Linie %s: %d Halte -> %d Saeulen (2 je Halte, beide Strassenseiten)."),
		*LineRef, Indices.Num(), Monitors.Num());
}

void AWiesbadenBusStopMonitor::BuildMonitorsForSide(bool bForward, const TArray<int32>& Indices,
	const TArray<FString>& Wanted, double SpeedCmS)
{
	const double PoleH = 225.0, PoleR = 7.0, PW = 250.0, PH = 140.0, PT = 12.0, PanelZ = 220.0;
	// Basismasse der Engine-Formen abtasten -> korrekte Skalierung unabhaengig
	// von deren Groesse (Cube 100, Zylinder abweichend).
	const FVector PoleSize = PoleMesh->GetBoundingBox().GetSize();
	const FVector PanelSize = PanelMesh->GetBoundingBox().GetSize();

	for (int32 c = 0; c < Indices.Num(); ++c)
	{
		const int32 Idx = Indices[c];
		if (!LineRoute->Route.StopArcCm.IsValidIndex(Idx)) { continue; }
		FVector Pos, Tangent;
		if (!WiesbadenRailTransport::SamplePolyline(LineRoute->WorldPath, LineRoute->ArcCm, LineRoute->Route.StopArcCm[Idx], Pos, Tangent)) { continue; }
		const FVector Dir = Tangent.GetSafeNormal();
		const FVector RightDir = FVector(-Dir.Y, Dir.X, 0.0).GetSafeNormal();   // Bordsteinseite (wie Bus)
		// Bordsteinkante DIESER Richtung: die beiden Saeulen einer Halte stehen
		// sich gegenueber, und jede nennt die Durchfahrtszeit ihrer Richtung.
		const FVector SideDir = bForward ? RightDir : -RightDir;
		const FVector MXY = Pos + SideDir * SidewalkOffsetCm;               // Saeule auf dem Gehweg
		double GZ = Pos.Z;
		ResolveGround(MXY.X, MXY.Y, GZ);
		const FVector Fwd = -SideDir;   // Panel/Text blicken zur Strasse (zu den Wartenden)
		const FRotator FaceRot = FRotationMatrix::MakeFromXZ(Fwd, FVector::UpVector).Rotator();

		// Mast
		if (UStaticMeshComponent* Pole = NewObject<UStaticMeshComponent>(this))
		{
			Pole->SetStaticMesh(PoleMesh);
			Pole->SetupAttachment(Root);
			Pole->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pole->RegisterComponent();
			if (PoleMat) { Pole->SetMaterial(0, PoleMat); }
			Pole->SetWorldScale3D(FVector((PoleR * 2.0) / FMath::Max(PoleSize.X, 1.0),
				(PoleR * 2.0) / FMath::Max(PoleSize.Y, 1.0), PoleH / FMath::Max(PoleSize.Z, 1.0)));
			Pole->SetWorldLocation(FVector(MXY.X, MXY.Y, GZ + PoleH * 0.5));
			Parts.Add(Pole);
		}
		// Panel (dunkel), blickt zur Strasse
		if (UStaticMeshComponent* Panel = NewObject<UStaticMeshComponent>(this))
		{
			Panel->SetStaticMesh(PanelMesh);
			Panel->SetupAttachment(Root);
			Panel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Panel->RegisterComponent();
			if (PanelMat) { Panel->SetMaterial(0, PanelMat); }
			Panel->SetWorldScale3D(FVector(PT / FMath::Max(PanelSize.X, 1.0),
				PW / FMath::Max(PanelSize.Y, 1.0), PH / FMath::Max(PanelSize.Z, 1.0)));   // X=Dicke(Fwd), Y=Breite, Z=Hoehe
			Panel->SetWorldRotation(FaceRot);
			Panel->SetWorldLocation(FVector(MXY.X, MXY.Y, GZ + PanelZ) + Fwd * (PoleR + PT * 0.5));
			Parts.Add(Panel);
		}
		// Text (bernsteingelb), oben-links auf der Panel-Front
		UTextRenderComponent* Txt = NewObject<UTextRenderComponent>(this);
		Txt->SetupAttachment(Root);
		Txt->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Txt->RegisterComponent();
		Txt->SetTextRenderColor(FColor(255, 178, 20));
		Txt->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
		Txt->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextTop);
		Txt->SetWorldSize(8.0f);
		Txt->SetWorldRotation(FaceRot);
		const FVector Up(0.0, 0.0, 1.0);
		// Panel-Front, oben mittig (zentriert -> unabhaengig von der Dir-Richtung).
		const FVector Front = FVector(MXY.X, MXY.Y, GZ + PanelZ) + Fwd * (PoleR + PT + 1.5);
		Txt->SetWorldLocation(Front + Up * (PH * 0.5 - 12.0));
		Texts.Add(Txt);

		FMonitor M;
		M.StopIndex = Idx;
		M.Name = LineRoute->File.StopNames.IsValidIndex(Idx) ? LineRoute->File.StopNames[Idx] : Wanted[c];
		M.bForward = bForward;
		// Zieltext DIESER Saeule: an der Gegenseite faehrt der Bus zum anderen
		// Endpunkt - sonst stuende auf beiden Tafeln dasselbe Ziel.
		M.Destination = (bForward || LineRoute->File.Origin.IsEmpty()) ? Destination : LineRoute->File.Origin;
		M.OffsetSeconds = WiesbadenBusLine::SecondsToStopOnLeg(LineRoute->Route, SpeedCmS,
			StopDwellSeconds, TerminusDwellSeconds, Idx, bForward);
		M.Text = Txt;
		Monitors.Add(M);
		// Belegzeile je Saeule: Seite, Halte, Position und die Durchfahrtszeit der
		// Richtung, die diese Tafel ankuendigt (die beiden Saeulen einer Halte
		// stehen sich gegenueber und nennen verschiedene Zeiten).
		UE_LOG(LogWbMonitor, Log, TEXT("Saeule %s an Halt %d '%s' auf (%.0f, %.0f, %.0f) - Durchfahrt Ziel %s nach %.0f s."),
			bForward ? TEXT("Hinfahrt") : TEXT("Gegenrichtung"), Idx, *M.Name, MXY.X, MXY.Y, GZ,
			*M.Destination, M.OffsetSeconds);
	}
}

void AWiesbadenBusStopMonitor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bReady || Monitors.Num() == 0) { return; }
	UpdateAccum += DeltaSeconds;
	if (UpdateAccum < 1.0) { return; }   // Sekunden-Takt reicht
	UpdateAccum = 0.0;

	const double WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const double ServiceSeconds = (double)ServiceStartHour * 3600.0 + WorldTime;

	for (const FMonitor& M : Monitors)
	{
		if (!M.Text) { continue; }
		TArray<double> Until;
		if (bContinuousService && Fleet.Num() > 0 && CycleSeconds > 0.0)
		{
			// Dauerbetrieb: die naechste Durchfahrt JEDES Wagens. Damit stehen hier
			// genau die Zeiten, die die Busse fahren - ohne Fahrplandatei.
			WiesbadenBusLine::FleetDepartures(ServiceSeconds, Fleet, CycleSeconds,
				M.OffsetSeconds, DisplayRows, Until);
		}
		else
		{
			WiesbadenBusLine::NextDepartures(ServiceSeconds, *Schedule, M.OffsetSeconds, DisplayRows, Until);
		}
		FString S = M.Name + TEXT("\n");
		if (Until.Num() == 0)
		{
			S += TEXT("kein Verkehr");
		}
		for (const double U : Until)
		{
			const int32 Min = (int32)FMath::FloorToDouble(U / 60.0);
			if (Min <= 0) { S += FString::Printf(TEXT("%s  %s   sofort\n"), *LineRef, *M.Destination); }
			else { S += FString::Printf(TEXT("%s  %s   %d min\n"), *LineRef, *M.Destination, Min); }
		}
		M.Text->SetText(FText::FromString(S));
	}
}
