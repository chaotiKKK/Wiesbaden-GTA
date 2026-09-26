// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenPlayerController.h"

#include "WiesbadenReal.h"
#include "Core/WiesbadenDevActions.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "UI/WiesbadenMinimap.h"
#include "TimerManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenVehicleControl.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "Vehicles/WiesbadenHeliLightRig.h"
#include "Vehicles/WiesbadenHelicopterAutopilot.h"
#include "Vehicles/WiesbadenVehicleCameraComponent.h"
#include "Vehicles/WiesbadenVehicleTestHarness.h"
#include "World/WiesbadenCitySubsystem.h"
#include "NPC/WiesbadenPursuerActor.h"
#include "HAL/IConsoleManager.h"
#include "World/WiesbadenDennoShop.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "Weapons/WiesbadenWeaponComponent.h"
#include "Weapons/WiesbadenWeaponSpec.h"

namespace
{
	/**
	 * Vorgabedauer der Dev-Flugbefehle, in Sekunden.
	 *
	 * NOTWENDIG, WEIL DIE ENGINE KEIN ARGUMENT ANGEBEN KANN: ein
	 * UFUNCTION(Exec) laesst sich ueber die Konsole nur OHNE Argument
	 * aufrufen. UObject::CallFunctionByNameWithArguments sucht naemlich nach
	 * einem *Objekt-Property* und nicht nach einem Funktionsparameter -
	 * am 26.09.2026 an der Engine gemessen:
	 *   WbHeliFly 24             -> "Bad or missing property 'Sekunden'"
	 *   WbHeliFly=24             -> keine Fehlermeldung, KEINE Wirkung
	 *   WbHeliFly Sekunden=24    -> "Bad or missing property 'Sekunden'"
	 * Die dokumentierte Form "WbHeliFly 24" hat also nie funktioniert; der
	 * Aufruf blieb ohne Wirkung, die Flugtelemetrie lief nicht an, und
	 * Tools\flight_check.ps1 meldete danach trotzdem Erfolg.
	 *
	 * Deshalb holen die zeitbasierten Dev-Befehle ihre Dauer aus dieser
	 * Variable, wenn kein Argument ankam (Argument 0), und die Werkzeuge
	 * rufen sie argumentfrei auf.
	 */
	/**
	 * Wunschmodus der Fahrzeugkamera: 0 = Folge, 1 = Orbit, 2 = Cockpit.
	 *
	 * Ergaenzt Taste C, die das Spiel nicht zuverlaessig erreicht - in einem
	 * Lauf blieb der Modus auf 0 stehen, und das Bild war damit kein
	 * Cockpitbild. Ueber die Konsole als "wb.HeliKamera=2" zu setzen, ohne
	 * Leerzeichen (das -ExecCmds-Argument darf nicht am Wort getrennt werden).
	 */
	TAutoConsoleVariable<int32> CVarWbHeliKamera(
		TEXT("wb.HeliKamera"), -1,
		TEXT("Kameramodus des besessenen Fahrzeugs: 0 Folge, 1 Orbit, 2 Cockpit. "
			 "-1 = keine Vorgabe. Ohne Tastatur wirksam."));

	/**
	 * Wunschstellung des Bordabzugs: 1 = halten, 0 = loslassen, -1 = keine
	 * Vorgabe.
	 *
	 * Warum es den Befehl gibt: das Bordgeschoetz liest seine Tasten
	 * (Linke Maustaste, Gamepad-RT, Taste V), und keine davon laesst sich von
	 * aussen zuverlaessig halten. Gemessen am 26.09.2026: vier Wege
	 * (Mausklick aufs Fenster, Maus mit minimierter Konsole, WM_LBUTTONDOWN
	 * an das Fenster, wiederholter KEYDOWN) - null Abzugsflanken im Log,
	 * waehrend im selben Lauf 19 Bilder und vier Kameramoduswechsel klappten.
	 * Der Schalter haelt den echten Abzug, es wird also die echte Kanone
	 * abgefeuert - mit Muendungsfeuer, Leuchtspur und Schusszaehler.
	 */
	TAutoConsoleVariable<int32> CVarWbHeliFeuer(
		TEXT("wb.HeliFeuer"), -1,
		TEXT("Bordabzug des besessenen Helikopters: 1 halten, 0 loslassen, "
			 "-1 = keine Vorgabe. Ohne Tastatur wirksam."));

