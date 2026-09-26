// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "WiesbadenSebboFigureComponent.generated.h"

class UAnimSequence;

/**
 * Bewegungen der Spielerfigur - DIE Liste. Je Eintrag ein Clip A_Sebbo_<Name>;
 * TripoClip nennt den Clip im Tripo-GLB (Stamm ohne ".001"), BlenderFrom die
 * Bewegung, aus der Blender den Clip baut (das Modell hat kein Ducken). Die
 * Werkzeuge lesen genau diese Liste (Tools/sebbo_bewegungen.py): Blender
 * benennt um bzw. baut, der Import verlangt danach. Eine neue Bewegung = ein
 * Eintrag hier plus eine Regel in ChooseMove.
 */
UENUM()
enum class EWbSebboMove : uint8
{
	Idle    UMETA(TripoClip = "wait"),
	Walk    UMETA(TripoClip = "walk"),
	Run     UMETA(TripoClip = "run"),
	Jump    UMETA(TripoClip = "jump"),
	Turn    UMETA(TripoClip = "turn"),
	Swagger UMETA(TripoClip = "swagger"),
	Call    UMETA(TripoClip = "make_a_call_02"),
	Kick    UMETA(TripoClip = "front_kick_02"),
	Hit     UMETA(TripoClip = "hit_to_body_01"),
	Surf    UMETA(TripoClip = "surf"),
	CrouchIdle UMETA(BlenderFrom = "Idle"),
	CrouchWalk UMETA(BlenderFrom = "Walk"),
	Count   UMETA(Hidden)
};

/** Was der Pawn je Bild meldet - mehr weiss die Figur nicht von ihm. */
struct FWbFigureInput
{
	/** Gemessenes Tempo ueber Grund in m/s. */
	float SpeedMps = 0.0f;
	/** Blickrichtung (Yaw) in Grad - die Figur leitet daraus die Drehrate ab. */
	float YawDeg = 0.0f;
	bool bAirborne = false;
	bool bRiding = false;
	/** Geduckt (Kapsel schon niedrig) - Ducken statt Stehen/Gehen/Rennen. */
	bool bCrouching = false;
	/** Gesundheit - ein Abfall spielt die Trefferreaktion. */
	float HealthPoints = 0.0f;
};

/** Eingang der datenreinen Clip-Wahl (ChooseMove). */
struct FWbSebboMoveState
{
	float SpeedMps = 0.0f;
	/** Geglaettete Drehrate in Grad je Sekunde. */
	float YawRateDegS = 0.0f;
	/** Wie lange die Figur schon still steht (weder geht noch dreht), s. */
	float StillSeconds = 0.0f;
	bool bAirborne = false;
	bool bRiding = false;
	bool bCrouching = false;
	/** Bisherige Bewegung - fuer die Hysterese beim Drehen. */
	EWbSebboMove Previous = EWbSebboMove::Idle;
};

/** Eine Spur im Mischer: Clip, Abspielstelle, Gewicht. */
struct FWbSebboLayer
{
	EWbSebboMove Move = EWbSebboMove::Count;
	/** Abspielstelle in Sekunden. */
	float Time = 0.0f;
	float PlayRate = 1.0f;
	bool bLoop = true;
	float Weight = 0.0f;
	/** Gewicht zu Beginn der laufenden Blende. */
	float StartWeight = 0.0f;
};

/**
 * WEICHES UEBERBLENDEN zwischen Sebbos Bewegungen. Datenrein (testbar): die
 * Figur-Komponente sagt, welche Bewegung laufen soll, der Mischer blendet die
 * neue ueber die Blendzeit ein und die bisherigen im selben Mass aus. Vorher
 * schaltete PlayAnimation hart um - die Figur sprang beim Anlaufen, Landen
 * und jedem Tempowechsel von einer Pose in die andere.
 *
 * Die Gewichte summieren sich immer zu 1. Laeuft eine Bewegung noch im
 * Ausblenden und wird wieder gewollt (Gehen - Rennen - Gehen), blendet sie
 * von ihrem jetzigen Gewicht aus wieder ein und behaelt ihre Abspielstelle.
 */
struct FWbSebboMixer
{
	/** Aelteste zuerst, die gewollte Bewegung zuletzt. */
	TArray<FWbSebboLayer> Layers;

	/** Hoechstens so viele Spuren zugleich (die aeltesten fallen weg). */
	static constexpr int32 MaxLayers = 4;

