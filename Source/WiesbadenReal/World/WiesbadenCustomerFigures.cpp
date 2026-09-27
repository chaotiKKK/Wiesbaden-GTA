// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "World/WiesbadenCustomerFigures.h"

#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/SkeletalMesh.h"
#include "Modules/ModuleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbCustomerFigures, Log, All);

namespace
{
	const TCHAR* const AnimNames[static_cast<int32>(ECustomerAnim::Count)] = {
		TEXT("Idle"), TEXT("Walk"), TEXT("Wave"), TEXT("Sit") };
}

bool FWbCustomerFigure::IsComplete() const
{
	if (!Mesh || !Mesh->GetSkeleton() || Anims.Num() != static_cast<int32>(ECustomerAnim::Count))
	{
		return false;
	}
	for (const UAnimSequence* Seq : Anims)
	{
		if (!Seq || Seq->GetSkeleton() != Mesh->GetSkeleton())
		{
			return false;
		}
	}
	return true;
}

UAnimSequence* FWbCustomerFigure::Anim(ECustomerAnim Kind) const
{
	const int32 Index = static_cast<int32>(Kind);
	return Anims.IsValidIndex(Index) ? Anims[Index].Get() : nullptr;
}

namespace WiesbadenCustomerFigures
{
	const TCHAR* RootPath()
	{
		return TEXT("/Game/Assets/People/Kunden");
	}

	const TCHAR* AnimName(ECustomerAnim Kind)
	{
		const int32 Index = static_cast<int32>(Kind);
		return Index >= 0 && Index < static_cast<int32>(ECustomerAnim::Count) ? AnimNames[Index] : TEXT("?");
	}

	FString MeshPath(const FString& Name)
	{
		return FString::Printf(TEXT("%s/%s/Meshes/SK_%s.SK_%s"), RootPath(), *Name, *Name, *Name);
	}

	FString AnimPath(const FString& Name, ECustomerAnim Kind)
	{
		const TCHAR* Anim = AnimName(Kind);
		return FString::Printf(TEXT("%s/%s/Animations/A_%s_%s.A_%s_%s"), RootPath(), *Name, *Name, Anim, *Name, Anim);
	}

	TArray<FString> NamesFromMeshPackages(const TArray<FString>& PackageNames)
	{
		const FString Prefix = FString(RootPath()) + TEXT("/");
		TArray<FString> Names;
		for (const FString& Package : PackageNames)
		{
			if (!Package.StartsWith(Prefix))
			{
				continue;
			}
			// <Name>/Meshes/SK_<Name> - der Ordner und das Mesh muessen zusammenpassen.
			TArray<FString> Parts;
			Package.RightChop(Prefix.Len()).ParseIntoArray(Parts, TEXT("/"), /*CullEmpty=*/false);
			if (Parts.Num() == 3 && !Parts[0].IsEmpty() && Parts[1] == TEXT("Meshes")
				&& Parts[2].Equals(TEXT("SK_") + Parts[0], ESearchCase::CaseSensitive))
			{
				Names.AddUnique(Parts[0]);
			}
		}
		Names.Sort();
		return Names;
	}

	TArray<FString> FindNames()
	{
		IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
#if WITH_EDITOR
		// Ungekocht (Editor, -game aus dem Editor) ist die Registry beim Start
		// womoeglich noch beim Einlesen; der Kundenordner ist klein.
		Registry.ScanPathsSynchronous({ FString(RootPath()) }, /*bForceRescan=*/false);
#endif
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(RootPath()));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
		TArray<FAssetData> Found;
		Registry.GetAssets(Filter, Found);
		TArray<FString> Packages;
		for (const FAssetData& Data : Found)
		{
			Packages.Add(Data.PackageName.ToString());
		}
		return NamesFromMeshPackages(Packages);
	}

	FWbCustomerFigure Load(const FString& Name)
	{
		FWbCustomerFigure Figure;
		Figure.Name = Name;
		Figure.Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath(Name));
		for (int32 I = 0; I < static_cast<int32>(ECustomerAnim::Count); ++I)
		{
			Figure.Anims.Add(LoadObject<UAnimSequence>(nullptr, *AnimPath(Name, static_cast<ECustomerAnim>(I))));
		}
		if (!Figure.IsComplete())
		{
			UE_LOG(LogWbCustomerFigures, Warning,
				TEXT("Kundenfigur %s unvollstaendig (Mesh %d, Idle %d, Walk %d, Wave %d, Sit %d oder fremdes Skelett) - bleibt aussen vor."),
				*Name, Figure.Mesh != nullptr, Figure.Anims[0] != nullptr, Figure.Anims[1] != nullptr,
				Figure.Anims[2] != nullptr, Figure.Anims[3] != nullptr);
		}
		return Figure;
	}

	TArray<FWbCustomerFigure> LoadAll()
	{
		TArray<FWbCustomerFigure> Figures;
		FString Names;
		for (const FString& Name : FindNames())
		{
			FWbCustomerFigure Figure = Load(Name);
			if (Figure.IsComplete())
			{
				Names += (Names.IsEmpty() ? TEXT("") : TEXT(", ")) + Name;
				Figures.Add(MoveTemp(Figure));
			}
		}
		UE_LOG(LogWbCustomerFigures, Log, TEXT("%d Kundenfiguren: %s"), Figures.Num(),
			Names.IsEmpty() ? TEXT("keine") : *Names);
		return Figures;
	}

	int32 PickFigure(int32 Count, const TArray<int32>& Avoid, uint32 Roll)
	{
		if (Count <= 0)
		{
			return INDEX_NONE;
		}
		TArray<int32> Candidates;
		for (int32 I = 0; I < Count; ++I)
		{
			if (!Avoid.Contains(I))
			{
				Candidates.Add(I);
			}
		}
		if (Candidates.IsEmpty())
		{
			const int32 Last = Avoid.IsEmpty() ? INDEX_NONE : Avoid[0];
			for (int32 I = 0; I < Count; ++I)
			{
				if (I != Last)
				{
					Candidates.Add(I);
				}
			}
		}
		if (Candidates.IsEmpty())
		{
			return 0;   // nur eine Figur, und die war zuletzt dran
		}
		return Candidates[Roll % static_cast<uint32>(Candidates.Num())];
	}
}
