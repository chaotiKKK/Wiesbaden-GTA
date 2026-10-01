// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenInputMap.h"

#include "GameFramework/PlayerController.h"

namespace WiesbadenInputMap
{
	const TArray<FWiesbadenHeliBinding>& HelicopterBindings()
	{
		static const TArray<FWiesbadenHeliBinding> Bindings = []()
		{
			TArray<FWiesbadenHeliBinding> Out;
			auto Add = [&Out](EWiesbadenHeliAction Action, const TCHAR* Description,
				const TCHAR* Keyboard, const TCHAR* Pad,
				EWiesbadenHeliAction Related = EWiesbadenHeliAction::MAX)
			{
				FWiesbadenHeliBinding Binding;
				Binding.Action = Action;
				Binding.RelatedAction = Related;
				Binding.Beschreibung = Description;
				Binding.Tastatur = Keyboard;
				Binding.Gamepad = Pad;
				Out.Add(Binding);
			};
			Add(EWiesbadenHeliAction::Pitch, TEXT("Nick"), TEXT("W / S"), TEXT("Linker Stick hoch / runter"));
			Add(EWiesbadenHeliAction::Roll, TEXT("Rollen"), TEXT("A / D"), TEXT("Linker Stick links / rechts"));
			Add(EWiesbadenHeliAction::Yaw, TEXT("Gieren"), TEXT("Q / E"), TEXT("LB / RB"));
			Add(EWiesbadenHeliAction::CollectiveUp, TEXT("Kollektiv rauf"), TEXT("Leertaste"), TEXT("RT"));
			Add(EWiesbadenHeliAction::CollectiveDown, TEXT("Kollektiv runter"), TEXT("Strg"), TEXT("LT"));
			Add(EWiesbadenHeliAction::Engine, TEXT("Triebwerk"), TEXT("G"), TEXT("X"));
			Add(EWiesbadenHeliAction::Fire, TEXT("Bordgeschuetz (Trockentest in der Flugstunde)"), TEXT("Linke Maustaste"), TEXT("A"));
			Add(EWiesbadenHeliAction::CameraMode, TEXT("Kameramodus"), TEXT("C"), TEXT("R3"));
			Add(EWiesbadenHeliAction::Look, TEXT("Kamera umsehen"), TEXT("Maus"), TEXT("Rechter Stick"));
			Add(EWiesbadenHeliAction::Searchlight, TEXT("Suchscheinwerfer"), TEXT("L"), TEXT("Steuerkreuz hoch"));
			Add(EWiesbadenHeliAction::LandingLight, TEXT("Landescheinwerfer"), TEXT("B"), TEXT("Steuerkreuz runter"));
			Add(EWiesbadenHeliAction::Exit, TEXT("Helikopter verlassen"), TEXT("F"), TEXT("Y"));
			return Out;
		}();
		return Bindings;
	}

	float HelicopterAxis(const APlayerController* PC, EWiesbadenHeliAction Action)
	{
		if (!PC)
		{
			return 0.0f;
		}
		const auto Down = [PC](const FKey& Key) { return PC->IsInputKeyDown(Key); };
		const auto Analog = [PC](const FKey& Key) { return PC->GetInputAnalogKeyState(Key); };
		const auto Stronger = [](float Keyboard, float Pad)
		{
			return FMath::Abs(Keyboard) >= FMath::Abs(Pad) ? Keyboard : Pad;
		};
		switch (Action)
		{
		case EWiesbadenHeliAction::Pitch:
			// Linker Stick nach oben wird im Gamepad-Poll bereits positiv geliefert.
			return Stronger((Down(EKeys::W) ? 1.0f : 0.0f) - (Down(EKeys::S) ? 1.0f : 0.0f),
				Analog(EKeys::Gamepad_LeftY));
		case EWiesbadenHeliAction::Roll:
			return Stronger((Down(EKeys::D) ? 1.0f : 0.0f) - (Down(EKeys::A) ? 1.0f : 0.0f),
				Analog(EKeys::Gamepad_LeftX));
		case EWiesbadenHeliAction::Yaw:
			// Die Tabelle nennt LB/RB; positiv ist E/RB (rechts gieren).
			return Stronger((Down(EKeys::E) ? 1.0f : 0.0f) - (Down(EKeys::Q) ? 1.0f : 0.0f),
				(Down(EKeys::Gamepad_RightShoulder) ? 1.0f : 0.0f)
				- (Down(EKeys::Gamepad_LeftShoulder) ? 1.0f : 0.0f));
		case EWiesbadenHeliAction::Collective:
			return Stronger((Down(EKeys::SpaceBar) ? 1.0f : 0.0f)
				- (Down(EKeys::LeftControl) ? 1.0f : 0.0f),
				Analog(EKeys::Gamepad_RightTriggerAxis) - Analog(EKeys::Gamepad_LeftTriggerAxis));
		case EWiesbadenHeliAction::CollectiveUp:
			return Stronger(Down(EKeys::SpaceBar) ? 1.0f : 0.0f,
				Analog(EKeys::Gamepad_RightTriggerAxis));
		case EWiesbadenHeliAction::CollectiveDown:
			return Stronger(Down(EKeys::LeftControl) ? 1.0f : 0.0f,
				Analog(EKeys::Gamepad_LeftTriggerAxis));
		default:
			return 0.0f;
		}
	}