	/**
	 * Bewegung einblenden. StartTime gilt nur fuer eine neue Spur bzw. eine
	 * Einmalbewegung (die immer von vorn beginnt). BlendSeconds <= 0 schaltet hart.
	 */
	void Start(EWbSebboMove Move, bool bLoop, float PlayRate, float BlendSeconds, float StartTime = 0.0f);

	/** Zeit und Blende fortschreiben; ClipLength(Move) = Clip-Laenge in s. */
	void Advance(float DeltaSeconds, TFunctionRef<float(EWbSebboMove)> ClipLength);

	/** Die gewollte (oberste) Spur, nullptr ohne Spuren. */
	FWbSebboLayer* Top() { return Layers.Num() > 0 ? &Layers.Last() : nullptr; }
	const FWbSebboLayer* Top() const { return Layers.Num() > 0 ? &Layers.Last() : nullptr; }

	/** Gewicht einer Bewegung (0, wenn sie nicht laeuft). */
	float WeightOf(EWbSebboMove Move) const;

	/** Fortschritt der laufenden Blende, 1 = fertig. */
	float Alpha = 1.0f;

private:
	float AlphaStart = 1.0f;
	float BlendSeconds = 0.0f;
};

/** Was die Anim-Instanz je Bild abtastet (Spielstrang -> Anim-Strang). */
struct FWbSebboPoseSample
{
	const UAnimSequence* Sequence = nullptr;
	float Time = 0.0f;
	bool bLoop = true;
	float Weight = 0.0f;
};

/**
 * Anim-Instanz ohne Animation-Blueprint: mischt die Clips, die ihr die
 * Figur-Komponente je Bild meldet (Gewichte des FWbSebboMixer), zu EINER Pose.
 * Das Abtasten macht ihr Proxy (Evaluate) - so, wie es auch der Einzelknoten
 * von PlayAnimation tut, nur mit mehreren gewichteten Clips.
 */
UCLASS(Transient, NotBlueprintable)
class WIESBADENREAL_API UWiesbadenSebboAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	void SetSamples(TArray<FWbSebboPoseSample>&& InSamples) { Samples = MoveTemp(InSamples); }
	const TArray<FWbSebboPoseSample>& GetSamples() const { return Samples; }

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

private:
	TArray<FWbSebboPoseSample> Samples;
};

/**
 * Die animierte Spielfigur Sebbo: geriggtes Tripo-Modell (61 Knochen) samt
 * Clip-Wahl und Wiedergabe. Einziger Besitzer aller Animationszustaende; der
 * Pawn meldet nur Tempo, Luft, Mitfahrt, Blickrichtung und Gesundheit
 * (Animate) und stoesst Einmalbewegungen an (PlayOneShot).
 *
 * Pipeline: Tools/Blender/build_sebbo_player.py -> Tools/import_tripo_figure.py
 * (WB_FIGUR=Sebbo) -> /Game/Assets/People/Sebbo.
 */
