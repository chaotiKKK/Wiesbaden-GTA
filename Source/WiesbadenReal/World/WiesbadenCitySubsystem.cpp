// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCitySubsystem.h"

#include "WiesbadenReal.h"

#include "GIS/RoadNetworkGenerator.h"   // ERoadMeshChannel

#include "Camera/CameraActor.h"
#include "Components/BoxComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Core/WiesbadenGameInstance.h"
#include "World/BuildingCollisionSpawnerComponent.h"
#include "LandscapeHeightfieldCollisionComponent.h"
#include "LandscapeProxy.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "UnrealEngine.h"
#include "Engine/World.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "Vehicles/WiesbadenChaosCar.h"
#include "Vehicles/WiesbadenHelicopter.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/ScopeExit.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "World/WiesbadenCityActor.h"
#include "World/WiesbadenCityChunk.h"
#include "World/WiesbadenStreamingSource.h"
#include "UnrealClient.h"
#include "WorldPartition/WorldPartitionSubsystem.h"

#include "RHI.h"
#include "Stats/Stats.h"

void UWiesbadenCitySubsystem::DumpStreetNetwork(const FString& Path) const
{
	const AWiesbadenWorldBuilder* Builder = FindBakedCityBuilder();
	if (!Builder)
	{
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Strassen-Abzug: kein gebackener WorldBuilder gefunden."));
		return;
	}

	const FRoadNetwork& Net = Builder->RoadNetwork;

	// Semikolon als Trenner: Strassennamen enthalten Kommas
	// ("Bahnhofstrasse, Zufahrt"), und ein CSV, das an den eigenen Daten
	// zerbricht, verschiebt den Fehler nur in die Auswertung.
	TArray<FString> Lines;
	Lines.Reserve(Net.Segments.Num() + 1);
	Lines.Add(TEXT("WayId;Name;Art;Punkte;LaengeM;StartX;StartY;EndX;EndY;Flaeche"));

	double TotalLengthM = 0.0;
	for (const FRoadSegment& Segment : Net.Segments)
	{
		const TArray<FVector>& Line = Segment.bIsArea ? Segment.AreaOutline : Segment.Centerline;
		if (Line.Num() < 2)
		{
			continue;
		}

		double LengthCm = 0.0;
		for (int32 Index = 1; Index < Line.Num(); ++Index)
		{
			LengthCm += FVector::Dist(Line[Index - 1], Line[Index]);
		}
		TotalLengthM += LengthCm * 0.01;

		Lines.Add(FString::Printf(
			TEXT("%lld;%s;%d;%d;%.1f;%.0f;%.0f;%.0f;%.0f;%d"),
			Segment.SourceWayId,
			*Segment.StreetName.Replace(TEXT(";"), TEXT(",")),
			static_cast<int32>(Segment.HighwayType),
			Line.Num(),
			LengthCm * 0.01,
			Line[0].X, Line[0].Y,
			Line.Last().X, Line.Last().Y,
			Segment.bIsArea ? 1 : 0));
	}

	if (FFileHelper::SaveStringArrayToFile(Lines, *Path))
	{
		UE_LOG(LogWbStreaming, Log,
			TEXT("Strassen-Abzug: %d Segmente, %.1f km, geschrieben nach %s"),
			Lines.Num() - 1, TotalLengthM * 0.001, *Path);
	}
	else
	{
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Strassen-Abzug: Schreiben nach %s fehlgeschlagen."), *Path);
	}
}

void UWiesbadenCitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	CityStatus = TEXT("Initialisiert");
}

void UWiesbadenCitySubsystem::Deinitialize()
{
	// Laufzeit-Build-Ereignis abloesen, damit ein gestorbenes Subsystem nie
	// vom GameInstance aufgerufen wird (GameInstance ueberlebt Welten).
	if (UWiesbadenGameInstance* GI = WeakGameInstance.Get())
	{
		if (OnCityDataLoadedHandle.IsValid())
		{
			GI->OnCityDataLoadedNative.Remove(OnCityDataLoadedHandle);
			OnCityDataLoadedHandle.Reset();
		}
	}
	WeakGameInstance.Reset();

	Super::Deinitialize();
}

void UWiesbadenCitySubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	bWorldPartitionActive = InWorld.IsPartitionedWorld();
	if (bWorldPartitionActive)
	{
		UE_LOG(LogWbCore, Log, TEXT("Welt ist partitioniert - World Partition aktiv."));
	}
	else
	{
		UE_LOG(LogWbCore, Log,
			TEXT("Welt ist NICHT partitioniert - World-Partition-Streaming inaktiv; Stadt wird ungestreamt geladen."));
	}

	// Streaming-Quelle folgt dem Player-Pawn (nur relevant bei World Partition).
	if (bWorldPartitionActive)
	{
		EnsureStreamingSource();
	}

	InitializeCity();
}

void UWiesbadenCitySubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	ClearCity();
	Super::OnWorldEndPlay(InWorld);
}

void UWiesbadenCitySubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Gesamtzeit dieses Subsystems messen.
	//
	// Der Spiel-Strang braucht 400 ms je Bild, der Renderer nur 8,3 ms. Die
	// Simulationen darin kosten zusammen 1,8 ms. Diese Messung trennt "mein
	// Subsystem" von "alles andere auf dem Spiel-Strang".
	const double SubsystemStart = FPlatformTime::Seconds();
	ON_SCOPE_EXIT
	{
		SubsystemTimeMs += (FPlatformTime::Seconds() - SubsystemStart) * 1000.0;
	};

	// Tageszeit von der Kommandozeile: -WbTime=<Stunden>, einmalig beim
	// ersten Tick.
	//
	// Die Tageszeit laeuft mit einer Stunde je 2,5 Realminuten und startet um
	// 9 Uhr - wer die Nachtbeleuchtung sehen will, muesste sonst eine halbe
	// Stunde fahren.
	//
	// Der Schalter sass zuvor an zwei falschen Stellen: erst im
	// Screenshot-Pfad (der Zustand stimmte, aber die Beleuchtung folgt ueber
	// eine Ueberblendung von Sekunden - das Bild blieb taghell), dann im
	// Stadt-Aufbau, der in einer GEBACKENEN Karte gar nicht laeuft und den
	// Schalter deshalb kommentarlos verschluckte. Der Tick des Subsystems
	// laeuft in jeder Sitzung.
	if (!bTimeOverrideApplied)
	{
		bTimeOverrideApplied = true;

		float ForcedHours = -1.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbTime="), ForcedHours) && ForcedHours >= 0.0f)
		{
			Weather.SetTimeOfDay(ForcedHours);
			UE_LOG(LogWbCore, Log,
				TEXT("Tageszeit auf %.1f Uhr gesetzt (-WbTime). Sonnenstand %.2f, Nacht: %s."),
				Weather.GetState().TimeOfDayHours,
				Weather.GetState().SunElevationFactor,
				Weather.GetState().bIsNight ? TEXT("ja") : TEXT("nein"));
		}
	}

	// Ladereichweite der World Partition setzen - einmalig.
	//
	// Die Konsolenvariable ist der einzige Weg: Die Grid-Einstellungen des
	// RuntimeHash sind weder ueber Python noch ueber eine oeffentliche
	// C++-Schnittstelle erreichbar.
	if (!bLoadingRangeApplied)
	{
		bLoadingRangeApplied = true;

		float RangeMeters = WorldPartitionLoadingRangeMeters;
		FParse::Value(FCommandLine::Get(), TEXT("WbLoadRange="), RangeMeters);

		if (RangeMeters > 0.0f && GEngine)
		{
			// Das ist ein Konsolen-BEFEHL, keine Variable:
			//   wp.Runtime.OverrideRuntimeSpatialHashLoadingRange -grid=<i> -range=<cm>
			// FindConsoleVariable findet ihn deshalb nicht - der erste Versuch
			// lief ins Leere und meldete nur eine Warnung.
			const FString Command = FString::Printf(
				TEXT("wp.Runtime.OverrideRuntimeSpatialHashLoadingRange -grid=0 -range=%.0f"),
				RangeMeters * 100.0f);

			GEngine->Exec(GetWorld(), *Command);

			UE_LOG(LogWbStreaming, Log,
				TEXT("World-Partition-Ladereichweite auf %.0f m gesetzt (%s)."),
				RangeMeters, *Command);
		}

		// Lastanteile einzeln abschalten, um die Ursache des Ruckelns zu
		// trennen. MUSS hier stehen und nicht im Screenshot-Pfad: Die
		// Bildzeitmessung laeuft von Sekunde vier bis acht, eine Ausblendung
		// bei Sekunde acht kaeme zu spaet.
		//
		//   -WbHideChunks     Strassen und Gebaeude ausblenden
		//   -WbHideFurniture  Schilder, Leitpfosten, Laternen, Baeume ausblenden
		if (UWorld* LoadWorld = GetWorld())
		{
			if (FParse::Param(FCommandLine::Get(), TEXT("WbHideChunks")))
			{
				int32 Hidden = 0;
				for (TActorIterator<AWiesbadenCityChunk> It(LoadWorld); It; ++It)
				{
					It->SetActorHiddenInGame(true);
					++Hidden;
				}
				UE_LOG(LogWbStreaming, Log,
					TEXT("Lasttrennung: %d Chunk-Actors ausgeblendet."), Hidden);
			}

			// Strassen-Kollision abschalten.
			//
			// Der Spiel-Strang braucht 429 ms, der Renderer 8,7 ms, mein
			// Subsystem davon 4,2 ms. Ausblenden von Geometrie aendert nichts,
			// weil das nur das Zeichnen betrifft. Die Physik dagegen verarbeitet
			// die Fahrbahnen weiterhin - seit dieser Sitzung mit Kollision, und
			// die Boeschungen haben 3,5 Mio. Dreiecke ergaenzt.
			if (FParse::Param(FCommandLine::Get(), TEXT("WbNoRoadCollision")))
			{
				int32 Disabled = 0;
				for (TActorIterator<AWiesbadenCityChunk> It(LoadWorld); It; ++It)
				{
					if (UProceduralMeshComponent* Mesh = It->GetRoadMesh())
					{
						Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						++Disabled;
					}
				}
				UE_LOG(LogWbStreaming, Log,
					TEXT("Lasttrennung: Strassen-Kollision an %d Chunks abgeschaltet."), Disabled);
			}

			// Gelaende FRUEH ausblenden.
			//
			// -WbHideTerrain im Screenshot-Pfad greift erst nach acht
			// Sekunden und damit nach dem Messfenster. Die Landscape war
			// deshalb der einzige grosse Posten, der bei allen bisherigen
			// Vergleichsmessungen IMMER aktiv war - 4033 x 4033 Punkte in
			// 64 x 64 Komponenten.
			if (FParse::Param(FCommandLine::Get(), TEXT("WbHideLandscape")))
			{
				int32 Hidden = 0;
				for (TActorIterator<ALandscapeProxy> It(LoadWorld); It; ++It)
				{
					It->SetActorHiddenInGame(true);
					++Hidden;
				}
				UE_LOG(LogWbStreaming, Log,
					TEXT("Lasttrennung: %d Landscape-Actors ausgeblendet."), Hidden);
			}

			// Nur die Baeume ausblenden: -WbHideTrees.
			//
			// -WbHideFurniture nimmt alle Instanz-Komponenten weg - Schilder,
			// Laternen, Leitpfosten, Baeume - und beantwortet damit nicht,
			// welche davon es ist. Das benannte GPU-Profil zeigt zwei
			// Eintraege mit zusammen 55 der 70 ms:
			//
			//     27,69 ms  WorldGridMaterial  Cone (100 Instanzen)
			//     27,25 ms  M_WbTree           Cone (100 Instanzen)
			//
			// Das ist derselbe Kegel zweimal - einmal im Tiefendurchgang, wo
			// undurchsichtige Materialien den Tiefen-Shader des
			// Standardmaterials benutzen, einmal im Basisdurchgang. Bei
			// 1.532.254 Baeumen zu je 8 x 8 x 16 Metern ueberdecken sich die
			// sichtbaren vielfach, und jeder Bildpunkt wird dutzendfach
			// beschrieben.
			if (FParse::Param(FCommandLine::Get(), TEXT("WbHideTrees")))
			{
				int32 Hidden = 0;
				int32 Instances = 0;
				for (TActorIterator<AActor> It(LoadWorld); It; ++It)
				{
					TArray<UInstancedStaticMeshComponent*> Instanced;
					It->GetComponents<UInstancedStaticMeshComponent>(Instanced);
					for (UInstancedStaticMeshComponent* Component : Instanced)
					{
						if (Component && Component->GetInstanceCount() > 0
							&& Component->GetFName() == TEXT("Trees"))
						{
							Instances += Component->GetInstanceCount();
							Component->SetVisibility(false);
							++Hidden;
						}
					}
				}
				UE_LOG(LogWbStreaming, Log,
					TEXT("Lasttrennung: %d Baum-Komponenten mit %d Instanzen ausgeblendet."),
					Hidden, Instances);
			}

			// Baumbestand ausduennen: -WbThinTrees=<N> behaelt jede N-te Instanz.
			//
			// Trennt zwei Erklaerungen, die dieselbe Ersparnis erzeugen wuerden:
			// Ueberdeckung (viel Flaeche mehrfach beschrieben) oder schiere
			// Instanzverwaltung. Das Bild von der Messstelle
			// (Saved/Diagnose/Messstelle00000.png) zeigt am Horizont eine
			// Handvoll winziger Kegel und sonst keinen Baum - Flaeche kann es
			// also nicht sein. Bleibt die Verwaltung: EINE Komponente haelt
			// 1.532.254 Instanzen, und sie haengt am WorldBuilder, wird also
			// nie gestreamt.
			//
			// Skaliert die Ersparnis mit N, ist die Instanzzahl die Ursache und
			// die Loesung heisst ausduennen und auf die Chunks verteilen.
			{
				int32 KeepEveryNth = 0;
				if (FParse::Value(FCommandLine::Get(), TEXT("WbThinTrees="), KeepEveryNth)
					&& KeepEveryNth > 1)
				{
					for (TActorIterator<AActor> It(LoadWorld); It; ++It)
					{
						TArray<UInstancedStaticMeshComponent*> Instanced;
						It->GetComponents<UInstancedStaticMeshComponent>(Instanced);
						for (UInstancedStaticMeshComponent* Component : Instanced)
						{
							if (!Component || Component->GetFName() != TEXT("Trees")
								|| Component->GetInstanceCount() == 0)
							{
								continue;
							}

							const int32 Before = Component->GetInstanceCount();
							TArray<FTransform> Kept;
							Kept.Reserve(Before / KeepEveryNth + 1);
							for (int32 Index = 0; Index < Before; Index += KeepEveryNth)
							{
								FTransform Transform;
								if (Component->GetInstanceTransform(Index, Transform, true))
								{
									Kept.Add(Transform);
								}
							}

							Component->ClearInstances();
							Component->PreAllocateInstancesMemory(Kept.Num());
							Component->AddInstances(Kept, false, true);
							Component->MarkRenderStateDirty();

							UE_LOG(LogWbStreaming, Log,
								TEXT("Lasttrennung: Baeume von %d auf %d ausgeduennt (jede %d-te)."),
								Before, Kept.Num(), KeepEveryNth);
						}
					}
				}
			}

			if (FParse::Param(FCommandLine::Get(), TEXT("WbHideFurniture")))
			{
				int32 Hidden = 0;
				for (TActorIterator<AActor> It(LoadWorld); It; ++It)
				{
					TArray<UInstancedStaticMeshComponent*> Instanced;
					It->GetComponents<UInstancedStaticMeshComponent>(Instanced);
					for (UInstancedStaticMeshComponent* Component : Instanced)
					{
						if (Component && Component->GetInstanceCount() > 0)
						{
							Component->SetVisibility(false);
							++Hidden;
						}
					}
				}
				UE_LOG(LogWbStreaming, Log,
					TEXT("Lasttrennung: %d Instanz-Komponenten ausgeblendet."), Hidden);
			}
		}

		// Beleuchtungsverfahren protokollieren.
		//
		// Die dunklen Schattenfassaden wurden mehrfach dem fehlenden
		// indirekten Licht zugeschrieben, ohne dass je geprueft wurde, WELCHES
		// Verfahren tatsaechlich laeuft. 0 = keines, 1 = Lumen.
			// Lumen abschaltbar machen: -WbNoLumen.
	//
	// r.DynamicGlobalIlluminationMethod steht in DefaultEngine.ini auf 1. Eine
	// Vorgabe aus der INI greift vor -ExecCmds - ein Vergleichslauf "mit und
	// ohne Lumen" ueber die Kommandozeile misst deshalb zweimal dasselbe.
	// Genau das ist passiert: beide Laeufe meldeten weiterhin Methode 1.
	//
	// Hier, im ersten Tick, ist die INI laengst angewendet.
	// Lumen ist in DefaultEngine.ini AUS (siehe Begruendung dort).
	// -WbLumen schaltet es zum Vergleich wieder ein, -WbNoLumen erzwingt Aus.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbLumen")))
	{
		for (const TCHAR* Name : { TEXT("r.DynamicGlobalIlluminationMethod"),
			TEXT("r.ReflectionMethod") })
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Var->Set(1, ECVF_SetByCode);
			}
		}
		UE_LOG(LogWbStreaming, Log, TEXT("Lumen eingeschaltet (-WbLumen)."));
	}

	// Auflösungsskalierung: -WbScreenPercentage=<Prozent>.
	//
	// Nach der Kantenglaettung und den Schatten ist die schiere Pixelzahl der
	// naechste Hebel, und der einzige, der ALLE Durchgaenge zugleich
	// entlastet - Tiefendurchgang, Basisdurchgang, Beleuchtung,
	// Nachbearbeitung. 80 Prozent bedeuten 36 Prozent weniger Bildpunkte.
	//
	// Mit TAA kostet das wenig Schaerfe, weil die Glaettung ueber mehrere
	// Bilder mittelt. Ob das im Video auffaellt, ist eine Frage fuers Auge und
	// nicht fuer die Messung - deshalb ein Schalter und keine feste Vorgabe.
	{
		float ScreenPercent = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbScreenPercentage="), ScreenPercent)
			&& ScreenPercent > 10.0f && ScreenPercent <= 200.0f)
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(
					TEXT("r.ScreenPercentage")))
			{
				Var->Set(ScreenPercent, ECVF_SetByCode);
				UE_LOG(LogWbStreaming, Log,
					TEXT("Auflösungsskalierung: %.0f Prozent."), ScreenPercent);
			}
		}
	}

	// Kantenglaettung umschalten: -WbAA=<Modus>.
	//
	// `stat gpu` im laufenden Betrieb zeigt, wo die Bildzeit haengt:
	//
	//     Queue Total                 52,93 ms
	//       Postprocessing            25,61 ms
	//         TemporalSuperResolution 23,66 ms   <- 45 Prozent
	//       Shadow Depths              7,17 ms
	//       RenderDeferredLighting     5,88 ms
	//       Basepass                   4,51 ms
	//
	// TSR fuehrt seine Historie bei Stufe "Epic" in 2560 x 1440 - der
	// vierfachen Pixelzahl des 1280 x 720 grossen Bildes. Auf einer
	// RTX 3050 Ti Laptop ist das nicht zu bezahlen.
	//
	// WICHTIG: NICHT ueber -ExecCmds versuchen. Eine Vorgabe aus
	// DefaultEngine.ini greift davor, und ein Vergleichslauf misst dann
	// zweimal dasselbe - genau so ist der Lumen-Vergleich einmal ins Leere
	// gelaufen.
	//
	//   1 = TSR, Historie in Bildaufloesung statt doppelter
	//   2 = TAA  (aelter, guenstiger, neigt zu Schlieren)
	//   3 = FXAA (rein bildbasiert, am guenstigsten, am weichsten)
	//   4 = keine Kantenglaettung (nur zur Messung, flimmert)
	{
		int32 AaMode = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbAA="), AaMode) && AaMode > 0)
		{
			const auto SetVar = [](const TCHAR* Name, int32 Value)
			{
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
				{
					Var->Set(Value, ECVF_SetByCode);
				}
			};

			switch (AaMode)
			{
			case 1:
				SetVar(TEXT("r.AntiAliasingMethod"), 4);
				SetVar(TEXT("r.TSR.History.ScreenPercentage"), 100);
				break;
			case 2: SetVar(TEXT("r.AntiAliasingMethod"), 2); break;
			case 3: SetVar(TEXT("r.AntiAliasingMethod"), 1); break;
			case 4: SetVar(TEXT("r.AntiAliasingMethod"), 0); break;
			default: break;
			}
		}

		// Immer melden, auch ohne Schalter. Ein Vergleichslauf, bei dem die
		// Einstellung gar nicht angekommen ist, sieht sonst aus wie ein
		// Ergebnis.
		const auto ReadVar = [](const TCHAR* Name) -> int32
		{
			IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
			return Var ? Var->GetInt() : -1;
		};
		UE_LOG(LogWbStreaming, Log,
			TEXT("Kantenglaettung: r.AntiAliasingMethod = %d (0 keine, 1 FXAA, 2 TAA, 4 TSR), ")
			TEXT("r.TSR.History.ScreenPercentage = %d."),
			ReadVar(TEXT("r.AntiAliasingMethod")),
			ReadVar(TEXT("r.TSR.History.ScreenPercentage")));

		UE_LOG(LogWbStreaming, Log,
			TEXT("Schatten: Kaskaden %d, Aufloesung %d, Reichweitenfaktor %d Prozent."),
			ReadVar(TEXT("r.Shadow.CSM.MaxCascades")),
			ReadVar(TEXT("r.Shadow.MaxCSMResolution")),
			FMath::RoundToInt(
				(IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.DistanceScale"))
					? IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.DistanceScale"))->GetFloat()
					: -0.01f) * 100.0f));
	}

	if (FParse::Param(FCommandLine::Get(), TEXT("WbNoLumen")))
	{
		for (const TCHAR* Name : { TEXT("r.DynamicGlobalIlluminationMethod"),
			TEXT("r.ReflectionMethod") })
		{
			if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name))
			{
				Var->Set(0, ECVF_SetByCode);
			}
		}
		UE_LOG(LogWbStreaming, Log, TEXT("Lumen abgeschaltet (-WbNoLumen)."));
	}

	if (IConsoleVariable* GiVar = IConsoleManager::Get().FindConsoleVariable(
			TEXT("r.DynamicGlobalIlluminationMethod")))
		{
			UE_LOG(LogWbStreaming, Log,
				TEXT("Globale Beleuchtung: r.DynamicGlobalIlluminationMethod = %d ")
				TEXT("(0 = keine, 1 = Lumen)."),
				GiVar->GetInt());
		}
		else
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Globale Beleuchtung: Konsolenvariable nicht gefunden."));
		}
	}

	// Strassennetz ausschreiben: -WbDumpStreets[=<Pfad>].
	//
	// "Die Platter Strasse stadtauswaerts fehlt in Spielwelt und Minikarte"
	// laesst sich am Bild nicht pruefen - man sieht immer nur den Ausschnitt,
	// in dem man gerade steht, und ein Fehlen faellt nur dort auf, wo man
	// zufaellig hinschaut. Diese Datei erlaubt den Abgleich der GESAMTEN
	// Karte gegen die Quelldaten.
	//
	// Geschrieben wird je Segment die OSM-Way-Id. Ueber den Namen zu
	// vergleichen waere unzuverlaessig: "Platter Strasse" steht in Wiesbaden
	// an 60 Wegen, davon einige als Wirtschaftsweg und Fussweg, und ein
	// fehlender Fahrstreifen ginge zwischen den vorhandenen unter. Die Id ist
	// eindeutig.
	{
		FString DumpPath;
		const bool bWantDump = FParse::Value(FCommandLine::Get(), TEXT("WbDumpStreets="), DumpPath)
			|| FParse::Param(FCommandLine::Get(), TEXT("WbDumpStreets"));
		if (bWantDump && !bStreetsDumped)
		{
			bStreetsDumped = true;
			if (DumpPath.IsEmpty())
			{
				DumpPath = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("Strassen.csv");
			}
			DumpStreetNetwork(DumpPath);
		}
	}

	// Gleichmaessig durch die Stadt fahren: -WbAutoDrive=<km/h>.
	//
	// Alle bisherigen Bildzeitmessungen liefen mit STEHENDEM Fahrzeug und
	// haben damit die Haelfte des Problems nicht gesehen. Ein Lauf, bei dem
	// der Wagen von selbst die Platter Strasse hinunterrollte, zeigte
	// Einbrueche auf 230 bis 400 ms und im Protokoll 825 Zeilen
	// "Input trimesh contains N bad triangles" - jede davon ein
	// Kollisions-Kochvorgang fuer eine nachgeladene Zelle.
	//
	// Ein Mensch am Steuer faehrt jedes Mal anders; zum Vergleichen von
	// Aenderungen braucht es eine wiederholbare Bewegung. Der Pawn wird
	// deshalb gleichmaessig versetzt - World Partition streamt um ihn herum,
	// also entsteht dieselbe Nachladelast wie beim Fahren.
	{
		float DriveKmh = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbAutoDrive="), DriveKmh)
			&& DriveKmh > 0.0f)
		{
			AutoDriveElapsed += DeltaTime;

			// Erst nach der Ladephase losfahren. Die Karte braucht ueber drei
			// Minuten; wer davor losfaehrt, misst wieder das Laden.
			float StartAfter = 240.0f;
			FParse::Value(FCommandLine::Get(), TEXT("WbAutoDriveStart="), StartAfter);

			if (AutoDriveElapsed > StartAfter)
			{
				if (UWorld* DriveWorld = GetWorld())
				{
					if (APlayerController* PC = DriveWorld->GetFirstPlayerController())
					{
						if (APawn* Pawn = PC->GetPawn())
						{
							if (!bAutoDriveStarted)
							{
								bAutoDriveStarted = true;
								AutoDriveOrigin = Pawn->GetActorLocation();
								UE_LOG(LogWbStreaming, Log,
									TEXT("Fahrt beginnt bei (%.0f, %.0f) mit %.0f km/h."),
									AutoDriveOrigin.X, AutoDriveOrigin.Y, DriveKmh);
							}

							// Nach OSTEN, also entlang +X. Eine feste Richtung
							// statt einer Route: Es geht um die Nachladelast,
							// nicht um Fahrverhalten, und eine gerade Linie ist
							// zwischen zwei Laeufen exakt dieselbe.
							//
							// Bei einem Fahrzeug auf echter Physik wird NICHT
							// versetzt: Ein Teleport je Bild wuerde die
							// Radkraefte durcheinanderbringen und das Messen
							// des Fahrverhaltens unmoeglich machen. Dort zaehlt
							// nur die Strecke mit.
							const double StepCm = DriveKmh / 3.6 * 100.0 * DeltaTime;

							if (!Pawn->IsA(AWiesbadenChaosCar::StaticClass()))
							{
								FVector Next = Pawn->GetActorLocation();
								Next.X += StepCm;
								Pawn->SetActorLocation(Next, /*bSweep=*/false, nullptr,
									ETeleportType::TeleportPhysics);
							}

							AutoDriveDistanceCm += StepCm;
						}
					}
				}
			}
		}
	}

	// Bild aus der Spielphase: -WbShot=<Sekunden>.
	//
	// Zahlen sagen, WIEVIEL etwas kostet, aber nicht, WAS im Bild steht. Beim
	// Baum-Befund war genau das die offene Frage: 100 Kegel fuer 27 ms sind
	// nur erklaerbar, wenn sie einen grossen Teil der Flaeche mehrfach
	// ueberdecken - und ob das so ist, sieht man in einer Sekunde am Bild und
	// in keiner Messung.
	//
	// Der Rundgang (-WbTour) kann das nicht ersetzen: Der faehrt eigene Posen
	// an, waehrend hier die Frage ist, was die Kamera AN DER MESSSTELLE sieht.
	{
		float ShotAfterSeconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbShot="), ShotAfterSeconds)
			&& ShotAfterSeconds > 0.0f)
		{
			ShotElapsed += DeltaTime;
			if (ShotElapsed > ShotAfterSeconds)
			{
				const FString ShotPath = FPaths::ProjectSavedDir()
					/ TEXT("Diagnose") / TEXT("Messstelle");

				// MIT Oberflaeche aufnehmen.
				//
				// Ohne das fehlen die "stat"-Anzeigen der Engine im Bild - und
				// genau die sind der Grund, warum es diesen Schalter gibt: Ein
				// Lauf mit -ExecCmds="stat scenerendering" lieferte ein
				// tadelloses Bild der Stadt ohne eine einzige Zahl darauf.
				// Das eigene HUD (Tacho, Minikarte) wird auch ohne die Angabe
				// aufgenommen, die Engine-Anzeigen nicht.
				FScreenshotRequest::RequestScreenshot(ShotPath, true, true);
				UE_LOG(LogWbStreaming, Log,
					TEXT("Bild der Messstelle nach %.0f Sekunden: %s"),
					ShotElapsed, *ShotPath);
				ShotElapsed = -100000.0f;   // nur einmal ausloesen
			}
		}
	}

	// GPU-Profil einmalig anfordern: -WbProfileGPU=<Sekunden>.
	//
	// Notwendig, weil die Bildzeit inzwischen nachweislich an der Grafikkarte
	// haengt (Spiel-Strang 18 ms, Grafikkarte 79-82 ms bei 82 ms Bildzeit).
	// Welcher Zeichendurchgang das ist, sagt keine der bisherigen Zahlen -
	// "ProfileGPU" listet sie einzeln auf.
	//
	// Warum nicht ueber -ExecCmds: Der Befehl misst das Bild, in dem er
	// ausgefuehrt wird. Zum Startzeitpunkt laedt die Karte noch (ueber drei
	// Minuten), und ein Profil der Ladephase beantwortet die Frage nicht -
	// dieselbe Falle, in die schon die ersten Bildzeitmessungen gelaufen sind.
	{
		float ProfileAfterSeconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbProfileGPU="), ProfileAfterSeconds)
			&& ProfileAfterSeconds > 0.0f)
		{
			ProfileGpuElapsed += DeltaTime;

			// Zwei Schritte, zwei Sekunden auseinander.
			//
			// Der erste schaltet r.ShowMaterialDrawEvents ein. Ohne das steht
			// im Profil nur "ParallelDraw (Index: 0, Num: 2)" - und genau da
			// lagen 92 der 111 ms, ohne dass erkennbar war, WAS gezeichnet
			// wird. Mit dem Schalter traegt jeder Zeichenaufruf den Namen
			// seines Materials.
			//
			// Der Schalter wirkt erst im naechsten Bild des Render-Strangs.
			// Beides im selben Tick zu tun waere ein Rennen, das man nur
			// daran merkt, dass die Namen fehlen.
			if (!bProfileGpuArmed && ProfileGpuElapsed > ProfileAfterSeconds)
			{
				bProfileGpuArmed = true;
				if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(
						TEXT("r.ShowMaterialDrawEvents")))
				{
					Var->Set(1, ECVF_SetByCode);
					UE_LOG(LogWbStreaming, Log,
						TEXT("Materialnamen im GPU-Profil eingeschaltet."));
				}
				else
				{
					UE_LOG(LogWbStreaming, Warning,
						TEXT("r.ShowMaterialDrawEvents nicht gefunden - Profil bleibt namenlos."));
				}
			}

			if (bProfileGpuArmed && ProfileGpuElapsed > ProfileAfterSeconds + 2.0f)
			{
				UE_LOG(LogWbStreaming, Log,
					TEXT("GPU-Profil angefordert nach %.0f Sekunden (-WbProfileGPU)."),
					ProfileGpuElapsed);

				if (UWorld* ProfileWorld = GetWorld())
				{
					if (APlayerController* PC = ProfileWorld->GetFirstPlayerController())
					{
						PC->ConsoleCommand(TEXT("ProfileGPU"));
					}
				}
				ProfileGpuElapsed = -100000.0f;   // nur einmal ausloesen
			}
		}
	}

	// Selbstabbruch nach vorgegebener Zeit: -WbQuitAfter=<Sekunden>.
	//
	// Ohne ihn liessen sich Messlaeufe nur mit taskkill beenden - und ein hart
	// abgeschossener Prozess schreibt sein Log nicht mehr zu Ende. Mehrere
	// Vergleichsmessungen ergaben deshalb "keine Zahl", obwohl die Messung im
	// Spiel lief: Die Zeilen standen im Puffer und gingen verloren.
	{
		float QuitAfterSeconds = 0.0f;
		if (FParse::Value(FCommandLine::Get(), TEXT("WbQuitAfter="), QuitAfterSeconds)
			&& QuitAfterSeconds > 0.0f)
		{
			QuitAfterElapsed += DeltaTime;
			if (QuitAfterElapsed > QuitAfterSeconds)
			{
				UE_LOG(LogWbStreaming, Log,
					TEXT("Messlauf beendet nach %.0f Sekunden (-WbQuitAfter)."), QuitAfterElapsed);

				if (UWorld* QuitWorld = GetWorld())
				{
					if (APlayerController* PC = QuitWorld->GetFirstPlayerController())
					{
						PC->ConsoleCommand(TEXT("quit"));
					}
				}
				QuitAfterElapsed = -100000.0f;   // nur einmal ausloesen
			}
		}
	}

	// Bildzeit mitschreiben.
	//
	// Die Zaehler werden nach vier Sekunden EINMAL zurueckgesetzt: Bis dahin
	// laufen Streaming, Shader-Kompilierung und der Aufbau der Ausstattung.
	// Ohne diesen Schnitt misst man den Ladevorgang, nicht den Spielbetrieb -
	// die erste Messung meldete 7 Bilder/s und war damit wertlos.
	MeasurementDelay += DeltaTime;

	// Alle 15 Sekunden neu melden statt nur einmal.
	//
	// Die Einmal-Messung im Diagnoselauf ist nicht belastbar: Sie laeuft in
	// einem Fenster OHNE FOKUS, und Unreal drosselt solche Fenster. Der
	// schlechteste Wert lag in jedem Lauf bei exakt 400,0 ms - ein exakt
	// gleicher Extremwert ueber alle Laeufe ist keine Messung, sondern eine
	// Begrenzung. Belastbar ist die Zahl nur aus einer echten Spielsitzung,
	// und dafuer muss sie wiederholt erscheinen.
	if (bMeasurementStarted && FrameCount > 0 && MeasurementDelay > 15.0f)
	{
		const double AverageMs = FrameTimeSumMs / FrameCount;
		UE_LOG(LogWbStreaming, Log,
			TEXT("Bildzeit (%d Bilder): Mittel %.1f ms (%.0f Bilder/s), schlechtestes %.1f ms, ")
			TEXT("%d Ausreisser ueber dem Doppelten des Mittels, %d ueber 50 ms. ")
			TEXT("Simulationen: Ampeln %.1f, Verkehr %.1f, Fussgaenger %.1f ms."),
			FrameCount, AverageMs, 1000.0 / FMath::Max(AverageMs, 0.01), WorstFrameMs,
			SpikeCount, HitchCount,
			LightTimeMs / FrameCount, TrafficTimeMs / FrameCount, PedestrianTimeMs / FrameCount);

		// Aufteilung auf die Straenge - und auf die Grafikkarte.
		//
		// Ohne sie laesst sich nicht sagen, WO die Zeit hingeht. Das Ausblenden
		// von Gelaende, Strassen, Gebaeuden und Ausstattung aenderte die
		// Bildzeit auf die Nachkommastelle nicht (119,8 ms in allen Faellen) -
		// ein unbewegter Wert misst nicht das, was er zu messen scheint.
		//
		// Die GPU-Zeit kam spaeter dazu, und ihr Fehlen hat mich eine
		// Fehldiagnose gekostet: Aus "Renderer 8,8 ms bei einfarbigen
		// Materialien" gegen "142 ms bei normalen" habe ich geschlossen, die
		// Materialien seien die Last. Der Renderer-Zaehler misst aber die
		// Arbeit des RENDER-STRANGS, nicht die der Grafikkarte. Ist die Karte
		// der Engpass, schiebt der Strang seine Befehle billig weg und wartet -
		// die Wartezeit taucht in keinem der beiden Zaehler auf. Ein Beleg
		// dafuer stand in der Messung selbst: Bei einfarbigen Materialien
		// waren Spiel-Strang (7,6 ms) und Render-Strang (8,8 ms) beide klein,
		// die Bildzeit lag trotzdem bei 70-99 ms. Die Differenz kann dann nur
		// von der Karte kommen.
		UE_LOG(LogWbStreaming, Log,
			TEXT("Straenge: Spiel %.1f ms, Renderer %.1f ms, Grafikkarte %.1f ms."),
			FPlatformTime::ToMilliseconds(GGameThreadTime),
			FPlatformTime::ToMilliseconds(GRenderThreadTime),
			FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles()));

		// Hoehenlage von Fahrbahn und Gelaende.
		//
		// "Auf der Platter Strasse steht die Fahrbahn in der Luft" braucht
		// eine Zahl, sonst bleibt es Beobachtung. Die Messung gibt es laengst
		// (LogHeightStackNearPlayer vergleicht Fahrbahn-Vertices mit der
		// Gelaendehoehe an DERSELBEN Stelle) - sie hing nur im Block der
		// einmaligen Geometrie-Bilanz und lief deshalb genau ein Mal, acht
		// Sekunden nach dem Start, an der Startposition. Wo der Spieler
		// spaeter faehrt, wurde nie gemessen.
		LogHeightStackNearPlayer();

		// Verkehrsfluss: Steher, Tempo, Rotphasen.
		//
		// "Die Autos stauen sich" laesst sich ohne Zahl nicht pruefen. Ein
		// hoher Steher-Anteil heisst Stau, viele Spurwechsel heissen, dass
		// der Verkehr sich selbst wieder aufloest.
		//
		// Der Bericht stand bis hierher im Block der EINMALIGEN Geometrie-
		// Bilanz und erschien deshalb genau ein Mal, acht Sekunden nach dem
		// Start. Ein Stau, der eine Viertelstunde spaeter entsteht, kam darin
		// nie vor - und war damit nur als Beobachtung vorhanden, nicht als
		// Messwert. Hier laeuft er alle 15 Sekunden mit.
		if (TrafficSimulation.Vehicles.Num() > 0)
		{
			const FWiesbadenTrafficReport& Traffic = TrafficSimulation.Report;
			UE_LOG(LogWbTraffic, Log,
				TEXT("Verkehrsfluss: %d Fahrzeuge, %d stehen trotz Fahrwunsch (%.0f %%), ")
				TEXT("mittleres Tempo %.0f km/h, %d an Rot gehalten, %d Spurwechsel."),
				Traffic.ActiveVehicleCount, Traffic.StalledVehicleCount,
				Traffic.ActiveVehicleCount > 0
					? 100.0 * Traffic.StalledVehicleCount / Traffic.ActiveVehicleCount : 0.0,
				Traffic.MeanSpeedKmh, TrafficSimulation.GetVehiclesHeldAtRed(),
				Traffic.LaneChangesThisTick);
		}

		MeasurementDelay = 0.0f;
		FrameTimeSumMs = 0.0;
		WorstFrameMs = 0.0;
		FrameCount = 0;
		HitchCount = 0;
		SpikeCount = 0;
		LightTimeMs = 0.0;
		TrafficTimeMs = 0.0;
		PedestrianTimeMs = 0.0;
		SubsystemTimeMs = 0.0;
		GameThreadTimeMs = 0.0;
		RenderThreadTimeMs = 0.0;
		GpuTimeMs = 0.0;
	}

	if (!bMeasurementStarted && MeasurementDelay > 4.0f)
	{
		bMeasurementStarted = true;
		MeasurementDelay = 0.0f;
		FrameTimeSumMs = 0.0;
		WorstFrameMs = 0.0;
		FrameCount = 0;
		HitchCount = 0;
		SpikeCount = 0;
		LightTimeMs = 0.0;
		TrafficTimeMs = 0.0;
		PedestrianTimeMs = 0.0;
		SubsystemTimeMs = 0.0;
		GameThreadTimeMs = 0.0;
		RenderThreadTimeMs = 0.0;
		GpuTimeMs = 0.0;
	}

	if (FrameCount > 0 || DeltaTime < 0.5f)
	{
		const double FrameMs = DeltaTime * 1000.0;

		// Ausreisser RELATIV zaehlen, nicht gegen eine feste Schranke.
		//
		// Die 50-ms-Schranke unten ist wertlos geworden, sobald die mittlere
		// Bildzeit selbst in ihre Naehe kommt: Bei 46 ms Mittel meldete der
		// Zaehler 0 Aussetzer, bei 51 ms Mittel 168 - obwohl sich am
		// Ruckelverhalten nichts geaendert hatte. Beinahe jedes Bild lag knapp
		// darueber. Ich habe diesen Sprung erst fuer eine Verschlechterung
		// gehalten; er war eine Eigenschaft der Schranke.
		//
		// Als Ruckeln wahrgenommen wird ein Bild, das deutlich laenger dauert
		// als seine Nachbarn. Der Vergleich laeuft deshalb gegen das laufende
		// Mittel des aktuellen Fensters.
		if (FrameCount > 10)
		{
			const double RunningMean = FrameTimeSumMs / FrameCount;
			if (FrameMs > RunningMean * 2.0)
			{
				++SpikeCount;
			}
		}

		FrameTimeSumMs += FrameMs;
		WorstFrameMs = FMath::Max(WorstFrameMs, FrameMs);
		++FrameCount;
		if (FrameMs > 50.0)
		{
			++HitchCount;
		}
	}

	// Kreuzungs-Rundgang treiben (nur mit -WbTour aktiv).
	TickJunctionTour(DeltaTime);

	// Wetter-Zustandsmaschine (Tageszeit + Wetter-Uebergang) treiben.
	Weather.Tick(DeltaTime);

	// Bezugspunkt des Verkehrs auf den Spieler setzen: Fahrzeuge entstehen in
	// seinem Umkreis und werden hinter ihm wieder abgeraeumt. Ohne das
	// verteilt die Simulation sie ueber das gesamte 2.700-km-Netz und man
	// begegnet praktisch keinem.
	if (const UWorld* World = GetWorld())
	{
		if (const APlayerController* PC = World->GetFirstPlayerController())
		{
			FVector ViewLocation = FVector::ZeroVector;
			FRotator ViewRotation = FRotator::ZeroRotator;
			PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
			TrafficSimulation.SetObserverLocation(ViewLocation);
			PedestrianSimulation.SetObserverLocation(ViewLocation);

			// Das Spielerfahrzeug als Hindernis melden, damit der Verkehr
			// dafuer bremst. Bezug ist der Pawn, nicht der Blickpunkt: die
			// Kamera schwebt hinter dem Fahrzeug und laege dadurch bis zu
			// mehrere Meter neben der tatsaechlichen Fahrzeugposition.
			if (const APawn* Pawn = PC->GetPawn())
			{
				// Halbe Laenge eines Kaefers - genauer waere es, sie vom
				// Fahrzeug zu erfragen; der Unterschied liegt bei den hier
				// verwendeten Bremsabstaenden aber unter der Wahrnehmbarkeit.
				constexpr double PlayerHalfLengthCm = 207.0;
				TrafficSimulation.SetPlayerObstacle(Pawn->GetActorLocation(), PlayerHalfLengthCm);
			}
			else
			{
				TrafficSimulation.ClearPlayerObstacle();
			}
		}
	}

	// Verkehrs-Simulation: Fahrzeuge folgen dem Spur-Graph; die Dichte aus
	// dem City-Prompt steuert die Spawn-Rate (No-Op, wenn nicht initialisiert).
	// Ampeln VOR dem Verkehr fortschreiten: die Fahrzeuge fragen im selben
	// Tick den Schaltzustand ab.
	// Zeit je Simulation getrennt messen.
	//
	// Die Bildzeit liegt bei 128 ms und aendert sich NICHT, wenn man Strassen,
	// Gebaeude und Ausstattung ausblendet - die Last liegt also nicht beim
	// Zeichnen. Welche der drei Simulationen sie traegt, zeigt nur eine
	// getrennte Messung.
	{
		const double LightStart = FPlatformTime::Seconds();
		TrafficLightSystem.Tick(DeltaTime);
		const double TrafficStart = FPlatformTime::Seconds();
		TrafficSimulation.Tick(DeltaTime);
		const double PedestrianStart = FPlatformTime::Seconds();
		PedestrianSimulation.Tick(DeltaTime);
		const double End = FPlatformTime::Seconds();

		// Strangzeiten MITTELN, nicht abtasten.
		//
		// GGameThreadTime ist die Zeit des LETZTEN Bildes. Einmal am Ende
		// gelesen liefert sie eine Momentaufnahme: zwei Laeufe meldeten
		// 428,9 und 212,1 ms, innerhalb eines Laufes aber auf die
		// Nachkommastelle identische Werte fuer verschiedene Konfigurationen.
		// Das war kein Vergleich, sondern zweimal dasselbe Bild.
		GameThreadTimeMs += FPlatformTime::ToMilliseconds(GGameThreadTime);
		RenderThreadTimeMs += FPlatformTime::ToMilliseconds(GRenderThreadTime);
		GpuTimeMs += FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles());

		LightTimeMs += (TrafficStart - LightStart) * 1000.0;
		TrafficTimeMs += (PedestrianStart - TrafficStart) * 1000.0;
		PedestrianTimeMs += (End - PedestrianStart) * 1000.0;
	}

	// Sichtbare Fahrzeuge (ISM-Pool am CityActor) aus der Simulation speisen.
	if (CityActor)
	{
		CityActor->UpdateTrafficVehicles(TrafficSimulation.Vehicles);

		// Fussgaenger analog: die Simulation liefert Positionen, der ISM-Pool
		// am CityActor zeichnet sie.
		TArray<FPlacedPedestrian> Walkers;
		PedestrianSimulation.CollectPlaced(Walkers);
		CityActor->UpdatePedestrians(Walkers);
	}

	// Gebaeude-Kollision dem Spieler nachfuehren.
	if (BuildingCollision)
	{
		if (const UWorld* TickWorld = GetWorld())
		{
			if (const APlayerController* PC = TickWorld->GetFirstPlayerController())
			{
				FVector ViewLocation = FVector::ZeroVector;
				FRotator ViewRotation = FRotator::ZeroRotator;
				PC->GetPlayerViewPoint(ViewLocation, ViewRotation);
				BuildingCollision->UpdateAround(ViewLocation);
			}
		}
	}

	// Einmalige Verkehrsbilanz, kurz nach dem Anlaufen.
	//
	// Ohne sie ist nicht zu erkennen, ob der Verkehr tatsaechlich sichtbar
	// ist: die Simulation meldet Erfolg, sobald sie initialisiert wurde -
	// auch dann, wenn kein einziges Fahrzeug gezeichnet wird. Genau so blieb
	// unbemerkt, dass im gebackenen Level der Traeger fuer den Fahrzeug-Pool
	// fehlte. Der Wert wird verzoegert gelesen, weil die Simulation ihre
	// Fahrzeuge erst ueber die ersten Sekunden verteilt einsetzt.
	if (!bTrafficReported && TrafficSimulation.Vehicles.Num() > 0)
	{
		TrafficReportDelay += DeltaTime;
		if (TrafficReportDelay >= 5.0f)
		{
			bTrafficReported = true;

			const int32 Simulated = TrafficSimulation.Vehicles.Num();
			const int32 Visible = CityActor ? CityActor->GetVisibleTrafficVehicleCount() : 0;

			if (Visible > 0)
			{
				UE_LOG(LogWbTraffic, Log,
					TEXT("Verkehr laeuft: %d Fahrzeuge simuliert, %d davon im Sichtbereich gezeichnet."),
					Simulated, Visible);
			}
			else
			{
				UE_LOG(LogWbTraffic, Warning,
					TEXT("Verkehr simuliert %d Fahrzeuge, aber KEINES wird gezeichnet. ")
					TEXT("Moegliche Ursachen: kein Fahrzeug-Traeger (CityActor), ")
					TEXT("kein VehicleMesh zugewiesen, oder alle ausserhalb des Cull-Radius."),
					Simulated);
			}
		}
	}

	// Ampel-Bilanz: Zahl der Ampeln UND ob sie den Verkehr tatsaechlich
	// anhalten. Das System war lange vollstaendig implementiert und wurde
	// trotzdem nie wirksam, weil niemand SetTrafficLightSystem rief.
	if (!bTrafficLightsReported && TrafficLightSystem.GetTrafficLightCount() > 0)
	{
		TrafficLightReportDelay += DeltaTime;
		// 6 s, nicht 12: der Diagnose-Screenshot beendet den Lauf rund 11 s nach
		// dem Start (Geometrie-Bilanz bei 8 s + 3 s Nachlauf). Eine Bilanz, die
		// spaeter faellig ist, wird nie ausgegeben - genau so fehlte sie zweimal.
		if (TrafficLightReportDelay >= 6.0f)
		{
			bTrafficLightsReported = true;

			const int32 Held = TrafficSimulation.GetVehiclesHeldAtRed();

			// Entfernung zur naechsten Ampel: Ohne sie bleibt "kein Fahrzeug
			// gehalten" zweideutig - es koennte auch schlicht keine Ampel in
			// Reichweite des Verkehrs liegen (der entsteht nur um den Spieler).
			double NearestLightM = -1.0;
			if (const UWorld* LightWorld = GetWorld())
			{
				if (const APlayerController* PC = LightWorld->GetFirstPlayerController())
				{
					FVector ViewLoc = FVector::ZeroVector;
					FRotator ViewRot = FRotator::ZeroRotator;
					PC->GetPlayerViewPoint(ViewLoc, ViewRot);

					double BestSq = TNumericLimits<double>::Max();
					for (const FWiesbadenTrafficLight& Light : TrafficLightSystem.Lights)
					{
						const double Dx = Light.Location.X - ViewLoc.X;
						const double Dy = Light.Location.Y - ViewLoc.Y;
						BestSq = FMath::Min(BestSq, Dx * Dx + Dy * Dy);
					}
					if (BestSq < TNumericLimits<double>::Max())
					{
						NearestLightM = FMath::Sqrt(BestSq) / 100.0;
					}
				}
			}
			if (Held > 0)
			{
				UE_LOG(LogWbTraffic, Log,
					TEXT("Ampeln wirksam: %d im Netz, %d Fahrzeug(e) stehen an Rot, naechste Ampel %.0f m ")
					TEXT("(Zyklus %.0f s, Gruen %.0f s)."),
					TrafficLightSystem.GetTrafficLightCount(), Held, NearestLightM,
					TrafficLightSystem.Settings.CycleSeconds,
					TrafficLightSystem.Settings.GreenSecondsPerCycle);
			}
			else
			{
				UE_LOG(LogWbTraffic, Warning,
					TEXT("Ampeln: %d im Netz, KEIN Fahrzeug gehalten. Naechste Ampel %.0f m entfernt, ")
					TEXT("Verkehr entsteht im Umkreis von %.0f m - %s"),
					TrafficLightSystem.GetTrafficLightCount(), NearestLightM,
					TrafficSimulation.Settings.SpawnRadiusMeters,
					NearestLightM > TrafficSimulation.Settings.SpawnRadiusMeters
						? TEXT("also liegt gar keine Ampel im Verkehrsbereich (kein Fehler).")
						: TEXT("eine Ampel LIEGT in Reichweite - die Kopplung greift nicht."));
			}
		}
	}

	// Fussgaenger-Bilanz - dieselbe Logik wie beim Verkehr: die Simulation zu
	// starten heisst nicht, dass auch jemand gezeichnet wird.
	if (!bPedestriansReported && PedestrianSimulation.GetPedestrianCount() > 0)
	{
		PedestrianReportDelay += DeltaTime;
		if (PedestrianReportDelay >= 5.0f)
		{
			bPedestriansReported = true;

			const int32 Simulated = PedestrianSimulation.GetPedestrianCount();
			const int32 Visible = CityActor ? CityActor->GetVisiblePedestrianCount() : 0;

			if (Visible > 0)
			{
				UE_LOG(LogWbCore, Log,
					TEXT("Fussgaenger laufen: %d simuliert, %d gezeichnet (%.1f km Gehweg)."),
					Simulated, Visible, PedestrianSimulation.GetReport().TotalSidewalkKm);
			}
			else
			{
				UE_LOG(LogWbCore, Warning,
					TEXT("Fussgaenger: %d simuliert, aber KEINER wird gezeichnet. ")
					TEXT("Moegliche Ursachen: kein CityActor als Traeger oder kein Mesh am Spawner."),
					Simulated);
			}
		}
	}

	// Einmalige Geometrie-Bilanz der gebackenen Stadt.
	//
	// Beim ersten Blick in den Editor war die Stadt unsichtbar - Strassen,
	// Fassaden und Gelaende fehlten. Ob die Geometrie fehlt, ungeladen ist
	// oder nur nicht gerendert wird, liess sich nicht sagen, weil es keine
	// Kennzahl dafuer gab. Diese Bilanz schliesst die Luecke: sie zaehlt die
	// geladenen Chunk-Actors UND ihre Mesh-Abschnitte. Null Actors heisst
	// "nicht gestreamt", Actors ohne Abschnitte heissen "leer gespeichert" -
	// zwei voellig verschiedene Ursachen, die sich sonst gleich anfuehlen.
	if (!bGeometryReported)
	{
		GeometryReportDelay += DeltaTime;
		if (GeometryReportDelay >= 8.0f)
		{
			bGeometryReported = true;
			if (FrameCount > 10)
			{
				const double AverageMs = FrameTimeSumMs / FrameCount;
				UE_LOG(LogWbStreaming, Log,
					TEXT("Bildzeit ueber %d Bilder: Mittel %.1f ms (%.0f Bilder/s), ")
					TEXT("schlechtestes %.1f ms, %d Aussetzer ueber 50 ms."),
					FrameCount, AverageMs, 1000.0 / FMath::Max(AverageMs, 0.01),
					WorstFrameMs, HitchCount);

				UE_LOG(LogWbStreaming, Log,
					TEXT("Davon je Bild: Ampeln %.1f ms, Verkehr %.1f ms, Fussgaenger %.1f ms ")
					TEXT("(zusammen %.1f ms von %.1f ms)."),
					LightTimeMs / FrameCount, TrafficTimeMs / FrameCount,
					PedestrianTimeMs / FrameCount,
					(LightTimeMs + TrafficTimeMs + PedestrianTimeMs) / FrameCount,
					AverageMs);

				UE_LOG(LogWbStreaming, Log,
					TEXT("Straenge im Mittel: Spiel %.1f ms, Renderer %.1f ms, ")
					TEXT("Grafikkarte %.1f ms. Davon dieses Subsystem %.1f ms."),
					GameThreadTimeMs / FrameCount,
					RenderThreadTimeMs / FrameCount,
					GpuTimeMs / FrameCount,
					SubsystemTimeMs / FrameCount);
			}

			LogGeometryBalance();
			LogMaterialBalance();
			LogHeightStackNearPlayer();
			RequestDiagnosticScreenshot();
		}
	}

	// Luftaufnahme: nach dem Kamerawechsel ein paar Frames abwarten.
	if (AerialShotDelay > 0.0f)
	{
		AerialShotDelay -= DeltaTime;
		if (AerialShotDelay <= 0.0f)
		{
			AerialShotDelay = -1.0f;
			CaptureDiagnosticScreenshot();
		}
	}

	// Nach dem Diagnose-Screenshot beenden (nur mit -WbScreenshot aktiv).
	if (ScreenshotQuitDelay > 0.0f)
	{
		ScreenshotQuitDelay -= DeltaTime;
		if (ScreenshotQuitDelay <= 0.0f)
		{
			ScreenshotQuitDelay = -1.0f;
			FPlatformMisc::RequestExit(false);
		}
	}

	UpdateStreamingState();
}

