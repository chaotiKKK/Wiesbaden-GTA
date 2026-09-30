// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * Geschicklichkeitsparcours: Slalom, Vollbremsung in eine Stoppbox und
 * Handbremswende, gewertet nach Zeit und Sauberkeit.
 *
 * Reine Logik ohne Welt: alles im PARCOURS-System (cm), X entlang der Strecke
 * (Startlinie bei 0), Y nach rechts (wie UE: +Y = rechts in Fahrtrichtung).
 * Der Actor (AWiesbadenParcours) rechnet die Wagenpose hinein und stellt die
 * Kegel auf; die Automation-Tests fahren die ganze Runde mit der echten
 * Fahrphysik durch.
 *
 * Ablauf: Start -> Slalom (fuenf Kegel, der erste links umfahren, dann
 * abwechselnd) -> mit mindestens 40 km/h ueber die Bremslinie, dahinter in der
 * Stoppbox zum Stehen kommen -> in der Wendezone per Handbremse wenden ->
 * zurueck ueber die Startlinie ins Ziel.
 *
 * Regen-Variante: dieselbe Strecke auf nasser Fahrbahn, mit eigenen
 * Medaillengrenzen und eigener Bestzeit. Sie gilt, wenn der Belagsgrip des
 * Wagens die ganze Runde nass war (FWbParcoursBewertung::RegenGripBis).
 */

class FConfigFile;

enum class EWbParcoursAbschnitt : uint8
{
	Bereit,
	Slalom,
	Bremsen,
	Wende,
	Rueckweg,
	Ziel,
	Abgebrochen,
};

WIESBADENREAL_API const TCHAR* WbParcoursAbschnittName(EWbParcoursAbschnitt Abschnitt);

struct WIESBADENREAL_API FWbParcoursLayout
{
	/** Slalomkegel; der erste wird links umfahren, dann abwechselnd. */
	TArray<FVector2D> SlalomKegel;

	float StartHalbeBreite = 300.0f;
	float BremslinieX = 16500.0f;
	float MindestKmhAnBremslinie = 40.0f;
	float BoxVonX = 17500.0f;
	float BoxBisX = 18700.0f;
	float BoxHalbeBreite = 300.0f;
	float WendeVonX = 20000.0f;
	float WendeBisX = 23000.0f;
	float WendeHalbeBreite = 1500.0f;
	float ZielHalbeBreite = 2500.0f;

	float KegelRadiusCm = 20.0f;
	/** Kaefer-Grundriss (halbe Laenge/Breite) fuer die Kegelberuehrung. */
	float WagenHalbeLaengeCm = 205.0f;
	float WagenHalbeBreiteCm = 80.0f;
	float ZeitlimitSekunden = 240.0f;

	/** Alle Kegel zum Aufstellen; die ersten SlalomKegel.Num() sind der Slalom. */
	TArray<FVector2D> AlleKegel() const;

	static FWbParcoursLayout Standard();
};

/** Eine Abtastung der Wagenpose im Parcours-System. */
struct FWbParcoursProbe
{
	FVector2D PosCm = FVector2D::ZeroVector;
	/** Kurs relativ zur Strecke, -180..180 Grad (0 = Fahrtrichtung, 180 = zurueck). */
	float KursGrad = 0.0f;
	float Kmh = 0.0f;
	bool bHandbremse = false;
	float DtSekunden = 0.0f;
	/** Belags-Griffigkeit unter dem Wagen (1 = trocken, nasse Fahrbahn 0,65). */
	float BelagsGrip = 1.0f;
};