	bool IsHelicopterActionDown(const APlayerController* PC, EWiesbadenHeliAction Action)
	{
		if (!PC)
		{
			return false;
		}
		const auto Down = [PC](const FKey& Key) { return PC->IsInputKeyDown(Key); };
		switch (Action)
		{
		case EWiesbadenHeliAction::Pitch:
		case EWiesbadenHeliAction::Roll:
		case EWiesbadenHeliAction::Yaw:
		case EWiesbadenHeliAction::Collective:
			return FMath::Abs(HelicopterAxis(PC, Action)) > 0.15f;
		case EWiesbadenHeliAction::CollectiveUp:
		case EWiesbadenHeliAction::CollectiveDown:
			return HelicopterAxis(PC, Action) > 0.15f;
		case EWiesbadenHeliAction::Engine:
			return Down(EKeys::G) || Down(EKeys::Gamepad_FaceButton_Left);
		case EWiesbadenHeliAction::Fire:
			return Down(EKeys::LeftMouseButton) || Down(EKeys::Gamepad_FaceButton_Bottom);
		case EWiesbadenHeliAction::CameraMode:
			return Down(EKeys::C) || Down(EKeys::Gamepad_RightThumbstick);
		case EWiesbadenHeliAction::Look:
			return FMath::Abs(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightX)) > 0.15f
				|| FMath::Abs(PC->GetInputAnalogKeyState(EKeys::Gamepad_RightY)) > 0.15f;
		case EWiesbadenHeliAction::Searchlight:
			return Down(EKeys::L) || Down(EKeys::Gamepad_DPad_Up);
		case EWiesbadenHeliAction::LandingLight:
			return Down(EKeys::B) || Down(EKeys::Gamepad_DPad_Down);
		case EWiesbadenHeliAction::Exit:
			return Down(EKeys::F) || Down(EKeys::Gamepad_FaceButton_Top);
		default:
			return false;
		}
	}

	const TArray<FWiesbadenBelegung>& Belegungen()
	{
		// Eine Zeile je Aktion. Reihenfolge egal, eine Aktion GENAU einmal -
		// der Test WiesbadenReal.Input.Belegung prueft beides.
		static const TArray<FWiesbadenBelegung> Tabelle = []()
		{
			TArray<FWiesbadenBelegung> B;

			FWiesbadenBelegung Feuern;
			Feuern.Action = EWiesbadenInputAction::Feuern;
			Feuern.Beschreibung = TEXT("Feuern");
			Feuern.Tasten = { EKeys::LeftMouseButton, EKeys::LeftControl, EKeys::Enter };
			Feuern.Gamepad = { EKeys::Gamepad_RightTriggerAxis };
			Feuern.bAnalog = true;
			B.Add(Feuern);

			FWiesbadenBelegung Zielen;
			Zielen.Action = EWiesbadenInputAction::Zielen;
			Zielen.Beschreibung = TEXT("Zielen (ADS)");
			Zielen.Tasten = { EKeys::RightMouseButton };
			Zielen.Gamepad = { EKeys::Gamepad_LeftTriggerAxis };
			Zielen.bAnalog = true;
			B.Add(Zielen);

			FWiesbadenBelegung Springen;
			Springen.Action = EWiesbadenInputAction::Springen;
			Springen.Beschreibung = TEXT("Springen");
			Springen.Tasten = { EKeys::SpaceBar };
			Springen.Gamepad = { EKeys::Gamepad_FaceButton_Bottom };
			B.Add(Springen);

			FWiesbadenBelegung Rennen;
			Rennen.Action = EWiesbadenInputAction::RennenHalten;
			Rennen.Beschreibung = TEXT("Rennen (halten)");
			Rennen.Tasten = { EKeys::LeftShift };
			Rennen.Gamepad = { EKeys::Gamepad_LeftThumbstick };
			B.Add(Rennen);

			FWiesbadenBelegung Ducken;
			Ducken.Action = EWiesbadenInputAction::DuckenHalten;
			Ducken.Beschreibung = TEXT("Ducken (halten)");
			Ducken.Tasten = { EKeys::X };
			Ducken.Gamepad = { EKeys::Gamepad_RightThumbstick };
			B.Add(Ducken);

			// Mausrad wird NICHT als Taste gelesen: es laeuft ueber RouteMausrad
			// (Zoom/Wechsel/Schnittebene). Die Schultertasten sind der
			// Gamepad-Weg fuer denselben Wechsel.
			FWiesbadenBelegung WaffeVor;
			WaffeVor.Action = EWiesbadenInputAction::WaffeVor;
			WaffeVor.Beschreibung = TEXT("Naechste Waffe (Rad / RB)");
			WaffeVor.Gamepad = { EKeys::Gamepad_RightShoulder };
			B.Add(WaffeVor);

			FWiesbadenBelegung WaffeZurueck;
			WaffeZurueck.Action = EWiesbadenInputAction::WaffeZurueck;
			WaffeZurueck.Beschreibung = TEXT("Vorherige Waffe (Rad / LB)");
			WaffeZurueck.Gamepad = { EKeys::Gamepad_LeftShoulder };
			B.Add(WaffeZurueck);

			FWiesbadenBelegung Ansicht;
			Ansicht.Action = EWiesbadenInputAction::AnsichtWechseln;
			Ansicht.Beschreibung = TEXT("Ansicht wechseln (Ego/Schulter)");
			Ansicht.Tasten = { EKeys::C };
			Ansicht.Gamepad = { EKeys::Gamepad_FaceButton_Top };
			B.Add(Ansicht);

			// Einsteigen. Am Gamepad X - IM FAHRZEUG ist X aber der
			// Rueckwaertsgang (WiesbadenCar), dort nutzt der GameMode fuer
			// das Aussteigen Y (FaceButton_Top). Diese Zeile gilt dem
			// Eintreten zu Fuss; die Ausnahme steht im GameMode.
			FWiesbadenBelegung Einsteigen;
			Einsteigen.Action = EWiesbadenInputAction::Einsteigen;
			Einsteigen.Beschreibung = TEXT("Einsteigen / Interagieren");
			Einsteigen.Tasten = { EKeys::F };
			Einsteigen.Gamepad = { EKeys::Gamepad_FaceButton_Left };
			B.Add(Einsteigen);

			return B;
		}();
		return Tabelle;
	}

	bool TriggerGedrueckt(float Achse, float Schwelle)
	{
		// >= Schwelle: der Trigger meldet selten exakt den Schwellwert, aber
		// genau 0.35 kam bei leichten Anschlaegen vor (gemessen am RT).
		return Achse >= Schwelle;
	}

	bool IsActionDown(const APlayerController* PC, EWiesbadenInputAction Action)
	{
		if (!PC)
		{
			return false;
		}

		for (const FWiesbadenBelegung& Zeile : Belegungen())
		{
			if (Zeile.Action != Action)
			{
				continue;
			}
			for (const FKey& Key : Zeile.Tasten)
			{
				if (PC->IsInputKeyDown(Key))
				{
					return true;
				}
			}
			for (const FKey& Key : Zeile.Gamepad)
			{
				if (Zeile.bAnalog)
				{
					if (TriggerGedrueckt(PC->GetInputAnalogKeyState(Key)))
					{
						return true;
					}
				}
				else if (PC->IsInputKeyDown(Key))
				{
					return true;
				}
			}
		}
		return false;
	}

	EMausradRoute RouteMausrad(bool bSchneidendeWaffe, bool bZielt)
	{
		// Die schneidende Waffe hat Vorrang: das Rad dreht die Schnittebene,
		// auch waehrend gezielt wird (Dead-Space-Prinzip).
		if (bSchneidendeWaffe)
		{
			return EMausradRoute::Schnittebene;
		}
		return bZielt ? EMausradRoute::Zoom : EMausradRoute::Waffenwechsel;
	}

	float ZoomStufe(float Aktuell, int32 Klicks, float Schritt, float MaxZoom)
	{
		const float Oben = FMath::Max(MaxZoom, 1.0f);
		return FMath::Clamp(Aktuell + Klicks * Schritt, 1.0f, Oben);
	}
}	const TArray<FWbHelicopterLessonStep>& WiesbadenHelicopterLesson::Steps()

