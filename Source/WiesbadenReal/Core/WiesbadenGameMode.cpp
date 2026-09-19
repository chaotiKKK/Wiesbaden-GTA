// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenGameMode.h"

#include "World/WiesbadenCityChunk.h"
#include "GIS/WiesbadenRoadClearance.h"

#include "WiesbadenReal.h"

#include "EngineUtils.h"
#include "Core/WiesbadenPlayerController.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "GameFramework/PlayerController.h"
#include "Vehicles/WiesbadenCar.h"
#include "Vehicles/WiesbadenChaosCar.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Vehicles/WiesbadenCarSpawn.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Vehicles/WiesbadenFootPawn.h"
#include "NPC/WiesbadenStoreMerchant.h"
#include "Missions/WiesbadenMissionSubsystem.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "Vehicles/WiesbadenLegacyHelicopter.h"
#include "UI/WiesbadenVehicleHUD.h"
#include "Core/WiesbadenGameStateSubsystem.h"
#include "Store/WiesbadenStore.h"
#include "Engine/GameInstance.h"
#include "World/WiesbadenStreamingSource.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "Engine/World.h"
#include "World/WiesbadenCitySubsystem.h"
#include "World/WiesbadenNerobergbahn.h"
#include "World/WiesbadenNerotalbahn.h"
#include "World/WiesbadenNerotal48.h"
#include "World/WiesbadenLandmarks.h"
#include "Audio/WiesbadenAudioSubsystem.h"
#include "Engine/GameInstance.h"
#include "World/WiesbadenBusRoute.h"
#include "World/WiesbadenBusStopMonitor.h"
#include "World/WiesbadenWeatherFX.h"
#include "World/WiesbadenParkFeatures.h"
#include "NPC/WiesbadenSylvia.h"

AWiesbadenGameMode::AWiesbadenGameMode()
{
	// Der GameMode tickt, aber nur fuer die Ein/Aussteigen-Taste (F). Die
	// Streaming-UEberwachung liegt weiterhin im City-Subsystem.
	//
	// Bewusst hier und nicht im Pawn: sonst braeuchte JEDES Fahrzeug dieselbe
	// Abfrage, und beim Aussteigen gaebe es keinen Pawn mehr, der sie stellt.
	PrimaryActorTick.bCanEverTick = true;

	// Fahrzeug-HUD (Tacho, Drehzahl, Gang, Kontrollleuchten). Bewusst hier
	// statt per Blueprint: das HUD zeichnet rein per Canvas und braucht damit
	// kein Asset - im gebackenen Spiel ist es sofort da.
	HUDClass = AWiesbadenVehicleHUD::StaticClass();

	// Eigener PlayerController - nur wegen der Dev-Konsolenbefehle
	// (WbTeleport/WbResetVehicle/WbTraffic). Der PlayerController ist der
	// ExecActor in ULocalPlayer::Exec und damit der einzige zuverlaessig per
	// -ExecCmds ansprechbare Ort fuer skriptbare Dev-Befehle; ein
	// GameInstanceSubsystem wird von dieser Kette nicht erreicht.
	PlayerControllerClass = AWiesbadenPlayerController::StaticClass();
}

void AWiesbadenGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Audio-Mischpult scharfschalten: Basis-SoundMix aktiv + gespeicherte
	// Bus-Lautstaerken anwenden (no-op, falls die Mix-Assets fehlen).
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UWiesbadenAudioSubsystem* Audio = GI->GetSubsystem<UWiesbadenAudioSubsystem>())
		{
			Audio->ApplyMix();
		}
	}

	// Wetter-/Tageslicht-Renderer: die Komponente setzt Sonne (Drehung, Staerke,
	// Farbe) und Wetter-FX aus dem Wetterzustand des CitySubsystems. Sie wurde
	// bisher nirgends angelegt - deshalb stand die Sonne im Spiel immer gleich.
	if (UWiesbadenWeatherFXComponent* WeatherFX = NewObject<UWiesbadenWeatherFXComponent>(this, TEXT("WeatherFX")))
	{
		WeatherFX->RegisterComponent();
	}

	// Engine-Bildschirmwarnungen (z. B. der rote "RAY TRACING GEOMETRY ... EXCEEDS
	// BUDGET"-Hinweis) sind Entwickler-Diagnose und wirken auf Spieler wie ein
	// Fehler. Im -game-/gepackten Lauf ausblenden; im Editor (PIE, GIsEditor=true)
	// bleiben sie fuer die Entwicklung sichtbar. Die Spiel-HUDs zeichnen ueber
	// Canvas und sind davon nicht betroffen.
	if (GEngine && !GIsEditor)
	{
		GEngine->bEnableOnScreenDebugMessages = false;
	}

	// -WbZuFuss=<Sekunden>: nach dieser Zeit von selbst aussteigen. Ohne den
	// Schalter zeigt jedes mit -WbShot aufgenommene Bild nur das Auto.
	if (!FParse::Value(FCommandLine::Get(), TEXT("WbZuFuss="), OnFootAfterSeconds))
	{
		OnFootAfterSeconds = -1.0f;
	}

	CitySubsystem = GetWorld() ? GetWorld()->GetSubsystem<UWiesbadenCitySubsystem>() : nullptr;
	if (!CitySubsystem)
	{
		UE_LOG(LogWbCore, Error,
			TEXT("UWiesbadenCitySubsystem fehlt - Stadt-Initialisierung nicht moeglich (GameMode im Spieler-Setting gesetzt?)."));
		return;
	}

	// Status des Subsystems an Blueprints weiterreichen.
	CitySubsystem->OnCityStateChanged.AddDynamic(this, &AWiesbadenGameMode::HandleCityStatus);

	// Stadt anstossen (idempotent; hat das Subsystem bereits in
	// OnWorldBeginPlay gemacht, passiert hier nichts mehr).
	InitializeCity();

	// Die Nerobergbahn faehrt von Spielbeginn an. Sie positioniert sich
	// selbst (Strecke aus OSM-Koordinaten) und tastet ihre Gleishoehen ab,
	// sobald der Neroberg gestreamt ist.
	if (UWorld* BahnWorld = GetWorld())
	{
		FActorSpawnParameters BahnParams;
		BahnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		BahnWorld->SpawnActor<AWiesbadenNerobergbahn>(
			AWiesbadenNerobergbahn::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, BahnParams);

		// Die Nerotalbahn: die historische Talstrassenbahn vom Nerotal die
		// Taunusstrasse hinunter. Setzt sich wie die Nerobergbahn selbst und
		// legt ihre Gleishoehen ueber denselben korrigierten Profil-Pfad aufs
		// Gelaende.
		BahnWorld->SpawnActor<AWiesbadenNerotalbahn>(
			AWiesbadenNerotalbahn::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, BahnParams);
	}

	// Nerotal 48: Garten mit Pool und Palmen.
	//
	// Das Haus selbst kommt aus den amtlichen Daten (OSM way 476086889).
	// Was dort nicht steht, ist alles ausserhalb des Grundrisses - dafuer
	// ist dieser Actor da. Er setzt sich anhand der echten Koordinaten
	// selbst und wartet, bis das Gelaende gestreamt ist.
	if (UWorld* GartenWorld = GetWorld())
	{
		FActorSpawnParameters GartenParams;
		GartenParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GartenWorld->SpawnActor<AWiesbadenNerotal48>(
			AWiesbadenNerotal48::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, GartenParams);
	}

	// Wiesbaden-Wahrzeichen (Marktkirche, Russisch-Orthodoxe Kirche): setzen
	// sich wie die Bahnen selbst anhand ihrer OSM-Koordinaten und bauen sich
	// aus Primitiven, sobald ihre WP-Zelle gestreamt ist. Kein Re-Bake noetig.
	if (UWorld* LandmarkWorld = GetWorld())
	{
		FActorSpawnParameters LandmarkParams;
		LandmarkParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		LandmarkWorld->SpawnActor<AWiesbadenLandmarks>(
			AWiesbadenLandmarks::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, LandmarkParams);

			// OEPNV: die ESWE-Linien 6 und 3.
			//
			// Je Linie ein Bus-Actor und ein Abfahrtsmonitor. Was die Linie ausmacht
			// (Strecke, Halte, Namen, Takt, Zielschilder, DFI-Halte), steht in
			// Data/Raw/Bus/line<ref>.json - hier steht nur, WELCHE Linien gefahren
			// werden. Die Fahrzeuge sind feste Wagen mit Dauerbetrieb (Wendezeit an
			// beiden Enden), siehe AWiesbadenBusRoute.
			struct FWbBusLine { const TCHAR* LineFile; const TCHAR* ScheduleFile; };
			static const FWbBusLine BusLines[] = {
				{ TEXT("line6.json"), TEXT("line6_schedule.json") },   // Nordfriedhof <-> Mainz-Gonsenheim
				{ TEXT("line3.json"), TEXT("") },                        // Nordfriedhof <-> Biebrich Rheinufer
			};
			for (const FWbBusLine& Line : BusLines)
			{
				// Verzoegert spawnen: LineFile/ScheduleFile muessen VOR BeginPlay
				// stehen, sonst laedt der Actor die Vorgabe (Linie 6).
				AWiesbadenBusRoute* Bus = LandmarkWorld->SpawnActorDeferred<AWiesbadenBusRoute>(
					AWiesbadenBusRoute::StaticClass(), FTransform::Identity, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Bus)
				{
					Bus->LineFile = Line.LineFile;
					Bus->ScheduleFile = Line.ScheduleFile;
					Bus->FinishSpawning(FTransform::Identity);
				}

				AWiesbadenBusStopMonitor* Monitor = LandmarkWorld->SpawnActorDeferred<AWiesbadenBusStopMonitor>(
					AWiesbadenBusStopMonitor::StaticClass(), FTransform::Identity, nullptr, nullptr,
					ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
				if (Monitor)
				{
					Monitor->LineFile = Line.LineFile;
					Monitor->ScheduleFile = Line.ScheduleFile;
					Monitor->FinishSpawning(FTransform::Identity);
				}
			}

		// Formale Parkanlagen (Bowling Green am Kurhaus, Reisinger-Anlagen):
		// lange Wasserbecken + Fontaenen, ebenfalls selbstsetzend zur Laufzeit.
		LandmarkWorld->SpawnActor<AWiesbadenParkFeatures>(
			AWiesbadenParkFeatures::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, LandmarkParams);

		// Sylvia steht als reine Runtime-Szene vor Platter Strasse 144. Der
		// Actor loest die Adresse und den Boden selbst auf; die gebackene
		// Alkis-Karte und ihre External-Actor-Pakete bleiben unberuehrt.
		LandmarkWorld->SpawnActor<AWiesbadenSylvia>(
			AWiesbadenSylvia::StaticClass(),
			FVector::ZeroVector, FRotator::ZeroRotator, LandmarkParams);
	}

	// Spielerfahrzeug einsetzen. Bei einer gebackenen Stadt liegt das
	// Strassennetz sofort vor; wird die Stadt erst zur Laufzeit erzeugt,
	// schlaegt der Versuch hier fehl und wird ueber HandleCityStatus
	// wiederholt, sobald die Stadt bereitsteht.
	if (bSpawnPlayerCar)
	{
		// Helikopter direkt mit absetzen - er braucht die Fahrzeugposition als
		// Bezug und war bisher ueberhaupt nicht in der Welt vorhanden.
		if (SpawnPlayerCarAtStartAddress() && bSpawnHelicopter)
		{
			SpawnHelicopterNearStart();
		}
	}
}