	TAutoConsoleVariable<int32> CVarWbSekunden(
		TEXT("wb.Sekunden"), 24,
		TEXT("Dauer von WbHeliFly / WbHeliYaw / WbDrive in Sekunden. "
			 "Greift, wenn der Befehl ohne Argument aufgerufen wurde - "
			 "die Engine kann ueber -ExecCmds keins uebergeben."));

	/** Angeforderte Dauer, sonst die aus wb.Sekunden. */
	int32 WbSekundenOderVorgabe(int32 Angefordert)
	{
		return Angefordert > 0 ? Angefordert : CVarWbSekunden.GetValueOnGameThread();
	}
}


void AWiesbadenPlayerController::WbTeleport(int32 Ziel)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTeleport %d erkannt, aber kein besessener Pawn."), Ziel);
		return;
	}
	const int32 ZielClamped = FMath::Clamp(Ziel, 0, 2);
	if (Ziel != ZielClamped)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTeleport %d ausserhalb 0-2 - auf Ziel %d begrenzt."), Ziel, ZielClamped);
	}
	const EWiesbadenDevTeleport Target = static_cast<EWiesbadenDevTeleport>(ZielClamped);
	const FVector Von = ControlledPawn->GetActorLocation();
	ControlledPawn->SetActorLocation(FWiesbadenDevActions::TeleportSpawnCm(Target),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Nach = ControlledPawn->GetActorLocation();
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbTeleport %d ausgefuehrt: von (%.0f,%.0f,%.0f) nach (%.0f,%.0f,%.0f), Distanz %.0f cm."),
		ZielClamped, Von.X, Von.Y, Von.Z, Nach.X, Nach.Y, Nach.Z, FVector::Dist(Von, Nach));
}

void AWiesbadenPlayerController::WbWarp(const FString& Strasse)
{
	WarpToStreet(Strasse);
}

bool AWiesbadenPlayerController::WarpToStreet(const FString& Strasse)
{
	UWorld* Welt = GetWorld();
	// "Pawn" ist selbst ein Klassenmember von AController - die Deklaration
	// waere eine Verdeckung (C4458, in diesem Projekt ein Fehler).
	APawn* ControlledPawn = GetPawn();
	if (!Welt || !ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: Warp nach \"%s\" nicht ausgefuehrt: keine Welt oder kein Pawn."),
			*Strasse);
		return false;
	}

	// Das Netz holen derselbe Weg wie im HUD: der Weltbauer, der es gebaut
	// hat, traegt es. Ohne Netz gibt es keine Strassen, und dann gibt es
	// auch nichts, wohin man springen koennte.
	const FRoadNetwork* Netz = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(Welt); It; ++It)
	{
		if (!It->RoadNetwork.Segments.IsEmpty())
		{
			Netz = &It->RoadNetwork;
			break;
		}
	}
	if (!Netz)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: Warp nach \"%s\" nicht ausgefuehrt: kein Strassennetz geladen."),
			*Strasse);
		return false;
	}

	FVector2D ZielXY = FVector2D::ZeroVector;
	float ZielYaw = 0.0f;
	double ZielZ = 0.0;
	if (!FWiesbadenMinimap::FindStreetWarpTarget(*Netz, Strasse, ZielXY, ZielYaw, ZielZ))
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: Warp gescheitert - keine Strasse passt zu \"%s\"."), *Strasse);
		return false;
	}

	// 60 cm ueber der Mittellinie: genug, dass der Karosserie-Boden nicht in
	// der Fahrbahn steckt, und wenig genug, dass es nicht auffaellt - die
	// Physik setzt das Fahrzeug sofort auf.
	const FVector Ziel(ZielXY.X, ZielXY.Y, ZielZ + 60.0);
	const FVector Von = ControlledPawn->GetActorLocation();
	ControlledPawn->SetActorLocationAndRotation(Ziel,
		FRotator(0.0, static_cast<double>(ZielYaw), 0.0),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	const FVector Nach = ControlledPawn->GetActorLocation();

	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: Warp nach \"%s\" ausgefuehrt: von (%.0f,%.0f,%.0f) nach (%.0f,%.0f,%.0f), "
			 "Richtung %.0f Grad, Distanz %.0f cm, als %s."),
		*Strasse, Von.X, Von.Y, Von.Z, Nach.X, Nach.Y, Nach.Z,
		ZielYaw, FVector::Dist(Von, Nach), *ControlledPawn->GetClass()->GetName());
	return true;
}