{
	static const TArray<FWbHelicopterLessonStep> LessonSteps = {
		{ TEXT("Kollektiv"), TEXT("Fuehre erst RT/Leertaste nach oben, dann LT/Strg nach unten."), EWiesbadenHeliAction::CollectiveUp, EWiesbadenHeliAction::CollectiveDown, false },
		{ TEXT("Nicken"), TEXT("Tippe kurz W oder S beziehungsweise den linken Stick vor/zurueck."), EWiesbadenHeliAction::Pitch, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Rollen"), TEXT("Tippe kurz A oder D beziehungsweise den linken Stick links/rechts."), EWiesbadenHeliAction::Roll, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Gieren"), TEXT("Drehe die Nase kurz mit Q/E oder LB/RB."), EWiesbadenHeliAction::Yaw, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Kameramodus"), TEXT("Schalte mit C oder R3 durch Follow, Orbit und Cockpit."), EWiesbadenHeliAction::CameraMode, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Freie Sicht"), TEXT("Bewege die Maus oder den rechten Stick, um dich umzusehen. In der Cockpitansicht gibt es kein Umsehen - mit C/R3 erst zur Aussenansicht."), EWiesbadenHeliAction::Look, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Suchscheinwerfer"), TEXT("Schalte mit L oder Steuerkreuz hoch den Suchscheinwerfer um."), EWiesbadenHeliAction::Searchlight, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Landescheinwerfer"), TEXT("Schalte mit B oder Steuerkreuz runter das Landescheinwerferlicht um."), EWiesbadenHeliAction::LandingLight, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Triebwerk"), TEXT("Schalte G/X aus und wieder ein, um den Motor-Schalter zu ueben."), EWiesbadenHeliAction::Engine, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Bordgeschuetz"), TEXT("Sicherer Trockentest: halte Feuer kurz. Es wird weder geschossen noch Munition verbraucht."), EWiesbadenHeliAction::Fire, EWiesbadenHeliAction::MAX, false },
		{ TEXT("Abschluss / Aussteigen"), TEXT("F / Y beendet die Flugstunde jederzeit durch Aussteigen. Enter / A schliesst sie hier ab."), EWiesbadenHeliAction::Exit, EWiesbadenHeliAction::MAX, true },
	};
	return LessonSteps;
}

int32 WiesbadenHelicopterLesson::ClampStep(int32 Step)
{
	return FMath::Clamp(Step, 0, StepCount);
}

int32 WiesbadenHelicopterLesson::AdvanceStep(int32 CurrentStep, bool bSatisfied, bool bSkip)
{
	const int32 Current = ClampStep(CurrentStep);
	// "Ueberspringen" gilt nur fuer die Uebungen. Der Abschlussschritt wird
	// bestaetigt (Enter/A) - ein Skip dort wuerde die Fusszeile versprechen,
	// was der Code nicht tut, und wuerde die Lektion ohne Bestaetigung schliessen.
	const bool bSkipAllowed = bSkip && Current < PracticeStepCount;
	return (Current < StepCount && (bSatisfied || bSkipAllowed))
		? Current + 1
		: Current;
}