bool AWiesbadenGameMode::BlockLoadSpawnCell(UWorld* World, const FVector& Location)
{
	UWorldPartitionSubsystem* WorldPartition =
		World ? World->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (!WorldPartition)
	{
		// Nicht partitioniert (z. B. Laufzeit-Stadtbuild) - kein Block-Load noetig.
		return true;
	}

	// Die vorhandene Streaming-Quelle folgt sonst dem (hier noch fehlenden) Pawn
	// und bliebe inaktiv. Fest an den Startort heften, damit WP diese Zelle laedt.
	AWiesbadenStreamingSource* Source = nullptr;
	for (TActorIterator<AWiesbadenStreamingSource> It(World); It; ++It)
	{
		Source = *It;
		break;
	}
	if (Source)
	{
		Source->PinSourceToLocation(Location);
	}

	// Blockierend streamen, bis WP die Zellen um den Startort geladen hat. Begrenzt,
	// damit ein haengendes Streaming den Start nicht ewig blockiert.
	bool bComplete = false;
	for (int32 Iter = 0; Iter < 48 && !bComplete; ++Iter)
	{
		World->UpdateLevelStreaming();
		World->FlushLevelStreaming(EFlushLevelStreamingType::Full);
		bComplete = WorldPartition->IsStreamingCompleted();
	}

	// Folge-dem-Pawn wieder einschalten: der zwischengespeicherte CurrentSource
	// bleibt bis zum naechsten Tick am Startort (kein Tick vor dem Spawn), die
	// Zellen bleiben also geladen; ab dann folgt die Quelle dem Fahrzeug.
	if (Source)
	{
		Source->bFollowPlayerPawn = true;
	}

	UE_LOG(LogWbCore, Log,
		TEXT("Spawn-Zelle Block-Load bei (%.0f, %.0f): Streaming %s."),
		Location.X, Location.Y, bComplete ? TEXT("abgeschlossen") : TEXT("Zeitlimit erreicht"));
	return bComplete;
}