void AWiesbadenPlayerController::WbResetVehicle()
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbResetVehicle erkannt, aber kein besessener Pawn."));
		return;
	}
	const FRotator Vorher = ControlledPawn->GetActorRotation();
	const FTransform Auf = FWiesbadenDevActions::UprightTransform(ControlledPawn->GetActorTransform());
	ControlledPawn->SetActorLocationAndRotation(Auf.GetLocation(), Auf.Rotator(),
		/*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);
	const FRotator Nachher = ControlledPawn->GetActorRotation();
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbResetVehicle ausgefuehrt: Nick/Roll vorher (%.1f/%.1f) -> nachher (%.1f/%.1f), Yaw %.1f bleibt."),
		Vorher.Pitch, Vorher.Roll, Nachher.Pitch, Nachher.Roll, Nachher.Yaw);
}

void AWiesbadenPlayerController::WbTraffic(int32 An)
{
	const UWorld* World = GetWorld();
	UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbTraffic %d erkannt, aber kein City-Subsystem."), An);
		return;
	}
	const float Vorher = City->TrafficSimulation.Settings.TrafficDensity;
	City->TrafficSimulation.Settings.TrafficDensity = (An != 0) ? 0.5f : 0.0f;
	const float Nachher = City->TrafficSimulation.Settings.TrafficDensity;
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbTraffic %d ausgefuehrt: Dichte %.2f -> %.2f."),
		An, Vorher, Nachher);
}

void AWiesbadenPlayerController::WbHealth(float MaxWaitSeconds)
{
	if (MaxWaitSeconds <= 0.0f)
	{
		// Sofort-Modus (manueller Aufruf): Ist-Zustand jetzt.
		WriteHealthReport();
		return;
	}

	// Gate-Modus: auf den geladenen Zustand warten (Deckel = MaxWaitSeconds), dann
	// erst schreiben. Ein Sekundentakt-Poll genuegt; das Streaming-"fertig"-Flag
	// flackert waehrend des Warmlaufs, deshalb keine feste Verzoegerung.
	const UWorld* World = GetWorld();
	HealthGateDeadlineSeconds = (World ? World->GetTimeSeconds() : 0.0) + MaxWaitSeconds;
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbHealth wartet auf geladenen Zustand (bis %.0f s)."), MaxWaitSeconds);
	GetWorldTimerManager().SetTimer(HealthGateTimer, this,
		&AWiesbadenPlayerController::PollHealthGate, 1.0f, /*bLoop=*/true, /*FirstDelay=*/1.0f);
}

void AWiesbadenPlayerController::PollHealthGate()
{
	const UWorld* World = GetWorld();
	const UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;

	const bool bTimeout = World && (World->GetTimeSeconds() >= HealthGateDeadlineSeconds);
	// Geladen = Stadt da, Streaming fertig UND Perf-Snapshot erhoben (8-s-Block).
	// Ohne bPerfValid meldete das Gate perf: "unknown", weil das Streaming-"fertig"-
	// Flackern das Tor schon vor dem Snapshot oeffnete. BuildHealthReport ist die
	// einzige Wahrheitsquelle - kein zweiter Zustands-Pfad.
	const FWiesbadenHealthReport R = City ? City->BuildHealthReport() : FWiesbadenHealthReport();
	const bool bLoaded = City && R.bStreamingComplete && R.bCityLoaded && R.bPerfValid;

	if (bLoaded || bTimeout || !City)
	{
		GetWorldTimerManager().ClearTimer(HealthGateTimer);
		WriteHealthReport();
	}
}

