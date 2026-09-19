// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NPC/WiesbadenSylvia.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSylviaSceneAssetsTest,
	"WiesbadenReal.NPC.Sylvia.SceneAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSylviaSceneAssetsTest::RunTest(const FString& Parameters)
{
	const FString ExpectedMeshPath(
		TEXT("/Game/Assets/People/Sylvia/sylvia/SkeletalMeshes/tripo_part_0.tripo_part_0"));
	const FString ExpectedShavingsPath(
		TEXT("/Game/Niagara/NS_SylviaWoodShavings.NS_SylviaWoodShavings"));

	TestEqual(TEXT("Sylvia mesh path is the cooked scene contract"),
		FString(AWiesbadenSylvia::GetSylviaMeshPath()), ExpectedMeshPath);
	TestEqual(TEXT("Wood shavings path is the cooked scene contract"),
		FString(AWiesbadenSylvia::GetWoodShavingsSystemPath()), ExpectedShavingsPath);

	// These are the two runtime boundaries: the imported figure and the
	// looping shavings effect must both resolve from cooked project content.
	USkeletalMesh* SylviaMesh = LoadObject<USkeletalMesh>(nullptr, *ExpectedMeshPath);
	TestNotNull(TEXT("Sylvia skeletal mesh is imported at the runtime path"), SylviaMesh);
	TestEqual(TEXT("Sylvia exposes all imported GLB figure parts"),
		AWiesbadenSylvia::GetFigurePartCount(), 15);
	for (int32 PartIndex = 1; PartIndex < AWiesbadenSylvia::GetFigurePartCount(); ++PartIndex)
	{
		const FString PartPath = AWiesbadenSylvia::GetSylviaMeshPartPath(PartIndex) + TEXT(".") +
			FString::Printf(TEXT("tripo_part_%d"), PartIndex);
		TestNotNull(*FString::Printf(TEXT("Sylvia GLB part %d is present"), PartIndex),
			LoadObject<USkeletalMesh>(nullptr, *PartPath));
	}

	UNiagaraSystem* Shavings = LoadObject<UNiagaraSystem>(nullptr, *ExpectedShavingsPath);
	TestNotNull(TEXT("Sylvia wood shavings Niagara system is present"), Shavings);

	// Keep the player-facing string stable at the scene boundary; a missing
	// heart or a different spelling makes the world-space bubble fail its job.
	TestEqual(TEXT("Thought bubble text"),
		FString(AWiesbadenSylvia::GetThoughtBubbleText()),
		FString(TEXT("Ich denke an Sebbo <3")));

	const FGeoCoordinate Address = AWiesbadenSylvia::GetPlatterStrasse144Coordinate();
	TestTrue(TEXT("Platter Strasse 144 latitude is Wiesbaden"),
		FMath::IsNearlyEqual(Address.Latitude, 50.0932604, 0.0000001));
	TestTrue(TEXT("Platter Strasse 144 longitude is Wiesbaden"),
		FMath::IsNearlyEqual(Address.Longitude, 8.2234186, 0.0000001));

	return true;
}
