// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "Vehicles/WiesbadenHelicopter.h"
#include "Weapons/WiesbadenGunshotSynth.h"
#include "Weapons/WiesbadenWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGunshotSynthTest,
	"WiesbadenReal.Weapons.GunshotSynth",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Der Schuss klingt wie ein Schuss - pruefbar an der Kurvenform.
 *
 * "Es macht ein Geraeusch" ist keine Aussage. Pruefbar ist dagegen: Er ist
 * nicht still, er uebersteuert nicht, er faengt laut an und hoert leise auf,
 * und zwei Schuesse sind nicht identisch.
 */
bool FGunshotSynthTest::RunTest(const FString& Parameters)
{
	FWiesbadenGunshotParams Params;

	TArray<int16> Samples;
	FWiesbadenGunshotSynth::RenderShot(Params, 0, Samples);

	const int32 Expected = FWiesbadenGunshotSynth::GetSampleCount(Params);
	TestEqual(TEXT("Laenge entspricht Abtastrate mal Dauer"), Samples.Num(), Expected);
	TestTrue(FString::Printf(TEXT("Ueber 20000 Abtastwerte bei 44,1 kHz (%d)"), Samples.Num()),
		Samples.Num() > 20000);

	// Nicht still, nicht uebersteuert.
	const float Peak = FWiesbadenGunshotSynth::GetPeakLevel(Samples);
	TestTrue(FString::Printf(TEXT("Nicht still (Spitze %.2f)"), Peak), Peak > 0.25f);
	TestTrue(FString::Printf(TEXT("Nicht uebersteuert (Spitze %.4f)"), Peak), Peak <= 1.0f);

	// Ein Schuss faengt laut an und hoert leise auf. Ohne diese Pruefung waere
	// gleichfoermiges Rauschen genauso "erfolgreich" wie ein Schuss.
	auto MeanAbs = [&Samples](int32 First, int32 Last)
	{
		double Sum = 0.0;
		for (int32 i = First; i < Last; ++i)
		{
			Sum += FMath::Abs(static_cast<double>(Samples[i]));
		}
		return Sum / FMath::Max(Last - First, 1);
	};

	const int32 Count = Samples.Num();
	const double Head = MeanAbs(0, Count / 20);              // erste 5 %
	const double Tail = MeanAbs(Count - Count / 20, Count);  // letzte 5 %

	TestTrue(
		FString::Printf(TEXT("Anfang deutlich lauter als Ende (%.0f gegen %.0f)"), Head, Tail),
		Head > Tail * 8.0);

	// Zwei Schuesse duerfen nicht identisch klingen - bei Dauerfeuer faellt das
	// sofort auf.
	TArray<int16> Second;
	FWiesbadenGunshotSynth::RenderShot(Params, 1, Second);

	bool bIdentical = (Second.Num() == Samples.Num());
	if (bIdentical)
	{
		for (int32 i = 0; i < Samples.Num(); ++i)
		{
			if (Samples[i] != Second[i])
			{
				bIdentical = false;
				break;
			}
		}
	}
	TestFalse(TEXT("Zwei Schuesse mit verschiedenem Seed sind verschieden"), bIdentical);

	// Aber derselbe Seed muss dieselbe Kurve liefern, sonst ist nichts pruefbar.
	TArray<int16> Repeat;
	FWiesbadenGunshotSynth::RenderShot(Params, 0, Repeat);
	TestEqual(TEXT("Gleicher Seed, gleiche Laenge"), Repeat.Num(), Samples.Num());

	bool bReproducible = true;
	for (int32 i = 0; i < Samples.Num() && bReproducible; ++i)
	{
		bReproducible = (Repeat[i] == Samples[i]);
	}
	TestTrue(TEXT("Gleicher Seed liefert dieselbe Kurve"), bReproducible);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWeaponTracerTest,
	"WiesbadenReal.Weapons.Tracer",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Die Leuchtspur fliegt, statt sofort da zu sein.
 *
 * Zuvor zeichnete die Waffe eine DrawDebugLine ueber die volle Strecke - man
 * sah nicht, wohin geschossen wurde, sondern nur, dass geschossen wurde.
 */
bool FWeaponTracerTest::RunTest(const FString& Parameters)
{
	// 380 m/s ueber 100 m: der Flug dauert rund 0,26 s.
	const float DistanceCm = 10000.0f;
	const float Speed = (380.0f * 100.0f) / DistanceCm;

	float Alpha = 0.0f;
	Alpha = UWiesbadenWeaponComponent::AdvanceTracerAlpha(Alpha, Speed, 1.0f / 60.0f);
	TestTrue(FString::Printf(TEXT("Nach einem Bild unterwegs, nicht am Ziel (%.3f)"), Alpha),
		Alpha > 0.0f && Alpha < 1.0f);

	// Nach der vollen Flugzeit ist sie angekommen und laeuft nicht darueber
	// hinaus - ein Anteil groesser 1 laege hinter dem Einschlag.
	float Flight = 0.0f;
	for (int32 Step = 0; Step < 60; ++Step)
	{
		Flight = UWiesbadenWeaponComponent::AdvanceTracerAlpha(Flight, Speed, 1.0f / 60.0f);
	}
	TestTrue(FString::Printf(TEXT("Nach einer Sekunde angekommen (%.3f)"), Flight),
		FMath::IsNearlyEqual(Flight, 1.0f, 0.001f));

	// Auch ein sehr grosser Zeitschritt darf nicht ueber das Ziel hinausfuehren.
	const float Overshoot = UWiesbadenWeaponComponent::AdvanceTracerAlpha(0.5f, Speed, 100.0f);
	TestTrue(FString::Printf(TEXT("Kein Ueberschiessen (%.3f)"), Overshoot),
		Overshoot <= 1.0f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterStickShapingTest,
	"WiesbadenReal.Vehicles.Helicopter.StickShaping",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Totzone und Expo-Kennlinie der Analogsticks.
 *
 * Die Steuerung las bisher nur Tasten: jede Eingabe war +1, -1 oder 0. Ein
 * Hubschrauber laesst sich so nicht dosieren.
 */
bool FHelicopterStickShapingTest::RunTest(const FString& Parameters)
{
	constexpr float Deadzone = 0.15f;
	constexpr float Expo = 0.6f;

	// Innerhalb der Totzone bleibt es bei null - sonst driftet der Hubschrauber,
	// weil kein Stick exakt mittig ruht.
	TestEqual(TEXT("Mitte ergibt null"),
		AWiesbadenHelicopter::ApplyStickShaping(0.0f, Deadzone, Expo), 0.0f);
	TestEqual(TEXT("Innerhalb der Totzone bleibt null"),
		AWiesbadenHelicopter::ApplyStickShaping(0.1f, Deadzone, Expo), 0.0f);
	TestEqual(TEXT("Auch negativ innerhalb der Totzone"),
		AWiesbadenHelicopter::ApplyStickShaping(-0.14f, Deadzone, Expo), 0.0f);

	// Der Vollausschlag muss erreichbar BLEIBEN. Ohne das Strecken nach der
	// Totzone fehlten oben 15 % und der Stick fuehlte sich kurz an.
	TestTrue(TEXT("Vollausschlag rechts bleibt 1"),
		FMath::IsNearlyEqual(AWiesbadenHelicopter::ApplyStickShaping(1.0f, Deadzone, Expo), 1.0f, 0.001f));
	TestTrue(TEXT("Vollausschlag links bleibt -1"),
		FMath::IsNearlyEqual(AWiesbadenHelicopter::ApplyStickShaping(-1.0f, Deadzone, Expo), -1.0f, 0.001f));

	// Expo heisst: in der Mitte feiner als linear.
	const float Half = AWiesbadenHelicopter::ApplyStickShaping(0.575f, Deadzone, Expo);
	TestTrue(FString::Printf(TEXT("Halber Ausschlag ergibt weniger als die Haelfte (%.3f)"), Half),
		Half > 0.0f && Half < 0.5f);

	// Ohne Expo bleibt es linear (nach der Totzone).
	const float Linear = AWiesbadenHelicopter::ApplyStickShaping(0.575f, Deadzone, 0.0f);
	TestTrue(FString::Printf(TEXT("Ohne Expo linear (%.3f)"), Linear),
		FMath::IsNearlyEqual(Linear, 0.5f, 0.01f));

	// Monoton: mehr Ausschlag darf nie weniger Wirkung ergeben.
	float Previous = -2.0f;
	bool bMonotonic = true;
	for (int32 Step = 0; Step <= 20; ++Step)
	{
		const float Raw = Step / 20.0f;
		const float Shaped = AWiesbadenHelicopter::ApplyStickShaping(Raw, Deadzone, Expo);
		bMonotonic = bMonotonic && (Shaped >= Previous - 0.0001f);
		Previous = Shaped;
	}
	TestTrue(TEXT("Kennlinie ist monoton"), bMonotonic);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterAutoLevelTest,
	"WiesbadenReal.Vehicles.Helicopter.AutoLevel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Selbststabilisierung richtet auf - aber nur, wenn nicht gesteuert wird.
 *
 * Ohne sie bleibt die Lage stehen, sobald man loslaesst: einmal schraeg, immer
 * schraeg. Das war der Hauptgrund fuer das Urteil "Steuerung ist grausam".
 */
bool FHelicopterAutoLevelTest::RunTest(const FString& Parameters)
{
	constexpr float Strength = 0.045f;
	constexpr float MaxAuthority = 0.55f;

	// Schraeglage nach rechts, keine Eingabe -> Korrektur nach links.
	const float Correcting = AWiesbadenHelicopter::ComputeAutoLevel(20.0f, 0.0f, Strength, MaxAuthority);
	TestTrue(FString::Printf(TEXT("Schraeglage rechts wird nach links korrigiert (%.3f)"), Correcting),
		Correcting < 0.0f);

	// Andersherum ebenso.
	const float Other = AWiesbadenHelicopter::ComputeAutoLevel(-20.0f, 0.0f, Strength, MaxAuthority);
	TestTrue(FString::Printf(TEXT("Schraeglage links wird nach rechts korrigiert (%.3f)"), Other),
		Other > 0.0f);

	// Waagerecht: keine Korrektur.
	TestTrue(TEXT("Waagerecht erzeugt keine Korrektur"),
		FMath::IsNearlyZero(AWiesbadenHelicopter::ComputeAutoLevel(0.0f, 0.0f, Strength, MaxAuthority)));

	// Bei voller Eingabe bleibt ein REST erhalten.
	//
	// Hier stand zuvor "Volle Eingabe schaltet die Stabilisierung ab". Das
	// klang vernuenftig - wer bewusst schraeg fliegt, soll nicht dagegen
	// anarbeiten muessen - war im Spiel aber der Grund fuer "Helikopter kaum
	// zu navigieren": Genau waehrend des Steuerns gab es null Stabilisierung,
	// und der Hubschrauber kippte weg. Ein Rest von 30 Prozent haelt die Lage
	// beherrschbar, ohne den schraegen Flug zu verhindern.
	const float AtFullInput = AWiesbadenHelicopter::ComputeAutoLevel(
		30.0f, 1.0f, Strength, MaxAuthority);
	TestTrue(TEXT("Volle Eingabe laesst einen Rest Stabilisierung"),
		!FMath::IsNearlyZero(AtFullInput));

	// Sie muss aber deutlich SCHWAECHER sein als ohne Eingabe, sonst kaempft
	// der Pilot doch wieder dagegen.
	const float AtNoInput = AWiesbadenHelicopter::ComputeAutoLevel(
		30.0f, 0.0f, Strength, MaxAuthority);
	TestTrue(TEXT("Bei voller Eingabe schwaecher als ohne"),
		FMath::Abs(AtFullInput) < FMath::Abs(AtNoInput) * 0.5f);

	// Teilweise Eingabe: abgeschwaecht, aber vorhanden.
	const float Partial = AWiesbadenHelicopter::ComputeAutoLevel(30.0f, 0.5f, Strength, MaxAuthority);
	const float Free = AWiesbadenHelicopter::ComputeAutoLevel(30.0f, 0.0f, Strength, MaxAuthority);
	TestTrue(FString::Printf(TEXT("Halbe Eingabe halbiert die Korrektur (%.3f gegen %.3f)"), Partial, Free),
		FMath::Abs(Partial) < FMath::Abs(Free) && FMath::Abs(Partial) > 0.0f);

	// Die Vollmacht wird eingehalten: Auch bei 90 Grad Schraeglage darf die
	// Stabilisierung nicht mehr Ausschlag erzeugen als erlaubt.
	const float Extreme = AWiesbadenHelicopter::ComputeAutoLevel(90.0f, 0.0f, Strength, MaxAuthority);
	TestTrue(FString::Printf(TEXT("Vollmacht eingehalten (%.3f von %.2f)"), Extreme, MaxAuthority),
		FMath::Abs(Extreme) <= MaxAuthority + 0.001f);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHelicopterControlAxisTest,
	"WiesbadenReal.Vehicles.Helicopter.ControlAxis",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

/**
 * Steuerachsen folgen mit begrenzter Geschwindigkeit, getrennt je Richtung.
 *
 * Deckt zugleich den Fehler ab, der bei der Fahrzeuglenkung aufgetreten ist:
 * FMath::Sign(0) ist 0 und weicht von JEDEM Ziel ab - eine naive Pruefung auf
 * "Vorzeichen verschieden" haelt das Einlenken aus der Mitte faelschlich fuer
 * eine Rueckstellung und laesst es mit der schnelleren Rate laufen.
 */
bool FHelicopterControlAxisTest::RunTest(const FString& Parameters)
{
	constexpr float Rise = 2.0f;
	constexpr float Return = 4.0f;

	// Aus der Mitte heraus gilt die AUFBAU-Rate, nicht die Ruecklaufrate.
	const float FromCentre = AWiesbadenHelicopter::AdvanceControlAxis(0.0f, 1.0f, Rise, Return, 0.1f);
	TestTrue(FString::Printf(TEXT("Aufbau aus der Mitte mit 2,0 je Sekunde (%.3f)"), FromCentre),
		FMath::IsNearlyEqual(FromCentre, 0.2f, 0.001f));

	// Zur Mitte hin gilt die schnellere Ruecklaufrate.
	const float ToCentre = AWiesbadenHelicopter::AdvanceControlAxis(1.0f, 0.0f, Rise, Return, 0.1f);
	TestTrue(FString::Printf(TEXT("Ruecklauf mit 4,0 je Sekunde (%.3f)"), ToCentre),
		FMath::IsNearlyEqual(ToCentre, 0.6f, 0.001f));

	// Gegenlenken zaehlt als Ruecklauf, solange es zur Mitte geht.
	const float Reversing = AWiesbadenHelicopter::AdvanceControlAxis(0.5f, -1.0f, Rise, Return, 0.1f);
	TestTrue(FString::Printf(TEXT("Gegenlenken laeuft mit der Ruecklaufrate (%.3f)"), Reversing),
		FMath::IsNearlyEqual(Reversing, 0.1f, 0.001f));

	// Kein Ueberschwingen ueber den Zielwert hinaus.
	const float Small = AWiesbadenHelicopter::AdvanceControlAxis(0.0f, 0.05f, Rise, Return, 10.0f);
	TestTrue(FString::Printf(TEXT("Kein Ueberschwingen (%.3f)"), Small),
		FMath::IsNearlyEqual(Small, 0.05f, 0.001f));

	// Der Bereich wird eingehalten.
	const float Clamped = AWiesbadenHelicopter::AdvanceControlAxis(0.9f, 5.0f, Rise, Return, 10.0f);
	TestTrue(FString::Printf(TEXT("Auf 1 begrenzt (%.3f)"), Clamped), Clamped <= 1.0f);

	return true;
}