void AWiesbadenPlayerController::WriteHealthReport()
{
	const UWorld* World = GetWorld();
	const UWiesbadenCitySubsystem* City = World ? World->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!City)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHealth erkannt, aber kein City-Subsystem."));
		return;
	}
	const FWiesbadenHealthReport Report = City->BuildHealthReport();
	const FString Json = Report.ToJson();
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Logs") / TEXT("WbHealth.json");
	FFileHelper::SaveStringToFile(Json, *Path);
	// Die Verdikte stecken im JSON ("healthy"/"warnings") - eine Zeile genuegt.
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHealth: %s"), *Json);
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHealth - Bericht nach %s geschrieben."), *Path);
}

void AWiesbadenPlayerController::WbCam(int32 Modus)
{
	APawn* ControlledPawn = GetPawn();
	UWiesbadenVehicleCameraComponent* Cam = ControlledPawn
		? ControlledPawn->FindComponentByClass<UWiesbadenVehicleCameraComponent>() : nullptr;
	if (!Cam)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbCam %d erkannt, aber kein Fahrzeug mit Kamera."), Modus);
		return;
	}
	const int32 ModusClamped = FMath::Clamp(Modus, 0, 2);
	if (Modus != ModusClamped)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbCam %d ausserhalb 0-2 - auf %d begrenzt."), Modus, ModusClamped);
	}
	Cam->SetCameraMode(static_cast<EWiesbadenVehicleCameraMode>(ModusClamped));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbCam %d gesetzt (0=Follow,1=Orbit,2=Cockpit)."), ModusClamped);
}

void AWiesbadenPlayerController::WbHeli(int32 Index)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Erst sammeln, dann waehlen. Die Reihenfolge des Iterators ist nicht
	// zugesichert, darum nennt das Protokoll jede Maschine samt Klasse - sonst
	// weiss hinterher niemand, welche der beiden im Bild ist.
	TArray<AWiesbadenHelicopter*> Helis;
	for (TActorIterator<AWiesbadenHelicopter> It(World); It; ++It)
	{
		Helis.Add(*It);
	}

	if (!Helis.IsValidIndex(Index))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeli %d - es gibt %d Helikopter in der Welt."),
			Index, Helis.Num());
		return;
	}

	Possess(Helis[Index]);
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbHeli %d von %d - %s (%s) uebernommen, steht bei (%.0f, %.0f, %.0f)."),
		Index, Helis.Num(), *Helis[Index]->GetName(),
		*Helis[Index]->GetClass()->GetName(),
		Helis[Index]->GetActorLocation().X, Helis[Index]->GetActorLocation().Y,
		Helis[Index]->GetActorLocation().Z);
}

void AWiesbadenPlayerController::WbNudge(int32 NickGrad, int32 RollGrad)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbNudge erkannt, aber kein besessener Pawn."));
		return;
	}
	const FRotator Vorher = ControlledPawn->GetActorRotation();
	const FRotator Nachher(Vorher.Pitch + NickGrad, Vorher.Yaw, Vorher.Roll + RollGrad);
	ControlledPawn->SetActorRotation(Nachher, ETeleportType::TeleportPhysics);
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbNudge %d/%d: Nick/Roll %.0f/%.0f -> %.0f/%.0f."),
		NickGrad, RollGrad, Vorher.Pitch, Vorher.Roll, Nachher.Pitch, Nachher.Roll);
}

// Test-Harness auf dem besessenen Helikopter holen oder anlegen.
//
// Die Harness-Komponente existiert im normalen Spiel nicht - sie wird erst hier,
// beim ersten Dev-Befehl, auf dem Pawn erzeugt. So bleibt die Fahrzeugklasse frei
// von Test-Code.
static UWiesbadenVehicleTestHarness* GetOrAddHarness(AActor* Owner)
{
	UWiesbadenVehicleTestHarness* Harness =
		Owner->FindComponentByClass<UWiesbadenVehicleTestHarness>();
	if (!Harness)
	{
		Harness = NewObject<UWiesbadenVehicleTestHarness>(Owner);
		Harness->RegisterComponent();
	}
	return Harness;
}

