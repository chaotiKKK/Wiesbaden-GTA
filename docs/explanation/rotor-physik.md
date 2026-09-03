# Erklaerung: Rotor-Physik

Warum der Helikopter fliegt, wie er fliegt: dieses Dokument erklaert das reine,
deterministische Rotor-Physik-Modul - was es modelliert und warum es bewusst ohne
Welt-/Actor-Zugriff gebaut ist (dadurch in Automation-Tests pruefbar). Es stand
frueher in der Wurzel-README; als verstaendnisorientierter Hintergrund gehoert es
hierher.

`Vehicles/WiesbadenRotorPhysics` ist ein reines, deterministisches
`FWiesbadenRotorPhysics`-Modul fuer Drehfluegler (kein Welt-/Actor-Zugriff,
daher in Automation-Tests pruefbar). Es modelliert:

- **Governor** haelt die Rotordrehzahl nahe der Soll-Drehzahl, begrenzt durch die Wellenleistung.
- **Lift** = Faktor * omega^2 * CollectivePitch (Blattanstellwinkel).
- **Zyklik** neigt die Rotorscheibe -> horizontale Kraft + Pitch/Roll-Moment.
- **Heckrotor** kompensiert das Reaktionsmoment und liefert die Yaw-Kontrolle (Pedal).
- **Koaxial-Rotoren** (`bCoaxialRotors`, Ka-52-Stil): gegenlaeufiger
  Doppelrotor verdoppelt den Auftrieb, hebt das Reaktionsmoment gegenseitig
  auf (kein Heckrotor) und steuert Yaw per direktem Pedal-Moment
  (differentielle Blattverstellung).
- **Blattspitzenverlust** (Retreating Blade Stall): Ab ~75 % der
  Hoechstgeschwindigkeit (`MaxForwardSpeedMetersPerS`) bricht der Auftrieb der
  ruecklaufenden Blaetter ein und sinkt bis vmax auf ~45 %% - begrenzt die
  Geschwindigkeit realistisch statt mit einem harten Clamp.
- **Autorotation**: Bei Triebwerksausfall und Sinkflug treibt der aufsteigende
  Luftstrom den Rotor an und erhaelt Drehzahl/Auftrieb. `EngineRpm`
  (Triebwerksdrehzahl = Rotordrehzahl * `EngineToMainRotorRatio`, 0 bei
  Triebwerk aus) speist Audio/HUD.

Ausgabe pro Tick: `Force` (N) und `Torque` (N*m) im Fahrzeug-Lokalkoordinatensystem.

