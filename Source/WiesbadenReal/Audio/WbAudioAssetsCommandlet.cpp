// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Audio/WbAudioAssetsCommandlet.h"

#include "Audio/WiesbadenAudioPropagation.h"
#include "HAL/FileManager.h"
#include "MetasoundBuilderSubsystem.h"
#include "MetasoundFrontendDocument.h"
#include "MetasoundSource.h"
#include "MetasoundStandardNodesNames.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundConcurrency.h"
#include "Sound/SoundSubmix.h"
#include "SubmixEffects/AudioMixerSubmixEffectReverb.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogWbAudioAssets, Log, All);

namespace
{
	const FName GFloatType(TEXT("float"));

	/** Asset anlegen (oder vorhandenes laden) und Paket zum Speichern merken. */
	template <typename T>
	T* MakeAsset(const FString& LongPackageName, const FName AssetName, TArray<UPackage*>& OutPackages)
	{
		UPackage* Package = CreatePackage(*LongPackageName);
		if (!Package)
		{
			return nullptr;
		}
		Package->FullyLoad();
		T* Asset = FindObject<T>(Package, *AssetName.ToString());
		if (!Asset)
		{
			Asset = NewObject<T>(Package, AssetName, RF_Public | RF_Standalone);
		}
		if (Asset)
		{
			OutPackages.AddUnique(Package);
		}
		return Asset;
	}

	/** Alle gesammelten Pakete auf Platte. */
	int32 SaveAll(const TArray<UPackage*>& Packages)
	{
		int32 Saved = 0;
		for (UPackage* Package : Packages)
		{
			const FString Filename = FPackageName::LongPackageNameToFilename(
				Package->GetName(), FPackageName::GetAssetPackageExtension());
			IFileManager::Get().MakeDirectory(*FPaths::GetPath(Filename), true);
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (UPackage::SavePackage(Package, nullptr, *Filename, SaveArgs))
			{
				++Saved;
			}
			else
			{
				UE_LOG(LogWbAudioAssets, Error, TEXT("Speichern fehlgeschlagen: %s"), *Filename);
			}
		}
		return Saved;
	}

	void ApplyTuning(USubmixEffectReverbPreset* Preset, EWbReverbSpace Space)
	{
		const FWbReverbTuning Tuning = WiesbadenAudioPropagation::ReverbTuningForSpace(Space);
		FSubmixEffectReverbSettings Settings;
		Settings.bBypass = Tuning.bBypass;
		Settings.bBypassEarlyReflections = Tuning.bBypassEarlyReflections;
		Settings.bBypassLateReflections = Tuning.bBypassLateReflections;
		Settings.ReflectionsDelay = Tuning.ReflectionsDelay;
		Settings.GainHF = Tuning.GainHF;
		Settings.ReflectionsGain = Tuning.ReflectionsGain;
		Settings.LateDelay = Tuning.LateDelay;
		Settings.DecayTime = Tuning.DecayTime;
		Settings.Density = Tuning.Density;
		Settings.Diffusion = Tuning.Diffusion;
		Settings.AirAbsorptionGainHF = Tuning.AirAbsorptionGainHF;
		Settings.DecayHFRatio = Tuning.DecayHFRatio;
		Settings.LateGain = Tuning.LateGain;
		Preset->SetSettings(Settings);
	}