void AWiesbadenPlayerController::WbHeliYaw(int32 Sekunden)
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliYaw erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	const int32 Dauer = WbSekundenOderVorgabe(Sekunden);
	GetOrAddHarness(Heli)->StartYawProbe(static_cast<float>(Dauer));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliYaw - Gierprobe fuer %d s gestartet."), Dauer);
}

void AWiesbadenPlayerController::WbHeliFly(int32 Sekunden)
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliFly erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	// Ohne Argument (die einzige Form, die die Konsole kann) kommt die Dauer
	// aus wb.Sekunden - siehe den Kommentar am CVar.
	const int32 Dauer = WbSekundenOderVorgabe(Sekunden);
	GetOrAddHarness(Heli)->StartFlightProfile(static_cast<float>(Dauer));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliFly - Flugprofil fuer %d s gestartet."), Dauer);
}

void AWiesbadenPlayerController::WbHeliTurm()
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliTurm erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	if (!Heli->RespawnOnTowerHelipad())
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliTurm - der Turm-Helipad war nicht erreichbar."));
		return;
	}
	// Die Meldung gehoert ins Log, weil ein Bild den Ort nicht beweisen kann:
	// erst die Flaeche selbst (Durchmesser), dann die Position.
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: Ka-52 auf dem Turm-Helipad des Sebbotower bei (%.0f, %.0f, %.0f) cm."),
		Heli->GetActorLocation().X, Heli->GetActorLocation().Y,
		Heli->GetActorLocation().Z);
}

void AWiesbadenPlayerController::WbHeliFeuer(int32 An)
{
	// Wie bei der Kamera: -1 (kein Argument) heisst "aus der Konsole lesen".
	// 0 ist ein ausdrueckliches "loslassen" und darf NICHT wie "kein
	// Argument" behandelt werden - sonst liess sich das Geschaeft nicht
	// wieder einfahren.
	const int32 Gewuenscht = An >= 0 ? An : CVarWbHeliFeuer.GetValueOnGameThread();
	if (Gewuenscht != 0 && Gewuenscht != 1)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliFeuer - Wert %d ist weder 0 noch 1."), Gewuenscht);
		return;
	}
	if (!Cast<AWiesbadenHelicopter>(GetPawn()))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliFeuer erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	CVarWbHeliFeuer.AsVariable()->Set(Gewuenscht, ECVF_SetByCode);
	UE_LOG(LogWbCore, Log, TEXT("WbDev: Bordabzug %s."), Gewuenscht ? TEXT("gehalten") : TEXT("losgelassen"));
}

void AWiesbadenPlayerController::WbHeliZiel(float Xcm, float Ycm,
	float HoeheUeberBodenCm, float DistanzMeter)
{
	// Aufnahmewerkzeug: der Hubschrauber schwebt in DistanzMeter vor einem
	// Zielpunkt und peilt ihn an. Ohne diesen Befehl zeigt das Geschuetz nur
	// "nach vorn" - ein Schuss auf ein bestimmtes Bauwerk war damit nicht
	// einstellbar (am 26.09.2026 fuer ein Zeltdach gebraucht).
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliZiel erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	if (!Heli->AimAtWorldTarget(Xcm, Ycm, HoeheUeberBodenCm, DistanzMeter))
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliZiel - Zielpunkt nicht erreichbar."));
	}
}

void AWiesbadenPlayerController::WbHeliLicht(int32 An)
{
	// Beide Suchscheinwerfer ohne Tastatur. Taste L erreicht das Spiel nicht
	// zuverlaessig (dieselbe Eingabeluecke wie beim Abzug, am 26.09.2026
	// gemessen), und ein Nachtbild ohne die Strahlen belegt nichts.
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliLicht erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	// Beim Einschalten erst alle Lichter scharfstellen: SetSearchlights(1)
	// bleibt wirkungslos, solange der Lichtkasten auf "alle aus" steht. Beim
	// Ausschalten bleiben die Positionslichter an - nur die beiden Strahlen
	// gehen aus.
	if (An != 0)
	{
		if (UWiesbadenHeliLightRig* Rig = Heli->GetLightRig())
		{
			Rig->SetAllLightsEnabled(true);
		}
	}
	Heli->SetSearchlights(An != 0);
}