bool AWiesbadenGameMode::SpawnPlayerCarAtStartAddress()
{
	if (PlayerCar)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Das Strassennetz liegt am gebackenen WorldBuilder - dort ist es als
	// nicht-transiente UPROPERTY in der Map serialisiert.
	AWiesbadenWorldBuilder* Builder = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It)
	{
		if (It->RoadNetwork.Lanes.Num() > 0)
		{
			Builder = *It;
			break;
		}
	}

	if (!Builder)
	{
		UE_LOG(LogWbCore, Verbose,
			TEXT("Spielerfahrzeug: noch kein Strassennetz im Level - Versuch wird wiederholt."));
		return false;
	}

	// Georeferenz mit denselben Einstellungen wie die Pipeline aufbauen.
	// Ein abweichender Ursprung wuerde die Startadresse um Kilometer
	// verschieben, daher wird die Einstellung des Builders uebernommen und
	// nicht der Projekt-Default angenommen.
	UGeoCoordinateConverter* Converter = NewObject<UGeoCoordinateConverter>(this);
	const bool bInitialized = Builder->bUseWiesbadenOrigin
		? Converter->InitializeWithWiesbadenOrigin()
		: Converter->Initialize(Builder->CustomOrigin);

	if (!bInitialized)
	{
		UE_LOG(LogWbCore, Error, TEXT("Spielerfahrzeug: Georeferenz konnte nicht aufgebaut werden."));
		return false;
	}

	const FVector TargetWorld = Converter->GeoToUnrealGround(PlayerStartAddress);

	FVector SpawnLocation;
	FRotator SpawnRotation;
	int32 LaneId = INDEX_NONE;

	if (!FWiesbadenCarSpawn::FindNearestDrivableLanePoint(
			Builder->RoadNetwork, TargetWorld,
			FMath::Max(10.0, PlayerStartSearchRadiusMeters) * 100.0,
			SpawnLocation, SpawnRotation, LaneId))
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Spielerfahrzeug: keine Fahrbahn nahe der Startadresse - Fahrzeug wird nicht eingesetzt."));
		return false;
	}

	// Steht schon ein Fahrzeug? Dann hier raus - VOR der Platzsuche.
	//
	// Diese Funktion wird zweimal durchlaufen. Die Pruefung stand bisher erst
	// beim Einsetzen, die Platzsuche lief also beide Male - und beim zweiten
	// Mal tastete sie den Boden ab, waehrend das Fahrzeug bereits dort stand.
	// Die Strahlen trafen sein DACH statt der Strasse und setzten es zwei
	// Meter hoeher: gemessen erst Z 11359, dann Z 11520, Raeder 224 bis 262 cm
	// ueber dem Boden.
	// Steht schon ein Spielerauto (Chaos ODER Kaefer)? Dann NICHT neu einsetzen
	// (Platzsuche uebersprungen), aber sicherstellen, dass es BESESSEN ist: der
	// erste Durchlauf kann es gespawnt haben, bevor ein PlayerController existierte
	// - dann erreichte WbDrive es nicht. Der zweite Durchlauf holt das nach.
	if (PlayerVehicle)
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			if (PC->GetPawn() != PlayerVehicle)
			{
				if (APawn* Other = PC->GetPawn())
				{
					PC->UnPossess();
					Other->Destroy();
				}
				PC->Possess(PlayerVehicle);
				UE_LOG(LogWbCore, Log,
					TEXT("Spielerfahrzeug: vorhandenes Fahrzeug nachtraeglich uebernommen."));
			}
		}
		return true;
	}

	// Fahrbahn-Zelle am Startort ZUERST laden (Block-Load), DANN den Boden messen.
	//
	// Die Wurzel des "Chaos-Wagen faehrt nicht": die Platzsuche unten traced die
	// Starthoehe, bevor die World-Partition-Zelle mit der Fahrbahn-Kollision
	// gestreamt ist - der Strahl faellt durch die kommende Strasse und trifft das
	// ~1,5 m tiefere Gelaende. Der Wagen spawnt dann unter/neben der Strasse; die
	// echte Physik bleibt stecken (der kinematische Kaefer merkt davon nichts).
	// Hier blockierend streamen, bis die Zelle da ist - danach trifft der Strahl
	// den Asphalt.
	BlockLoadSpawnCell(World, SpawnLocation);

	// Startplatz pruefen: frei UND eben.
	//
	// Zwei getrennte Bedingungen, und die zweite ist die wichtigere.
	//
	// FREI: Der Spielerwagen wird dort abgestellt, wo die Verkehrs-Simulation
	// ihre Fahrzeuge um den Spieler herum erzeugt - er stand deshalb
	// regelmaessig mitten in einem Pulk geparkter Kaefer.
	//
	// EBEN: Gemessen an der bisherigen Stelle lag der Boden unter den vier
	// Raedern bei 24, 40, 45 und 62 cm - ein Unterschied von 38 cm. Der
	// gesamte Federweg betraegt 20 cm. Ein Rad steckte damit rechnerisch zehn
	// Zentimeter im Asphalt, das gegenueberliegende hing 28 cm zu hoch; der
	// Wagen ruhte auf dem Fahrgestell, kein Rad trug, der Motor drehte ohne
	// Last gegen den Begrenzer. Das war die Ursache dafuer, dass das Fahrzeug
	// auf echter Physik nicht von der Stelle kam - nicht das Physik-Asset,
	// nicht der Kollisionskanal, nicht der Schlafzustand.
	//
	// Dem alten Fahrzeug fiel beides nicht auf: Es setzte sich per
	// SetActorLocation ueber jedes Hindernis und jede Kante hinweg.
	{
		// Radpositionen aus WiesbadenCar.cpp, am Modell vermessen.
		static const FVector WheelOffsets[] = {
			FVector(129.9, -65.6, 0.0), FVector(130.5, 65.3, 0.0),
			FVector(-112.2, -65.6, 0.0), FVector(-111.6, 65.3, 0.0) };

		constexpr double WheelRadiusCm = 34.3;
		constexpr double MaxSpreadCm = 15.0;   // Federweg ist 20 cm gesamt.

		// NUR gegen Fahrzeuge und Figuren pruefen, nicht gegen die Welt.
		//
		// Der erste Entwurf nahm einen blockierenden Test gegen ECC_Pawn und
		// meldete "kein freier Startplatz in +/- 22 m" - er zaehlte die
		// FAHRBAHN als Hindernis. Ein Kasten von 80 cm halber Hoehe schneidet
		// zwangslaeufig die Strasse, auf der er stehen soll.
		FCollisionObjectQueryParams Occupants;
		Occupants.AddObjectTypesToQuery(ECC_Pawn);
		Occupants.AddObjectTypesToQuery(ECC_PhysicsBody);

		FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WbPlayerSpawn), false);
		TraceParams.AddIgnoredActor(this);

		// Fahrzeuge aus der Bodenmessung ausschliessen.
		//
		// Sonst misst ein Strahl das Dach eines geparkten Autos als "Boden" -
		// und der Wagen landet einen Meter zu hoch. Bei der Ebenheitspruefung
		// waere ein Auto neben der Spur ausserdem ein kuenstlicher
		// Hoehenunterschied, der jede Stelle untauglich erscheinen liesse.
		for (TActorIterator<AWiesbadenChaosCar> It(World); It; ++It)
		{
			TraceParams.AddIgnoredActor(*It);
		}
		for (TActorIterator<AWiesbadenCar> It(World); It; ++It)
		{
			TraceParams.AddIgnoredActor(*It);
		}

		const FVector Forward = SpawnRotation.Vector();
		const FVector Original = SpawnLocation;
		bool bFound = false;

		static const double Offsets[] = { 0.0, 600.0, -600.0, 1200.0, -1200.0,
			1800.0, -1800.0, 2600.0, -2600.0, 3600.0, -3600.0 };

		for (const double Offset : Offsets)
		{
			const FVector Candidate = Original + Forward * Offset;

			// Boden unter allen vier Radpositionen abtasten.
			double MinGround = TNumericLimits<double>::Max();
			double MaxGround = -TNumericLimits<double>::Max();
			bool bAllHit = true;

			for (const FVector& Offs : WheelOffsets)
			{
				const FVector WheelXY = Candidate + SpawnRotation.RotateVector(Offs);
				FHitResult Hit;
				if (GetWorld()->LineTraceSingleByChannel(
						Hit, WheelXY + FVector(0, 0, 300.0), WheelXY - FVector(0, 0, 500.0),
						ECC_Visibility, TraceParams))
				{
					// Strassenmoebel verwerfen (Schild-/Laternen-/Poller-Oberkante):
					// ein "Boden" deutlich UEBER der Fahrspur ist keiner. Sonst nimmt
					// der Trace die Pfosten-Oberkante als Boden und das Fahrzeug wird
					// auf Pfostenhoehe + Radradius gesetzt -> es schwebt auf dem Pfosten.
					if (Hit.Location.Z > Candidate.Z + 90.0)
					{
						bAllHit = false;
						break;
					}
					MinGround = FMath::Min(MinGround, Hit.Location.Z);
					MaxGround = FMath::Max(MaxGround, Hit.Location.Z);
				}
				else
				{
					bAllHit = false;
					break;
				}
			}

			if (!bAllHit || (MaxGround - MinGround) > MaxSpreadCm)
			{
				continue;
			}

			// Auf die HOECHSTE der vier Stellen setzen, plus Radhalbmesser und
			// einen Finger breit Luft. Auf die tiefste zu setzen hiesse, ein
			// Rad in den Boden zu stellen.
			const FVector Placed(Candidate.X, Candidate.Y, MaxGround + WheelRadiusCm + 5.0);

			if (GetWorld()->OverlapAnyTestByObjectType(
					Placed + FVector(0, 0, 60.0), SpawnRotation.Quaternion(), Occupants,
					FCollisionShape::MakeBox(FVector(215.0, 85.0, 60.0)), TraceParams))
			{
				continue;
			}

			SpawnLocation = Placed;
			bFound = true;

			UE_LOG(LogWbCore, Log,
				TEXT("Startplatz: %.0f m versetzt, Boden unter den Raedern schwankt %.1f cm, ")
				TEXT("Fahrzeug auf Z %.1f."),
				Offset * 0.01, MaxGround - MinGround, SpawnLocation.Z);
			break;
		}

		if (!bFound)
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("Startplatz: in +/- 36 m keine ebene, freie Stelle gefunden - ")
				TEXT("das Fahrzeug steht moeglicherweise schief oder in anderen Fahrzeugen."));
			SpawnLocation = Original + FVector(0, 0, 40.0);
		}
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	// STANDARD ist (vorerst wieder) der kinematische Kaefer, weil er FAEHRT.
	//
	// AWiesbadenChaosCar (echte Chaos-Physik: Reifenmodell, Getriebe, Federung)
	// war kurz der Standard, ist es aber NICHT mehr: der Wagen kommt am Start nicht
	// vom Fleck. Ursache MESSTECHNISCH belegt - nicht Reifen/Physik-Asset, sondern
	// die SPAWN-PLATZIERUNG: die Platzsuche traced die Starthoehe, bevor die
	// Fahrbahn-Kollision der World-Partition-Zelle gestreamt ist, trifft das
	// ~1,5 m tiefere Gelaende und setzt den Wagen UNTER/NEBEN die Strasse. Die
	// echte Physik bleibt dann im Graben stecken; der kinematische Kaefer merkt
	// davon nichts (SetActorLocation ueber alles hinweg). Bis die Zelle VOR dem
	// Einsetzen verlaesslich geladen ist (Block-Load der Spawn-Zelle), faehrt der
	// Chaos-Wagen nur als Opt-in (-WbChaosCar); ein undrivable Standard waere
	// schlechter als der "auf Schienen"-Kaefer.
	//
	// Beide Autos implementieren IWiesbadenVehicleControl - HUD-Tacho und WbDrive
	// erreichen beide ueber dieselbe Steuernaht. Das tatsaechlich besessene Auto
	// steht in PlayerVehicle; fahrzeug-typ-unabhaengige Pfade (Helikopter-Bezug,
	// Wiedereinstieg, Idempotenz) nutzen diesen Zeiger.
	const bool bUseChaosCar = FParse::Param(FCommandLine::Get(), TEXT("WbChaosCar"));
	if (bUseChaosCar)
	{
		APawn* ChaosCar = World->SpawnActor<AWiesbadenChaosCar>(
			AWiesbadenChaosCar::StaticClass(), SpawnLocation, SpawnRotation, Params);

		if (!ChaosCar)
		{
			UE_LOG(LogWbCore, Error,
				TEXT("Spielerfahrzeug (Chaos): SpawnActor fehlgeschlagen."));
			return false;
		}
		PlayerVehicle = ChaosCar;

		if (APlayerController* ChaosPC = World->GetFirstPlayerController())
		{
			// Vorhandenen Pawn freigeben UND entfernen (wie im Kaefer-Zweig): sonst
			// bleibt er als toter Actor stehen und die Kamera kann an ihm haengen.
			if (APawn* Existing = ChaosPC->GetPawn())
			{
				if (Existing != ChaosCar)
				{
					ChaosPC->UnPossess();
					Existing->Destroy();
				}
			}
			ChaosPC->Possess(ChaosCar);

			// Besitz verifizieren: NUR wenn der Controller den ChaosCar wirklich
			// haelt, erreicht ihn WbDrive ueber die Steuernaht (IWiesbadenVehicleControl).
			if (ChaosPC->GetPawn() == ChaosCar)
			{
				UE_LOG(LogWbCore, Log,
					TEXT("Spielerfahrzeug: echte Chaos-Physik aktiv (-WbChaosCar, Opt-in) bei (%.0f, %.0f)."),
					SpawnLocation.X, SpawnLocation.Y);
			}
			else
			{
				UE_LOG(LogWbCore, Error,
					TEXT("Spielerfahrzeug (Chaos): Besitz nicht uebernommen - WbDrive erreicht das Fahrzeug nicht."));
			}
		}
		else
		{
			UE_LOG(LogWbCore, Warning,
				TEXT("Spielerfahrzeug (Chaos) steht, aber es gibt keinen PlayerController zum Uebernehmen."));
		}
		return true;
	}

	// STANDARD: der kinematische Kaefer (fahrbar). Chaos-Physik via -WbChaosCar.
	PlayerCar = World->SpawnActor<AWiesbadenCar>(
		AWiesbadenCar::StaticClass(), SpawnLocation, SpawnRotation, Params);

	if (!PlayerCar)
	{
		UE_LOG(LogWbCore, Error, TEXT("Spielerfahrzeug: SpawnActor fehlgeschlagen."));
		return false;
	}
	PlayerVehicle = PlayerCar;

	if (APlayerController* PC = World->GetFirstPlayerController())
	{
		// Vorhandenen Pawn freigeben, sonst bleibt die Kamera an ihm haengen.
		if (APawn* Existing = PC->GetPawn())
		{
			if (Existing != PlayerCar)
			{
				PC->UnPossess();
				Existing->Destroy();
			}
		}
		PC->Possess(PlayerCar);
	}
	else
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Spielerfahrzeug steht, aber es gibt keinen PlayerController zum Uebernehmen."));
	}

	UE_LOG(LogWbCore, Log,
		TEXT("Spielerfahrzeug eingesetzt: Platter Strasse 144 -> Spur %d bei (%.0f, %.0f, %.0f)."),
		LaneId, SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z);

	return true;
}

void AWiesbadenGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	// Die Interaktionstaste wird genau EINMAL je Bild ausgewertet.
	//
	// Zuvor lief dieselbe Abfrage zweimal: einmal am Bildanfang mit verworfenem
	// Ergebnis, einmal im Ein-/Ausstiegszweig. WasInputKeyJustPressed gilt fuer
	// das ganze Bild - derselbe Tastendruck zaehlte damit als zwei Interaktionen
	// am NPC.
	//
	// Y am Gamepad neben F auf der Tastatur.
	//
	// Ein- und Aussteigen war das letzte Stueck, das sich NUR ueber die
	// Tastatur bedienen liess - wer mit dem Gamepad fuhr, musste zum
	// Aussteigen zur Tastatur greifen. Y ist an dieser Stelle die uebliche
	// Belegung; A, B und X sind im Fahrzeug schon belegt (Hupe, Handbremse,
	// Rueckwaertsgang).
	//
	// Flankenerkennung: ohne sie wuerde der Wechsel jeden Frame ausgeloest,
	// solange die Taste gehalten wird.
	const bool bDown = PC->IsInputKeyDown(EKeys::F)
		|| PC->IsInputKeyDown(EKeys::Gamepad_FaceButton_Top);
	const bool bJustPressed = PC->WasInputKeyJustPressed(EKeys::F)
		|| PC->WasInputKeyJustPressed(EKeys::Gamepad_FaceButton_Top);

	if (bJustPressed && !bEntryKeyHeld)
	{
		// NPC-Haendler-Interaktion (Nordfriedhof). Es wird nur zu Fuss geprueft;
		// im Fahrzeug gilt weiterhin F = ein-/aussteigen. Zu Fuss hat der NPC
		// Vorrang: war die Interaktion erfolgreich, wird kein Fahrzeugwechsel
		// ausgeloest.
		AWiesbadenFootPawn* Foot = nullptr;
		if (APawn* Pawn = PC->GetPawn())
		{
			Foot = Cast<AWiesbadenFootPawn>(Pawn);
		}

		if (!Foot || !TryMerchantInteraction(Foot))
		{
			TogglePlayerVehicle();
		}
	}

	// Merker immer nachziehen - auch nach einer NPC-Interaktion. Blieb er stehen,
	// loeste das noch gehaltene F im naechsten Bild doch noch den Fahrzeugwechsel
	// aus: Man sass nach dem Gespraech im Auto.
	bEntryKeyHeld = bDown;

	// Selbsttaetig aussteigen, wenn -WbZuFuss=<Sekunden> gesetzt ist.
	//
	// Erst nach Ablauf der Frist, nicht sofort: die Stadt laedt noch, und ein
	// Aussteigen in ungeladenes Gelaende setzt die Figur ins Leere.
	ElapsedSeconds += DeltaSeconds;
	if (!bOnFootDone && OnFootAfterSeconds >= 0.0f && ElapsedSeconds >= OnFootAfterSeconds)
	{
		bOnFootDone = true;
		TogglePlayerVehicle();
		UE_LOG(LogWbVehicles, Log,
			TEXT("-WbZuFuss: nach %.1f s ausgestiegen."), ElapsedSeconds);
	}
}

