// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "WiesbadenGunshotSynth.generated.h"

/**
 * Klangliche Kenndaten eines Schusses.
 *
 * Ein Schuss ist kein einzelnes Geraeusch, sondern drei uebereinanderliegende
 * Vorgaenge, und nur alle drei zusammen klingen wie eine Waffe:
 *
 *   1. Der Knall der austretenden Treibgase - ein sehr kurzer, sehr lauter
 *      Rauschimpuls mit steiler Flanke. Er traegt die Lautstaerke.
 *   2. Der Koerperschall des Laufs und des Verschlusses - ein tiefer, kurz
 *      klingender Ton. Er traegt das Gewicht; ohne ihn klingt der Schuss wie
 *      ein Luftballon.
 *   3. Der Nachhall der Umgebung - abfallendes Rauschen ueber deutlich
 *      laengere Zeit. Ohne ihn klingt der Schuss abgeschnitten, als waere er
 *      in Watte abgegeben worden.
 */
USTRUCT(BlueprintType)
struct WIESBADENREAL_API FWiesbadenGunshotParams
{
	GENERATED_BODY()

	/** Abtastrate in Hz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "8000"))
	int32 SampleRate = 44100;

	/** Gesamtlaenge des Schusses in Sekunden (Knall + Nachhall). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.05"))
	float DurationSeconds = 0.55f;

	/** Abklingzeit des Muendungsknalls in Sekunden - sehr kurz. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.001"))
	float CrackDecaySeconds = 0.035f;

	/** Abklingzeit des Nachhalls in Sekunden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.01"))
	float TailDecaySeconds = 0.22f;

	/** Grundfrequenz des Koerperschalls in Hz (Lauf und Verschluss). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "20.0"))
	float BodyFrequencyHz = 92.0f;

	/** Abklingzeit des Koerperschalls in Sekunden. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.005"))
	float BodyDecaySeconds = 0.09f;

	/** Anteil des Koerperschalls am Gesamtpegel (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BodyMix = 0.45f;

	/** Anteil des Nachhalls am Gesamtpegel (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TailMix = 0.30f;

	/** Gesamtpegel (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float MasterGain = 0.85f;

	/**
	 * Tiefpass des Nachhalls (0..1, kleiner = dumpfer).
	 *
	 * Der Nachhall kommt von Hauswaenden zurueck; hohe Frequenzen werden dabei
	 * geschluckt. Ohne diese Daempfung zischt der Nachhall wie ein Zischlaut.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Waffe|Klang", meta = (ClampMin = "0.01", ClampMax = "1.0"))
	float TailDamping = 0.16f;
};

/**
 * Erzeugt die Abtastwerte eines Schusses - datenrein, ohne Welt und ohne
 * Audio-Engine, damit die Kurvenform pruefbar ist.
 *
 * Der Zufall ist ausdruecklich gesetzt (Seed): Zwei Schuesse duerfen
 * unterschiedlich klingen, aber derselbe Seed muss dieselben Werte liefern -
 * sonst laesst sich das Ergebnis nicht pruefen.
 */
struct WIESBADENREAL_API FWiesbadenGunshotSynth
{
	/**
	 * Schreibt einen vollstaendigen Schuss als 16-Bit-Mono nach OutSamples.
	 * Vorhandener Inhalt wird ersetzt.
	 */
	static void RenderShot(const FWiesbadenGunshotParams& Params, int32 Seed, TArray<int16>& OutSamples);

	/** Zahl der Abtastwerte, die RenderShot fuer diese Parameter liefert. */
	static int32 GetSampleCount(const FWiesbadenGunshotParams& Params);

	/**
	 * Groesster Betrag in der Kurve, normiert auf 0..1.
	 * Fuer die Pruefung, dass weder Stille noch Uebersteuerung entsteht.
	 */
	static float GetPeakLevel(const TArray<int16>& Samples);
};