void UWiesbadenCitySubsystem::LogGeometryBalance() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 ChunkCount = 0;
	int32 RoadSections = 0;
	int32 BuildingSections = 0;

	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		++ChunkCount;

		if (const UProceduralMeshComponent* Road = It->GetRoadMesh())
		{
			RoadSections += Road->GetNumSections();
		}
		if (const UProceduralMeshComponent* Building = It->GetBuildingMesh())
		{
			BuildingSections += Building->GetNumSections();
		}
	}

	if (ChunkCount == 0)
	{
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Stadt-Geometrie: KEIN Chunk-Actor geladen. Die Geometrie liegt in der Map, wird aber ")
			TEXT("nicht gestreamt - im Editor muessen World-Partition-Zellen erst geladen werden ")
			TEXT("(Fenster 'World Partition' -> Region laden); im Spiel folgt das Streaming dem Spieler."));
		return;
	}

	if (RoadSections == 0 && BuildingSections == 0)
	{
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Stadt-Geometrie: %d Chunk-Actors geladen, aber OHNE Mesh-Abschnitte. ")
			TEXT("Die Actors wurden leer gespeichert - der Stadt-Build muss wiederholt werden."),
			ChunkCount);
		return;
	}

	UE_LOG(LogWbStreaming, Log,
		TEXT("Stadt-Geometrie: %d Chunk-Actors geladen, %d Strassen- und %d Gebaeude-Abschnitte."),
		ChunkCount, RoadSections, BuildingSections);

	// Streaming-Diagnose: Wie weit sind die GELADENEN Chunks vom Spieler entfernt?
	// Sind viele weit jenseits des Streaming-Radius geladen, streamt WP nicht
	// distanzabhaengig aus -> die Last liegt an der WP-Einrichtung (Zellgroesse/
	// Zuordnung), nicht an der Sichtweite. Lage+Bounds des ersten Chunks testen die
	// "am Ursprung gespawnt"-Hypothese: Ursprung (0,0,0) mit Bounds weit weg = ok
	// (WP kann per Bounds zuordnen); Ursprung UND riesige Ausdehnung = kaputte
	// Zuordnung (dann helfen feinere Zellen nichts).
	FVector ViewLoc = FVector::ZeroVector;
	if (const APlayerController* PC = World->GetFirstPlayerController())
	{
		if (const APawn* Pawn = PC->GetPawn()) { ViewLoc = Pawn->GetActorLocation(); }
	}
	int32 Beyond2km = 0, Beyond4km = 0;
	double NearestM = TNumericLimits<double>::Max(), FarthestM = 0.0;
	bool bLoggedSample = false;
	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		const FBox Bounds = It->GetComponentsBoundingBox(true);
		const double DistM = FVector::Dist(ViewLoc, Bounds.GetCenter()) * 0.01;
		NearestM = FMath::Min(NearestM, DistM);
		FarthestM = FMath::Max(FarthestM, DistM);
		if (DistM > 2000.0) { ++Beyond2km; }
		if (DistM > 4000.0) { ++Beyond4km; }
		if (!bLoggedSample && Bounds.GetSize().X * 0.01 > 1000.0)
		{
			bLoggedSample = true;
			const FVector L = It->GetActorLocation();
			UE_LOG(LogWbStreaming, Log,
				TEXT("Streaming-Diagnose: Beispiel-Chunk %s Lage (%.0f, %.0f, %.0f), Gesamt-Bounds-Mitte (%.0f, %.0f), Ausdehnung %.0f x %.0f m - Komponenten einzeln:"),
				*It->GetName(), L.X, L.Y, L.Z, Bounds.GetCenter().X, Bounds.GetCenter().Y,
				Bounds.GetSize().X * 0.01, Bounds.GetSize().Y * 0.01);
			TArray<UPrimitiveComponent*> ChunkPrims;
			It->GetComponents(ChunkPrims);
			for (const UPrimitiveComponent* P : ChunkPrims)
			{
				if (!P || !P->IsRegistered())
				{
					continue;
				}
				const FBoxSphereBounds B = P->Bounds;
				int32 InstCount = -1;
				if (const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(P))
				{
					InstCount = Ism->GetInstanceCount();
				}
				UE_LOG(LogWbStreaming, Log,
					TEXT("   Komponente '%s' (%s): Bounds-Mitte (%.0f, %.0f, %.0f), Ausdehnung %.0f x %.0f m%s."),
					*P->GetName(), *P->GetClass()->GetName(),
					B.Origin.X, B.Origin.Y, B.Origin.Z,
					B.BoxExtent.X * 2.0 * 0.01, B.BoxExtent.Y * 2.0 * 0.01,
					InstCount >= 0 ? *FString::Printf(TEXT(", %d Instanzen"), InstCount) : TEXT(""));
			}
		}
	}
	UE_LOG(LogWbStreaming, Log,
		TEXT("Streaming-Diagnose: Spieler bei (%.0f, %.0f); geladene Chunks Distanz %.0f..%.0f m, davon %d jenseits 2 km, %d jenseits 4 km."),
		ViewLoc.X, ViewLoc.Y, NearestM, FarthestM, Beyond2km, Beyond4km);

	// Last-Inventar des Spiel-Strangs.
	//
	// Die Bildzeit-Diagnose zeigt: die Last liegt auf dem Spiel-Strang (~110 ms),
	// NICHT auf GPU (~16 ms) - Nanite/LODs braeuchten hier gar nichts. Der Spiel-
	// Strang bezahlt pro Bild fuer die VERWALTETE (nicht die sichtbare) Menge:
	// Primitive-Komponenten (Sichtbarkeit/Bounds), Foliage-Instanzen (HISM-Cluster-
	// Cull je Bild) und Kollisionskoerper (Physik-Szene). Diese Zaehlung macht die
	// Aufteilung sichtbar, damit die naechste Optimierung das Richtige trifft
	// (z. B. World-Partition-Ladebereich verkleinern statt an der Grafik zu drehen).
	int32 PrimComps = 0, MovablePrims = 0, CollisionPrims = 0;
	int32 FoliageComps = 0, FoliageInstances = 0;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		TArray<UPrimitiveComponent*> Prims;
		It->GetComponents(Prims);
		for (const UPrimitiveComponent* P : Prims)
		{
			if (!P || !P->IsRegistered())
			{
				continue;
			}
			++PrimComps;
			if (P->Mobility == EComponentMobility::Movable) { ++MovablePrims; }
			if (P->IsCollisionEnabled()) { ++CollisionPrims; }
			if (const UInstancedStaticMeshComponent* Ism = Cast<UInstancedStaticMeshComponent>(P))
			{
				++FoliageComps;
				FoliageInstances += Ism->GetInstanceCount();
			}
		}
	}

	UE_LOG(LogWbStreaming, Log,
		TEXT("Last-Inventar (Spiel-Strang): %d Primitive-Komponenten (%d beweglich, %d mit Kollision), ")
		TEXT("%d Instanz-Komponenten mit %d Instanzen gesamt. Kosten haengen an DIESEN Zahlen, nicht an der Sichtweite."),
		PrimComps, MovablePrims, CollisionPrims, FoliageComps, FoliageInstances);
}