UCLASS()
class WIESBADENREAL_API UWiesbadenSebboFigureComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:
	/**
	 * Laedt Modell und Clips. Ohne Stehen, Gehen, Rennen und Springen bleibt
	 * die Figur leer (false) - der Pawn zeigt dann sein statisches Modell.
	 *
	 * @param InJumpAirSeconds  Flugzeit eines Sprungs - die Sprungaufnahme wird
	 *                          darauf gestaucht.
	 */
	bool SetupFigure(float InJumpAirSeconds);

	/** Ein Bild: Bewegung waehlen und abspielen. */
	void Animate(float DeltaSeconds, const FWbFigureInput& Input);

	/**
	 * Einmalbewegung (Tritt, Treffer) ueber genau Seconds abspielen; solange
	 * ruhen die Dauerbewegungen.
	 */
	void PlayOneShot(EWbSebboMove Move, float Seconds);

	/** Bricht eine laufende Einmalbewegung dieser Art ab (Waffenwechsel). */
	void CancelOneShot(EWbSebboMove Move);

	bool IsFigureReady() const { return bReady; }
	bool HasMove(EWbSebboMove Move) const;
	/** Laenge des Clips in Sekunden (0 = fehlt). */
	float MoveLength(EWbSebboMove Move) const;
	EWbSebboMove GetCurrentMove() const { return CurrentMove; }

	/** Abspielrate der gewollten Bewegung (Figurprobe). */
	float GetMovePlayRate() const { return Mixer.Top() ? Mixer.Top()->PlayRate : 0.0f; }

	/** Gewicht einer Bewegung im Mischer (0..1; Figurprobe, Tests). */
	float GetMoveWeight(EWbSebboMove Move) const { return Mixer.WeightOf(Move); }

	/**
	 * Blendzeit von einer Bewegung zur naechsten in Sekunden. Datenrein.
	 * Absprung schnell (0,1 s - sonst hebt die Figur ab, bevor die Beine
	 * springen), Landung 0,15 s, Tritt/Treffer ein 0,08 s und aus 0,2 s,
	 * Gehen <-> Rennen 0,25 s, ins und aus dem Stolzieren/Telefonieren
	 * 0,35 s, sonst 0,2 s. Erster Clip und gleiche Bewegung: 0 (hart).
	 */
	static float BlendSecondsFor(EWbSebboMove From, EWbSebboMove To);

	/**
	 * Startstelle (normiert 0..1) fuer den Wechsel zwischen zwei Gangzyklen
	 * (Walk, Run, CrouchWalk): dieselbe Schrittphase wie im bisherigen Clip,
	 * damit sich beim Ueberblenden nicht linker und rechter Fuss mischen.
	 * -1, wenn eine der beiden Bewegungen kein Gangzyklus ist. Datenrein.
	 */
	static float GaitStartFor(EWbSebboMove From, float FromNormalizedTime, EWbSebboMove To);

	/**
	 * Welche Dauerbewegung passt zum Zustand? Datenrein, damit testbar.
	 *
	 * Mitfahrt = Surf, Luft = Jump, geduckt = CrouchWalk/CrouchIdle, schneller
	 * als RunFromMps = Run, sonst Gehen = Walk, Drehen im Stand = Turn. Im Stand wechseln Idle, Swagger (ab 10 s)
	 * und Call (ab 30 s) in festem Takt, der nach dem Telefonat neu beginnt.
	 */
	static EWbSebboMove ChooseMove(const FWbSebboMoveState& State, float RunFromMps,
		float SwaggerSeconds, float CallSeconds);

	/** Name der Bewegung = Asset-Stamm (A_Sebbo_<Name>). */
	static FString MoveName(EWbSebboMove Move);

	/** Tempo, fuer das der Schrittzyklus (A_Sebbo_Walk) gebaut ist, m/s. */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "0.1"))
	float WalkAnimSpeedMps = 1.67f;

	/** Tempo, fuer das der Rennzyklus (A_Sebbo_Run) gebaut ist, m/s (16 km/h). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "0.1"))
	float RunAnimSpeedMps = 4.44f;

	/** Ab diesem Tempo rennt die Figur statt zu gehen, m/s (Gehen 1,67, Sprint 4,44). */
	UPROPERTY(EditAnywhere, Category = "Wiesbaden|Figur", meta = (ClampMin = "0.5"))
	float RunFromMps = 3.0f;

private:
	/** Spielt eine Bewegung ab (Wechsel nur, wenn sie nicht schon laeuft). */
	void PlayMove(EWbSebboMove Move, bool bLoop, float PlayRate);

	/** Clip-Wahl eines Bilds (ohne Fortschreiben des Mischers). */
	void ChooseAndPlay(float DeltaSeconds, const FWbFigureInput& Input);

	/** Mischer fortschreiben und die Gewichte an die Anim-Instanz geben. */
	void AdvanceMixer(float DeltaSeconds);

	/** Die laufenden Clips samt Gewicht (weiches Ueberblenden). */
	FWbSebboMixer Mixer;


	/** Clips, Index = EWbSebboMove (fehlende = nullptr). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> Moves;

	bool bReady = false;
	float JumpAirSeconds = 1.26f;

	/** Laufende Bewegung (Count = keine, beim naechsten Bild neu starten). */
	EWbSebboMove CurrentMove = EWbSebboMove::Count;

	/** Laufende Einmalbewegung und ihre Restzeit. */
	EWbSebboMove OneShotMove = EWbSebboMove::Count;
	float OneShotRemaining = 0.0f;

	/** Stillstand, Entprellung und Drehrate. */
	float StillSeconds = 0.0f;
	float StopSeconds = 0.0f;
	float LastMovingSpeedMps = 0.0f;
	float YawRateDegS = 0.0f;
	float PreviousYaw = 0.0f;
	bool bHasPreviousYaw = false;

	/** Gesundheit im letzten Bild (negativ = noch keine Meldung). */
	float LastHealthPoints = -1.0f;
};