void AWiesbadenPlayerController::WbHeliKamera(int32 Modus)
{
	// Ohne Argument (die einzige Form, die die Konsole kann) kommt der
	// Wunschmodus aus wb.HeliKamera; sonst aus dem Argument, falls die
	// Konsole es doch einmal uebergeben sollte.
	const int32 Gewuenscht = Modus >= 0 ? Modus : CVarWbHeliKamera.GetValueOnGameThread();
	if (Gewuenscht < 0 || Gewuenscht > 2)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbHeliKamera - Modus %d liegt nicht zwischen 0 und 2."), Gewuenscht);
		return;
	}
	// Die CVar ist der einzige Weg: sie erreicht den besessenen Hubschrauber
	// auch dann, wenn der Befehl VOR der Uebernahme eintrifft (der Wert bleibt
	// stehen, bis der Pawn tickt).
	CVarWbHeliKamera.AsVariable()->Set(Gewuenscht, ECVF_SetByCode);
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliKamera - Sollmodus %d gesetzt."), Gewuenscht);
}

void AWiesbadenPlayerController::WbDrive(int32 Sekunden)
{	// Ueber die Steuernaht (Interface) statt auf eine konkrete Klasse - so greift
	// WbDrive auf BEIDE Fahrzeuge (Kaefer wie ChaosCar).
	APawn* ControlledPawn = GetPawn();
	if (!Cast<IWiesbadenVehicleControl>(ControlledPawn))
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbDrive erkannt, aber kein Fahrzeug besessen."));
		return;
	}
	const int32 Dauer = WbSekundenOderVorgabe(Sekunden);
	GetOrAddHarness(ControlledPawn)->StartDriveProfile(static_cast<float>(Dauer));
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbDrive - Fahrprofil fuer %d s gestartet."), Dauer);
}

// Autopilot-Komponente on-demand am Helikopter anlegen (wie der Test-Harness):
// im normalen Spiel existiert sie nicht, bis ein Dev-Befehl sie anfordert.
static UWiesbadenHelicopterAutopilot* GetOrAddAutopilot(AWiesbadenHelicopter* Heli)
{
	UWiesbadenHelicopterAutopilot* Autopilot =
		Heli->FindComponentByClass<UWiesbadenHelicopterAutopilot>();
	if (!Autopilot)
	{
		Autopilot = NewObject<UWiesbadenHelicopterAutopilot>(Heli);
		Autopilot->RegisterComponent();
	}
	return Autopilot;
}

void AWiesbadenPlayerController::WbHeliGoto(int32 DeltaXMeter, int32 DeltaYMeter, int32 DeltaZMeter)
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliGoto erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	const FVector Target = Heli->GetActorLocation()
		+ FVector(DeltaXMeter, DeltaYMeter, DeltaZMeter) * 100.0;
	GetOrAddAutopilot(Heli)->FlyTo(Target);
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbHeliGoto - Autopilot fliegt zu (%d, %d, %d) m relativ."),
		DeltaXMeter, DeltaYMeter, DeltaZMeter);
}

void AWiesbadenPlayerController::WbHeliHover()
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliHover erkannt, aber kein Helikopter besessen (erst WbHeli)."));
		return;
	}
	GetOrAddAutopilot(Heli)->HoldPosition();
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliHover - Autopilot haelt die Position."));
}

void AWiesbadenPlayerController::WbHeliOff()
{
	AWiesbadenHelicopter* Heli = Cast<AWiesbadenHelicopter>(GetPawn());
	if (!Heli)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbHeliOff erkannt, aber kein Helikopter besessen."));
		return;
	}
	// Nur einen VORHANDENEN Autopiloten abschalten - keinen erst anlegen, um ihn
	// gleich wieder zu loesen.
	if (UWiesbadenHelicopterAutopilot* Autopilot = Heli->FindComponentByClass<UWiesbadenHelicopterAutopilot>())
	{
		Autopilot->Disengage();
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliOff - Autopilot aus, Steuerung zurueck an Tastatur/Gamepad."));
	}
	else
	{
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbHeliOff - kein Autopilot aktiv."));
	}
}