void UWiesbadenCitySubsystem::LogMaterialBalance() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	int32 Total = 0;
	int32 WithoutMaterial = 0;

	auto CountSections = [&Total, &WithoutMaterial](const UProceduralMeshComponent* Mesh)
	{
		if (!Mesh)
		{
			return;
		}

		const int32 Sections = Mesh->GetNumSections();
		for (int32 Index = 0; Index < Sections; ++Index)
		{
			++Total;
			if (Mesh->GetMaterial(Index) == nullptr)
			{
				++WithoutMaterial;
			}
		}
	};

	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		CountSections(It->GetRoadMesh());
		CountSections(It->GetBuildingMesh());
	}

	if (Total == 0)
	{
		return;
	}

	if (WithoutMaterial > 0)
	{
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Material-Bilanz: %d von %d Mesh-Abschnitten OHNE Material - diese rendern mit dem ")
			TEXT("Default-Schachbrett. Materialien werden ausschliesslich beim Stadt-Build zugewiesen; ")
			TEXT("die Chunks speichern nur ihre Mesh-Komponenten, ein Nachziehen zur Laufzeit ist ")
			TEXT("daher nicht moeglich - der Build muss wiederholt werden."),
			WithoutMaterial, Total);
		return;
	}

	UE_LOG(LogWbStreaming, Log,
		TEXT("Material-Bilanz: alle %d Mesh-Abschnitte haben ein Material."), Total);
}


