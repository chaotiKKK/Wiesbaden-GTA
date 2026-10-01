// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WiesbadenAudioZonesSubsystem.h"

#include "Audio/WiesbadenAudioPropagation.h"
#include "Audio/WiesbadenAudioSubsystem.h"
#include "Components/AudioComponent.h"
#include "Components/MeshComponent.h"
#include "Engine/HitResult.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "EngineUtils.h"
#include "GIS/WiesbadenWorldBuilder.h"
#include "World/WiesbadenCityChunk.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbZones, Log, All);

namespace
{
	/** Start des Materialstrahls ueber dem Fuss (cm). */
	constexpr float SurfaceTraceStartCm = 30.0f;

	/** Weg des Materialstrahls nach unten (cm). */
	constexpr float SurfaceTraceLengthCm = 200.0f;

	/** Oben fuer Anker-Kommentar: Kanael wie bei der Bodenabfrage der Figur. */
	constexpr ECollisionChannel SurfaceTraceChannel = ECC_WorldStatic;
}

void UWiesbadenAudioZonesSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UE_LOG(LogWbZones, Log, TEXT("Audio-Zonen-Subsystem bereit."));
}

void UWiesbadenAudioZonesSubsystem::Deinitialize()
{
	for (UAudioComponent* Component : StepPool)
	{
		if (Component)
		{
			WiesbadenAudioPropagation::UnregisterSource(Component);
			Component->DestroyComponent();
		}
	}
	IndustrialCells.Reset();
	CommercialCells.Reset();
	IndexedBuilder.Reset();
	IndexedBuildingCount = INDEX_NONE;
	StepPool.Reset();
	StepSounds.Reset();
	StepSoundSurfaces.Reset();
	PlayedFootsteps = 0;
	NextStepSlot = 0;
	bRigReady = false;
	Super::Deinitialize();
}

TStatId UWiesbadenAudioZonesSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UWiesbadenAudioZonesSubsystem, STATGROUP_Tickables);
}

void UWiesbadenAudioZonesSubsystem::Tick(float DeltaSeconds)
{
	// Die erste Tick-Gelegenheit kommt nach dem Pawn-Spawn (die World's
	// Subsysteme werden davor initialisiert). So existieren die Klangplaetze
	// automatisch, auch wenn kein Blueprint EnsureRig explizit aufruft.
	// Der Rest bleibt pollingfrei: der Tick prueft keine Flaechen und spielt
	// keine Schritte, sondern richtet nur die drei/vier Komponenten ein.
	if (!bRigReady)
	{
		EnsureRig();
	}
}

EWbAudioZone UWiesbadenAudioZonesSubsystem::ZoneAt(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World) { return EWbAudioZone::Residential; }
	int32 Trees = 0;
	int32 Industry = 0;
	int32 Commercial = 0;
	constexpr double RadiusSq = 12000.0 * 12000.0;
	for (TActorIterator<AWiesbadenCityChunk> It(World); It; ++It)
	{
		if (It->HasStreamingAnchor()
			&& FVector::DistSquared2D(It->GetStreamingAnchor(), Location) > FMath::Square(45000.0))
		{
			continue;
		}
		for (const FPlacedRegionAsset& Asset : It->GetRegionAssets())
		{
			if (FVector::DistSquared2D(Asset.Location, Location) > RadiusSq) { continue; }
			Trees += Asset.Category == ERegionAssetCategory::Tree ? 1 : 0;
			Industry += Asset.Category == ERegionAssetCategory::Industrial ? 1 : 0;
			if (Trees >= 12 && Industry > 0) { break; }
		}
	}
	AWiesbadenWorldBuilder* Builder = nullptr;
	for (TActorIterator<AWiesbadenWorldBuilder> It(World); It; ++It) { Builder = *It; break; }
	auto CellOf = [](const FVector2D& P) { return FIntPoint(FMath::FloorToInt(P.X / 12000.0), FMath::FloorToInt(P.Y / 12000.0)); };
	if (IndexedBuilder.Get() != Builder || IndexedBuildingCount != (Builder ? Builder->Buildings.Num() : 0))
	{
		IndustrialCells.Reset(); CommercialCells.Reset();
		IndexedBuilder = Builder;
		IndexedBuildingCount = Builder ? Builder->Buildings.Num() : 0;
		if (Builder)
		{
			for (const FGeneratedBuilding& Building : Builder->Buildings)
			{
				const FVector2D P = !Building.FootprintExtentCm.IsNearlyZero()
					? Building.FootprintCenterCm : FVector2D(Building.Bounds.GetCenter());
				if (Building.RegionType == ECityRegionType::Industrial) { IndustrialCells.FindOrAdd(CellOf(P)).Add(P); }
				if (Building.RegionType == ECityRegionType::Commercial) { CommercialCells.FindOrAdd(CellOf(P)).Add(P); }
			}
		}
	}
	const FVector2D Listener(Location);
	const FIntPoint Cell = CellOf(Listener);
	auto NearbyCount = [&](const TMap<FIntPoint, TArray<FVector2D>>& Cells, int32 Limit)
	{
		int32 Count = 0;
		for (int32 X = Cell.X - 1; X <= Cell.X + 1; ++X)
		{
			for (int32 Y = Cell.Y - 1; Y <= Cell.Y + 1; ++Y)
			{
				if (const TArray<FVector2D>* Points = Cells.Find(FIntPoint(X, Y)))
				{
					for (const FVector2D& P : *Points)
					{ if (FVector2D::DistSquared(P, Listener) <= RadiusSq && ++Count >= Limit) { return Count; } }
				}
			}
		}
		return Count;
	};
	Industry += NearbyCount(IndustrialCells, 1);
	Commercial += NearbyCount(CommercialCells, 3);
	const EWbAudioZone Zone = WiesbadenAudioZones::ClassifyZone(Trees, Industry, Commercial);
	if (Zone != LastReportedZone)
	{
		UE_LOG(LogWbZones, Log, TEXT("Audio-Zone: %s bei %s (Baeume %d, Industrie %d, Gewerbe %d)."),
			*WiesbadenAudioZones::ZoneName(Zone), *Location.ToString(), Trees, Industry, Commercial);
		LastReportedZone = Zone;
	}
	return Zone;
}