	/** Distanzkurven, Hall-Submix/Preset und Concurrency. */
	void BuildPropagationAssets(TArray<UPackage*>& Packages)
	{
		struct FAttSpec { const TCHAR* Name; float FalloffCm; };
		const FAttSpec Specs[] = {
			{ TEXT("ATT_Near"), 3000.0f },
			{ TEXT("ATT_Mid"), 12000.0f },
			{ TEXT("ATT_Far"), 25000.0f },
		};

		for (const FAttSpec& Spec : Specs)
		{
			USoundAttenuation* Att = MakeAsset<USoundAttenuation>(
				FString::Printf(TEXT("/Game/Audio/Mix/%s"), Spec.Name), FName(Spec.Name), Packages);
			if (!Att)
			{
				continue;
			}

			// Distanzkurve + Entfernungs-Tiefpass + Occlusion: der ganze
			// Ausbreitungsteil steckt im Asset, nicht im Quell-Code.
			FSoundAttenuationSettings& Settings = Att->Attenuation;
			Settings.bAttenuate = true;
			Settings.bSpatialize = true;
			Settings.AttenuationShape = EAttenuationShape::Sphere;
			Settings.FalloffDistance = Spec.FalloffCm;
			Settings.bAttenuateWithLPF = true;
			Settings.bEnableOcclusion = true;
			Settings.OcclusionTraceChannel = ECC_Visibility;
			Settings.OcclusionLowPassFilterFrequency = 800.0f;
			Settings.OcclusionInterpolationTime = 0.2f;
		}

		USoundSubmix* Reverb = MakeAsset<USoundSubmix>(
			TEXT("/Game/Audio/Mix/SBX_Reverb"), FName(TEXT("SBX_Reverb")), Packages);
		USubmixEffectReverbPreset* Preset = MakeAsset<USubmixEffectReverbPreset>(
			TEXT("/Game/Audio/Mix/SFXP_Reverb"), FName(TEXT("SFXP_Reverb")), Packages);
		if (Reverb && Preset)
		{
			// Effekt in die Kette des Submixes; die Raumklasse schaltet zur
			// Laufzeit nur noch die Preset-Parameter um.
			Reverb->SubmixEffectChain.AddUnique(Preset);
			ApplyTuning(Preset, EWbReverbSpace::Indoor);
		}

		USoundConcurrency* Concurrency = MakeAsset<USoundConcurrency>(
			TEXT("/Game/Audio/Mix/CON_WbSfx"), FName(TEXT("CON_WbSfx")), Packages);
		if (Concurrency)
		{
			Concurrency->Concurrency.MaxCount = 4;
		}
	}
}

namespace
{
	// -- MetaSound-Kleinkram ---------------------------------------------
	//
	// Kleine Huelle um den UMetaSoundSourceBuilder: jedes Node und jeder Pin
	// wird beim Verbinden geprueft - ein Tippfehler im Pin-Namen laeuft nicht
	// still durch, sondern meldet sich hier.

	struct FGraph
	{
		UMetaSoundSourceBuilder* Builder = nullptr;
		FMetaSoundBuilderNodeInputHandle AudioOut;
		bool bOk = true;

		FMetaSoundNodeHandle Node(FName Name, FName Variant)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			const FMetaSoundNodeHandle Handle = Builder->AddNodeByClassName(
				FMetasoundFrontendClassName(Metasound::StandardNodes::Namespace, Name, Variant), Result, 1);
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Baustein %s (Variante '%s') fehlt."), *Name.ToString(), *Variant.ToString());
				bOk = false;
			}
			return Handle;
		}

		void Wire(const FMetaSoundNodeHandle& From, const TCHAR* OutPin,
			const FMetaSoundNodeHandle& To, const TCHAR* InPin)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			Builder->ConnectNodes(From, FName(OutPin), To, FName(InPin), Result);
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Kante %s -> %s fehlgeschlagen."), OutPin, InPin);
				bOk = false;
			}
		}

		void Input(const TCHAR* GraphName, const FMetaSoundNodeHandle& To,
			const TCHAR* InPin, float Default, bool bConstructor = false)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			const FName Name(GraphName);
			FName Type = GFloatType;
			const FMetasoundFrontendLiteral Literal =
				UMetaSoundBuilderSubsystem::GetChecked().CreateFloatMetaSoundLiteral(Default, Type);
			Builder->AddGraphInputNode(Name, GFloatType, Literal, Result, bConstructor);
			if (Result == EMetaSoundBuilderResult::Succeeded)
			{
				Builder->ConnectGraphInputToNode(Name, To, FName(InPin), Result);
			}
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Graph-Eingabe %s -> %s fehlgeschlagen."), GraphName, InPin);
				bOk = false;
			}
		}

		void ToAudioOut(const FMetaSoundNodeHandle& From, const TCHAR* OutPin)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			const FMetaSoundBuilderNodeOutputHandle Handle =
				Builder->FindNodeOutputByName(From, FName(OutPin), Result);
			if (Result == EMetaSoundBuilderResult::Succeeded)
			{
				Builder->ConnectNodes(Handle, AudioOut, Result);
			}
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error, TEXT("Ausgang %s fehlgeschlagen."), OutPin);
				bOk = false;
			}
		}

		UMetaSoundSource* Finish(const FString& PackageName, const FString& AssetName,
			TArray<UPackage*>& Packages)
		{
			if (!bOk)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("%s nicht gebaut (siehe Fehler oben)."), *AssetName);
				return nullptr;
			}
			UMetaSoundSource* Source = MakeAsset<UMetaSoundSource>(
				PackageName, FName(*AssetName), Packages);
			if (!Source)
			{
				return nullptr;
			}