void UWiesbadenCitySubsystem::LogHeightStackNearPlayer() const
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// WICHTIG: Alle Chunk-Actors stehen im Weltursprung; ihre Vertices tragen
	// absolute Weltkoordinaten. GetActorLocation() ist als Entfernungsmass
	// daher unbrauchbar (es misst schlicht den Abstand zum Ursprung) - es
	// zaehlen ausschliesslich die Mesh-Bounds der einzelnen Abschnitte.
	struct FNearest
	{
		double DistanceCm = TNumericLimits<double>::Max();
		FBox Box = FBox(ForceInit);
		int32 Vertices = 0;
		int32 Overlapping = 0;   // Abschnitte, die den Spielerpunkt ueberdecken
		int32 TotalSections = 0;
		int32 EmptySections = 0;
	};

	auto Survey = [&ViewLocation](UProceduralMeshComponent* Mesh, const FTransform& ToWorld, FNearest& Out)
	{
		if (!Mesh)
		{
			return;
		}

		const int32 Count = Mesh->GetNumSections();
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FProcMeshSection* Section = Mesh->GetProcMeshSection(Index);
			if (!Section)
			{
				continue;
			}

			++Out.TotalSections;
			if (Section->ProcVertexBuffer.Num() == 0)
			{
				++Out.EmptySections;
				continue;
			}

			const FBox Box = Section->SectionLocalBox.TransformBy(ToWorld);

			// Horizontaler Abstand des Spielerpunkts zur Box (0 = darueber).
			const double Dx = FMath::Max(0.0, FMath::Max(Box.Min.X - ViewLocation.X, ViewLocation.X - Box.Max.X));
			const double Dy = FMath::Max(0.0, FMath::Max(Box.Min.Y - ViewLocation.Y, ViewLocation.Y - Box.Max.Y));
			const double Distance = FMath::Sqrt(Dx * Dx + Dy * Dy);

			if (Distance <= 0.0)
			{
				++Out.Overlapping;
			}

			if (Distance < Out.DistanceCm)
			{
				Out.DistanceCm = Distance;
				Out.Box = Box;
				Out.Vertices = Section->ProcVertexBuffer.Num();
			}
		}
	};

	FNearest Road;
	FNearest Building;

	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		const FTransform ToWorld = It->GetActorTransform();
		Survey(It->GetRoadMesh(), ToWorld, Road);
		Survey(It->GetBuildingMesh(), ToWorld, Building);
	}

	auto Report = [](const FNearest& N, const TCHAR* Label)
	{
		if (N.DistanceCm == TNumericLimits<double>::Max())
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Hoehen-Stapel: %s - KEIN Abschnitt mit Geometrie gefunden (%d Abschnitte, davon %d leer)."),
				Label, N.TotalSections, N.EmptySections);
			return;
		}

		UE_LOG(LogWbStreaming, Log,
			TEXT("Hoehen-Stapel: %s - naechster Abschnitt %.1f m entfernt, %d Vertices, Z von %.0f bis %.0f cm; ")
			TEXT("%d Abschnitt(e) ueberdecken den Spielerpunkt; gesamt %d Abschnitte, %d leer."),
			Label, N.DistanceCm / 100.0, N.Vertices, N.Box.Min.Z, N.Box.Max.Z,
			N.Overlapping, N.TotalSections, N.EmptySections);
	};

	UE_LOG(LogWbStreaming, Log, TEXT("Hoehen-Stapel am Spieler (%.0f, %.0f, %.0f):"),
		ViewLocation.X, ViewLocation.Y, ViewLocation.Z);

	Report(Road, TEXT("Strasse"));
	Report(Building, TEXT("Gebaeude"));

	// Feinmessung: Der Abschnitts-Bounds umfasst eine ganze 500-m-Zelle und ist
	// als Hoehenaussage wertlos. Entscheidend ist der Z-Wert der Fahrbahn GENAU
	// am Spielerpunkt - also der naechstgelegene Vertex.
	{
		double BestVertexDistCm = TNumericLimits<double>::Max();
		double BestVertexZ = 0.0;

		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			UProceduralMeshComponent* Mesh = It->GetRoadMesh();
			if (!Mesh)
			{
				continue;
			}

			const FTransform ToWorld = It->GetActorTransform();
			const int32 Count = Mesh->GetNumSections();

			for (int32 Index = 0; Index < Count; ++Index)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(Index);
				if (!Section || Section->ProcVertexBuffer.Num() == 0)
				{
					continue;
				}

				// Nur Abschnitte durchsuchen, die den Punkt ueberhaupt ueberdecken -
				// sonst waeren es Millionen Vertices.
				const FBox Box = Section->SectionLocalBox.TransformBy(ToWorld);
				if (ViewLocation.X < Box.Min.X || ViewLocation.X > Box.Max.X ||
					ViewLocation.Y < Box.Min.Y || ViewLocation.Y > Box.Max.Y)
				{
					continue;
				}

				for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
				{
					const FVector VertexWorld = ToWorld.TransformPosition(Vertex.Position);
					const double Dx = VertexWorld.X - ViewLocation.X;
					const double Dy = VertexWorld.Y - ViewLocation.Y;
					const double DistSq = Dx * Dx + Dy * Dy;
					if (DistSq < BestVertexDistCm)
					{
						BestVertexDistCm = DistSq;
						BestVertexZ = VertexWorld.Z;
					}
				}
			}
		}

		if (BestVertexDistCm < TNumericLimits<double>::Max())
		{
			UE_LOG(LogWbStreaming, Log,
				TEXT("Hoehen-Stapel: naechster Fahrbahn-Vertex %.2f m entfernt bei Z = %.0f cm."),
				FMath::Sqrt(BestVertexDistCm) / 100.0, BestVertexZ);
		}
	}


	// Verdeckungs-Statistik: Fuer eine Stichprobe von Fahrbahn-Vertices die
	// Gelaendehoehe GENAU an deren XY messen.
	//
	// Der Einzelwert oben war irrefuehrend: er verglich einen Fahrbahn-Vertex
	// mit der Gelaendehoehe 2 m daneben. Bei 7,81 m Landscape-Aufloesung
	// aendert sich das Gelaende auf dieser Strecke leicht um mehr als die
	// 8 cm Fahrbahnversatz - die Fahrbahn verschwindet dann darunter, waehrend
	// hohe Gebaeude weiter durchstossen.
	{
		int32 Sampled = 0;
		int32 Buried = 0;
		int32 NormalsDown = 0;
		double SumNormalZ = 0.0;
		double SumBuryCm = 0.0;
		double MaxBuryCm = 0.0;
		double SumClearCm = 0.0;
		FVector WorstLocation = FVector::ZeroVector;
		double WorstTerrainZ = 0.0;

		// Was der Abwaertstrace an einem Fahrbahn-Vertex TATSAECHLICH trifft.
		// Seit die Fahrbahnen eigene Kollision haben, ist der oberste Treffer
		// meist die Strasse selbst - ein Trace misst hier also Strasse gegen
		// Strasse und nicht Strasse gegen Gelaende.
		int32 TraceHitRoad = 0;
		int32 TraceHitLandscape = 0;
		int32 TraceHitOther = 0;
		int32 TraceHitNothing = 0;

		// Alle Freiraeume sammeln, um die Verteilung auswerten zu koennen.
		TArray<double> ClearanceSamples;

		// Landscape-Proxies einmal einsammeln: die Gelaendehoehe kommt aus den
		// Heightfield-Daten, nicht aus einem Trace.
		TArray<ALandscapeProxy*> LandscapeProxies;
		for (TActorIterator<ALandscapeProxy> LsIt(World); LsIt; ++LsIt)
		{
			LandscapeProxies.Add(*LsIt);
		}

		// Weit gefasste Stichprobe.
		//
		// 200 m um den Spieler lagen in ebenem Gelaende und meldeten 20 cm
		// mittlere Bodenfreiheit. Im Spiel steht die Fahrbahn an Haengen aber
		// als Damm ueber dem Boden, mit sichtbar freier Kante - das ist an
		// einer ebenen Stichprobe nicht zu sehen. Wiesbaden liegt am Hang;
		// die Stichprobe muss die Hanglagen erfassen.
		constexpr int32 MaxSamples = 2000;
		constexpr double SearchRadiusCm = 300000.0;   // 3 km um den Spieler

		for (TActorIterator<AWiesbadenCityChunk> It(World); It && Sampled < MaxSamples; ++It)
		{
			UProceduralMeshComponent* Mesh = It->GetRoadMesh();
			if (!Mesh)
			{
				continue;
			}

			const FTransform ToWorld = It->GetActorTransform();
			const int32 Count = Mesh->GetNumSections();

			for (int32 Index = 0; Index < Count && Sampled < MaxSamples; ++Index)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(Index);
				if (!Section || Section->ProcVertexBuffer.Num() == 0)
				{
					continue;
				}

				const FBox Box = Section->SectionLocalBox.TransformBy(ToWorld);
				if (ViewLocation.X < Box.Min.X - SearchRadiusCm || ViewLocation.X > Box.Max.X + SearchRadiusCm ||
					ViewLocation.Y < Box.Min.Y - SearchRadiusCm || ViewLocation.Y > Box.Max.Y + SearchRadiusCm)
				{
					continue;
				}

				// NUR echte Fahrbahn abtasten.
				//
				// Ohne diesen Filter lief die Statistik ueber alle Abschnitte
				// eines Chunks - einschliesslich der BOESCHUNGEN, die
				// bauartbedingt vom Fahrbahnrand bis auf Gelaendehoehe
				// hinunterlaufen. Ihre unteren Vertices liegen sollgemaess am
				// Boden und zaehlten ausnahmslos als "verdeckt". Gemeldet
				// wurden dadurch 8 bis 14 Prozent verdeckte Fahrbahn, die es in
				// dieser Form nie gab - und mehrere Aenderungen an der
				// Einebnung wurden gegen diese Zahl bewertet.
				//
				// Gehwege und Bordsteine liegen 12 cm hoeher als der Asphalt
				// und wuerden die Zahl ebenfalls verfaelschen.
				const int32 SectionChannel = It->GetRoadSectionChannel(Index);
				if (SectionChannel != static_cast<int32>(ERoadMeshChannel::Carriageway)
					&& SectionChannel != static_cast<int32>(ERoadMeshChannel::Intersection))
				{
					continue;
				}

				// Gleichmaessig ausduennen, damit die Stichprobe nicht aus einem
				// einzigen Strassenzug stammt.
				const int32 Stride = FMath::Max(1, Section->ProcVertexBuffer.Num() / 40);

				for (int32 V = 0; V < Section->ProcVertexBuffer.Num() && Sampled < MaxSamples; V += Stride)
				{
					const FVector VertexWorld = ToWorld.TransformPosition(Section->ProcVertexBuffer[V].Position);
					if (FVector2D::Distance(FVector2D(VertexWorld), FVector2D(ViewLocation)) > SearchRadiusCm)
					{
						continue;
					}

					// Gelaendehoehe aus den Landscape-Daten an exakt dieser XY.
					// KEIN Trace: der wuerde die Fahrbahn selbst treffen.
					double LandscapeZ = 0.0;
					bool bHasLandscapeZ = false;
					for (ALandscapeProxy* Proxy : LandscapeProxies)
					{
						const TOptional<float> Height =
							Proxy->GetHeightAtLocation(VertexWorld, EHeightfieldSource::Complex);
						if (Height.IsSet())
						{
							LandscapeZ = *Height;
							bHasLandscapeZ = true;
							break;
						}
					}

					if (!bHasLandscapeZ)
					{
						continue;
					}

					// Zusatzbefund: Was liefert der Trace an derselben Stelle?
					// Weicht er vom Landscape-Wert ab, misst er etwas anderes.
					{
						FHitResult VertexHit;
						FCollisionQueryParams VertexParams(SCENE_QUERY_STAT(WbBury), true);
						const FVector From = FVector(VertexWorld.X, VertexWorld.Y, VertexWorld.Z + 20000.0);
						const FVector To = FVector(VertexWorld.X, VertexWorld.Y, VertexWorld.Z - 20000.0);

						if (World->LineTraceSingleByChannel(VertexHit, From, To, ECC_WorldStatic, VertexParams))
						{
							const AActor* HitActor = VertexHit.GetActor();
							if (HitActor && HitActor->IsA<ALandscapeProxy>())
							{
								++TraceHitLandscape;
							}
							else if (HitActor && HitActor->IsA<AWiesbadenCityChunk>())
							{
								++TraceHitRoad;
							}
							else
							{
								++TraceHitOther;
							}
						}
						else
						{
							++TraceHitNothing;
						}
					}

					++Sampled;

					// Normalenrichtung: Eine Fahrbahn muss nach OBEN zeigen. Zeigt
					// sie nach unten, schneidet Backface-Culling die Flaeche aus der
					// Ansicht - Geometrie, Hoehe und Material waeren dann alle in
					// Ordnung und die Strasse trotzdem unsichtbar.
					const double NormalZ = Section->ProcVertexBuffer[V].Normal.Z;
					SumNormalZ += NormalZ;
					if (NormalZ < 0.0)
					{
						++NormalsDown;
					}
					const double TerrainZ = LandscapeZ;
					const double Delta = VertexWorld.Z - TerrainZ;   // >0 = Fahrbahn oben

					if (Delta < 0.0)
					{
						++Buried;
						SumBuryCm += -Delta;
						if (-Delta > MaxBuryCm)
						{
							MaxBuryCm = -Delta;
							WorstLocation = VertexWorld;
							WorstTerrainZ = TerrainZ;
						}
					}
					else
					{
						SumClearCm += Delta;
						ClearanceSamples.Add(Delta);
					}
				}
			}
		}

		if (Sampled > 0)
		{
			const int32 Clear = Sampled - Buried;

			// Verteilung statt nur Mittelwert: Ein Mittelwert von 20 cm kann
			// aus lauter 20 cm bestehen - oder aus 95 Prozent null und ein
			// paar Metern. Im Bild sieht man den Unterschied sofort, in der
			// Kennzahl nicht.
			ClearanceSamples.Sort();
			if (ClearanceSamples.Num() > 0)
			{
				const int32 N = ClearanceSamples.Num();
				UE_LOG(LogWbStreaming, Log,
					TEXT("Bodenfreiheit-Verteilung (%d Werte): Median %.0f cm, ")
					TEXT("90%% unter %.0f cm, 99%% unter %.0f cm, groesster Wert %.0f cm."),
					N,
					ClearanceSamples[N / 2],
					ClearanceSamples[FMath::Min(N - 1, (N * 90) / 100)],
					ClearanceSamples[FMath::Min(N - 1, (N * 99) / 100)],
					ClearanceSamples.Last());
			}
			UE_LOG(LogWbStreaming, Log,
				TEXT("Verdeckungs-Statistik: %d Fahrbahn-Vertices geprueft - %d unter dem Gelaende (%.0f %%), ")
				TEXT("mittlere Verdeckung %.1f cm, maximal %.1f cm; frei liegende im Mittel %.1f cm ueber Grund."),
				Sampled, Buried, 100.0 * Buried / Sampled,
				Buried > 0 ? SumBuryCm / Buried : 0.0, MaxBuryCm,
				Clear > 0 ? SumClearCm / Clear : 0.0);

			// Gegenprobe zur Messmethode selbst: Ein Abwaertstrace an einem
			// Fahrbahn-Vertex trifft die Fahrbahn, nicht das Gelaende. Solange
			// hier die Strasse dominiert, waere jede trace-basierte
			// Verdeckungszahl eine Messung von Strasse gegen Strasse.
			UE_LOG(LogWbStreaming, Log,
				TEXT("Trace-Gegenprobe an denselben Vertices: %d x Fahrbahn, %d x Gelaende, ")
				TEXT("%d x anderes, %d x kein Treffer."),
				TraceHitRoad, TraceHitLandscape, TraceHitOther, TraceHitNothing);

			// Wo steht der Helikopter?
			//
			// Das Log meldete jeden Lauf "Helikopter abgesetzt", im Spiel war er
			// nie zu sehen. Eine Spawn-Meldung beweist eben nur das Absetzen,
			// nicht den Verbleib. Diese Zeile misst seine Hoehe UEBER GRUND -
			// ein stark negativer Wert heisst: er ist durch die Welt gefallen.
			for (TActorIterator<AWiesbadenHelicopter> HeliIt(World); HeliIt; ++HeliIt)
			{
				const FVector HeliLocation = HeliIt->GetActorLocation();

				double HeliGroundZ = 0.0;
				bool bHasHeliGround = false;
				for (ALandscapeProxy* Proxy : LandscapeProxies)
				{
					const TOptional<float> Height =
						Proxy->GetHeightAtLocation(HeliLocation, EHeightfieldSource::Complex);
					if (Height.IsSet())
					{
						HeliGroundZ = *Height;
						bHasHeliGround = true;
						break;
					}
				}

				const double PlayerDistanceM =
					FVector::Dist(HeliLocation, ViewLocation) / 100.0;

				if (bHasHeliGround)
				{
					UE_LOG(LogWbStreaming, Log,
						TEXT("Helikopter bei (%.0f, %.0f, %.0f): %.1f m ueber Grund, ")
						TEXT("%.0f m vom Spieler entfernt."),
						HeliLocation.X, HeliLocation.Y, HeliLocation.Z,
						(HeliLocation.Z - HeliGroundZ) / 100.0, PlayerDistanceM);
				}
				else
				{
					UE_LOG(LogWbStreaming, Warning,
						TEXT("Helikopter bei (%.0f, %.0f, %.0f) liegt ausserhalb des Gelaendes, ")
						TEXT("%.0f m vom Spieler entfernt."),
						HeliLocation.X, HeliLocation.Y, HeliLocation.Z, PlayerDistanceM);
				}
			}

			if (Buried > 0)
			{
				// Ort der schlimmsten Stelle ausgeben - ohne Position laesst sich
				// nicht pruefen, WAS dort eigentlich liegt.
				UE_LOG(LogWbStreaming, Log,
					TEXT("Schlimmste Verdeckung bei (%.0f, %.0f): Fahrbahn Z = %.1f, Gelaende Z = %.1f."),
					WorstLocation.X, WorstLocation.Y, WorstLocation.Z, WorstTerrainZ);
			}

			UE_LOG(LogWbStreaming, Log,
				TEXT("Normalen-Pruefung: %d von %d Fahrbahn-Vertices zeigen nach UNTEN, mittleres Normal.Z = %.3f ")
				TEXT("(erwartet nahe +1; negativ bedeutet, dass Backface-Culling die Fahrbahn ausblendet)."),
				NormalsDown, Sampled, Sampled > 0 ? SumNormalZ / Sampled : 0.0);
		}
		else
		{
			UE_LOG(LogWbStreaming, Warning,
				TEXT("Verdeckungs-Statistik: keine Fahrbahn-Vertices in Reichweite gefunden."));
		}
	}

	// Komponenten-Bounds gegen die tatsaechliche Geometrie pruefen.
	//
	// Nach den Bounds entscheidet Unreal das Frustum-Culling. Decken sie die
	// Abschnitte nicht ab, wird die Komponente weggeschnitten - Geometrie,
	// Material und Sichtbarkeitsflags waeren dabei alle in Ordnung, genau wie
	// gemessen. Das erklaert auch, warum einzelne Fahrbahn-Flecken sichtbar
	// sind und der Rest nicht.
	{
		auto Describe = [](UProceduralMeshComponent* Mesh, const TCHAR* Label)
		{
			if (!Mesh)
			{
				return;
			}

			const FTransform ToWorld = Mesh->GetComponentTransform();
			FBox Union(ForceInit);
			int32 Sections = 0;

			const int32 Count = Mesh->GetNumSections();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				if (FProcMeshSection* Section = Mesh->GetProcMeshSection(Index))
				{
					if (Section->ProcVertexBuffer.Num() > 0)
					{
						Union += Section->SectionLocalBox.TransformBy(ToWorld);
						++Sections;
					}
				}
			}

			if (Sections == 0)
			{
				return;
			}

			const FBoxSphereBounds B = Mesh->Bounds;
			const FBox BoundsBox(B.Origin - B.BoxExtent, B.Origin + B.BoxExtent);

			// Deckt der Bounds die Abschnitte ab? Kleine Toleranz gegen
			// Rundung.
			const bool bCovers = BoundsBox.ExpandBy(10.0).IsInsideOrOn(Union.Min)
				&& BoundsBox.ExpandBy(10.0).IsInsideOrOn(Union.Max);

			UE_LOG(LogWbStreaming, Log,
				TEXT("Bounds-Pruefung %s: Komponente (%.0f,%.0f,%.0f) +/- (%.0f,%.0f,%.0f) | ")
				TEXT("Geometrie X %.0f..%.0f Y %.0f..%.0f Z %.0f..%.0f (%d Abschnitte) | Abgedeckt: %s"),
				Label,
				B.Origin.X, B.Origin.Y, B.Origin.Z,
				B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z,
				Union.Min.X, Union.Max.X, Union.Min.Y, Union.Max.Y, Union.Min.Z, Union.Max.Z,
				Sections, bCovers ? TEXT("ja") : TEXT("NEIN"));
		};

		// Den Chunk nehmen, dessen Strassen-Geometrie den Spieler ueberdeckt.
		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			UProceduralMeshComponent* ChunkRoad = It->GetRoadMesh();
			if (!ChunkRoad)
			{
				continue;
			}

			bool bOverlaps = false;
			const FTransform ToWorld = It->GetActorTransform();
			const int32 Count = ChunkRoad->GetNumSections();
			for (int32 Index = 0; Index < Count && !bOverlaps; ++Index)
			{
				if (FProcMeshSection* Section = ChunkRoad->GetProcMeshSection(Index))
				{
					const FBox Box = Section->SectionLocalBox.TransformBy(ToWorld);
					bOverlaps = ViewLocation.X >= Box.Min.X && ViewLocation.X <= Box.Max.X
						&& ViewLocation.Y >= Box.Min.Y && ViewLocation.Y <= Box.Max.Y;
				}
			}

			if (bOverlaps)
			{
				UE_LOG(LogWbStreaming, Log, TEXT("Bounds-Pruefung fuer Chunk '%s':"), *It->GetName());
				Describe(ChunkRoad, TEXT("Strasse"));
				Describe(It->GetBuildingMesh(), TEXT("Gebaeude"));
				break;
			}
		}
	}

	// Gelaendehoehe aus DREI Quellen am selben Punkt.
	//
	// Der Line-Trace lieferte ueber voellig verschiedene Einebnungs-Parameter
	// hinweg immer exakt 11300 - ein Wert, der sich nicht bewegt, misst nicht
	// das, was man glaubt. GetHeightAtLocation fragt die Landscape-Daten direkt
	// (Complex = volle Aufloesung, Simple = vereinfachte Kollision). Weichen
	// die Werte voneinander ab, ist klar, warum die Fahrbahn trotz rechnerisch
	// ausreichendem Abstand verdeckt bleibt.
	{
		double ProbeX = ViewLocation.X;
		double ProbeY = ViewLocation.Y;
		double RoadZ = 0.0;
		bool bHaveRoad = false;

		// Den naechsten Fahrbahn-Vertex als Messpunkt nehmen.
		double BestDistSq = TNumericLimits<double>::Max();
		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			UProceduralMeshComponent* Mesh = It->GetRoadMesh();
			if (!Mesh)
			{
				continue;
			}

			const FTransform ToWorld = It->GetActorTransform();
			const int32 Count = Mesh->GetNumSections();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(Index);
				const UMaterialInterface* Material = Mesh->GetMaterial(Index);
				if (!Section || !Material || !Material->GetName().Equals(TEXT("M_WbRoad")))
				{
					continue;
				}

				const FBox Box = Section->SectionLocalBox.TransformBy(ToWorld);
				if (ViewLocation.X < Box.Min.X || ViewLocation.X > Box.Max.X ||
					ViewLocation.Y < Box.Min.Y || ViewLocation.Y > Box.Max.Y)
				{
					continue;
				}

				for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
				{
					const FVector W = ToWorld.TransformPosition(Vertex.Position);
					const double D2 = FMath::Square(W.X - ViewLocation.X) + FMath::Square(W.Y - ViewLocation.Y);
					if (D2 < BestDistSq)
					{
						BestDistSq = D2;
						ProbeX = W.X;
						ProbeY = W.Y;
						RoadZ = W.Z;
						bHaveRoad = true;
					}
				}
			}
		}

		if (bHaveRoad)
		{
			const FVector Probe(ProbeX, ProbeY, ViewLocation.Z);

			double TraceZ = 0.0;
			bool bTraceHit = false;
			{
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(WbProbe), true);
				if (World->LineTraceSingleByChannel(Hit,
					Probe + FVector(0, 0, 50000.0), Probe - FVector(0, 0, 50000.0),
					ECC_WorldStatic, Params))
				{
					TraceZ = Hit.ImpactPoint.Z;
					bTraceHit = true;
				}
			}

			for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
			{
				const TOptional<float> ComplexZ =
					It->GetHeightAtLocation(Probe, EHeightfieldSource::Complex);
				const TOptional<float> SimpleZ =
					It->GetHeightAtLocation(Probe, EHeightfieldSource::Simple);

				if (!ComplexZ.IsSet() && !SimpleZ.IsSet())
				{
					continue;
				}

				UE_LOG(LogWbStreaming, Log,
					TEXT("Gelaende-Vergleich am Fahrbahn-Vertex (%.0f, %.0f): Fahrbahn Z = %.1f | ")
					TEXT("Trace %s | Complex %s | Simple %s"),
					ProbeX, ProbeY, RoadZ,
					bTraceHit ? *FString::Printf(TEXT("%.1f (Abstand %.1f cm)"), TraceZ, RoadZ - TraceZ) : TEXT("kein Treffer"),
					ComplexZ.IsSet() ? *FString::Printf(TEXT("%.1f (Abstand %.1f cm)"), ComplexZ.GetValue(), RoadZ - ComplexZ.GetValue()) : TEXT("-"),
					SimpleZ.IsSet() ? *FString::Printf(TEXT("%.1f (Abstand %.1f cm)"), SimpleZ.GetValue(), RoadZ - SimpleZ.GetValue()) : TEXT("-"));
				break;
			}
		}
	}

	// Naechster Abschnitt JE MATERIAL.
	//
	// Entscheidende Frage: Gibt es am Spielerort ueberhaupt Fahrbahn, oder nur
	// Gehweg, Bordstein und Markierung? Die duennen Linien im Bild zeigen, dass
	// der Strassenkorridor gezeichnet wird - die Fahrbahnflaeche selbst fehlt.
	// Liegt der naechste M_WbRoad-Abschnitt kilometerweit weg, waehrend Gehweg
	// und Markierung direkt anliegen, klafft eine Luecke in der Erzeugung.
	{
		TMap<FString, double> NearestByMaterial;
		TMap<FString, double> NearestZ;

		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			UProceduralMeshComponent* Mesh = It->GetRoadMesh();
			if (!Mesh)
			{
				continue;
			}

			const FTransform ToWorld = It->GetActorTransform();
			const int32 Count = Mesh->GetNumSections();

			for (int32 Index = 0; Index < Count; ++Index)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(Index);
				if (!Section || Section->ProcVertexBuffer.Num() == 0)
				{
					continue;
				}

				const UMaterialInterface* Material = Mesh->GetMaterial(Index);
				const FString Name = Material ? Material->GetName() : TEXT("<ohne Material>");

				const FBox Box = Section->SectionLocalBox.TransformBy(ToWorld);
				const double Dx = FMath::Max(0.0, FMath::Max(Box.Min.X - ViewLocation.X, ViewLocation.X - Box.Max.X));
				const double Dy = FMath::Max(0.0, FMath::Max(Box.Min.Y - ViewLocation.Y, ViewLocation.Y - Box.Max.Y));
				const double Distance = FMath::Sqrt(Dx * Dx + Dy * Dy);

				double& Best = NearestByMaterial.FindOrAdd(Name, TNumericLimits<double>::Max());
				if (Distance < Best)
				{
					Best = Distance;

					// Z des Vertex, der dem Spieler am naechsten liegt.
					double BestVertexDistSq = TNumericLimits<double>::Max();
					double BestZ = 0.0;
					for (const FProcMeshVertex& Vertex : Section->ProcVertexBuffer)
					{
						const FVector W = ToWorld.TransformPosition(Vertex.Position);
						const double VdX = W.X - ViewLocation.X;
						const double VdY = W.Y - ViewLocation.Y;
						const double D2 = VdX * VdX + VdY * VdY;
						if (D2 < BestVertexDistSq)
						{
							BestVertexDistSq = D2;
							BestZ = W.Z;
						}
					}
					NearestZ.FindOrAdd(Name) = BestZ;
				}
			}
		}

		UE_LOG(LogWbStreaming, Log, TEXT("Naechster Abschnitt je Material (Spieler bei Z = %.0f cm):"), ViewLocation.Z);
		for (const TPair<FString, double>& Entry : NearestByMaterial)
		{
			UE_LOG(LogWbStreaming, Log, TEXT("    %-24s %8.1f m entfernt, naechster Vertex bei Z = %.0f cm"),
				*Entry.Key, Entry.Value / 100.0, NearestZ.FindRef(Entry.Key));
		}
	}

	// Abschnitte nach zugewiesenem Material zaehlen.
	//
	// Der Chunk speichert den ERoadMeshChannel nicht (kein UPROPERTY), das
	// Material ist also die einzige verbliebene Spur, welcher Abschnitt
	// Fahrbahn, Gehweg, Bordstein oder Markierung ist. Wenn die Fahrbahn
	// unsichtbar ist, muss sich hier zeigen, ob es ueberhaupt
	// Fahrbahn-Abschnitte gibt - und ob sie sichtbar geschaltet sind.
	{
		TMap<FString, int32> SectionsByMaterial;
		TMap<FString, int32> TrianglesByMaterial;
		int32 InvisibleSections = 0;
		int32 HiddenActors = 0;

		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			if (It->IsHidden())
			{
				++HiddenActors;
			}

			UProceduralMeshComponent* Mesh = It->GetRoadMesh();
			if (!Mesh)
			{
				continue;
			}

			const int32 Count = Mesh->GetNumSections();
			for (int32 Index = 0; Index < Count; ++Index)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(Index);
				if (!Section)
				{
					continue;
				}

				if (!Section->bSectionVisible)
				{
					++InvisibleSections;
				}

				const UMaterialInterface* Material = Mesh->GetMaterial(Index);
				const FString Name = Material ? Material->GetName() : TEXT("<ohne Material>");

				SectionsByMaterial.FindOrAdd(Name) += 1;
				TrianglesByMaterial.FindOrAdd(Name) += Section->ProcIndexBuffer.Num() / 3;
			}
		}

		UE_LOG(LogWbStreaming, Log,
			TEXT("Strassen-Abschnitte nach Material (%d unsichtbar geschaltet, %d Chunk-Actors versteckt):"),
			InvisibleSections, HiddenActors);

		for (const TPair<FString, int32>& Entry : SectionsByMaterial)
		{
			UE_LOG(LogWbStreaming, Log, TEXT("    %-24s %6d Abschnitte, %9d Dreiecke"),
				*Entry.Key, Entry.Value, TrianglesByMaterial.FindRef(Entry.Key));
		}
	}

	// Dreiecks-Geometrie der Fahrbahn pruefen.
	//
	// Letzter ungepruefter Punkt: Beim Chunking werden die Vertex-Indizes je
	// Zelle neu vergeben. Greifen die Indizes danach auf falsche Vertices zu,
	// entstehen entartete oder kilometerlange Dreiecke - die Vertices lagen
	// dann korrekt (wie gemessen), die Flaeche waere trotzdem unsichtbar.
	{
		int32 Triangles = 0;
		int32 Degenerate = 0;
		int32 Oversized = 0;
		double SumEdgeCm = 0.0;
		double MaxEdgeCm = 0.0;
		double TotalAreaSqM = 0.0;

		constexpr int32 MaxTriangles = 20000;
		constexpr double OversizedEdgeCm = 10000.0;   // 100 m - fuer eine Fahrbahn absurd

		for (TActorIterator<AWiesbadenCityChunk> It(World); It && Triangles < MaxTriangles; ++It)
		{
			UProceduralMeshComponent* Mesh = It->GetRoadMesh();
			if (!Mesh)
			{
				continue;
			}

			const int32 Sections = Mesh->GetNumSections();
			for (int32 S = 0; S < Sections && Triangles < MaxTriangles; ++S)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(S);
				if (!Section || Section->ProcIndexBuffer.Num() < 3)
				{
					continue;
				}

				// Nur Fahrbahn-Abschnitte betrachten.
				const UMaterialInterface* Material = Mesh->GetMaterial(S);
				if (!Material || !Material->GetName().Equals(TEXT("M_WbRoad")))
				{
					continue;
				}

				for (int32 I = 0; I + 2 < Section->ProcIndexBuffer.Num() && Triangles < MaxTriangles; I += 3)
				{
					const int32 I0 = Section->ProcIndexBuffer[I + 0];
					const int32 I1 = Section->ProcIndexBuffer[I + 1];
					const int32 I2 = Section->ProcIndexBuffer[I + 2];

					if (!Section->ProcVertexBuffer.IsValidIndex(I0) ||
						!Section->ProcVertexBuffer.IsValidIndex(I1) ||
						!Section->ProcVertexBuffer.IsValidIndex(I2))
					{
						++Degenerate;
						continue;
					}

					const FVector& A = Section->ProcVertexBuffer[I0].Position;
					const FVector& B = Section->ProcVertexBuffer[I1].Position;
					const FVector& C = Section->ProcVertexBuffer[I2].Position;

					const double E0 = FVector::Dist(A, B);
					const double E1 = FVector::Dist(B, C);
					const double E2 = FVector::Dist(C, A);
					const double LongestEdge = FMath::Max3(E0, E1, E2);

					const double AreaCmSq = 0.5 * FVector::CrossProduct(B - A, C - A).Size();

					++Triangles;
					SumEdgeCm += (E0 + E1 + E2) / 3.0;
					MaxEdgeCm = FMath::Max(MaxEdgeCm, LongestEdge);
					TotalAreaSqM += AreaCmSq / 10000.0;

					if (AreaCmSq < 1.0)
					{
						++Degenerate;
					}
					if (LongestEdge > OversizedEdgeCm)
					{
						++Oversized;
					}
				}
			}
		}

		if (Triangles > 0)
		{
			UE_LOG(LogWbStreaming, Log,
				TEXT("Fahrbahn-Dreiecke: %d geprueft - mittlere Kantenlaenge %.1f m, laengste Kante %.1f m, ")
				TEXT("%d entartet (Flaeche ~0), %d ueberlang (>100 m); Gesamtflaeche der Stichprobe %.0f m2."),
				Triangles, SumEdgeCm / Triangles / 100.0, MaxEdgeCm / 100.0,
				Degenerate, Oversized, TotalAreaSqM);
		}
	}

	// Wirksamkeits-Nachweis der Gebaeude-Kollision.
	//
	// Dass Koerper existieren, heisst nicht, dass sie blocken - genau diese
	// Luecke zwischen "initialisiert" und "wirkt" hat in diesem Projekt schon
	// den unsichtbaren Verkehr und die unsichtbaren Strassen verdeckt. Der
	// Trace geht waagerecht zum naechsten Gebaeude: trifft er einen
	// Box-Koerper, faehrt der Spieler dort nicht mehr hindurch.
	if (BuildingCollision && BuildingCollision->GetActiveBodyCount() > 0)
	{
		if (const AWiesbadenWorldBuilder* Builder = FindBakedCityBuilder())
		{
			TArray<int32> Nearest;
			UBuildingCollisionSpawnerComponent::SelectNearestBuildings(
				Builder->Buildings, ViewLocation, 15000.0, 1, Nearest);

			if (Nearest.Num() == 1)
			{
				const FVector Target = Builder->Buildings[Nearest[0]].Centroid;

				// Auf Fahrzeughoehe messen, nicht auf Kamerahoehe.
				const FVector From(ViewLocation.X, ViewLocation.Y, Target.Z + 150.0);
				const FVector To(Target.X, Target.Y, Target.Z + 150.0);

				FHitResult Hit;
				FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WbBuildingCollision), true);
				const bool bHit = World->LineTraceSingleByChannel(Hit, From, To, ECC_WorldStatic, TraceParams);

				// Auf die Marke pruefen, nicht auf den Typ: der Verkehr haelt
				// ebenfalls Box-Koerper bereit, ein Auto vor der Motorhaube haette
				// den Nachweis sonst faelschlich bestaetigt.
				const bool bHitBody = bHit && Hit.GetComponent()
					&& Hit.GetComponent()->ComponentHasTag(
						UBuildingCollisionSpawnerComponent::BuildingBodyTag);

				if (bHitBody)
				{
					UE_LOG(LogWbCore, Log,
						TEXT("Gebaeude-Kollision wirksam: %d Koerper aktiv, Trace zum naechsten Gebaeude ")
						TEXT("blockiert nach %.1f m."),
						BuildingCollision->GetActiveBodyCount(),
						FVector::Dist(From, Hit.ImpactPoint) / 100.0);
				}
				else
				{
					UE_LOG(LogWbCore, Warning,
						TEXT("Gebaeude-Kollision: %d Koerper aktiv, aber der Trace zum naechsten Gebaeude ")
						TEXT("wird NICHT geblockt (getroffen: %s). Der Spieler faehrt dort weiter hindurch."),
						BuildingCollision->GetActiveBodyCount(),
						bHit && Hit.GetComponent() ? *Hit.GetComponent()->GetName() : TEXT("nichts"));
				}
			}
		}
	}

	// Zuschnitt-Bilanz der Strassensegmente.
	//
	// An Kreuzungen werden die Arme zurueckgeschnitten, damit die
	// Kreuzungsflaeche Platz hat. Faellt der Zuschnitt zu gross aus, bleibt vom
	// Segment nichts uebrig - dann klaffen Luecken zwischen den Fahrbahnen und
	// die Zebrastreifen stehen frei in der Wiese.
	if (const AWiesbadenWorldBuilder* TrimBuilder = FindBakedCityBuilder())
	{
		const FRoadNetwork& Net = TrimBuilder->RoadNetwork;

		int32 Counted = 0;
		int32 FullyTrimmed = 0;
		int32 HeavilyTrimmed = 0;
		double SumRemaining = 0.0;
		constexpr double RadiusCm = 30000.0;   // 300 m um den Spieler

		for (const FRoadSegment& Segment : Net.Segments)
		{
			if (Segment.Centerline.Num() < 2)
			{
				continue;
			}

			const FVector& First = Segment.Centerline[0];
			if (FMath::Abs(First.X - ViewLocation.X) > RadiusCm
				|| FMath::Abs(First.Y - ViewLocation.Y) > RadiusCm)
			{
				continue;
			}

			auto LengthOf = [](const TArray<FVector>& Line)
			{
				double Sum = 0.0;
				for (int32 Index = 1; Index < Line.Num(); ++Index)
				{
					Sum += FVector::Dist2D(Line[Index - 1], Line[Index]);
				}
				return Sum;
			};

			const double Original = LengthOf(Segment.Centerline);
			if (Original <= 1.0)
			{
				continue;
			}

			++Counted;

			if (Segment.TrimmedCenterline.Num() < 2)
			{
				++FullyTrimmed;
				continue;
			}

			const double Remaining = LengthOf(Segment.TrimmedCenterline) / Original;
			SumRemaining += Remaining;
			if (Remaining < 0.5)
			{
				++HeavilyTrimmed;
			}
		}

		if (Counted > 0)
		{
			const int32 WithGeometry = Counted - FullyTrimmed;
			UE_LOG(LogWbRoads, Log,
				TEXT("Zuschnitt-Bilanz (%d Segmente im Umkreis 300 m): %d VOLLSTAENDIG weggeschnitten (%.0f %%), ")
				TEXT("%d auf unter die Haelfte gekuerzt; im Mittel bleiben %.0f %% Laenge."),
				Counted, FullyTrimmed, 100.0 * FullyTrimmed / Counted, HeavilyTrimmed,
				WithGeometry > 0 ? 100.0 * SumRemaining / WithGeometry : 0.0);
		}
	}

	// Strassen-Steckbrief am Spielerort.
	//
	// Aus dem Bild allein ist nicht zu sagen, ob eine breite helle Flaeche ein
	// zu breiter Gehweg, ein Fussweg oder eine Fahrbahn mit hellem Belag ist -
	// alle drei sehen aehnlich aus. Diese Kennzahlen entscheiden es.
	if (const AWiesbadenWorldBuilder* SegBuilder = FindBakedCityBuilder())
	{
		const FRoadNetwork& Net = SegBuilder->RoadNetwork;

		int32 BestSegment = INDEX_NONE;
		double BestDistSq = TNumericLimits<double>::Max();

		for (int32 Index = 0; Index < Net.Segments.Num(); ++Index)
		{
			const FRoadSegment& Segment = Net.Segments[Index];
			const TArray<FVector>& Line = Segment.TrimmedCenterline.Num() >= 2
				? Segment.TrimmedCenterline : Segment.Centerline;

			for (const FVector& Point : Line)
			{
				const double Dx = Point.X - ViewLocation.X;
				const double Dy = Point.Y - ViewLocation.Y;
				const double DistSq = Dx * Dx + Dy * Dy;
				if (DistSq < BestDistSq)
				{
					BestDistSq = DistSq;
					BestSegment = Index;
				}
			}
		}

		if (BestSegment != INDEX_NONE)
		{
			const FRoadSegment& S = Net.Segments[BestSegment];
			const UEnum* HighwayEnum = StaticEnum<EOSMHighwayType>();
			const UEnum* SurfaceEnum = StaticEnum<EOSMSurfaceType>();
			const UEnum* SidewalkEnum = StaticEnum<EOSMSidewalkType>();

			UE_LOG(LogWbRoads, Log,
				TEXT("Strasse am Spieler (%.1f m entfernt): '%s', Typ %s, Belag %s | ")
				TEXT("Fahrbahn %.2f m, Gehweg %s je %.2f m, Bordstein %.0f cm | %d+%d Spuren, %.0f km/h"),
				FMath::Sqrt(BestDistSq) / 100.0,
				S.StreetName.IsEmpty() ? TEXT("(ohne Namen)") : *S.StreetName,
				HighwayEnum ? *HighwayEnum->GetNameStringByValue(static_cast<int64>(S.HighwayType)) : TEXT("?"),
				SurfaceEnum ? *SurfaceEnum->GetNameStringByValue(static_cast<int64>(S.Surface)) : TEXT("?"),
				S.CarriagewayWidthCm / 100.0,
				SidewalkEnum ? *SidewalkEnum->GetNameStringByValue(static_cast<int64>(S.SidewalkType)) : TEXT("?"),
				S.SidewalkWidthCm / 100.0,
				S.KerbHeightCm,
				S.ForwardLaneCount, S.BackwardLaneCount, S.MaxSpeedKmh);
		}
	}

	// Ueberdeckung der achsparallelen Naeherung messen.
	//
	// Die Box umschliesst den Grundriss; bei einem gedrehten Gebaeude ist sie
	// groesser als er. Ragt sie bis auf die Fahrbahn, blockiert sie Spieler und
	// Verkehr an Stellen, an denen gar kein Haus steht. FootprintAreaSqm liegt
	// je Gebaeude vor - das Verhaeltnis beziffert den Fehler direkt.
	if (const AWiesbadenWorldBuilder* AreaBuilder = FindBakedCityBuilder())
	{
		TArray<int32> Nearby;
		UBuildingCollisionSpawnerComponent::SelectNearestBuildings(
			AreaBuilder->Buildings, ViewLocation, 15000.0, 200, Nearby);

		int32 Counted = 0;
		int32 ContainingPlayer = 0;
		int32 WithOriented = 0;
		double SumRatio = 0.0;
		double WorstRatio = 0.0;
		double SumOrientedRatio = 0.0;
		double WorstOrientedRatio = 0.0;

		for (const int32 Index : Nearby)
		{
			const FGeneratedBuilding& Candidate = AreaBuilder->Buildings[Index];
			if (Candidate.FootprintAreaSqm <= 1.0)
			{
				continue;
			}

			const FVector Extent = Candidate.Bounds.GetExtent();
			const double BoxAreaSqm = (Extent.X * 2.0 / 100.0) * (Extent.Y * 2.0 / 100.0);
			const double Ratio = BoxAreaSqm / Candidate.FootprintAreaSqm;

			++Counted;
			SumRatio += Ratio;
			WorstRatio = FMath::Max(WorstRatio, Ratio);

			// Gedrehte Box zum Vergleich - sie ist die tatsaechlich benutzte.
			if (Candidate.FootprintExtentCm.X > 1.0 && Candidate.FootprintExtentCm.Y > 1.0)
			{
				const double OrientedAreaSqm =
					(Candidate.FootprintExtentCm.X * 2.0 / 100.0)
					* (Candidate.FootprintExtentCm.Y * 2.0 / 100.0);
				const double OrientedRatio = OrientedAreaSqm / Candidate.FootprintAreaSqm;

				++WithOriented;
				SumOrientedRatio += OrientedRatio;
				WorstOrientedRatio = FMath::Max(WorstOrientedRatio, OrientedRatio);
			}

			if (ViewLocation.X >= Candidate.Bounds.Min.X && ViewLocation.X <= Candidate.Bounds.Max.X &&
				ViewLocation.Y >= Candidate.Bounds.Min.Y && ViewLocation.Y <= Candidate.Bounds.Max.Y)
			{
				++ContainingPlayer;
			}
		}

		if (Counted > 0)
		{
			UE_LOG(LogWbCore, Log,
				TEXT("Gebaeude-Kollision Ueberdeckung an %d Gebaeuden: achsparallel %.2fx (max %.2fx), ")
				TEXT("gedreht %.2fx (max %.2fx) an %d Gebaeuden; %d achsparallele Box(en) enthalten den Spieler."),
				Counted, SumRatio / Counted, WorstRatio,
				WithOriented > 0 ? SumOrientedRatio / WithOriented : 0.0, WorstOrientedRatio,
				WithOriented, ContainingPlayer);
		}
	}

	// Wicklungs-Vergleich zwischen Fahrbahn und Gebaeuden.
	//
	// Die Vertex-Normalen der Fahrbahn sind fest auf FVector::UpVector gesetzt
	// und sagen daher NICHTS ueber die Dreiecks-Wicklung aus - genau die
	// entscheidet aber, ob Backface-Culling die Flaeche wegschneidet. Deshalb
	// wird die geometrische Normale aus den Eckpunkten gerechnet.
	//
	// Verglichen wird mit den waagerechten Gebaeudeflaechen (Daechern): die
	// rendern sichtbar korrekt. Haben beide dasselbe Vorzeichen, ist die
	// Wicklung in Ordnung - unabhaengig davon, welche Konvention Unreal
	// intern verwendet.
	{
		auto MeanFlatNormalZ = [&](UProceduralMeshComponent* Mesh, int32& OutCount) -> double
		{
			OutCount = 0;
			double Sum = 0.0;
			if (!Mesh)
			{
				return 0.0;
			}

			constexpr int32 MaxTriangles = 400;
			constexpr double FlatToleranceCm = 20.0;

			const int32 Sections = Mesh->GetNumSections();
			for (int32 S = 0; S < Sections && OutCount < MaxTriangles; ++S)
			{
				FProcMeshSection* Section = Mesh->GetProcMeshSection(S);
				if (!Section || Section->ProcIndexBuffer.Num() < 3)
				{
					continue;
				}

				for (int32 I = 0; I + 2 < Section->ProcIndexBuffer.Num() && OutCount < MaxTriangles; I += 3)
				{
					const FVector& A = Section->ProcVertexBuffer[Section->ProcIndexBuffer[I + 0]].Position;
					const FVector& B = Section->ProcVertexBuffer[Section->ProcIndexBuffer[I + 1]].Position;
					const FVector& C = Section->ProcVertexBuffer[Section->ProcIndexBuffer[I + 2]].Position;

					// Nur waagerechte Dreiecke: senkrechte Waende haben von Haus
					// aus eine Normale ohne Z-Anteil und taugen nicht zum Vergleich.
					if (FMath::Abs(A.Z - B.Z) > FlatToleranceCm ||
						FMath::Abs(A.Z - C.Z) > FlatToleranceCm)
					{
						continue;
					}

					const FVector GeoNormal = FVector::CrossProduct(C - A, B - A).GetSafeNormal();
					if (GeoNormal.IsNearlyZero())
					{
						continue;
					}

					Sum += GeoNormal.Z;
					++OutCount;
				}
			}

			return OutCount > 0 ? Sum / OutCount : 0.0;
		};

		// Einen Chunk in Spielernaehe nehmen - er enthaelt beide Mesh-Arten.
		for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
		{
			int32 RoadTris = 0;
			int32 BuildingTris = 0;
			const double RoadNormalZ = MeanFlatNormalZ(It->GetRoadMesh(), RoadTris);
			const double BuildingNormalZ = MeanFlatNormalZ(It->GetBuildingMesh(), BuildingTris);

			if (RoadTris > 0 && BuildingTris > 0)
			{
				UE_LOG(LogWbStreaming, Log,
					TEXT("Wicklungs-Vergleich: Fahrbahn %.3f (%d Dreiecke) gegen waagerechte Gebaeudeflaechen ")
					TEXT("%.3f (%d Dreiecke). Gleiches Vorzeichen = Wicklung in Ordnung."),
					RoadNormalZ, RoadTris, BuildingNormalZ, BuildingTris);
				break;
			}
		}
	}

	// Gelaendehoehe per Trace. Die Procedural-Meshes haben wegen
	// bCreateCollision=False keine Kollision - der Treffer ist also das Landscape.
	const FVector Start = ViewLocation + FVector(0.0, 0.0, 100000.0);
	const FVector End = ViewLocation - FVector(0.0, 0.0, 100000.0);

	FHitResult Hit;
	FCollisionQueryParams TraceParams(SCENE_QUERY_STAT(WbHeightStack), true);
	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_WorldStatic, TraceParams))
	{
		UE_LOG(LogWbStreaming, Log,
			TEXT("Hoehen-Stapel: Gelaende bei Z = %.0f cm (getroffen: %s)."),
			Hit.ImpactPoint.Z, Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"));
	}
	else
	{
		UE_LOG(LogWbStreaming, Warning, TEXT("Hoehen-Stapel: kein Gelaende unter dem Spieler getroffen."));
	}
}
void UWiesbadenCitySubsystem::RequestDiagnosticScreenshot()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("WbScreenshot")))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Optionale Gelaende-Ausblendung: -WbHideTerrain.
	//
	// Die Fahrbahn liegt messbar ueber dem Gelaende und rendert trotzdem nur in
	// Fragmenten. Der Hoehen-Trace kann aber nur Kollisionsgeometrie sehen -
	// die Procedural-Meshes haben wegen bCreateCollision=False gar keine. Wird
	// die Strasse nach dem Ausblenden des Gelaendes sichtbar, verdeckt es sie.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbHideTerrain")))
	{
		int32 Hidden = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA<ALandscapeProxy>())
			{
				It->SetActorHiddenInGame(true);
				++Hidden;
			}
		}
		UE_LOG(LogWbStreaming, Log, TEXT("Gelaende ausgeblendet: %d Landscape-Actor(en)."), Hidden);
	}

	// Optionale Luftaufnahme: -WbAerial=<Hoehe in Metern>. Aus der Fahrerkamera
	// heraus laesst sich nicht beurteilen, ob das Strassennetz vorhanden ist -
	// ein Blick von oben zeigt das in einem einzigen Bild.
	// Kreuzungs-Rundgang hat Vorrang: -WbTour=<Anzahl>.
	int32 TourCount = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbTour="), TourCount) && TourCount > 0)
	{
		if (SetupJunctionTour(TourCount))
		{
			return;
		}
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Kreuzungs-Rundgang: keine Kreuzungen gefunden - normale Aufnahme."));
	}

	float AerialMeters = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbAerial="), AerialMeters) && AerialMeters > 0.0f)
	{
		if (SetupAerialView(AerialMeters))
		{
			// Der Kamerawechsel braucht Frames, bevor das Bild stimmt.
			AerialShotDelay = 2.0f;
			return;
		}
	}

	CaptureDiagnosticScreenshot();
}