void UWiesbadenAudioZonesSubsystem::EnsureRig()
{
	if (bRigReady)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Dasselbe Muster wie UWiesbadenAmbienceSubsystem: die Welt tickt das
	// Subsystem automatisch, EnsureRig muss deshalb nicht von jedem
	// Konsumenten manuell aufgerufen werden.
	SetTickableTickType(ETickableTickType::Always);

	// A/B-Schalter im Stil von -WbLumen: -WbNoFootsteps laesst die Stadt auf
	// ihren Sohlen. Ohne ihn waere der Bildvergleich wertlos, weil zwei
	// verschiedene Laeufe ohnehin nicht vergleichbar sind (Verkehr, Wetter,
	// fremde Threads) - nur derselbe Build mit und ohne Schalter zaehlt.
	if (FParse::Param(FCommandLine::Get(), TEXT("WbNoFootsteps")))
	{
		bFootstepsEnabled = false;
		UE_LOG(LogWbZones, Log,
			TEXT("-WbNoFootsteps gesetzt: die Fussschritte bleiben aus."));
	}

	// Ein Stimmenplatz und ein geladener MetaSound je Oberflaeche. Fehlt ein
	// MetaSound (Import nie gelaufen), fehlen beide Eintraege - still, kein
	// Fehler. Genau die Konvention der bestehenden Audio-Subsysteme.
	for (uint8 Index = 0; Index < static_cast<uint8>(EWbFootstepSurface::MAX); ++Index)
	{
		const EWbFootstepSurface Surface = static_cast<EWbFootstepSurface>(Index);
		const FString AssetPath = FString::Printf(TEXT("/Game/Audio/Meta/MS_Step_%s"),
			*WiesbadenAudioZones::SurfaceName(Surface));

		USoundBase* Sound = LoadObject<USoundBase>(nullptr, *AssetPath);
		if (!Sound)
		{
			continue;
		}

		UAudioComponent* Component = NewObject<UAudioComponent>(this);
		if (!Component)
		{
			continue;
		}

		Component->bAutoActivate = false;
		Component->SoundClassOverride =
			UWiesbadenAudioSubsystem::LoadBusSoundClass(EWbAudioBus::SFX);

		// NICHT RegisterComponent(): das holt die Welt aus dem BESITZER-Actor
		// (Engine: UActorComponent::RegisterComponent -> ensure(MyOwnerWorld))
		// und ueberspringt die Registrierung, wenn der Outer kein Actor ist.
		// Hier ist der Outer dieses Subsystem - der Pool haengt an keiner
		// Figur, seine Position setzt jeder Schritt selbst (SetWorldLocation
		// in PlayFootstepAt). Ohne Registrierung haengt der Klang nicht am
		// Audio-Geraet. Deshalb ausdruecklich mit der Welt registrieren.
		// Gefunden am 29.09.2026 vom Pool-Test: solange die MS_Step_*-Klaenge
		// fehlten, entstand hier keine einzige Komponente und der Fehler blieb
		// unsichtbar (der Test verglich danach 0 mit 0).
		Component->RegisterComponentWithWorld(World);

		// ConfigureSource vergibt die Attenuation und registriert die Quelle
		// fuer den Hall-Umschalter. Near, weil ein Schritt an den Fuessen
		// eine kleine Quelle ist.
		WiesbadenAudioPropagation::ConfigureSource(Component, EWbAudioRange::Near, true);
		Component->SetSound(Sound);

		StepPool.Add(Component);
		StepSounds.Add(Sound);
		StepSoundSurfaces.Add(Surface);
	}

	bRigReady = true;

	if (StepPool.IsEmpty() && !bWarnedMissingBeds)
	{
		bWarnedMissingBeds = true;
		UE_LOG(LogWbZones, Log,
			TEXT("Keine Fussschritt-Assets gefunden (Tools/make_audio_assets.cmd). "
			     "Die Stadt bleibt bei ihren Schritten still - kein Fehler."));
	}

	UE_LOG(LogWbZones, Log, TEXT("Fussschritt-Pool: %d von %d Klangen geladen."),
		StepPool.Num(), static_cast<int32>(EWbFootstepSurface::MAX));
}

