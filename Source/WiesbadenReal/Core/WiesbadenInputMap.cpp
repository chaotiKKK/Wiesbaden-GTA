// Copyright (c) 2026 Wiesbaden Real. All Rights Reserved.

#include "Core/WiesbadenInputMap.h"

#include "GameFramework/PlayerController.h"

namespace WiesbadenInputMap
{
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
}