bool UWiesbadenCitySubsystem::SetupAerialView(float HeightMeters)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		UE_LOG(LogWbStreaming, Warning,
			TEXT("Luftaufnahme: kein PlayerController - bleibe bei der Spielkamera."));
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Optionaler Zielort: -WbAtX=<cm> -WbAtY=<cm>.
	//
	// Ohne ihn steht die Kamera immer ueber dem Spielerstart. Eine Meldung wie
	// "die Platter Strasse sieht kaputt aus" laesst sich damit nicht pruefen -
	// man kommt gar nicht hin. Die Weltkoordinaten stammen aus den OSM-Daten
	// ueber denselben Konverter, den auch der Aufbau benutzt.
	float AtX = 0.0f;
	float AtY = 0.0f;
	const bool bHasX = FParse::Value(FCommandLine::Get(), TEXT("WbAtX="), AtX);
	const bool bHasY = FParse::Value(FCommandLine::Get(), TEXT("WbAtY="), AtY);

	if (bHasX && bHasY)
	{
		ViewLocation.X = AtX;
		ViewLocation.Y = AtY;

		UE_LOG(LogWbStreaming, Log,
			TEXT("Luftaufnahme an vorgegebener Stelle: (%.0f, %.0f), Hoehe %.0f m."),
			AtX, AtY, HeightMeters);
	}

	const FVector CameraLocation = ViewLocation + FVector(0.0, 0.0, HeightMeters * 100.0);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Leicht geneigt statt exakt senkrecht: so bleibt der Horizont im Bild und
	// die Hoehenlage der Geometrie ist beurteilbar.
	ACameraActor* Camera = World->SpawnActor<ACameraActor>(
		CameraLocation, FRotator(-70.0f, 0.0f, 0.0f), Params);
	if (!Camera)
	{
		return false;
	}

	PC->SetViewTarget(Camera);

	UE_LOG(LogWbStreaming, Log,
		TEXT("Luftaufnahme: Kamera %.0f m ueber dem Spieler bei (%.0f, %.0f, %.0f)."),
		HeightMeters, CameraLocation.X, CameraLocation.Y, CameraLocation.Z);
	return true;
}