EWbFootstepSurface UWiesbadenAudioZonesSubsystem::SurfaceUnderFoot(const FVector& Location)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return EWbFootstepSurface::Pflaster;
	}

	// Strahl knapp ueber dem Fuss nach unten. Bewusst NICHT der Weg, den
	// FollowGround geht (dort 2 m ueber der Mitte, weil die Figur sonst unter
	// Decken stand): der Schritt soll am Fuss klingen, nicht auf Kniehoehe.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(WbFootstepSurface), true);
	const FVector Start = Location + FVector(0.0, 0.0, SurfaceTraceStartCm);
	const FVector End = Start - FVector(0.0, 0.0, SurfaceTraceLengthCm);

	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, Start, End, SurfaceTraceChannel, Params))
	{
		return EWbFootstepSurface::Pflaster;
	}

	// Der Material-Slot steht in Hit.Item (FHitResult::Item, int32).
	// Hit.BoneItem waere der Knochen-Index eines Skeletts und liefert hier
	// Unsinn - genau die Verwechslung, die im Plan stand.
	UMeshComponent* Mesh = Cast<UMeshComponent>(Hit.GetComponent());
	if (!Mesh)
	{
		return EWbFootstepSurface::Pflaster;
	}

	UMaterialInterface* Material = Mesh->GetMaterial(Hit.Item);
	if (!Material)
	{
		return EWbFootstepSurface::Pflaster;
	}

	return WiesbadenAudioZones::SurfaceFromMaterialName(Material->GetName());
}

bool UWiesbadenAudioZonesSubsystem::PlayFootstepAt(
	const FVector& Location, EWbFootstepSurface Surface)
{
	if (!bFootstepsEnabled || !bRigReady || StepPool.IsEmpty()
		|| StepPool.Num() != StepSounds.Num()
		|| StepSounds.Num() != StepSoundSurfaces.Num())
	{
		return false;
	}

	// Ungueltige Enum-Werte folgen wie Name und Bandpass dem gangbaren
	// Pflaster-Default - niemals einen Klang einer anderen Oberflaeche nehmen.
	if (static_cast<uint8>(Surface) >= static_cast<uint8>(EWbFootstepSurface::MAX))
	{
		Surface = EWbFootstepSurface::Pflaster;
	}

	// Klang unabhaengig von der gewaehlten Stimme aufloesen. Ein fehlendes
	// Asset bleibt still; es faellt nicht auf den naechsten Oberflaechenklang.
	const int32 SoundIndex = WiesbadenAudioZones::FindStepSoundIndex(
		StepSoundSurfaces, Surface);
	if (!StepSounds.IsValidIndex(SoundIndex) || !StepSounds[SoundIndex])
	{
		return false;
	}

	// Der feste globale Ringer bleibt erhalten: jeder Schritt bekommt den
	// naechsten Stimmenplatz, aber dessen Klang wird vor dem Abspielen exakt
	// auf die angefragte Oberflaeche gesetzt.
	const int32 PlayedSlot = NextStepSlot;
	UAudioComponent* Component = StepPool[PlayedSlot];
	NextStepSlot = (NextStepSlot + 1) % StepPool.Num();
	if (!Component)
	{
		return false;
	}

	Component->Stop();
	Component->SetWorldLocation(Location);
	Component->SetSound(StepSounds[SoundIndex]);
	Component->Play();

	++PlayedFootsteps;

	// Laufbeleg nach Saved/Logs (NIE stdout - das verschluckt Projektzeilen).
	// Auf Log-Ebene, aber gedrosselt: ein dichter Passantenstrom loest leicht
	// Dutzende Schritte je Sekunde aus, und eine Zeile je Schritt begraebt
	// genau das Log, in dem der Beleg stehen soll. Die ersten Schritte und
	// dann jeder 50. zeigen lueckenlos, dass gespielt wird - bei Verbose
	// waere die Zeile dagegen unsichtbar und der Lauf saehe stumm aus.
	if (PlayedFootsteps <= 10 || PlayedFootsteps % 50 == 0)
	{
		UE_LOG(LogWbZones, Log,
			TEXT("Fussschritt #%d: %s auf %s, Poolplatz %d/%d."),
			PlayedFootsteps,
			*WiesbadenAudioZones::SurfaceName(Surface),
			*Location.ToCompactString(),
			PlayedSlot,
			StepPool.Num());
	}

	return true;
}