struct WIESBADENREAL_API FWbParcoursErgebnis
{
	bool bImZiel = false;
	bool bAbgebrochen = false;
	float FahrzeitSekunden = 0.0f;
	float StrafSekunden = 0.0f;
	float GesamtSekunden = 0.0f;
	int32 KegelGetroffen = 0;
	int32 TorFehler = 0;
	float KmhAnBremslinie = 0.0f;
	bool bAngehalten = false;
	bool bNichtAngehalten = false;
	bool bZuLangsam = false;
	/** Abstand des Haltepunkts zur Stoppbox (m), 0 = in der Box. */
	float StoppAbweichungM = 0.0f;
	bool bHandbremseGenutzt = false;
	bool bWendezoneVerfehlt = false;
	/** 0..100 - 100 = keine Beruehrung, kein Fehler. */
	int32 Sauberkeit = 100;
	/** Regen-Variante: die ganze Runde auf nasser Fahrbahn. */
	bool bRegen = false;
	/** Hoechster Belagsgrip seit dem Start (entscheidet die Variante). */
	float BelagsGripMax = 1.0f;
	FString Medaille;
};

class WIESBADENREAL_API FWbParcoursBewertung
{
public:
	// Strafen in Sekunden (auf die Fahrzeit) - klein genug, dass ein sauberer
	// Lauf immer vorn liegt, gross genug, dass Abkuerzen nicht lohnt.
	static constexpr float StrafeKegel = 2.0f;
	static constexpr float StrafeTor = 5.0f;
	static constexpr float StrafeJeMeterNebenBox = 1.0f;
	static constexpr float StrafeNichtAngehalten = 8.0f;
	static constexpr float StrafeZuLangsam = 3.0f;
	static constexpr float StrafeOhneHandbremse = 3.0f;
	static constexpr float StrafeWendezone = 5.0f;

	explicit FWbParcoursBewertung(const FWbParcoursLayout& InLayout = FWbParcoursLayout::Standard());

	void Schritt(const FWbParcoursProbe& Probe);

	EWbParcoursAbschnitt GetAbschnitt() const { return Abschnitt; }
	bool IstAktiv() const;
	bool IstFertig() const { return Abschnitt == EWbParcoursAbschnitt::Ziel || Abschnitt == EWbParcoursAbschnitt::Abgebrochen; }
	float GetFahrzeit() const { return Fahrzeit; }
	float GetStrafSekunden() const { return Strafe; }
	const FWbParcoursLayout& GetLayout() const { return Layout; }
	const TArray<bool>& GetKegelUmgefahren() const { return KegelUmgefahren; }

	/** Kegel, die seit dem letzten Aufruf umgefahren wurden (zum Umkippen). */
	TArray<int32> HoleNeuUmgefahrene();
	/** Ereignisse seit dem letzten Aufruf ("Kegel 3 beruehrt +2 s", ...). */
	TArray<FString> HoleNeueMeldungen();

	FWbParcoursErgebnis GetErgebnis() const;

	/** Laeuft die Runde (bisher) ganz auf nasser Fahrbahn? */
	bool IstRegen() const;

	static int32 BerechneSauberkeit(const FWbParcoursErgebnis& E);
	/** Gold/Silber/Bronze nach Gesamtzeit; Gold nur mit Sauberkeit >= 90.
	 *  Bezug: der Parcours-Fahrer braucht mit der Kaefer-Physik ~42 s. */
	static FString BerechneMedaille(float GesamtSekunden, int32 Sauberkeit, bool bRegen = false);
	static constexpr float GoldSekunden = 46.0f;
	static constexpr float SilberSekunden = 55.0f;
	static constexpr float BronzeSekunden = 70.0f;
	/** Regen: derselbe Abstand zur Referenz wie trocken (46/55/70 zu ~42 s),
	 *  Bezug ist der Fahrer auf nasser Fahrbahn (Grip 0,65): 47,6 s. */
	static constexpr float GoldSekundenRegen = 52.0f;
	static constexpr float SilberSekundenRegen = 62.0f;
	static constexpr float BronzeSekundenRegen = 79.0f;
	/** Regenwertung nur, wenn der Belagsgrip die GANZE Runde hoechstens hier
	 *  lag: nasse Fahrbahn ist 0,65; Schnee (0,79) und halb eingeblendeter
	 *  Regen zaehlen trocken, damit die Regen-Grenzen zum Grip passen. */
	static constexpr float RegenGripBis = 0.70f;

private:
	void StrafeDazu(float Sekunden, const FString& Grund);
	void PruefeKegel(const FWbParcoursProbe& P);