#if WITH_EDITORONLY_DATA
			// BuildAndOverwriteMetaSound verweigert serialisierte Assets (IsAsset()).
			// Der Edit-Time-Pfad - baugleich zu UMetaSoundEditorSubsystem::BuildToAsset -
			// ist Build(Options) mit gesetztem ExistingMetaSound. Das Dokument landet in
			// der UPROPERTY RootMetasoundDocument und wird ueber SaveAll persistiert.
			Builder->InitNodeLocations();
			FMetaSoundBuilderOptions Options;
			Options.Name = FName(*AssetName);
			Options.bForceUniqueClassName = true;
			Options.bAddToRegistry = true;
			Options.ExistingMetaSound =
				TScriptInterface<IMetaSoundDocumentInterface>(Source);
			if (!Builder->Build(Options).GetObject())
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Build fuer %s fehlgeschlagen."), *AssetName);
				return nullptr;
			}
#else
			Builder->BuildAndOverwriteMetaSound(Source, false);
#endif
			return Source;
		}
	};

	bool StartGraph(FGraph& OutGraph, const FString& AssetName)
	{
		EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
		FMetaSoundBuilderNodeOutputHandle OnPlay;
		FMetaSoundBuilderNodeInputHandle OnFinished;
		TArray<FMetaSoundBuilderNodeInputHandle> AudioOuts;
		UMetaSoundSourceBuilder* Builder = UMetaSoundBuilderSubsystem::GetChecked().CreateSourceBuilder(
			FName(*AssetName), OnPlay, OnFinished, AudioOuts, Result,
			EMetaSoundOutputAudioFormat::Mono, false);
		if (!Builder || Result != EMetaSoundBuilderResult::Succeeded || AudioOuts.IsEmpty())
		{
			UE_LOG(LogWbAudioAssets, Error,
				TEXT("Source-Geruest fuer %s fehlgeschlagen."), *AssetName);
			return false;
		}
		OutGraph.Builder = Builder;
		OutGraph.AudioOut = AudioOuts[0];
		return true;
	}

	/** Ambience-Bett: Rauschen ueber einen Filter (Bandlage je Bett). */
	void BuildBed(TArray<UPackage*>& Packages, const TCHAR* BedName,
		const TCHAR* FilterName, float CutoffHz)
	{
		const FString AssetName = FString::Printf(TEXT("MS_Amb%s"), BedName);
		FGraph Graph;
		if (!StartGraph(Graph, AssetName))
		{
			return;
		}

		const FMetaSoundNodeHandle Noise =
			Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
		const FMetaSoundNodeHandle Filter =
			Graph.Node(FName(FilterName), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(Noise, TEXT("Audio"), Filter, TEXT("In"));
		Graph.Input(TEXT("Cutoff Hz"), Filter, TEXT("Cutoff Frequency"), CutoffHz, true);
		Graph.ToAudioOut(Filter, TEXT("Out"));

		Graph.Finish(FString::Printf(TEXT("/Game/Audio/Meta/%s"), *AssetName),
			AssetName, Packages);
	}

	void BuildAmbienceBeds(TArray<UPackage*>& Packages)
	{
		// Wind: weiches Rauschen (Tiefpass). Stadtsummen: sehr dunkel.
		// Roomtone: fast nur Grundrauschen. Voegel/Nacht: hohe Baender.
		BuildBed(Packages, TEXT("Wind"), TEXT("One-Pole Low Pass Filter"), 320.0f);
		BuildBed(Packages, TEXT("City"), TEXT("One-Pole Low Pass Filter"), 140.0f);
		BuildBed(Packages, TEXT("Room"), TEXT("One-Pole Low Pass Filter"), 90.0f);
		BuildBed(Packages, TEXT("Birds"), TEXT("One-Pole High Pass Filter"), 1800.0f);
		BuildBed(Packages, TEXT("Night"), TEXT("One-Pole High Pass Filter"), 3800.0f);
	}

	/**
	 * Fahrzeug-Layer fuer den Boxer: Zuendpuls (Sine ueber Rpm/30),
	 * Ansaugung (Rauschen nach Last), Auspuff (dunkles Rauschen), Rollen
	 * (helles Rauschen) und Hupe (65 Hz) - als GETRENNTE Layer gemischt,
	 * nicht als eine Welle. Die Parameter sind exakt die Paare aus
	 * WiesbadenAudioPropagation::EngineParamPairs.
	 *
	 * Mix-Abstimmung 24.09.2026 (gerenderte Hoerprobe,
	 * Tools/render_engineboxer.py + .planning/audio-overhaul/mix/): die
	 * Rausch-Layer lagen 25-31 dB unter dem Zuendpuls - der Mix war faktisch
	 * ein reiner Sinus mit Rauschteppich. Jetzt liegen sie 12-16 dB darunter
	 * (Leerlauf/Teillast) und die Ansaugung steigt mit dem Gas auf ~6 dB an.
	 */
	void BuildEngineSource(TArray<UPackage*>& Packages)
	{
		FGraph Graph;
		if (!StartGraph(Graph, TEXT("MS_EngineBoxer")))
		{
			return;
		}

		// Zuendpuls: Frequenz = Rpm * RevPerRpm (4-Zylinder-Viertakter: /30).
		const FMetaSoundNodeHandle HzScale = Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Input(TEXT("Rpm"), HzScale, TEXT("PrimaryOperand"), 900.0f);
		Graph.Input(TEXT("RevPerRpm"), HzScale, TEXT("AdditionalOperands"), 0.0333f, true);
		const FMetaSoundNodeHandle Sine =
			Graph.Node(FName(TEXT("Sine")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(HzScale, TEXT("Out"), Sine, TEXT("Frequency"));
		const FMetaSoundNodeHandle Firing =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(Sine, TEXT("Audio"), Firing, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("FiringGain"), Firing, TEXT("AdditionalOperands"), 0.42f, true);

		// Ansaugung: gefiltertes Rauschen, skaliert mit dem Gaspedal.
		const FMetaSoundNodeHandle NoiseIntake =
			Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
		const FMetaSoundNodeHandle LpIntake =
			Graph.Node(FName(TEXT("One-Pole Low Pass Filter")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(NoiseIntake, TEXT("Audio"), LpIntake, TEXT("In"));
		Graph.Input(TEXT("Intake Cutoff Hz"), LpIntake, TEXT("Cutoff Frequency"), 1000.0f, true);
		// Fester Layer-Gain VOR der Lastregelung - die Ansaugung war der einzige
		// Layer ohne Balance-Wert und damit im Gehoer praktisch nicht vorhanden.
		const FMetaSoundNodeHandle IntakeGain =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(LpIntake, TEXT("Out"), IntakeGain, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Intake Gain"), IntakeGain, TEXT("AdditionalOperands"), 4.0f, true);
		const FMetaSoundNodeHandle Intake =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(IntakeGain, TEXT("Out"), Intake, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Throttle"), Intake, TEXT("AdditionalOperands"), 0.3f);

		// Auspuff: dunkles Rauschen, konstante Lautstaerke.
		const FMetaSoundNodeHandle NoiseExhaust =
			Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
		const FMetaSoundNodeHandle LpExhaust =
			Graph.Node(FName(TEXT("One-Pole Low Pass Filter")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(NoiseExhaust, TEXT("Audio"), LpExhaust, TEXT("In"));
		Graph.Input(TEXT("Exhaust Cutoff Hz"), LpExhaust, TEXT("Cutoff Frequency"), 230.0f, true);
		const FMetaSoundNodeHandle Exhaust =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(LpExhaust, TEXT("Out"), Exhaust, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Exhaust Gain"), Exhaust, TEXT("AdditionalOperands"), 1.8f, true);

		// Rollen: helles Rauschen, konstant (Tempo-Gain bleibt die offene Naht).
		const FMetaSoundNodeHandle NoiseRoll =
			Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
		const FMetaSoundNodeHandle LpRoll =
			Graph.Node(FName(TEXT("One-Pole Low Pass Filter")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(NoiseRoll, TEXT("Audio"), LpRoll, TEXT("In"));
		Graph.Input(TEXT("Roll Cutoff Hz"), LpRoll, TEXT("Cutoff Frequency"), 650.0f, true);
		const FMetaSoundNodeHandle Roll =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(LpRoll, TEXT("Out"), Roll, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Roll Gain"), Roll, TEXT("AdditionalOperands"), 1.2f, true);

		// Hupe: 65 Hz (Nebelhorn-Grundton), Tor ueber den Horn-Parameter.
		const FMetaSoundNodeHandle HornSine =
			Graph.Node(FName(TEXT("Sine")), Metasound::StandardNodes::AudioVariant);
		Graph.Input(TEXT("Horn Hz"), HornSine, TEXT("Frequency"), 65.0f, true);
		const FMetaSoundNodeHandle Horn =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(HornSine, TEXT("Audio"), Horn, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Horn"), Horn, TEXT("AdditionalOperands"), 0.0f);

		// Layer-Mix und Lauf-Tor.
		const FMetaSoundNodeHandle Mix =
			Graph.Node(FName(TEXT("Add")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(Firing, TEXT("Out"), Mix, TEXT("PrimaryOperand"));
		Graph.Wire(Intake, TEXT("Out"), Mix, TEXT("AdditionalOperands"));
		Graph.Wire(Exhaust, TEXT("Out"), Mix, TEXT("AdditionalOperands"));
		Graph.Wire(Roll, TEXT("Out"), Mix, TEXT("AdditionalOperands"));
		Graph.Wire(Horn, TEXT("Out"), Mix, TEXT("AdditionalOperands"));
		const FMetaSoundNodeHandle Gate =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(Mix, TEXT("Out"), Gate, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("EngineRunning"), Gate, TEXT("AdditionalOperands"), 0.0f);
		Graph.ToAudioOut(Gate, TEXT("Out"));

		Graph.Finish(TEXT("/Game/Audio/Meta/MS_EngineBoxer"),
			TEXT("MS_EngineBoxer"), Packages);
	}
}

int32 UWbAudioAssetsCommandlet::Main(const FString& Params)
{
	TArray<UPackage*> Packages;
	BuildPropagationAssets(Packages);
	BuildAmbienceBeds(Packages);
	BuildEngineSource(Packages);

	const int32 Saved = SaveAll(Packages);
	UE_LOG(LogWbAudioAssets, Display,
		TEXT("WbAudioAssets fertig: %d/%d Pakete gespeichert."), Saved, Packages.Num());
	return (Saved == Packages.Num()) ? 0 : 1;
}