bool AWiesbadenGameMode::TryMerchantInteraction(AWiesbadenFootPawn* Foot)
{
	// Die Tastenflanke wertet der Tick aus und ruft nur dann hier an - eine
	// zweite Abfrage hier waere dieselbe Flanke ein zweites Mal.
	if (!Foot)
	{
		return false;
	}

	AWiesbadenStoreMerchant* Merchant = FindMerchantInReach(*Foot);
	if (!Merchant)
	{
		return false;
	}

	// Der NPC am Nordfriedhof macht ZWEIERLEI: Laden (Freischaltungen kaufen)
	// und Auftragsvergabe. Der automatische Missionsstart wurde entfernt - neue
	// Auftraege gibt es NUR noch hier im Gespraech. Laeuft schon einer, gibt der
	// NPC keinen neuen (RequestNextMission liefert dann false).
	const bool bStore = Merchant->TryInteract(Foot);
	bool bMission = false;
	if (const UWorld* World = GetWorld())
	{
		if (UWiesbadenMissionSubsystem* Missions =
			World->GetSubsystem<UWiesbadenMissionSubsystem>())
		{
			bMission = Missions->RequestNextMission();
		}
	}
	// true, wenn etwas geschah (Kauf ODER neuer Auftrag) -> kein Fahrzeugwechsel.
	// Sonst false, damit F am NPC nicht das Einsteigen blockiert.
	return bStore || bMission;
}

AWiesbadenStoreMerchant* AWiesbadenGameMode::PickMerchantInReach(
	const TArray<AWiesbadenStoreMerchant*>& Merchants, const FVector& FromLocation)
{
	// Naechster Haendler innerhalb SEINER eigenen Interaktionsreichweite -
	// gleiche Suchform wie FindNearbyVehicle, nur mit Actor-Reichweite.
	AWiesbadenStoreMerchant* Nearest = nullptr;
	double NearestDist = -1.0;
	for (AWiesbadenStoreMerchant* Merchant : Merchants)
	{
		if (!Merchant)
		{
			continue;
		}

		const double Dist = FVector::Dist(Merchant->GetActorLocation(), FromLocation);
		if (Dist <= Merchant->InteractRangeCm && (NearestDist < 0.0 || Dist < NearestDist))
		{
			Nearest = Merchant;
			NearestDist = Dist;
		}
	}
	return Nearest;
}

AWiesbadenStoreMerchant* AWiesbadenGameMode::FindMerchantInReach(const APawn& Foot) const
{
	// Der Weltsuchlauf laeuft nur beim Tastendruck (siehe Tick), nicht je Bild.
	TArray<AWiesbadenStoreMerchant*> Merchants;
	for (TActorIterator<AWiesbadenStoreMerchant> It(GetWorld()); It; ++It)
	{
		Merchants.Add(*It);
	}
	return PickMerchantInReach(Merchants, Foot.GetActorLocation());
}

APawn* AWiesbadenGameMode::FindNearbyVehicle(const FVector& Location) const
{
	const double RadiusSq = FMath::Square(FMath::Max(1.0f, EntryRadiusMeters) * 100.0);

	APawn* Best = nullptr;
	double BestSq = RadiusSq;

	auto Consider = [&Best, &BestSq, &Location](APawn* Candidate)
	{
		if (!Candidate)
		{
			return;
		}

		// Horizontal messen: ein Helikopter auf einem Dach ist nicht
		// erreichbar, aber die Hoehe soll nicht ueber die Naehe entscheiden.
		const FVector Delta = Candidate->GetActorLocation() - Location;
		const double DistSq = Delta.X * Delta.X + Delta.Y * Delta.Y;
		if (DistSq < BestSq)
		{
			BestSq = DistSq;
			Best = Candidate;
		}
	};

	Consider(PlayerVehicle);   // das aktuelle Auto (Chaos ODER Kaefer)
	Consider(PlayerHelicopter);
	return Best;
}

APawn* AWiesbadenGameMode::CommandeerTrafficVehicle(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World || !CitySubsystem)
	{
		return nullptr;
	}

	TArray<FTrafficVehicle>& Vehicles = CitySubsystem->TrafficSimulation.Vehicles;

	// Naechstes Verkehrsfahrzeug im Einstiegsradius suchen. Waagerecht
	// gemessen: ein Auto auf der Bruecke darueber ist nicht erreichbar, sein
	// Abstand im Raum aber klein.
	const double RadiusSq = FMath::Square(FMath::Max(1.0f, TrafficEntryRadiusMeters) * 100.0);
	int32 BestIndex = INDEX_NONE;
	double BestSq = RadiusSq;

	for (int32 i = 0; i < Vehicles.Num(); ++i)
	{
		const FVector Delta = Vehicles[i].Location - Location;
		if (FMath::Abs(Delta.Z) > 300.0)
		{
			continue;
		}
		const double FlatSq = Delta.X * Delta.X + Delta.Y * Delta.Y;
		if (FlatSq < BestSq)
		{
			BestSq = FlatSq;
			BestIndex = i;
		}
	}

	if (BestIndex == INDEX_NONE)
	{
		return nullptr;
	}

	const FVector SpawnLocation = Vehicles[BestIndex].Location;
	const FRotator SpawnRotation = Vehicles[BestIndex].Forward.Rotation();

	// Aus der Simulation NEHMEN, bevor das Auto entsteht. Bliebe es drin,
	// stuende an derselben Stelle weiter eine gezeichnete Instanz - der
	// Spieler saesse sichtbar in einem zweiten Wagen.
	Vehicles.RemoveAtSwap(BestIndex);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.Owner = this;

	AWiesbadenCar* Taken = World->SpawnActor<AWiesbadenCar>(
		AWiesbadenCar::StaticClass(), SpawnLocation, SpawnRotation, Params);

	if (!Taken)
	{
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Verkehrsfahrzeug uebernehmen: SpawnActor fehlgeschlagen."));
		return nullptr;
	}

	// Das uebernommene Auto ist ab jetzt DAS Spielerfahrzeug: HUD, Tacho und
	// die Hindernismeldung an den Verkehr haengen alle an PlayerCar; PlayerVehicle
	// verweist ebenfalls darauf (Helikopter-Bezug, Wiedereinstieg).
	PlayerCar = Taken;
	PlayerVehicle = Taken;

	UE_LOG(LogWbVehicles, Log,
		TEXT("Verkehrsfahrzeug uebernommen bei (%.0f, %.0f), %d Fahrzeuge verbleiben."),
		SpawnLocation.X, SpawnLocation.Y, Vehicles.Num());

	return Taken;
}