	FWbParcoursLayout Layout;
	TArray<FVector2D> Kegel;
	TArray<bool> KegelUmgefahren;
	TArray<bool> SlalomGewertet;   // jede Seite nur beim ersten Passieren
	TArray<int32> NeuUmgefahren;
	TArray<FString> NeueMeldungen;

	EWbParcoursAbschnitt Abschnitt = EWbParcoursAbschnitt::Bereit;
	bool bHatVorige = false;
	FVector2D Vorige = FVector2D::ZeroVector;
	float Fahrzeit = 0.0f;
	float Strafe = 0.0f;

	int32 KegelGetroffen = 0;
	int32 TorFehler = 0;
	bool bBremslinieUeberfahren = false;
	float KmhAnBremslinie = 0.0f;
	bool bAngehalten = false;
	float StoppAbweichungM = 0.0f;
	bool bNichtAngehalten = false;
	bool bZuLangsam = false;
	bool bHandbremseInWende = false;
	bool bWendezoneBetreten = false;
	bool bWendezoneVerfehlt = false;
	float MaxBelagsGrip = 1.0f;
};

/** Bestzeit je Variante (trocken/Regen), Abschnitt [WiesbadenReal.Parcours]. */
struct WIESBADENREAL_API FWbParcoursBestzeit
{
	/** Gesamtzeit in Sekunden, 0 = noch keine. */
	static float Lesen(const FConfigFile& Ini, bool bRegen);
	/** Traegt die Zeit ein, wenn sie schneller ist; true = neue Bestzeit. */
	static bool Eintragen(FConfigFile& Ini, bool bRegen, float GesamtSekunden);
};

/** Steuerbefehl des Parcours-Fahrers (wie FWiesbadenCarControl). */
struct FWbParcoursSteuerung
{
	float Gas = 0.0f;
	float Bremse = 0.0f;
	float Lenkung = 0.0f;   // -1 links .. +1 rechts
	bool bHandbremse = false;
};

/**
 * Fahrer fuer Nachweislaeufe (-WbParcours 1): faehrt die Ideallinie ueber die
 * NORMALE Steuernaht, damit Parcours und Wertung ohne Tastatur belegbar sind.
 * Reine Verfolgung eines Zielpunkts (Pure Pursuit) mit Tempo-Vorgabe je
 * Abschnitt; die Wende faehrt er mit Vollausschlag und Handbremse. Auf
 * nasser Fahrbahn nimmt er Slalom-, Brems- und Wendetempo mit Wurzel(Grip)
 * zurueck: Quer- und Bremsweg-Bedarf wachsen mit v^2, der Grip nur linear.
 */
class WIESBADENREAL_API FWbParcoursFahrer
{
public:
	explicit FWbParcoursFahrer(const FWbParcoursLayout& InLayout = FWbParcoursLayout::Standard());

	/** Nutzbarer Lenkwinkel bei Tempo (Grad) - aus der Fahrphysik des Wagens. */
	float MaxLenkGrad = 35.0f;
	float LenkAbfallMS = 12.0f;
	/** Absichtlich Fehler machen (WbParcours 2): mitten ueber Slalomkegel 2
	 *  und mit 30 km/h an die Bremslinie - belegt Strafen und Umkippen. */
	bool bFehlerMachen = false;

	FWbParcoursSteuerung Steuern(const FWbParcoursProbe& P, EWbParcoursAbschnitt Abschnitt);

	/** Slalom-Ideallinie: Querversatz (cm) bei X - zum Pruefen und Zeichnen. */
	float SlalomLinieY(float X) const;

private:
	float LenkungZu(const FWbParcoursProbe& P, const FVector2D& Ziel) const;
	static void TempoHalten(FWbParcoursSteuerung& S, float IstKmh, float SollKmh);

	FWbParcoursLayout Layout;
	bool bWendeBegonnen = false;
};