void UWiesbadenCitySubsystem::CaptureDiagnosticScreenshot()
{
	const FString Path = FPaths::ProjectSavedDir() / TEXT("Diagnose") / TEXT("Stadt");
	FScreenshotRequest::RequestScreenshot(Path, false, true);

	UE_LOG(LogWbStreaming, Log, TEXT("Diagnose-Screenshot angefordert: %s"), *Path);

	// Dem Renderer ein paar Frames Zeit lassen, danach beenden - der Lauf ist
	// automatisiert und soll nicht offen stehen bleiben.
	ScreenshotQuitDelay = 3.0f;
}


UWiesbadenGameInstance* UWiesbadenCitySubsystem::GetGameInstance() const
{
	UWorld* World = GetWorld();
	return World ? Cast<UWiesbadenGameInstance>(World->GetGameInstance()) : nullptr;
}

FLastBuildInfo UWiesbadenCitySubsystem::GetLastBuildInfo() const
{
	const UWiesbadenGameInstance* GI = GetGameInstance();
	return GI ? GI->GetLastBuildInfo() : FLastBuildInfo();
}

FString UWiesbadenCitySubsystem::GetLastBuildTimestamp() const
{
	return GetLastBuildInfo().Timestamp;
}

double UWiesbadenCitySubsystem::GetLastBuildDurationSeconds() const
{
	return GetLastBuildInfo().DurationSeconds;
}

FString UWiesbadenCitySubsystem::GetLastBuildResult() const
{
	return GetLastBuildInfo().Result;
}

FString UWiesbadenCitySubsystem::GetLastBuildSummary() const
{
	return GetLastBuildInfo().GetSummary();
}

FTerrainQualityReport UWiesbadenCitySubsystem::GetTerrainQuality() const
{
	const UWiesbadenGameInstance* GI = GetGameInstance();
	return GI ? GI->GetTerrainQuality() : FTerrainQualityReport();
}

void UWiesbadenCitySubsystem::InitializeCity()
{
	UWiesbadenGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		BroadcastState(TEXT("Kein UWiesbadenGameInstance - Stadt-Initialisierung nicht moeglich."), false);
		return;
	}

	// Gebackene Stadt (nach Editor-Build als Map gespeichert) hat Vorrang vor
	// allem anderen: Die Geometrie liegt bereits im Level, ein Laufzeit-Build
	// wuerde sie doppelt erzeugen und Minuten kosten. Die RoadNetwork-Daten
	// liegen am WorldBuilder-Actor (nicht-transiente UPROPERTY, in der Map
	// serialisiert) - daraus starten wir auch hier die Verkehrs-Simulation,
	// statt sie wie bisher komplett zu ueberspringen.
	const bool bCityBakedInLevel = HasBakedCityInLevel();
	if (bCityBakedInLevel)
	{
		if (AWiesbadenWorldBuilder* Builder = FindBakedCityBuilder())
		{
			if (!Builder->RoadNetwork.IsEmpty())
			{
				TrafficSimulation.Initialize(Builder->RoadNetwork, Builder->TrafficSettings);

				// Ampeln: Das System war vollstaendig implementiert und getestet,
				// wurde aber nie mit der Simulation verbunden - der Verkehr fuhr
				// durch jede rote Ampel. SetTrafficLightSystem MUSS gerufen werden,
				// sonst bleibt der Zeiger nullptr und alles gilt als gruen.
				TrafficLightSystem.Initialize(Builder->RoadNetwork, Builder->TrafficLightSettings);
				TrafficSimulation.SetTrafficLightSystem(&TrafficLightSystem);
				PedestrianSimulation.Initialize(Builder->RoadNetwork, Builder->PedestrianSettings);

				// Traeger fuer die sichtbaren Fahrzeuge erzeugen.
				//
				// Die Simulation allein rechnet nur Positionen; gezeichnet
				// werden die Fahrzeuge vom InstancedStaticMesh-Pool am
				// CityActor. Ohne ihn lief die Verkehrs-Simulation zwar, aber
				// Tick() sprang ueber UpdateTrafficVehicles hinweg
				// (CityActor == nullptr) - der Verkehr war vollstaendig
				// unsichtbar.
				//
				// ApplyCityData wird bewusst NICHT aufgerufen: Strassen,
				// Gebaeude und Terrain liegen bereits gebacken im Level. Der
				// Actor dient hier ausschliesslich als Wirt fuer den
				// Fahrzeug-Pool; seine Procedural-Meshes bleiben leer.
				SpawnTrafficHostActor();

				// Gebaeude-Kollision: Die Stadt-Meshes sind ohne Kollision gebacken,
				// der Spieler fuhr durch die Haeuser. Die Grundriss-Bounds liegen
				// am gebackenen WorldBuilder und reichen fuer Box-Koerper aus.
				if (BuildingCollision && Builder->Buildings.Num() > 0)
				{
					BuildingCollision->SetBuildings(Builder->Buildings);
					UE_LOG(LogWbCore, Log,
						TEXT("Gebaeude-Kollision: %d Grundrisse uebernommen."),
						Builder->Buildings.Num());
				}

				// Verkehr abschaltbar: -WbNoTraffic.
				//
				// Fuer die Fahrprobe der Physik unverzichtbar. Der Spielerwagen
				// wird dort eingesetzt, wo der Verkehr seine Fahrzeuge um den
				// Spieler herum erzeugt - beim Chaos-Fahrzeug stand er
				// daraufhin MITTEN in einem Pulk geparkter Kaefer und kam
				// nicht heraus: Motor bei 4600 Umdrehungen im ersten Gang,
				// Geschwindigkeit 0. Das alte Fahrzeug fiel darauf nicht
				// herein, weil es sich per SetActorLocation durch alles
				// hindurchgesetzt hat.
				if (FParse::Param(FCommandLine::Get(), TEXT("WbNoTraffic")))
				{
					TrafficSimulation.Reset();
					UE_LOG(LogWbCore, Log,
						TEXT("Verkehr abgeschaltet (-WbNoTraffic)."));
				}

				UE_LOG(LogWbCore, Log,
					TEXT("Gebackene Stadt: Verkehrs-Simulation initialisiert: Dichte %.2f, %.1f km Netz, %d Spuren, ")
					TEXT("Fahrzeug-Traeger %s."),
					Builder->TrafficSettings.TrafficDensity,
					Builder->RoadNetwork.GetTotalDrivableLengthKm(),
					Builder->RoadNetwork.Lanes.Num(),
					CityActor ? TEXT("bereit") : TEXT("FEHLT - Verkehr bleibt unsichtbar"));
				BroadcastState(FString::Printf(TEXT("Stadt liegt gebacken im Level - Verkehr aktiv (%d Spuren)."),
					Builder->RoadNetwork.Lanes.Num()), true);
				return;
			}
		}
		BroadcastState(TEXT("Stadt liegt gebacken im Level (kein Laufzeit-Build noetig)."), true);
		return;
	}

	// Alle uebrigen Faelle ueber die datenreine Entscheidungsfunktion.
	if (!ShouldRunRuntimeBuild(
			/*bCityBakedInLevel=*/false,
			GI->HasCityData(),
			GI->ShouldGenerateAtRuntime(),
			GI->IsCityDataLoading()))
	{
		if (GI->HasCityData())
		{
			// Daten bereits verfuegbar (z. B. nach Levelwechsel oder frueherem
			// Laufzeit-Build) - direkt spawnen. Erfolg haengt am Spawn-Ergebnis.
			SpawnCityActor(*GI->GetCityData());
			BroadcastState(CityStatus, IsCityReady());
		}
		else if (!GI->ShouldGenerateAtRuntime())
		{
			// Produktionspfad: Die Stadt liegt als gebackene World-Partition-Map
			// im Level; World Partition streamt die Zellen (Streaming-Quelle ist
			// bereits registriert). Es wird nichts generiert.
			BroadcastState(TEXT("Gebackene World-Partition-Stadt erwartet (kein Laufzeit-Build)."), true);
		}
		// else: Laufzeit-Build laeuft bereits - wir warten auf OnCityDataLoaded.
		return;
	}

	// Laufzeit-Build anstossen und auf das Lade-Ende warten.
	if (!OnCityDataLoadedHandle.IsValid())
	{
		OnCityDataLoadedHandle = GI->OnCityDataLoadedNative.AddUObject(this, &UWiesbadenCitySubsystem::OnCityDataLoaded);
		WeakGameInstance = GI;
	}

	bCreateCollision = GI->bCreateCollision;

	BroadcastState(TEXT("Lade Stadt zur Laufzeit..."), true);
	GI->LoadCityDataAsync();
}

