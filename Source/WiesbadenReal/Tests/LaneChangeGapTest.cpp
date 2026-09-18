// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Misc/AutomationTest.h"

#include "GIS/WiesbadenTrafficSimulation.h"

namespace
{
	using FSim = FWiesbadenTrafficSimulation;

	FWiesbadenTrafficSettings MakeGapSettings()
	{
		FWiesbadenTrafficSettings S;
		S.MinGapCm = 700.0;
		S.LaneChangeMinGapCm = 700.0;
		S.LaneChangeGapSeconds = 1.2;
		return S;
	}

	/** km/h -> cm/s, damit die Testwerte lesbar bleiben. */
	constexpr double Kmh(double V) { return V * 100000.0 / 3600.0; }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLaneChangeGapRuleTest,
	"WiesbadenReal.Traffic.SpurwechselLuecke",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FLaneChangeGapRuleTest::RunTest(const FString& Parameters)
{
	// -- 0. Die VOREINSTELLUNG muss stimmen, nicht nur die Formel. ---------
	//
	// Der Defekt sass in den Standardwerten, nicht in der Rechnung: gefordert
	// waren konstant 1400 cm, waehrend die Simulation ihre Kolonnen auf
	// MinGapCm = 700 cm packt. Ein Test, der sich seine Settings selbst baut,
	// haette das nie gesehen - er haette die Formel mit gesunden Zahlen
	// gefuettert und gruen gemeldet, waehrend im Spiel niemand ueberholt.
	{
		const FWiesbadenTrafficSettings Defaults;
		const double DefaultCityGap = FSim::RequiredLaneChangeGapCm(Defaults, Kmh(15.0));
		TestTrue(FString::Printf(
			TEXT("Voreinstellung: bei 15 km/h hoechstens der Folgeabstand (%.0f <= %.0f cm)"),
			DefaultCityGap, Defaults.MinGapCm),
			DefaultCityGap <= Defaults.MinGapCm + 1.0);
	}

	const FWiesbadenTrafficSettings S = MakeGapSettings();

	// -- 1. DER KERN: im Stadttempo darf die Luecke nicht groesser sein ------
	//      als der Abstand, den die Simulation selbst herstellt.
	//
	// Genau daran scheiterte das Ueberholen: gefordert waren fest 1400 cm,
	// waehrend die Kolonne auf MinGapCm = 700 cm steht. Eine solche Luecke
	// entsteht im dichten Verkehr nirgends - die Regel machte sich selbst
	// unmoeglich. Gemessen: 66 % aller Anlaeufe scheiterten hier.
	const double CityGap = FSim::RequiredLaneChangeGapCm(S, Kmh(15.0));
	TestTrue(FString::Printf(
		TEXT("Bei 15 km/h nicht mehr als der Folgeabstand verlangt (%.0f <= %.0f cm)"),
		CityGap, S.MinGapCm),
		CityGap <= S.MinGapCm + 1.0);

	// Und die alte Zahl waere hier zu gross gewesen - das ist der Beleg, dass
	// dieser Test den kaputten Stand wirklich faengt.
	TestTrue(FString::Printf(TEXT("Die alten 1400 cm waeren hier zu viel (%.0f)"), CityGap),
		CityGap < 1400.0);

	// -- 2. Bei Tempo wird die Regel STRENGER, nicht lockerer. --------------
	//
	// Der Gegenfehler waere, die Luecke einfach kleiner zu machen: dann zoege
	// ein Fahrzeug bei 80 km/h in eine 7-Meter-Luecke. Die Zeitluecke loest
	// beides zugleich.
	const double FastGap = FSim::RequiredLaneChangeGapCm(S, Kmh(80.0));
	TestTrue(FString::Printf(TEXT("Bei 80 km/h deutlich mehr verlangt (%.0f cm)"), FastGap),
		FastGap > 2000.0);
	TestTrue(FString::Printf(
		TEXT("Schnell strenger als langsam (%.0f > %.0f cm)"), FastGap, CityGap),
		FastGap > CityGap);

	// Die geforderte Strecke ist genau die Zeitluecke mal Tempo.
	const double Expected = S.LaneChangeGapSeconds * Kmh(80.0);
	TestTrue(FString::Printf(TEXT("80 km/h ergibt %.1f s Zeitluecke (%.0f cm)"),
		S.LaneChangeGapSeconds, FastGap),
		FMath::IsNearlyEqual(FastGap, Expected, 1.0));

	// -- 3. Im Stand greift die Untergrenze. --------------------------------
	//
	// Ohne sie waere die geforderte Luecke bei Tempo null ebenfalls null - ein
	// stehendes Fahrzeug zoege in eine Luecke von gar nichts.
	TestTrue(FString::Printf(TEXT("Im Stand gilt die Untergrenze (%.0f cm)"),
		FSim::RequiredLaneChangeGapCm(S, 0.0)),
		FMath::IsNearlyEqual(FSim::RequiredLaneChangeGapCm(S, 0.0), S.LaneChangeMinGapCm, 1.0));

	TestTrue(TEXT("Negatives Tempo faellt auf die Untergrenze"),
		FMath::IsNearlyEqual(FSim::RequiredLaneChangeGapCm(S, -500.0),
			S.LaneChangeMinGapCm, 1.0));

	// -- 4. Monoton: schneller verlangt nie weniger. ------------------------
	double Previous = -1.0;
	bool bMonotone = true;
	for (double Speed = 0.0; Speed <= Kmh(130.0); Speed += Kmh(5.0))
	{
		const double Need = FSim::RequiredLaneChangeGapCm(S, Speed);
		bMonotone = bMonotone && (Need >= Previous - 0.01);
		Previous = Need;
	}
	TestTrue(TEXT("Die geforderte Luecke waechst monoton mit dem Tempo"), bMonotone);

	// -- 5. Der Umschaltpunkt liegt dort, wo er hingehoert. -----------------
	//
	// Unterhalb von MinGapCm / LaneChangeGapSeconds entscheidet die
	// Untergrenze, darueber die Zeit. Ohne diese Pruefung koennte eine
	// Aenderung an einem der beiden Werte den anderen stillschweigend
	// wirkungslos machen.
	const double SwitchSpeed = S.LaneChangeMinGapCm / S.LaneChangeGapSeconds;
	TestTrue(TEXT("Knapp darunter entscheidet die Untergrenze"),
		FMath::IsNearlyEqual(FSim::RequiredLaneChangeGapCm(S, SwitchSpeed * 0.9),
			S.LaneChangeMinGapCm, 1.0));
	TestTrue(TEXT("Knapp darueber entscheidet die Zeitluecke"),
		FSim::RequiredLaneChangeGapCm(S, SwitchSpeed * 1.5) > S.LaneChangeMinGapCm + 1.0);

	return true;
}