void AWiesbadenGameMode::TogglePlayerVehicle()
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	APawn* Current = PC->GetPawn();

	// -- Zu Fuss: einsteigen ------------------------------------------------
	if (Current == FootPawn && FootPawn)
	{
		APawn* Vehicle = FindNearbyVehicle(FootPawn->GetActorLocation());

		// Kein eigenes Fahrzeug in der Naehe? Dann ein VERKEHRSFAHRZEUG
		// uebernehmen. Der Verkehr besteht aus Instanzen ohne eigene Actors;
		// das naechstgelegene wird aus der Simulation genommen und an seiner
		// Stelle ein fahrbares Auto abgesetzt.
		if (!Vehicle)
		{
			Vehicle = CommandeerTrafficVehicle(FootPawn->GetActorLocation());
		}

		if (!Vehicle)
		{
			UE_LOG(LogWbVehicles, Log,
				TEXT("Einsteigen: kein Fahrzeug innerhalb von %.0f m."), EntryRadiusMeters);
			return;
		}	// Helikopter-Hangar (Ausgabe-Senke, TP2 Stueck 3): der Ka-52 ist standard
	// nur mit gekauftem Hangar einsteigbar. Ohne sie nicht uebernehmen, sondern
	// einen HUD-Hinweis zeigen. Autos/Verkehrsfahrzeuge bleiben unberuehrt.
	// DEBUG: Wenn WbDev_AllowHelicopterWithoutHangar definiert ist, ist das Tor
	// ohne Kauf offen - dann wird kein Hinweis mehr gezeigt.
	if (Cast<AWiesbadenHelicopter>(Vehicle))
	{
		const UGameInstance* GI = GetGameInstance();
		const UWiesbadenGameStateSubsystem* GS =
			GI ? GI->GetSubsystem<UWiesbadenGameStateSubsystem>() : nullptr;
		const bool bHasHangar =
			GS && GS->HasUnlock(FWiesbadenStore::HelikopterHangarId());
		if (!FWiesbadenStore::MayEnterHelicopter(bHasHangar))
		{
			// Tor ist noch geschlossen. Kauf-Hinweis, wenn kein DEBUG-Tor -
			// die DEBUG-Wahl kennt nur FWiesbadenStore, nicht der GameMode.
			if (FWiesbadenStore::ShouldShowHangarPurchaseHint())
			{
				if (AWiesbadenVehicleHUD* HUD = Cast<AWiesbadenVehicleHUD>(PC->GetHUD()))
				{
					HUD->ShowTransientHint(TEXT("Helikopter-Hangar kaufen (750 EUR)"));
				}
			}
			return;
		}
	}


		PC->UnPossess();
		PC->Possess(Vehicle);

		// Die Figur wird nicht zerstoert, sondern nur versteckt - so bleibt
		// beim naechsten Aussteigen ihre Ausrichtung erhalten.
		FootPawn->SetActorHiddenInGame(true);
		FootPawn->SetActorEnableCollision(false);

		UE_LOG(LogWbVehicles, Log, TEXT("Eingestiegen in %s."), *Vehicle->GetName());
		return;
	}

	// -- Am Steuer: aussteigen ----------------------------------------------
	if (!Current)
	{
		return;
	}

	// Neben dem Fahrzeug absetzen, nicht darin - sonst steckt die Figur in
	// der Karosserie und wird vom Sweep sofort weggeschoben.
	const FVector ExitLocation = Current->GetActorLocation()
		+ Current->GetActorRightVector() * -250.0
		+ FVector(0.0, 0.0, 50.0);
	const FRotator ExitRotation(0.0f, Current->GetActorRotation().Yaw, 0.0f);

	if (!FootPawn)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Params.Owner = this;

		FootPawn = World->SpawnActor<AWiesbadenFootPawn>(
			AWiesbadenFootPawn::StaticClass(), ExitLocation, ExitRotation, Params);

		if (!FootPawn)
		{
			UE_LOG(LogWbVehicles, Warning, TEXT("Aussteigen: Spielerfigur konnte nicht erzeugt werden."));
			return;
		}
	}
	else
	{
		FootPawn->SetActorLocationAndRotation(ExitLocation, ExitRotation);
	}

	FootPawn->SetActorHiddenInGame(false);
	FootPawn->SetActorEnableCollision(true);

	PC->UnPossess();
	PC->Possess(FootPawn);

	UE_LOG(LogWbVehicles, Log, TEXT("Ausgestiegen aus %s."), *Current->GetName());
}