AWiesbadenWorldBuilder* UWiesbadenCitySubsystem::FindBakedCityBuilder() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	// Der AWiesbadenWorldBuilder bleibt nach 'Save City as Map' als Actor mit
	// der gebauten Geometrie im Level; bCityBaked ist nicht transient und
	// ueberlebt das Speichern der Map.
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It)
	{
		if (It->bCityBaked)
		{
			return *It;
		}
	}
	return nullptr;
}

bool UWiesbadenCitySubsystem::HasBakedCityInLevel() const
{
	return FindBakedCityBuilder() != nullptr;
}

void UWiesbadenCitySubsystem::OnCityDataLoaded()
{
	UWiesbadenGameInstance* GI = GetGameInstance();
	if (!GI || !GI->HasCityData())
	{
		BroadcastState(TEXT("Stadt-Load ohne gueltige Daten abgeschlossen."), false);
		return;
	}

	SpawnCityActor(*GI->GetCityData());
	BroadcastState(CityStatus, IsCityReady());
}

void UWiesbadenCitySubsystem::SpawnTrafficHostActor()
{
	if (CityActor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	CityActor = World->SpawnActor<AWiesbadenCityActor>(
		AWiesbadenCityActor::StaticClass(), FTransform::Identity, Params);

	if (!CityActor)
	{
		UE_LOG(LogWbCore, Warning,
			TEXT("Fahrzeug-Traeger konnte nicht gespawnt werden - der Verkehr bleibt unsichtbar."));
		return;
	}

	// Kollisionskoerper fuer die Gebaeude an denselben Traeger haengen.
	BuildingCollision = NewObject<UBuildingCollisionSpawnerComponent>(CityActor);
	if (BuildingCollision)
	{
		BuildingCollision->SetupAttachment(CityActor->GetRootComponent());
		BuildingCollision->RegisterComponent();
	}

	// Materialien setzen; der Fahrzeug-Pool braucht sie fuer die Farbpalette.
	// Geometrie wird nicht angewendet - sie liegt gebacken im Level.
	if (UWiesbadenGameInstance* GI = GetGameInstance())
	{
		GI->ApplyMaterialsToCityActor(CityActor);
	}
}

void UWiesbadenCitySubsystem::SpawnCityActor(const FWiesbadenCityData& Data)
{
	if (CityActor)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	CityActor = World->SpawnActor<AWiesbadenCityActor>(AWiesbadenCityActor::StaticClass(), FTransform::Identity, Params);
	if (!CityActor)
	{
		BroadcastState(TEXT("Stadt-Actor konnte nicht gespawnt werden."), false);
		return;
	}

	// Materialien und Spawn-Parameter aus der GameInstance-Konfiguration.
	if (UWiesbadenGameInstance* GI = GetGameInstance())
	{
		GI->ApplyMaterialsToCityActor(CityActor);
		CityActor->TerrainPreviewGridSize = GI->TerrainPreviewGridSize;
		bCreateCollision = GI->bCreateCollision;
	}

	CityActor->ApplyCityData(Data, bCreateCollision);

	// Verkehrs-Simulation auf dem finalen Netz initialisieren (Dichte aus dem
	// City-Prompt). Hier, nicht in der Pipeline: die Daten liegen jetzt im
	// GameInstance und der Netz-Zeiger bleibt ueber die Session stabil.
	if (!Data.RoadNetwork.IsEmpty())
	{
		TrafficSimulation.Initialize(Data.RoadNetwork, Data.TrafficSettings);
		UE_LOG(LogWbCore, Log,
			TEXT("Verkehrs-Simulation initialisiert: Dichte %.2f, %.1f km Netz, %d Spuren."),
			Data.TrafficSettings.TrafficDensity,
			Data.RoadNetwork.GetTotalDrivableLengthKm(),
			Data.RoadNetwork.Lanes.Num());
	}

	// Wetterlage aus dem City-Prompt der Pipeline uebernehmen (wenn gesetzt).
	// Gleiche Lage ist ein No-Op; ein Wechsel blendet sanft ueber das Wetter-System.
	Weather.SetTargetWeather(Data.CityPromptSpec.Weather);

	// Start-Tageszeit aus dem Prompt (z. B. "abends" -> 19 Uhr), wenn gesetzt.
	// Ohne Angabe bleibt der Default (Settings.StartTimeOfDayHours = 9 Uhr).
	if (Data.CityPromptSpec.TimeOfDayHours >= 0.0f)
	{
		Weather.SetTimeOfDay(Data.CityPromptSpec.TimeOfDayHours);
	}

	CityStatus = FString::Printf(TEXT("Stadt gespawnt: %d Gebaeude, %d Segmente, %d Spuren."),
		Data.Buildings.Num(), Data.RoadNetwork.Segments.Num(), Data.RoadNetwork.Lanes.Num());

	UE_LOG(LogWbCore, Log, TEXT("%s"), *CityStatus);
}

void UWiesbadenCitySubsystem::SetWeatherTarget(ECityWeatherPreset NewWeather)
{
	Weather.SetTargetWeather(NewWeather);
}

void UWiesbadenCitySubsystem::EnsureStreamingSource()
{
	if (StreamingSource || !GetWorld())
	{
		return;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	StreamingSource = GetWorld()->SpawnActor<AWiesbadenStreamingSource>(
		AWiesbadenStreamingSource::StaticClass(), FTransform::Identity, Params);
}

void UWiesbadenCitySubsystem::UpdateStreamingState()
{
	if (!bWorldPartitionActive)
	{
		return;
	}

	UWorldPartitionSubsystem* WorldPartition = GetWorld() ? GetWorld()->GetSubsystem<UWorldPartitionSubsystem>() : nullptr;
	if (!WorldPartition)
	{
		return;
	}

	const bool bComplete = WorldPartition->IsStreamingCompleted();
	if (!bStreamingCheckedOnce || bComplete != bStreamingComplete)
	{
		bStreamingCheckedOnce = true;
		bStreamingComplete = bComplete;
		UE_LOG(LogWbCore, Log, TEXT("World Partition Streaming %s."),
			bComplete ? TEXT("abgeschlossen (alle Zellen geladen)") : TEXT("aktiv (Zellen werden geladen/entladen)"));
	}
}

void UWiesbadenCitySubsystem::ClearCity()
{
	if (CityActor)
	{
		CityActor->Destroy();
		CityActor = nullptr;
	}
	TrafficSimulation.Reset();
	bStreamingComplete = false;
	bStreamingCheckedOnce = false;
	CityStatus = TEXT("Stadt entfernt");
}

void UWiesbadenCitySubsystem::BroadcastState(const FString& Status, bool bSuccess)
{
	CityStatus = Status;
	LastCityError = bSuccess ? FString() : Status;
	OnCityStateChanged.Broadcast(Status, bSuccess);
}

TStatId UWiesbadenCitySubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenCitySubsystem, STATGROUP_Tickables);
}

bool UWiesbadenCitySubsystem::SetupJunctionTour(int32 JunctionCount)
{
	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!World || !PC)
	{
		return false;
	}

	// Kreuzungen aus dem gespeicherten Strassennetz des World-Builders.
	const FRoadNetwork* Network = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It)
	{
		if (!It->RoadNetwork.Intersections.IsEmpty())
		{
			Network = &It->RoadNetwork;
			break;
		}
	}

	if (!Network)
	{
		return false;
	}

	// Auswahl: die dem Spieler naechsten Kreuzungen. Weiter entfernte waeren
	// nicht geladen und ergaeben leere Bilder.
	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	PC->GetPlayerViewPoint(ViewLocation, ViewRotation);

	// Optionaler Zielort, damit sich eine bestimmte Strasse pruefen laesst.
	float AtX = 0.0f;
	float AtY = 0.0f;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbAtX="), AtX)
		&& FParse::Value(FCommandLine::Get(), TEXT("WbAtY="), AtY))
	{
		ViewLocation.X = AtX;
		ViewLocation.Y = AtY;
	}

	// Zielort ueber den STRASSENNAMEN: -WbAtStreet="Wolkenbruch"
	//
	// Ueber Koordinaten ist eine Meldung wie "bei Wolkenbruch stehen Objekte
	// auf der Fahrbahn" nicht nachzugehen - man muesste Laenge und Breite von
	// Hand umrechnen, und meine Naeherung lag um Hunderte Meter daneben. Der
	// Name steht dagegen an jedem Segment, mit exakten Weltkoordinaten.
	FString AtStreet;
	if (FParse::Value(FCommandLine::Get(), TEXT("WbAtStreet="), AtStreet) && !AtStreet.IsEmpty())
	{
		bool bFound = false;
		double BestLengthCm = 0.0;

		for (const FRoadSegment& Segment : Network->Segments)
		{
			if (!Segment.StreetName.Contains(AtStreet))
			{
				continue;
			}

			const TArray<FVector>& Line = Segment.Centerline.Num() >= 2
				? Segment.Centerline : Segment.TrimmedCenterline;
			if (Line.Num() < 2)
			{
				continue;
			}

			// Das LAENGSTE Segment des Namens: Bei Strassen, die es in mehreren
			// Stadtteilen gibt, ist das der Hauptzug.
			if (Segment.LengthCm > BestLengthCm)
			{
				BestLengthCm = Segment.LengthCm;
				ViewLocation.X = Line[Line.Num() / 2].X;
				ViewLocation.Y = Line[Line.Num() / 2].Y;
				bFound = true;
			}
		}

		UE_LOG(LogWbStreaming, Log,
			TEXT("Zielort \"%s\": %s bei (%.0f, %.0f), laengstes Segment %.0f m."),
			*AtStreet, bFound ? TEXT("gefunden") : TEXT("NICHT gefunden"),
			ViewLocation.X, ViewLocation.Y, BestLengthCm / 100.0);
	}

	// Den SPIELER mitversetzen, nicht nur die Kamera.
	//
	// World Partition streamt um die Streaming-Quelle, und das ist der Pawn.
	// Wird nur die Kamera versetzt, steht sie ueber ungeladenem Gebiet: leere
	// Wiese mit ein paar Laternen, weil Ausstattungs-Actors eigenstaendig
	// laden. Das sah aus wie "der Platz fehlt" und war "der Platz ist nicht
	// geladen" - zwei voellig verschiedene Aussagen.
	if (APawn* Pawn = PC->GetPawn())
	{
		const FVector Target(ViewLocation.X, ViewLocation.Y, Pawn->GetActorLocation().Z);
		if (!Pawn->GetActorLocation().Equals(Target, 100.0))
		{
			Pawn->SetActorLocation(Target, /*bSweep=*/false, nullptr,
				ETeleportType::TeleportPhysics);

			bTourTeleported = true;

			UE_LOG(LogWbStreaming, Log,
				TEXT("Spieler zum Zielort versetzt (%.0f, %.0f) - Streaming folgt ihm."),
				Target.X, Target.Y);
		}
	}

	TArray<TPair<double, int32>> ByDistance;
	ByDistance.Reserve(Network->Intersections.Num());
	for (int32 Index = 0; Index < Network->Intersections.Num(); ++Index)
	{
		const FRoadIntersection& Intersection = Network->Intersections[Index];
		if (Intersection.Arms.Num() < 3)
		{
			continue;   // Zweiarmige "Kreuzungen" sind nur Knicke.
		}
		ByDistance.Emplace(
			FVector2D::DistSquared(FVector2D(ViewLocation), FVector2D(Intersection.Location)),
			Index);
	}

	if (ByDistance.IsEmpty())
	{
		return false;
	}

	ByDistance.Sort([](const TPair<double, int32>& A, const TPair<double, int32>& B)
	{
		return A.Key < B.Key;
	});

	const int32 Count = FMath::Min(JunctionCount, ByDistance.Num());

	// Je Kreuzung: einmal senkrecht von oben, dann vier schraege Blicke.
	//
	// Der senkrechte Blick zeigt die Flaechen, die schraegen zeigen Kanten,
	// Hoehenversatz und Ueberlappungen - aus EINEM Winkel ist beides nie
	// gleichzeitig zu sehen.
	static const struct { double Bearing; const TCHAR* Name; } Views[] = {
		{   0.0, TEXT("Nord")  },
		{  90.0, TEXT("Ost")   },
		{ 180.0, TEXT("Sued")  },
		{ 270.0, TEXT("West")  },
	};

	constexpr double ObliqueDistanceCm = 2600.0;
	constexpr double ObliqueHeightCm = 1400.0;
	constexpr double TopHeightCm = 3200.0;

	TourPoses.Reset();
	for (int32 Rank = 0; Rank < Count; ++Rank)
	{
		const FRoadIntersection& Intersection = Network->Intersections[ByDistance[Rank].Value];
		const FVector Centre = Intersection.Location;

		FTourPose Top;
		Top.Location = Centre + FVector(0.0, 0.0, TopHeightCm);
		Top.Rotation = FRotator(-90.0f, 0.0f, 0.0f);
		Top.Label = FString::Printf(TEXT("K%02d_%d-armig_Oben"), Rank, Intersection.Arms.Num());
		TourPoses.Add(Top);

		for (const auto& View : Views)
		{
			const double Rad = FMath::DegreesToRadians(View.Bearing);
			const FVector Offset(
				FMath::Cos(Rad) * ObliqueDistanceCm,
				FMath::Sin(Rad) * ObliqueDistanceCm,
				ObliqueHeightCm);

			FTourPose Pose;
			Pose.Location = Centre + Offset;
			Pose.Rotation = (Centre - Pose.Location).Rotation();
			Pose.Label = FString::Printf(TEXT("K%02d_%s"), Rank, View.Name);
			TourPoses.Add(Pose);
		}
	}

	TourIndex = 0;

	// Nach einem Versetzen muss das STREAMING nachkommen.
	//
	// 1,5 Sekunden reichen fuer den Kamerawechsel, aber nicht dafuer, dass
	// World Partition die Geometrie am neuen Ort laedt. Das Bild zeigte
	// deshalb leere Wiese mit ein paar Laternen - Ausstattungs-Actors laden
	// eigenstaendig und waren schon da. Das sah aus wie "der Platz fehlt" und
	// hiess "der Platz ist noch nicht geladen".
	// Nach einem Sprung an einen anderen Ort dauert das Nachladen LANGE.
	//
	// Der erste Satz Chunks braucht ueber vier Minuten; ein neues Viertel
	// entsprechend Dutzende Sekunden. Mit 25 Sekunden zeigte das Bild am
	// Mauritiusplatz leere Wiese, obwohl Strassen und Gebaeude dort in den
	// Daten stehen und die Minikarte sie zeichnet.
	//
	// Einstellbar ueber -WbTourWarmup=<Sekunden>, damit sich das ohne
	// Neuuebersetzen anpassen laesst.
	float WarmupSeconds = 90.0f;
	FParse::Value(FCommandLine::Get(), TEXT("WbTourWarmup="), WarmupSeconds);

	TourDelay = bTourTeleported ? FMath::Max(WarmupSeconds, 1.0f) : 1.5f;

	if (bTourTeleported)
	{
		UE_LOG(LogWbStreaming, Log,
			TEXT("Rundgang wartet %.0f Sekunden auf das Nachladen."), TourDelay);
	}

	UE_LOG(LogWbStreaming, Log,
		TEXT("Kreuzungs-Rundgang: %d Kreuzungen, %d Ansichten."),
		Count, TourPoses.Num());

	return true;
}

void UWiesbadenCitySubsystem::TickJunctionTour(float DeltaSeconds)
{
	if (TourIndex == INDEX_NONE || !TourPoses.IsValidIndex(TourIndex))
	{
		return;
	}

	UWorld* World = GetWorld();
	APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!World || !PC)
	{
		TourIndex = INDEX_NONE;
		return;
	}

	TourDelay -= DeltaSeconds;
	if (TourDelay > 0.0f)
	{
		return;
	}

	const FTourPose& Pose = TourPoses[TourIndex];

	// Kamera einmal anlegen und danach nur noch versetzen.
	if (!TourCamera)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		TourCamera = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Pose.Location, Pose.Rotation, Params);

		if (!TourCamera)
		{
			TourIndex = INDEX_NONE;
			return;
		}
		PC->SetViewTarget(TourCamera);
	}

	TourCamera->SetActorLocationAndRotation(Pose.Location, Pose.Rotation);

	const FString Path = FPaths::ProjectSavedDir() / TEXT("Diagnose") / (FParse::Param(FCommandLine::Get(), TEXT("WbHideLandscape")) ? TEXT("Tour2") : TEXT("Tour")) / Pose.Label;
	FScreenshotRequest::RequestScreenshot(Path, false, true);

	UE_LOG(LogWbStreaming, Log, TEXT("Rundgang %d/%d: %s"),
		TourIndex + 1, TourPoses.Num(), *Pose.Label);

	++TourIndex;

	if (TourIndex >= TourPoses.Num())
	{
		TourIndex = INDEX_NONE;
		ScreenshotQuitDelay = 3.0f;
		return;
	}

	// Zwischen zwei Aufnahmen genug Zeit, damit das Bild wirklich steht -
	// sonst zeigt der Screenshot noch die vorige Pose.
	TourDelay = 0.55f;
}