void AWiesbadenPlayerController::WbSpawnPursuer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const APawn* P = GetPawn();
	const FVector Base = P ? P->GetActorLocation() : FVector::ZeroVector;
	// 40 m versetzt -> innerhalb des Detect-Radius (50 m), faengt sofort an zu verfolgen.
	const FVector Spawn = Base + FVector(4000.0, 0.0, 0.0);
	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const AWiesbadenPursuerActor* Pursuer = World->SpawnActor<AWiesbadenPursuerActor>(
		AWiesbadenPursuerActor::StaticClass(), Spawn, FRotator::ZeroRotator, Sp);
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbSpawnPursuer - Verfolger %s bei (%.0f, %.0f)."),
		Pursuer ? TEXT("gespawnt") : TEXT("NICHT gespawnt"), Spawn.X, Spawn.Y);
}

void AWiesbadenPlayerController::WbFussAnsicht(int32 Modus)
{
	AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(GetPawn());
	if (!Foot)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbFussAnsicht - zu Fuss nicht aktiv (Pawn ist kein FootPawn)."));
		return;
	}
	// 0/1 setzen die Ansicht ausdruecklich, 2 schaltet um - dieselbe
	// Zustandsaenderung wie die C-Taste, nur skriptbar.
	const bool bWant = Modus == 1;
	if (Modus == 2)
	{
		Foot->ToggleEgoCamera();
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbFussAnsicht - umgeschaltet, jetzt %s."),
			Foot->IsEgoCamera() ? TEXT("Ego") : TEXT("Schulter"));
	}
	else
	{
		Foot->SetEgoCamera(bWant);
		UE_LOG(LogWbCore, Log, TEXT("WbDev: WbFussAnsicht - gesetzt auf %s."),
			bWant ? TEXT("Ego") : TEXT("Schulter"));
	}
}

void AWiesbadenPlayerController::WbFussWaffe(int32 Index)
{
	AWiesbadenFootPawn* Foot = Cast<AWiesbadenFootPawn>(GetPawn());
	if (!Foot)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbFussWaffe - zu Fuss nicht aktiv."));
		return;
	}
	if (!UWiesbadenWeaponComponent::IsValidWeaponIndex(Index))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("WbDev: WbFussWaffe %d - ausserhalb der Tabelle (0..%d)."), Index,
			static_cast<int32>(EWiesbadenWeaponId::Count) - 1);
		return;
	}
	Foot->SelectWeapon(Index);
	const FWiesbadenWeaponSpec& Spec = WiesbadenWeapons::Spec(Index);
	UE_LOG(LogWbCore, Log,
		TEXT("WbDev: WbFussWaffe - Waffe %d (%s) gewaehlt, Schaden %.0f, V %.0f cm/s%s."),
		Index, Spec.DisplayName, Spec.Damage, Spec.MuzzleVelocityCmPerS,
		Spec.bMelee ? TEXT(", Nahkampf") : TEXT(""));
}

void AWiesbadenPlayerController::WbDennoAuftrag(int32 Seed, float DelaySeconds)
{
	UWorld* World = GetWorld();
	AWiesbadenDennoShop* Shop = nullptr;
	for (TActorIterator<AWiesbadenDennoShop> It(World); World && It; ++It)
	{
		Shop = *It;
		break;
	}
	if (!Shop)
	{
		UE_LOG(LogWbCore, Warning, TEXT("WbDev: WbDennoAuftrag %d - kein Denno-Laden in der Welt."), Seed);
		return;
	}
	Shop->RequestDevDelivery(Seed, DelaySeconds);
	UE_LOG(LogWbCore, Log, TEXT("WbDev: WbDennoAuftrag %d - Lieferauftrag angefordert (Laden %s)."),
		Seed, Shop->IsBuilt() ? TEXT("steht") : TEXT("noch im Aufbau, vorgemerkt"));
}
