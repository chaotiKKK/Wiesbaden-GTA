// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "CoreMinimal.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "NiagaraSystem.h"
#include "NPC/WiesbadenSylvia.h"
#include "UObject/UObjectGlobals.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSylviaSceneAssetsTest,
	"WiesbadenReal.NPC.Sylvia.SceneAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FSylviaSceneAssetsTest::RunTest(const FString& /*Parameters*/)
{
	struct FAssetContract
	{
		const TCHAR* Label;
		const TCHAR* Path;
		UClass* Type;
	};

	// These are the runtime boundaries: every referenced asset must resolve
	// from cooked content, not merely match a copied path literal.
	const FAssetContract AssetContracts[] = {
		{TEXT("Sylvia skeletal mesh"), AWiesbadenSylvia::GetSylviaMeshPath(), USkeletalMesh::StaticClass()},
		{TEXT("Sylvia wood shavings"), AWiesbadenSylvia::GetWoodShavingsSystemPath(), UNiagaraSystem::StaticClass()},
	};
	for (const FAssetContract& Contract : AssetContracts)
	{
		TestNotNull(*FString::Printf(TEXT("%s resolves from cooked content"), Contract.Label),
			StaticLoadObject(Contract.Type, nullptr, Contract.Path));
	}

	TestEqual(TEXT("Part zero aliases the root mesh path"),
		AWiesbadenSylvia::GetSylviaMeshPartPath(0), FString(AWiesbadenSylvia::GetSylviaMeshPath()));

	const int32 FigurePartCount = AWiesbadenSylvia::GetFigurePartCount();
	TestEqual(TEXT("Imported GLB figure part count"), FigurePartCount, 15);
	for (int32 PartIndex = 1; PartIndex < FigurePartCount; ++PartIndex)
	{
		TestNotNull(*FString::Printf(TEXT("Sylvia GLB part %d resolves"), PartIndex),
			LoadObject<USkeletalMesh>(nullptr, *AWiesbadenSylvia::GetSylviaMeshPartPath(PartIndex)));
	}

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
