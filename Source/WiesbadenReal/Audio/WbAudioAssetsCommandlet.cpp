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

		FMetaSoundNodeHandle Node(FName Name, FName Variant,
			FName InNamespace = Metasound::StandardNodes::Namespace)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			const FMetaSoundNodeHandle Handle = Builder->AddNodeByClassName(
				FMetasoundFrontendClassName(InNamespace, Name, Variant), Result, 1);
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

		/** Bestehende Graph-Eingabe an einen zweiten Node-Eingang (Throttle, SpeedKmh). */
		void Link(const TCHAR* GraphName, const FMetaSoundNodeHandle& To, const TCHAR* InPin)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			Builder->ConnectGraphInputToNode(FName(GraphName), To, FName(InPin), Result);
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Graph-Eingabe %s -> %s (zweite Leitung) fehlgeschlagen."), GraphName, InPin);
				bOk = false;
			}
		}

		/**
		 * Konstanten-Default direkt am Node-Eingang - fuer Pins, die keine
		 * eigene Graph-Eingabe bekommen (Float-Arrays, Enums). Ein Tippfehler
		 * im Pin-Namen meldet sich auch hier.
		 */
		void Default(const FMetaSoundNodeHandle& To, const TCHAR* InPin,
			const TArray<float>& Value)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			Builder->SetNodeInputDefault(To, FName(InPin), Value, Result);
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Array-Konstante an %s fehlgeschlagen."), InPin);
				bOk = false;
			}
		}

		void Default(const FMetaSoundNodeHandle& To, const TCHAR* InPin, int32 Value)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			Builder->SetNodeInputDefault(To, FName(InPin), Value, Result);
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Enum-Konstante %d an %s fehlgeschlagen."), Value, InPin);
				bOk = false;
			}
		}

		void Default(const FMetaSoundNodeHandle& To, const TCHAR* InPin, float Value)
		{
			EMetaSoundBuilderResult Result = EMetaSoundBuilderResult::Succeeded;
			Builder->SetNodeInputDefault(To, FName(InPin), Value, Result);
			if (Result != EMetaSoundBuilderResult::Succeeded)
			{
				UE_LOG(LogWbAudioAssets, Error,
					TEXT("Float-Konstante %f an %s fehlgeschlagen."), Value, InPin);
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
	 * Fahrzeug-Layer fuer den Boxer: Zuendpuls (gewichtetes Oberton-Gemisch
	 * ueber Rpm/30), Ansaugung (Rauschen nach Last), Auspuff (dunkles
	 * Rauschen), Rollen (helles Rauschen) und Nebelhorn (65+98 Hz mit
	 * Oberton-Staeben wie in der Referenz) - als GETRENNTE
	 * Layer gemischt, nicht als eine Welle. Die Parameter sind exakt die
	 * Paare aus WiesbadenAudioPropagation::EngineParamPairs.
	 *
	 * Obertoene 24.09.2026: der Zuendpuls ist kein reiner Sinus mehr, sondern
	 * das Oberwell-Gemisch aus WiesbadenEngineAudio.cpp (sechs gewichtete
	 * Harmonische, Boxer-Versatz auf halber Zuednfrequenz, weiche
	 * tanh-Saettigung). Ein reiner Sinus klingt nach Turbine; fuer kleine
	 * Lautsprecher entsteht der Koerper erst aus den Obertoenen.
	 *
	 * Mix-Abstimmung 24.09.2026 (gerenderte Hoerprobe,
	 * Tools/render_engineboxer.py + .planning/audio-overhaul/mix/): die
	 * Rausch-Layer lagen 25-31 dB unter dem Zuendpuls - der Mix war faktisch
	 * ein reiner Sinus mit Rauschteppich. Jetzt liegen sie 12-16 dB darunter
	 * (Leerlauf/Teillast) und die Ansaugung steigt mit dem Gas auf ~6 dB an.
	 *
	 * Fahrdynamik 25.09.2026: die letzten zwei offenen Naehte sind zu -
	 * Rollen skaliert mit SpeedKmh (Gain voll ab 50 km/h, Tiefpass
	 * 390 + 5.2 * km/h) und der Zuendpuls traegt einen Last-Gain
	 * (0.62 + 0.76 * Throttle). Beide Formeln sind auf die freigegebene
	 * Endabnahme geeicht: bei halbem Gas und 50 km/h bleibt der Faktor
	 * exakt 1.0 bzw. die alten 650 Hz.
	 *
	 * Last-Verschiebung 25.09.2026: zusaetzlich dreht die Faerbung des
	 * Zuendpulses mit dem Gas (Delta-Obertonstaeb, Ueberlagerung mit
	 * 2 * Throttle - 1; bei halbem Gas exakt 0 -> Endabnahme unveraendert).
	 * Ebenso oeffnet der Ansaug-Tiefpass mit dem Gas (312.5 + 1375 *
	 * Throttle Hz, wie die Referenz-Formel 0.05 + 0.22 * Throttle) -
	 * bei halbem Gas exakt die alten 1000 Hz.
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

		// Oberwell-Gemisch wie in WiesbadenEngineAudio.cpp: sechs gewichtete
		// Harmonische ueber der Zuednfrequenz. Der Additive-Synth-Node
		// summiert Sinusoiden auf Vielfachen {1..6} der Grundfrequenz; die
		// Gewichte {1.00, 0.62, 0.38, 0.22, 0.13, 0.07} sind durch ihre Summe
		// 2.42 geteilt (Amplitude [-1,1] wie die Referenz). Die Pan-Liste
		// bleibt leer (volle Pegel auf beiden Ausgaengen), genommen wird die
		// linke Spur als Mono-Summe.
		const FMetaSoundNodeHandle Harmonics =
			Graph.Node(FName(TEXT("Additive Synth")), FName());
		Graph.Wire(HzScale, TEXT("Out"), Harmonics, TEXT("Base Frequency"));
		Graph.Default(Harmonics, TEXT("HarmonicMultipliers"),
			{ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f });
		Graph.Default(Harmonics, TEXT("Amplitudes"),
			{ 1.00f / 2.42f, 0.62f / 2.42f, 0.38f / 2.42f,
			  0.22f / 2.42f, 0.13f / 2.42f, 0.07f / 2.42f });

		// Oberton-Verschiebung mit der Last: unter Gas verlagern sich die
		// Gewichte auf die OBEREN Harmonischen, im Leerlauf auf den Grundton.
		// Die Amplitudes-Arrays sind statisch, darum spannt ein ZWEITER
		// Additive Synth das Delta auf (Vorzeichen ueber Phase 180 Grad),
		// ueberlagert mit (2 * Throttle - 1). Die Gewichte sind um die Basis
		// aufgespannt: dark = Basis - Delta, bright = Basis + Delta, beide
		// Summe 2.42 - die Grundpegel-Summe bleibt also konstant, nur die
		// Faerbung dreht. Bei halbem Gas ist der Faktor EXAKT 0.0, die
		// freigegebene Endabnahme bleibt bit-identisch.
		const FMetaSoundNodeHandle Delta =
			Graph.Node(FName(TEXT("Additive Synth")), FName());
		Graph.Wire(HzScale, TEXT("Out"), Delta, TEXT("Base Frequency"));
		Graph.Default(Delta, TEXT("HarmonicMultipliers"),
			{ 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f });
		Graph.Default(Delta, TEXT("Amplitudes"),
			{ 0.14f / 2.42f, 0.02f / 2.42f, 0.03f / 2.42f,
			  0.05f / 2.42f, 0.05f / 2.42f, 0.03f / 2.42f });
		Graph.Default(Delta, TEXT("Phases"),
			{ 180.0f, 180.0f, 0.0f, 0.0f, 0.0f, 0.0f });
		const FMetaSoundNodeHandle ShiftScale =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Input(TEXT("Throttle"), ShiftScale, TEXT("PrimaryOperand"), 0.3f);
		Graph.Input(TEXT("Last Shift Scale"), ShiftScale, TEXT("AdditionalOperands"), 2.0f, true);
		const FMetaSoundNodeHandle ShiftBase =
			Graph.Node(FName(TEXT("Add")), GFloatType);
		Graph.Wire(ShiftScale, TEXT("Out"), ShiftBase, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Last Shift Base"), ShiftBase, TEXT("AdditionalOperands"), -1.0f, true);
		const FMetaSoundNodeHandle ShiftClamp =
			Graph.Node(FName(TEXT("Clamp")), GFloatType, FName(TEXT("Clamp")));
		Graph.Wire(ShiftBase, TEXT("Out"), ShiftClamp, TEXT("In"));
		Graph.Default(ShiftClamp, TEXT("Min"), -1.0f);
		Graph.Default(ShiftClamp, TEXT("Max"), 1.0f);
		const FMetaSoundNodeHandle DeltaShift =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(Delta, TEXT("Out Left Audio"), DeltaShift, TEXT("PrimaryOperand"));
		Graph.Wire(ShiftClamp, TEXT("Value"), DeltaShift, TEXT("AdditionalOperands"));
		const FMetaSoundNodeHandle HarmonicsSum =
			Graph.Node(FName(TEXT("Add")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(Harmonics, TEXT("Out Left Audio"), HarmonicsSum, TEXT("PrimaryOperand"));
		Graph.Wire(DeltaShift, TEXT("Out"), HarmonicsSum, TEXT("AdditionalOperands"));

		// Boxer-Versatz: gegenueberliegende Zylinder zuenden minimal
		// ungleichmaessig, die Amplitude wackelt mit der HALBEN Zuednfrequenz
		// (ein Arbeitstakt = Zweiertakt). Ohne diese Modulation klingt der
		// Motor zu glatt: Tone *= 1 + 0.14 * sin(...).
		const FMetaSoundNodeHandle HzHalf = Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Wire(HzScale, TEXT("Out"), HzHalf, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Boxer Half Hz"), HzHalf, TEXT("AdditionalOperands"), 0.5f, true);
		const FMetaSoundNodeHandle BoxerSine =
			Graph.Node(FName(TEXT("Sine")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(HzHalf, TEXT("Out"), BoxerSine, TEXT("Frequency"));
		const FMetaSoundNodeHandle BoxerWobble =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(BoxerSine, TEXT("Audio"), BoxerWobble, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Boxer Offset"), BoxerWobble, TEXT("AdditionalOperands"), 0.14f, true);
		const FMetaSoundNodeHandle BoxerMod =
			Graph.Node(FName(TEXT("Add")), FName(TEXT("audio by float")));
		Graph.Wire(BoxerWobble, TEXT("Out"), BoxerMod, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Boxer Base"), BoxerMod, TEXT("AdditionalOperands"), 1.0f, true);
		const FMetaSoundNodeHandle Boxered =
			Graph.Node(FName(TEXT("Multiply")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(HarmonicsSum, TEXT("Out"), Boxered, TEXT("PrimaryOperand"));
		Graph.Wire(BoxerMod, TEXT("Out"), Boxered, TEXT("AdditionalOperands"));

		// Pegel-Ausgleich + Last-Gain: die normierte Oberton-Summe misst 5.6 dB
		// leiser als ein reiner Sinus desselben Knoten-Gains - 0.42 * 1.91 = 0.80
		// haelt die beim Mix-Abstimmung gemessene Balance. Der Zuednpuls wachst
		// zusaetzlich mit dem Gaspedal (0.62 + 0.76 * Throttle): halbem Gas
		// (Throttle 0.5) bleibt der Faktor 1.0 - die freigegebene Balance der
		// Endabnahme ist unveraendert, Leerlauf liegt 4.2 dB darunter, Vollgas
		// 2.8 dB darueber.
		const FMetaSoundNodeHandle LastScale =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Link(TEXT("Throttle"), LastScale, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Last Scale"), LastScale, TEXT("AdditionalOperands"), 0.76f, true);
		const FMetaSoundNodeHandle LastBase =
			Graph.Node(FName(TEXT("Add")), GFloatType);
		Graph.Wire(LastScale, TEXT("Out"), LastBase, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Last Base"), LastBase, TEXT("AdditionalOperands"), 0.62f, true);
		const FMetaSoundNodeHandle LastGain =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Input(TEXT("FiringGain"), LastGain, TEXT("PrimaryOperand"), 0.80f, true);
		Graph.Wire(LastBase, TEXT("Out"), LastGain, TEXT("AdditionalOperands"));
		const FMetaSoundNodeHandle Firing =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(Boxered, TEXT("Out"), Firing, TEXT("PrimaryOperand"));
		Graph.Wire(LastGain, TEXT("Out"), Firing, TEXT("AdditionalOperands"));

		// Ansaugung: gefiltertes Rauschen, skaliert mit dem Gaspedal; das
		// Filter OEFFNET mit der Last wie in der Referenz
		// (NoiseCutoff = 0.05 + 0.22 * Throttle): linear von 312.5 Hz im
		// Stand auf 1687.5 Hz bei Vollthrottle, Begrenzung 200..3000 Hz.
		// Bei halbem Gas exakt die 1000 Hz der Endabnahme - die Eichung
		// laesst auch hier die fahrt-Lage bit-identisch.
		const FMetaSoundNodeHandle NoiseIntake =
			Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
		const FMetaSoundNodeHandle LpIntake =
			Graph.Node(FName(TEXT("One-Pole Low Pass Filter")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(NoiseIntake, TEXT("Audio"), LpIntake, TEXT("In"));
		const FMetaSoundNodeHandle IntakeCutScale =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Link(TEXT("Throttle"), IntakeCutScale, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Intake Cutoff Per Throttle"), IntakeCutScale, TEXT("AdditionalOperands"), 1375.0f, true);
		const FMetaSoundNodeHandle IntakeCutBase =
			Graph.Node(FName(TEXT("Add")), GFloatType);
		Graph.Wire(IntakeCutScale, TEXT("Out"), IntakeCutBase, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Intake Cutoff Hz"), IntakeCutBase, TEXT("AdditionalOperands"), 312.5f, true);
		const FMetaSoundNodeHandle IntakeCutClamp =
			Graph.Node(FName(TEXT("Clamp")), GFloatType, FName(TEXT("Clamp")));
		Graph.Wire(IntakeCutBase, TEXT("Out"), IntakeCutClamp, TEXT("In"));
		Graph.Default(IntakeCutClamp, TEXT("Min"), 200.0f);
		Graph.Default(IntakeCutClamp, TEXT("Max"), 3000.0f);
		Graph.Wire(IntakeCutClamp, TEXT("Value"), LpIntake, TEXT("Cutoff Frequency"));
		// Fester Layer-Gain VOR der Lastregelung - die Ansaugung war der einzige
		// Layer ohne Balance-Wert und damit im Gehoer praktisch nicht vorhanden.
		const FMetaSoundNodeHandle IntakeGain =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(LpIntake, TEXT("Out"), IntakeGain, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Intake Gain"), IntakeGain, TEXT("AdditionalOperands"), 4.0f, true);
		const FMetaSoundNodeHandle Intake =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(IntakeGain, TEXT("Out"), Intake, TEXT("PrimaryOperand"));
		Graph.Link(TEXT("Throttle"), Intake, TEXT("AdditionalOperands"));

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

		// Rollen: helles Rauschen, Pegel und Filter fahren mit dem Tempo -
		// SpeedKmh (aus EngineParamPairs) skaliert den Gain auf volle Staerke
		// ab 50 km/h (0.02 pro km/h, auf [0,1] begrenzt) und oeffnet den
		// Tiefpass von 390 Hz im Stand auf 390 + 5.2 * km/h (bei 50 km/h exakt
		// die 650 Hz der Endabnahme, Begrenzung 200..2500 Hz).
		const FMetaSoundNodeHandle SpeedScale =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Input(TEXT("SpeedKmh"), SpeedScale, TEXT("PrimaryOperand"), 0.0f);
		Graph.Input(TEXT("Roll Speed Scale"), SpeedScale, TEXT("AdditionalOperands"), 0.02f, true);
		const FMetaSoundNodeHandle SpeedClamp =
			Graph.Node(FName(TEXT("Clamp")), GFloatType, FName(TEXT("Clamp")));
		Graph.Wire(SpeedScale, TEXT("Out"), SpeedClamp, TEXT("In"));
		Graph.Default(SpeedClamp, TEXT("Min"), 0.0f);
		Graph.Default(SpeedClamp, TEXT("Max"), 1.0f);
		const FMetaSoundNodeHandle RollGain =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Input(TEXT("Roll Gain"), RollGain, TEXT("PrimaryOperand"), 1.2f, true);
		Graph.Wire(SpeedClamp, TEXT("Value"), RollGain, TEXT("AdditionalOperands"));
		const FMetaSoundNodeHandle NoiseRoll =
			Graph.Node(FName(TEXT("Noise")), Metasound::StandardNodes::AudioVariant);
		const FMetaSoundNodeHandle LpRoll =
			Graph.Node(FName(TEXT("One-Pole Low Pass Filter")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(NoiseRoll, TEXT("Audio"), LpRoll, TEXT("In"));
		const FMetaSoundNodeHandle CutScale =
			Graph.Node(FName(TEXT("Multiply")), GFloatType);
		Graph.Link(TEXT("SpeedKmh"), CutScale, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Roll Cutoff Per Kmh"), CutScale, TEXT("AdditionalOperands"), 5.2f, true);
		const FMetaSoundNodeHandle CutBase =
			Graph.Node(FName(TEXT("Add")), GFloatType);
		Graph.Wire(CutScale, TEXT("Out"), CutBase, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Roll Cutoff Hz"), CutBase, TEXT("AdditionalOperands"), 390.0f, true);
		const FMetaSoundNodeHandle CutClamp =
			Graph.Node(FName(TEXT("Clamp")), GFloatType, FName(TEXT("Clamp")));
		Graph.Wire(CutBase, TEXT("Out"), CutClamp, TEXT("In"));
		Graph.Default(CutClamp, TEXT("Min"), 200.0f);
		Graph.Default(CutClamp, TEXT("Max"), 2500.0f);
		Graph.Wire(CutClamp, TEXT("Value"), LpRoll, TEXT("Cutoff Frequency"));
		const FMetaSoundNodeHandle Roll =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(LpRoll, TEXT("Out"), Roll, TEXT("PrimaryOperand"));
		Graph.Wire(RollGain, TEXT("Out"), Roll, TEXT("AdditionalOperands"));

		// Hupe als SCHIFFSNEBELHORN wie in WiesbadenEngineAudio.cpp: Grundton
		// 65 Hz mit reiner Quinte 98 Hz, je vier abfallende Teiltoene
		// {1, 0.60, 0.32, 0.16} - bewusst NICHT normiert, die Pegelkontrolle
		// kommt aus tanh((A+B) * 0.42). Der Tor schwingt traege 180 ms ein
		// (InterpTo, lineare Rampe auf den Zielwert; die Referenz laeuft
		// exponentiell - klanglich nicht unterscheidbar).
		const FMetaSoundNodeHandle HornA =
			Graph.Node(FName(TEXT("Additive Synth")), FName());
		Graph.Input(TEXT("Horn Hz"), HornA, TEXT("Base Frequency"), 65.0f, true);
		Graph.Default(HornA, TEXT("HarmonicMultipliers"),
			{ 1.0f, 2.0f, 3.0f, 4.0f });
		Graph.Default(HornA, TEXT("Amplitudes"),
			{ 1.0f, 0.60f, 0.32f, 0.16f });
		const FMetaSoundNodeHandle HornB =
			Graph.Node(FName(TEXT("Additive Synth")), FName());
		Graph.Input(TEXT("Horn Hz B"), HornB, TEXT("Base Frequency"), 98.0f, true);
		Graph.Default(HornB, TEXT("HarmonicMultipliers"),
			{ 1.0f, 2.0f, 3.0f, 4.0f });
		Graph.Default(HornB, TEXT("Amplitudes"),
			{ 1.0f, 0.60f, 0.32f, 0.16f });
		const FMetaSoundNodeHandle HornSum =
			Graph.Node(FName(TEXT("Add")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(HornA, TEXT("Out Left Audio"), HornSum, TEXT("PrimaryOperand"));
		Graph.Wire(HornB, TEXT("Out Left Audio"), HornSum, TEXT("AdditionalOperands"));
		const FMetaSoundNodeHandle HornShape =
			Graph.Node(FName(TEXT("WaveShaper")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(HornSum, TEXT("Out"), HornShape, TEXT("In"));
		Graph.Input(TEXT("Horn Saturation"), HornShape, TEXT("Amount"), 0.42f, true);
		Graph.Input(TEXT("Horn Saturation Gain"), HornShape, TEXT("OutputGain"), 0.39693f, true);
		Graph.Default(HornShape, TEXT("Type"), 2); // EWaveShaperType::Tanh
		const FMetaSoundNodeHandle HornAttack =
			Graph.Node(FName(TEXT("ConversionFloatToTime")), FName());
		Graph.Input(TEXT("Horn Attack S"), HornAttack, TEXT("In"), 0.18f, true);
		const FMetaSoundNodeHandle HornEnv =
			Graph.Node(FName(TEXT("InterpTo")), Metasound::StandardNodes::AudioVariant);
		Graph.Input(TEXT("Horn"), HornEnv, TEXT("Target"), 0.0f);
		Graph.Wire(HornAttack, TEXT("Out"), HornEnv, TEXT("Interp Time"));
		const FMetaSoundNodeHandle HornScale =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(HornShape, TEXT("Out"), HornScale, TEXT("PrimaryOperand"));
		Graph.Input(TEXT("Horn Gain"), HornScale, TEXT("AdditionalOperands"), 0.85f, true);
		const FMetaSoundNodeHandle Horn =
			Graph.Node(FName(TEXT("Multiply")), FName(TEXT("audio by float")));
		Graph.Wire(HornScale, TEXT("Out"), Horn, TEXT("PrimaryOperand"));
		Graph.Wire(HornEnv, TEXT("Value"), Horn, TEXT("AdditionalOperands"));

		// Weiche Saettigung wie in der Referenz: tanh(x * 1.3) statt hartem
		// Clipping. Der WaveShaper rechnet tanh((x + Bias) * Amount) /
		// tanh(Amount) - der OutputGain tanh(1.3) = 0.8617 dreht die
		// Normalisierung wieder weg, es bleibt exakt tanh(1.3 x). Laeuft nur
		// ueber die MOTOR-Layer, die Hupe kommt wie in der Referenz danach.
		const FMetaSoundNodeHandle EngineMix =
			Graph.Node(FName(TEXT("Add")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(Firing, TEXT("Out"), EngineMix, TEXT("PrimaryOperand"));
		Graph.Wire(Intake, TEXT("Out"), EngineMix, TEXT("AdditionalOperands"));
		Graph.Wire(Exhaust, TEXT("Out"), EngineMix, TEXT("AdditionalOperands"));
		Graph.Wire(Roll, TEXT("Out"), EngineMix, TEXT("AdditionalOperands"));
		const FMetaSoundNodeHandle Shaped =
			Graph.Node(FName(TEXT("WaveShaper")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(EngineMix, TEXT("Out"), Shaped, TEXT("In"));
		Graph.Input(TEXT("Saturation Amount"), Shaped, TEXT("Amount"), 1.3f, true);
		Graph.Input(TEXT("Saturation Gain"), Shaped, TEXT("OutputGain"), 0.8617f, true);
		Graph.Default(Shaped, TEXT("Type"), 2); // EWaveShaperType::Tanh

		// Layer-Mix und Lauf-Tor.
		const FMetaSoundNodeHandle Mix =
			Graph.Node(FName(TEXT("Add")), Metasound::StandardNodes::AudioVariant);
		Graph.Wire(Shaped, TEXT("Out"), Mix, TEXT("PrimaryOperand"));
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
