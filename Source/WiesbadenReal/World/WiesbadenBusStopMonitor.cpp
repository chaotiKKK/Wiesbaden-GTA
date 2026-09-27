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
#include "EngineUtils.h"
#include "GIS/WiesbadenWorldBuilder.h"

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

	// ESWE-Haltestelle aus Blender (Tools/import_eswe_haltestelle.py).
	ShelterMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/EsweHalte/SM_WbEsweWartehalle.SM_WbEsweWartehalle"));
	MastMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/EsweHalte/SM_WbEsweHaltemast.SM_WbEsweHaltemast"));
	DfiMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Props/EsweHalte/SM_WbEsweDfi.SM_WbEsweDfi"));
	if (!ShelterMesh || !MastMesh || !DfiMesh)
	{
		UE_LOG(LogWbMonitor, Warning, TEXT("ESWE-Haltestelle: Meshes fehlen (Halle %d, Mast %d, DFI %d) - Tools/import_eswe_haltestelle.cmd laufen lassen."),
			ShelterMesh ? 1 : 0, MastMesh ? 1 : 0, DfiMesh ? 1 : 0);
	}

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

	bReady = (LineRoute->WorldPath.Num() >= 2 && LineRoute->Route.StopArcCm.Num() >= 2 && DfiMesh && MastMesh);
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
	// Je Fahrtrichtung eine Haltestelle, jeweils RECHTS der Fahrt: wer in die
	// eine Richtung faehrt, wartet auf der einen Seite, die Gegenrichtung hat
	// ihre eigene Halte (mit Ziel und Durchfahrtszeit IHRER Richtung).
	BuildMonitorsForSide(true, Wanted, SpeedCmS);
	BuildMonitorsForSide(false, Wanted, SpeedCmS);
	UE_LOG(LogWbMonitor, Log, TEXT("Abfahrtsmonitor Linie %s: %d DFI-Stelen, %d eigene ESWE-Haltestellen (%s)."),
		*LineRef, Monitors.Num(), Furniture.Num(),
		LineRoute->Route.HasReturnLeg() ? TEXT("Gegenrichtung an ihren eigenen Halten") : TEXT("Gegenrichtung gegenueber"));
}

UStaticMeshComponent* AWiesbadenBusStopMonitor::AddPart(UStaticMesh* Mesh, const FVector& Location, const FQuat& Rot)
{
	if (!Mesh) { return nullptr; }
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetStaticMesh(Mesh);
	C->SetupAttachment(Root);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->RegisterComponent();
	C->SetWorldLocationAndRotation(Location, Rot);
	Parts.Add(C);
	return C;
}

UTextRenderComponent* AWiesbadenBusStopMonitor::AddText(const FVector& Location, const FVector& Facing,
	float Size, const FColor& Color, const FString& Text)
{
	UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
	T->SetupAttachment(Root);
	T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	T->RegisterComponent();
	T->SetTextRenderColor(Color);
	T->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	T->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	T->SetWorldSize(Size);
	// Eine Textflaeche ist von ihrer +X-Seite lesbar.
	T->SetWorldLocationAndRotation(Location, FRotationMatrix::MakeFromXZ(Facing, FVector::UpVector).Rotator());
	T->SetText(FText::FromString(Text));
	Texts.Add(T);
	return T;
}

bool AWiesbadenBusStopMonitor::JoinStop(const FVector& Base, const FVector& Dir, const FString& Line,
	FStopFurniture& OutStop, int32& OutDfiSlot)
{
	for (FStopFurniture& F : Furniture)
	{
		// Dieselbe Halte: nah beieinander UND dieselbe Fahrtrichtung (die
		// Gegenrichtung steht auf der anderen Strassenseite).
		if (FVector::Dist2D(F.Base, Base) > 1500.0 || FVector::DotProduct(F.Dir, Dir) < 0.7) { continue; }
		if (!F.Lines.Contains(Line))
		{
			F.Lines.Add(Line);
			F.Lines.Sort([](const FString& A, const FString& B) { return FCString::Atoi(*A) < FCString::Atoi(*B); });
			const FString Joined = FString::Join(F.Lines, TEXT("   "));
			for (UTextRenderComponent* T : F.LineTexts) { if (T) { T->SetText(FText::FromString(Joined)); } }
		}
		OutDfiSlot = F.DfiCount++;
		OutStop = F;
		return true;
	}
	return false;
}