bool AWiesbadenGameMode::SpawnHelicopterNearStart()
{
	UWorld* World = GetWorld();
	if (!World || !PlayerVehicle)
	{
		return false;
	}

	// Nur einmal absetzen: SpawnPlayerCarAtStartAddress wird auch aus
	// HandleCityStatus heraus gerufen, sobald die Stadt bereitsteht - ohne
	// Sperre stuenden zwei Helikopter da.
	if (PlayerHelicopter)
	{
		return true;
	}

	// Seitlich vom Fahrzeug absetzen und per Trace auf den Boden stellen.
	// VOR das Fahrzeug setzen, nicht daneben: seitlich liegt er ausserhalb des
	// Blickfelds der Verfolgerkamera und war schlicht nicht zu finden.
	const FVector Base = PlayerVehicle->GetActorLocation()
		+ PlayerVehicle->GetActorForwardVector() * (HelicopterDistanceMeters * 100.0)
		+ PlayerVehicle->GetActorRightVector() * 600.0;

	FVector SpawnLocation = Base + FVector(0.0, 0.0, 200.0);

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(PlayerVehicle);
	if (World->LineTraceSingleByChannel(Hit,
		Base + FVector(0.0, 0.0, 20000.0), Base - FVector(0.0, 0.0, 20000.0),
		ECC_Visibility, Params))
	{
		SpawnLocation = Hit.Location + FVector(0.0, 0.0, 60.0);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	// Verzoegert spawnen und die Selbstuebernahme abschalten: der Helikopter
	// steht wie das Auto auf AutoPossessPlayer = Player0 und riss den Spieler
	// sonst beim Absetzen aus dem Fahrzeug - man startete mitten in der Luft.
	const FTransform SpawnTransform(PlayerVehicle->GetActorRotation(), SpawnLocation);

	PlayerHelicopter = World->SpawnActorDeferred<AWiesbadenHelicopter>(
		AWiesbadenHelicopter::StaticClass(), SpawnTransform, this);

	if (PlayerHelicopter)
	{
		PlayerHelicopter->AutoPossessPlayer = EAutoReceiveInput::Disabled;
		PlayerHelicopter->FinishSpawning(SpawnTransform);
	}

	// Material setzen: der WUERFEL-Rueckfall trug ohne Zuweisung das
	// Default-Schachbrett.
	if (PlayerHelicopter)
	{
		// Das importierte Ka-52-Modell bringt seine eigenen PBR-Materialien mit
		// (M_Ka52PBR je Slot, geprueft von WiesbadenReal.Vehicles.HelicopterModell).
		// Die alte Zell-Tarnung darueberzulegen wuerde sie ueberschreiben - der
		// neue Heli saehe dann aus wie der alte, und das Standstueck daneben waere
		// nicht mehr zu unterscheiden.
		if (PlayerHelicopter->HasImportedModel())
		{
			UE_LOG(LogWbVehicles, Log,
				TEXT("Helikopter: importiertes Ka-52-Modell - eigene Materialien bleiben stehen."));
		}
		else if (UMaterialInterface* Paint = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Materials/City/M_WbHelicopter.M_WbHelicopter")))
		{
			// Rotorblaetter tragen ein eigenes dunkles Rotor-Material, NICHT die
			// Zell-Tarnung - ein Tarnmuster auf drehenden Blaettern sieht falsch aus.
			UMaterialInterface* RotorPaint = LoadObject<UMaterialInterface>(
				nullptr, TEXT("/Game/Assets/Landmarks/M_HeliRotorBase.M_HeliRotorBase"));

			TArray<UStaticMeshComponent*> Meshes;
			PlayerHelicopter->GetComponents<UStaticMeshComponent>(Meshes);
			for (UStaticMeshComponent* Mesh : Meshes)
			{
				if (!Mesh)
				{
					continue;
				}
				// Die durchscheinenden FX-Scheiben (Rotor-Blur, Downwash-Staub)
				// tragen ihre eigenen Translucent-Materialien - NICHT ueberlackieren,
				// sonst wuerde die Blaugrau-Zelle die Effekte verdecken.
				const FString CompName = Mesh->GetName();
				if (CompName.Contains(TEXT("Blur")) || CompName.Contains(TEXT("Dust")))
				{
					continue;
				}
				// Solide Rotorblaetter -> Rotor-Material; alles andere -> Zell-Tarnung.
				if (CompName.Contains(TEXT("Rotor")) && RotorPaint)
				{
					Mesh->SetMaterial(0, RotorPaint);
					continue;
				}
				Mesh->SetMaterial(0, Paint);
			}
		}
	}

	if (!PlayerHelicopter)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Helikopter konnte nicht abgesetzt werden."));
		return false;
	}

	UE_LOG(LogWbVehicles, Log,
		TEXT("Helikopter abgesetzt: %.0f m neben dem Fahrzeug bei (%.0f, %.0f, %.0f) - mit F einsteigen."),
		HelicopterDistanceMeters, SpawnLocation.X, SpawnLocation.Y, SpawnLocation.Z);

	// Das alte Modell daneben aufstellen - es ist die Anschauung zum neuen.
	if (bSpawnLegacyHelicopter)
	{
		SpawnLegacyHelicopterNearStart();
	}
	return true;
}

double AWiesbadenGameMode::ComputeHelicopterStandDistanceCm(
	double OwnDiscCm, double LegacyDiscCm, double OwnLengthCm, double LegacyLengthCm)
{
	// Rotorkreise: die Scheiben duerfen sich nicht schneiden, 5 m Luft zwischen
	// den Blattspitzen. Rumpflaengen: bei gleicher Ausrichtung duerfen sich die
	// Rumpfspitzen nicht beruehren, 2 m Luft.
	const double Discs = (FMath::Max(OwnDiscCm, 0.0) + FMath::Max(LegacyDiscCm, 0.0)) * 0.5 + 500.0;
	const double Hulls = (FMath::Max(OwnLengthCm, 0.0) + FMath::Max(LegacyLengthCm, 0.0)) * 0.5 + 200.0;

	// Ohne Netze (frischer Checkout) bleibt ein Mindestabstand - ein Standstueck
	// darf nie im Spielerheli stehen.
	return FMath::Max(800.0, FMath::Max(Discs, Hulls));
}

TArray<FVector> AWiesbadenGameMode::BuildHelicopterStandCandidates(
	const FVector& Anchor, double ForwardYawDeg, double StandDistanceCm)
{
	TArray<FVector> Candidates;

	// Erst seitlich ausweichen, dann weiter weg. Ein Platz 30 Grad neben der
	// Flucht sieht der gewohnten Aufstellung noch aehnlich; einer 20 m weiter
	// vorn waere ein anderes Bild. Deshalb faechert die Richtung zuerst auf.
	static const double AngleOffsets[] = {
		0.0, 30.0, -30.0, 60.0, -60.0, 90.0, -90.0,
		120.0, -120.0, 150.0, -150.0, 180.0 };
	static const double DistanceFactors[] = { 1.0, 1.6, 2.4 };

	Candidates.Reserve(UE_ARRAY_COUNT(AngleOffsets) * UE_ARRAY_COUNT(DistanceFactors));
	for (const double Factor : DistanceFactors)
	{
		for (const double Offset : AngleOffsets)
		{
			const double Yaw = ForwardYawDeg + Offset;
			const FVector Direction = FRotator(0.0, Yaw, 0.0).Vector();
			Candidates.Add(Anchor + Direction * (StandDistanceCm * Factor));
		}
	}

	return Candidates;
}

bool AWiesbadenGameMode::IsHelicopterStandFree(
	const FVector& Point, double FootprintCm,
	const FWiesbadenRoadClearance& Carriageway, double& OutGroundZ) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	// Mitte plus acht Punkte auf dem Rumpfkreis. Nur die Mitte zu pruefen
	// reicht nicht: der Rumpf ist ueber 14 m lang, die Mitte kann neben der
	// Fahrbahn liegen waehrend die Nase darauf steht.
	TArray<FVector2D> Offsets;
	Offsets.Add(FVector2D::ZeroVector);
	for (int32 Step = 0; Step < 8; ++Step)
	{
		const double Angle = 2.0 * PI * Step / 8.0;
		Offsets.Add(FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * FootprintCm);
	}

	// FAHRBAHN aus dem STRASSENNETZ, nicht aus der Kollision.
	//
	// Das Standstueck wird im ersten Bild gesetzt - da hat World Partition noch
	// keine einzige Stadtkachel hereingestreamt, und ein Lot trifft nur die
	// Landschaft. Eine Pruefung gegen die Fahrbahn-Kollision meldete deshalb
	// selbst mitten auf der Strasse "frei"; nachgemessen am Standort des
	// Spielerautos, das auf der Fahrbahn steht und als frei galt. Das Netz
	// liegt dagegen serialisiert am WorldBuilder und ist sofort da.
	for (const FVector2D& Offset : Offsets)
	{
		if (Carriageway.IsBlocked(FVector2D(Point.X + Offset.X, Point.Y + Offset.Y)))
		{
			return false;   // Fahrbahn - genau das soll nie passieren.
		}
	}

	// Die HOEHE darf weiterhin aus dem Lot kommen: die Landschaft ist von
	// Anfang an geladen und traegt den Hubschrauber.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbLegacyHeliStand), false);
	if (PlayerVehicle) { Params.AddIgnoredActor(PlayerVehicle); }
	if (PlayerHelicopter) { Params.AddIgnoredActor(PlayerHelicopter); }

	bool bAnyGround = false;
	double HighestZ = -TNumericLimits<double>::Max();

	for (const FVector2D& Offset : Offsets)
	{
		const FVector Probe(Point.X + Offset.X, Point.Y + Offset.Y, Point.Z);

		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit,
			Probe + FVector(0.0, 0.0, 20000.0), Probe - FVector(0.0, 0.0, 20000.0),
			ECC_Visibility, Params))
		{
			continue;   // Loch oder ungeladen - dieser Punkt traegt nichts bei.
		}

		bAnyGround = true;
		HighestZ = FMath::Max(HighestZ, Hit.Location.Z);
	}

	if (!bAnyGround)
	{
		return false;
	}

	// Der HOECHSTE Treffer traegt: auf unebenem Grund wuerde der tiefste die
	// Maschine mit der anderen Seite im Boden versenken.
	OutGroundZ = HighestZ;
	return true;
}

