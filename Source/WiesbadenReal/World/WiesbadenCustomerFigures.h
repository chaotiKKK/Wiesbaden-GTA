// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "WiesbadenCustomerFigures.generated.h"

class UAnimSequence;
class USkeletalMesh;

/** Die Bewegungen jeder Kundenfigur (Tools/Blender/build_customer_figure.py). */
enum class ECustomerAnim : uint8 { Idle, Walk, Wave, Sit, Count };

/**
 * Eine geladene Kundenfigur: Skelett-Mesh und ihre vier Bewegungen, alle auf
 * demselben Skelett. Die Zeiger halten die Assets, solange der Halter lebt
 * (UPROPERTY im Laden bzw. im Kunden).
 */
USTRUCT()
struct WIESBADENREAL_API FWbCustomerFigure
{
	GENERATED_BODY()

	UPROPERTY(Transient) FString Name;
	UPROPERTY(Transient) TObjectPtr<USkeletalMesh> Mesh = nullptr;
	/** Index = ECustomerAnim. */
	UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Anims;

	/** Mesh da und jede Bewegung da, auf dem Skelett des Meshes. */
	bool IsComplete() const;
	UAnimSequence* Anim(ECustomerAnim Kind) const;
};

/**
 * Die Tripo-Figuren, die als Lieferkunden und Ladengaeste abwechseln.
 *
 * Jede Figur liegt unter /Game/Assets/People/Kunden/<Name> mit
 * Meshes/SK_<Name> und Animations/A_<Name>_{Idle,Walk,Wave,Sit}
 * (Tools/kunden_figuren.json -> Tools/Blender/build_customer_figure.py ->
 * Tools/import_tripo_figure.py). Das Spiel findet sie ueber die Asset-Registry:
 * eine neue Figur braucht keine C++-Aenderung. Alle teilen Schritttempo und
 * Bewegungslaengen, das Spiel behandelt sie gleich.
 */
namespace WiesbadenCustomerFigures
{
	/** Ordner aller Kundenfiguren. */
	WIESBADENREAL_API const TCHAR* RootPath();
	/** "Idle", "Walk", ... - der Namensteil in A_<Name>_<Bewegung>. */
	WIESBADENREAL_API const TCHAR* AnimName(ECustomerAnim Kind);
	/** Objektpfade fuer LoadObject. */
	WIESBADENREAL_API FString MeshPath(const FString& Name);
	WIESBADENREAL_API FString AnimPath(const FString& Name, ECustomerAnim Kind);

	/**
	 * Datenrein: Figurnamen aus den Paketnamen der Skelett-Meshes unter
	 * RootPath - nur, was genau <Root>/<Name>/Meshes/SK_<Name> heisst;
	 * sortiert, ohne Doppelte.
	 */
	WIESBADENREAL_API TArray<FString> NamesFromMeshPackages(const TArray<FString>& PackageNames);

	/** Vorhandene Figuren laut Asset-Registry, sortiert. */
	WIESBADENREAL_API TArray<FString> FindNames();

	/** Eine Figur laden (unvollstaendig: IsComplete() == false, mit Warnung). */
	WIESBADENREAL_API FWbCustomerFigure Load(const FString& Name);

	/** Alle vollstaendigen Figuren laden, sortiert nach Namen. */
	WIESBADENREAL_API TArray<FWbCustomerFigure> LoadAll();

	/**
	 * Datenrein: welche von Count Figuren als naechste auftritt.
	 * Avoid in Rangfolge: Avoid[0] ist die zuletzt gezeigte Figur, danach
	 * die, die gerade zu sehen sind (Gaeste im Laden). Gewaehlt wird unter
	 * den Figuren, die in Avoid nicht vorkommen; sind das keine, unter allen
	 * ausser Avoid[0] - dieselbe Figur zweimal hintereinander gibt es erst,
	 * wenn es nur eine gibt. Roll waehlt unter den Kandidaten.
	 * INDEX_NONE bei Count == 0.
	 */
	WIESBADENREAL_API int32 PickFigure(int32 Count, const TArray<int32>& Avoid, uint32 Roll);
}