void AWiesbadenBusStopMonitor::BuildMonitorsForSide(bool bForward, const TArray<FString>& Wanted, double SpeedCmS)
{
	const WiesbadenBusLineFile::FLineRoute& L = *LineRoute;
	// Eigener Rueckweg: Linie, Halte und Namen der Gegenrichtungs-Relation.
	// Ohne ihn faehrt der Bus die Hinweg-Linie rueckwaerts - dann steht die
	// Halte der Gegenrichtung der Hinweg-Halte gegenueber (altes Verhalten).
	const bool bOwnReturn = !bForward && L.Route.HasReturnLeg();
	const TArray<FVector>& Path = bOwnReturn ? L.ReturnWorldPath : L.WorldPath;
	const TArray<double>& Arc = bOwnReturn ? L.ReturnArcCm : L.ArcCm;
	const TArray<double>& StopArc = bOwnReturn ? L.Route.ReturnStopArcCm : L.Route.StopArcCm;
	const TArray<FString>& Names = bOwnReturn ? L.File.ReturnStopNames : L.File.StopNames;
	// monitor_stops "*" = alle Halte: auf dem Rueckweg dann ALLE Rueckweg-Halte
	// (deren Namen teils anders lauten als die der Hinfahrt).
	const bool bAllStops = Wanted == L.File.StopNames;
	const TArray<FString>& WantHere = (bOwnReturn && bAllStops) ? Names : Wanted;
	const FString Dest = (bForward || L.File.Origin.IsEmpty()) ? Destination : L.File.Origin;
	// Strassennetz fuer die Fahrbahnkante (gebackene Karte; sonst Vorgabe).
	const FRoadNetwork* Net = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(GetWorld()); It; ++It)
	{
		if (!It->RoadNetwork.IsEmpty()) { Net = &It->RoadNetwork; break; }
	}

	for (int32 c = 0; c < WantHere.Num(); ++c)
	{
		const int32 Idx = Names.IndexOfByKey(WantHere[c]);
		if (Idx == INDEX_NONE || !StopArc.IsValidIndex(Idx)) { continue; }
		// Am Ausstieg (letzte Halte einer Richtung mit eigenem Rueckweg) steigt
		// niemand ein - dort keine Abfahrtstafel. Die Einstiegshaltestelle am
		// Nordfriedhof ist Halt 0 des Hinwegs und bekommt ihre eigene.
		if (L.Route.HasReturnLeg() && Idx == StopArc.Num() - 1) { continue; }
		FVector Pos, Tangent;
		if (!WiesbadenRailTransport::SamplePolyline(Path, Arc, StopArc[Idx], Pos, Tangent)) { continue; }
		FVector Dir = Tangent.GetSafeNormal2D();
		if (!bForward && !bOwnReturn) { Dir = -Dir; }   // rueckwaerts auf der Hinweg-Linie
		// Rechte Hand der Fahrt (Welt: Ost +X, SUED +Y - siehe Bus-Actor).
		const FVector Side = FVector(-Dir.Y, Dir.X, 0.0).GetSafeNormal();
		double RoadZ = Pos.Z;
		ResolveGround(Pos.X, Pos.Y, RoadZ);
		// Bordsteinkante aus dem Strassennetz (dieselbe, an der der Bus haelt),
		// sonst die Vorgabe. Nie naeher als die Aussenseite des haltenden Busses.
		double Kerb = CurbOffsetCm;
		double NetKerb = 0.0;
		const bool bNetKerb = Net && WiesbadenBusLineFile::RightKerbOffsetCm(*Net, Pos, Dir, NetKerb) && NetKerb > 0.0;
		if (bNetKerb) { Kerb = FMath::Max(NetKerb + 10.0, 180.0); }

		FStopFurniture Stop;
		int32 DfiSlot = 0;
		bool bShared = false;
		// Teilt sich die Halte mit einer anderen Linie (3 und 6 ab Nordfriedhof)?
		for (TActorIterator<AWiesbadenBusStopMonitor> It(GetWorld()); It && !bShared; ++It)
		{
			bShared = It->JoinStop(Pos + Side * Kerb, Dir, LineRef, Stop, DfiSlot);
		}
		bool bHall = false;
		if (!bShared)
		{
			// Bordsteinkante; steht dahinter Bebauung (Boden viel hoeher als die
			// Fahrbahn), in 40-cm-Schritten zur Fahrbahn ruecken.
			// Bekannte Kante: nicht in die Fahrbahn ruecken, nur die Halle weglassen.
			double Curb = Kerb;
			const double MinOff = bNetKerb ? Kerb : Kerb - 240.0;
			for (double Off = Kerb; Off >= MinOff; Off -= 40.0)
			{
				double BackZ = RoadZ;
				const FVector Back = Pos + Side * (Off + 230.0);
				if (ResolveGround(Back.X, Back.Y, BackZ) && FMath::Abs(BackZ - RoadZ) < 80.0) { Curb = Off; bHall = true; break; }
			}
			double BaseZ = RoadZ + 15.0;   // Bordstein
			const FVector BaseXY = Pos + Side * Curb;
			double Probe = BaseZ;
			if (ResolveGround(BaseXY.X + Side.X * 60.0, BaseXY.Y + Side.Y * 60.0, Probe) && FMath::Abs(Probe - RoadZ) < 60.0) { BaseZ = Probe; }
			Stop.Base = FVector(BaseXY.X, BaseXY.Y, BaseZ);
			Stop.Dir = Dir;
			Stop.Side = Side;
			Stop.Lines.Add(LineRef);
			// Mesh-Achsen: X entlang der Fahrt, +Y vom Bordstein weg (rechts der
			// Fahrt), Z hoch - nur eine Drehung um die Hochachse. (Mit -Y nach
			// aussen waere es eine Spiegelung: MakeFromXY(Dir, -Side) kippte die
			// Z-Achse nach unten und stellte alles kopfueber unter die Strasse.)
			const FQuat Rot = FRotationMatrix::MakeFromXZ(Dir, FVector::UpVector).ToQuat();
			auto At = [&Stop](double X, double Y, double Z)
			{
				return Stop.Base + Stop.Dir * X + Stop.Side * Y + FVector::UpVector * Z;
			};
			if (bHall)
			{
				if (UStaticMeshComponent* Hall = AddPart(ShelterMesh, At(0.0, 70.0, 0.0), Rot))
				{
					// Glas und Pfosten halten den Spieler auf; Boden-/Spurstrahlen
					// (WorldStatic) gehen hindurch.
					Hall->SetCollisionObjectType(ECC_WorldDynamic);
					Hall->SetCollisionResponseToAllChannels(ECR_Ignore);
					Hall->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
					Hall->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				}
			}
			// Haltemast vor dem Bus (Vordertuer), Schilder zum ankommenden Bus.
			const double MastX = 520.0, MastY = 45.0;
			AddPart(MastMesh, At(MastX, MastY, 0.0), Rot);
			const FString& Name = Names[Idx];
			const float NameSize = FMath::Clamp(46.0f / (0.55f * (float)FMath::Max(Name.Len(), 1)), 2.5f, 6.5f);
			for (const double Face : { -1.0, 1.0 })
			{
				const double FX = MastX - 5.5 + Face * 1.3;
				const FVector Facing = Stop.Dir * Face;
				AddText(At(FX, MastY, 223.5), Facing, NameSize, FColor(20, 20, 20), Name);
				Stop.LineTexts.Add(AddText(At(FX, MastY, 205.0), Facing, 10.0f, FColor(10, 10, 10), LineRef));
				AddText(At(FX, MastY, 239.0), Facing, 4.5f, FColor(240, 240, 240), TEXT("ESWE Verkehr"));
			}
			Furniture.Add(Stop);
		}

		// DFI-Stele (je Linie eine) hinter der Halle.
		const double DfiX = -330.0 - 150.0 * DfiSlot, DfiY = 50.0;
		const FQuat DfiRot = FRotationMatrix::MakeFromXZ(Stop.Dir, FVector::UpVector).ToQuat();
		const FVector DfiFoot = Stop.Base + Stop.Dir * DfiX + Stop.Side * DfiY;
		AddPart(DfiMesh, DfiFoot, DfiRot);
		FMonitor M;
		M.StopIndex = Idx;
		M.Name = Names[Idx];
		M.bForward = bForward;
		M.bReturnPath = bOwnReturn;
		M.Destination = Dest;
		M.OffsetSeconds = bOwnReturn
			? WiesbadenBusLine::SecondsToReturnStop(L.Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds, Idx)
			: WiesbadenBusLine::SecondsToStopOnLeg(L.Route, SpeedCmS, StopDwellSeconds, TerminusDwellSeconds, Idx, bForward);
		const FVector Screen = DfiFoot + FVector::UpVector * 225.0;
		M.Text = AddText(Screen - Stop.Side * 9.0, -Stop.Side, 5.0f, FColor(255, 178, 20), M.Name);
		M.TextBack = AddText(Screen + Stop.Side * 9.0, Stop.Side, 5.0f, FColor(255, 178, 20), M.Name);
		Monitors.Add(M);
		// Belegzeile je Halte: Richtung, Ort, Ausstattung und die Durchfahrtszeit.
		UE_LOG(LogWbMonitor, Log, TEXT("Halte %s %d '%s' auf (%.0f, %.0f, %.0f): %s - Durchfahrt Ziel %s nach %.0f s."),
			bForward ? TEXT("Hinfahrt") : (bOwnReturn ? TEXT("Rueckweg") : TEXT("Gegenrichtung")), Idx, *M.Name,
			DfiFoot.X, DfiFoot.Y, DfiFoot.Z,
			bShared ? TEXT("DFI an der Halte einer anderen Linie") : (bHall ? TEXT("Halle+Mast+DFI") : TEXT("Mast+DFI (kein Platz fuer die Halle)")),
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
		if (M.TextBack) { M.TextBack->SetText(FText::FromString(S)); }
	}
}