bool AWiesbadenGameMode::SpawnLegacyHelicopterNearStart()
{
	UWorld* World = GetWorld();
	if (!World || !PlayerVehicle || !PlayerHelicopter)
	{
		return false;
	}
	// Nur einmal: SpawnHelicopterNearStart laeuft auch aus HandleCityStatus.
	if (LegacyHelicopter)
	{
		return true;
	}

	// Standabstand aus den Rotorkreisen und der Rumpflaenge. Am CDO gemessen -
	// die Netze haengen dort schon im Konstruktor, eine Welt braucht es dafuer
	// nicht; nur bei fehlenden Assets bleibt der Mindestabstand.
	const AWiesbadenLegacyHelicopter* LegacyCDO = GetDefault<AWiesbadenLegacyHelicopter>();
	const double OwnDiscCm = PlayerHelicopter->GetUpperRotorDiameterCm();
	const double LegacyDiscCm = LegacyCDO ? LegacyCDO->GetUpperRotorDiameterCm() : 0.0;
	const double OwnLengthCm = PlayerHelicopter->GetNoseToTailCm();
	const double LegacyLengthCm = LegacyCDO ? LegacyCDO->GetNoseToTailCm() : 0.0;
	const double StandDistanceCm = ComputeHelicopterStandDistanceCm(
		OwnDiscCm, LegacyDiscCm, OwnLengthCm, LegacyLengthCm);

	// VOM SPIELERHELI AUS messen, nicht vom Auto.
	//
	// Vom Auto aus gerechnet (erster Entwurf) rueckte das Standstueck um den
	// Seitenversatz des Helis aus der Flucht - die beiden standen dann schraeg
	// zueinander statt nebeneinander. Und der Abstand war nicht mehr der
	// gerechnete, sondern die Diagonale darueber.
	// Platz SUCHEN statt blind setzen.
	//
	// Bisher stand das Standstueck immer genau vor dem Spielerheli, mit einem
	// Lot fuer die Hoehe und sonst keiner Pruefung. Fuehrt dort eine Strasse
	// entlang, stand die Maschine mitten auf der Fahrbahn - der Verkehr faehrt
	// hindurch, und zu sehen ist ein Hubschrauber auf der Strasse. Der erste
	// Kandidat ist weiterhin der gewohnte Platz; erst wenn der auf der Fahrbahn
	// liegt, wird gefaechert.
	const double FootprintCm = FMath::Max(LegacyLengthCm * 0.5, 200.0);
	const FVector Anchor = PlayerHelicopter->GetActorLocation();
	const TArray<FVector> Candidates = BuildHelicopterStandCandidates(
		Anchor, PlayerHelicopter->GetActorRotation().Yaw, StandDistanceCm);

	// Fahrbahn-Index NUR um den Ankerpunkt: ein Index ueber alle rund 125.000
	// Abschnitte der Stadt kostete eine spuerbare Pause fuer eine einzige
	// Frage. Der Umkreis muss den weitesten Kandidaten samt Grundriss
	// einschliessen, sonst gilt dort alles als frei.
	const double AreaRadiusCm = StandDistanceCm * 2.4 + FootprintCm + 1000.0;

	AWiesbadenWorldBuilder* NetworkBuilder = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It)
	{
		if (It->RoadNetwork.Segments.Num() > 0)
		{
			NetworkBuilder = *It;
			break;
		}
	}

	FWiesbadenRoadClearance Carriageway;
	if (NetworkBuilder)
	{
		// Ohne Gehweg: der Hubschrauber darf am Fahrbahnrand stehen, nur nicht
		// auf der Fahrbahn. 150 cm Zuschlag halten ihn von der Bordsteinkante weg.
		Carriageway.BuildAround(NetworkBuilder->RoadNetwork,
			FVector2D(Anchor.X, Anchor.Y), AreaRadiusCm,
			/*ExtraMarginCm=*/150.0, /*bIncludeSidewalk=*/false);
	}

	if (Carriageway.IsEmpty())
	{
		// Kein Netz, keine Aussage. Lieber gar nicht aufstellen als blind auf
		// die Strasse - genau das war der Fehler.
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Alter Helikopter nicht aufgestellt: kein Strassennetz zum Pruefen ")
			TEXT("der Fahrbahn um (%.0f, %.0f)."), Anchor.X, Anchor.Y);
		return false;
	}

	// Der Ursprung des ALTEN Rumpfnetzes liegt an der Rumpfunterseite (gemessene
	// Bounds z 0..17,1 cm, nicht der Mittelpunkt), der Actor-Ursprung darf also
	// direkt auf die Aufstandsflaeche - die kleine Zugabe deckt die Unebenheit
	// des Gelaendes ab.
	constexpr double GroundClearanceCm = 5.0;

	FVector StandLocation = FVector::ZeroVector;
	int32 ChosenIndex = INDEX_NONE;
	for (int32 Index = 0; Index < Candidates.Num(); ++Index)
	{
		double GroundZ = 0.0;
		if (IsHelicopterStandFree(Candidates[Index], FootprintCm, Carriageway, GroundZ))
		{
			StandLocation = FVector(
				Candidates[Index].X, Candidates[Index].Y, GroundZ + GroundClearanceCm);
			ChosenIndex = Index;
			break;
		}
	}

	if (ChosenIndex == INDEX_NONE)
	{
		// Lieber gar kein Standstueck als eines auf der Fahrbahn. Tritt nur
		// ein, wenn im ganzen Umkreis nichts als Strasse liegt.
		UE_LOG(LogWbVehicles, Warning,
			TEXT("Alter Helikopter nicht aufgestellt: kein fahrbahnfreier Platz ")
			TEXT("unter %d geprueften Stellen um (%.0f, %.0f) (%d Fahrbahnabschnitte)."),
			Candidates.Num(), Anchor.X, Anchor.Y, Carriageway.GetSpanCount());
		return false;
	}

	// Nur die Richtung uebernehmen: das Standstueck steht aufrecht, auch wenn
	// der Spielerheli Laengs- oder Querneigung hat.
	const FRotator StandRotation(0.0, PlayerHelicopter->GetActorRotation().Yaw, 0.0);

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.Owner = this;

	LegacyHelicopter = World->SpawnActor<AWiesbadenLegacyHelicopter>(
		AWiesbadenLegacyHelicopter::StaticClass(), StandLocation, StandRotation, SpawnParams);

	if (!LegacyHelicopter)
	{
		UE_LOG(LogWbVehicles, Warning, TEXT("Alter Helikopter konnte nicht aufgestellt werden."));
		return false;
	}

	UE_LOG(LogWbVehicles, Log,
		TEXT("Alter Helikopter steht fahrbahnfrei bei (%.0f, %.0f, %.0f), %.1f m vom ")
		TEXT("Spielerheli (Platz %d von %d geprueften) - Rotorkreise %.1f / %.1f m, ")
		TEXT("Rumpf %.1f m, Grundriss %.1f m."),
		StandLocation.X, StandLocation.Y, StandLocation.Z,
		FVector::Dist2D(StandLocation, Anchor) * 0.01,
		ChosenIndex + 1, Candidates.Num(),
		OwnDiscCm * 0.01, LegacyDiscCm * 0.01, LegacyLengthCm * 0.01,
		FootprintCm * 0.01);
	return true;
}

void AWiesbadenGameMode::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CitySubsystem)
	{
		CitySubsystem->OnCityStateChanged.RemoveDynamic(this, &AWiesbadenGameMode::HandleCityStatus);
		CitySubsystem = nullptr;
	}
	Super::EndPlay(EndPlayReason);
}

void AWiesbadenGameMode::InitializeCity()
{
	if (CitySubsystem)
	{
		CitySubsystem->InitializeCity();
	}
	else
	{
		UE_LOG(LogWbCore, Warning, TEXT("InitializeCity: Kein City-Subsystem verfuegbar."));
	}
}

bool AWiesbadenGameMode::IsCityReady() const
{
	return CitySubsystem && CitySubsystem->IsCityReady();
}

bool AWiesbadenGameMode::IsCityStreamingComplete() const
{
	return CitySubsystem && CitySubsystem->IsCityStreamingComplete();
}

bool AWiesbadenGameMode::IsWorldPartitionActive() const
{
	return CitySubsystem && CitySubsystem->IsWorldPartitionActive();
}

FString AWiesbadenGameMode::GetCityStatus() const
{
	return CitySubsystem ? CitySubsystem->GetCityStatus() : TEXT("Kein City-Subsystem");
}

FString AWiesbadenGameMode::GetLastCityError() const
{
	return CitySubsystem ? CitySubsystem->GetLastCityError() : TEXT("Kein City-Subsystem");
}

FLastBuildInfo AWiesbadenGameMode::GetLastBuildInfo() const
{
	return CitySubsystem ? CitySubsystem->GetLastBuildInfo() : FLastBuildInfo();
}

FString AWiesbadenGameMode::GetLastBuildTimestamp() const
{
	return GetLastBuildInfo().Timestamp;
}

double AWiesbadenGameMode::GetLastBuildDurationSeconds() const
{
	return GetLastBuildInfo().DurationSeconds;
}

FString AWiesbadenGameMode::GetLastBuildResult() const
{
	return GetLastBuildInfo().Result;
}

FString AWiesbadenGameMode::GetLastBuildSummary() const
{
	return GetLastBuildInfo().GetSummary();
}

FTerrainQualityReport AWiesbadenGameMode::GetTerrainQuality() const
{
	return CitySubsystem ? CitySubsystem->GetTerrainQuality() : FTerrainQualityReport();
}

void AWiesbadenGameMode::HandleCityStatus(FString Status, bool bSuccess)
{
	OnCityStatus.Broadcast(Status, bSuccess);

	// Wird die Stadt erst zur Laufzeit erzeugt, gibt es beim Start noch kein
	// Strassennetz. Sobald sie bereitsteht, wird der Einsatz nachgeholt -
	// SpawnPlayerCarAtStartAddress ist idempotent.
	if (bSuccess && bSpawnPlayerCar && !PlayerVehicle)
	{
		// Helikopter direkt mit absetzen - er braucht die Fahrzeugposition als
		// Bezug und war bisher ueberhaupt nicht in der Welt vorhanden.
		if (SpawnPlayerCarAtStartAddress() && bSpawnHelicopter)
		{
			SpawnHelicopterNearStart();
		}
	}
}
