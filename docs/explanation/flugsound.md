# Erklaerung: Flugsound (Helikopter)

Wie der Helikopter klingt und warum ohne jede Audio-Datei: dieses Dokument
erklaert die prozedurale Klangsynthese aus dem Flugzustand und die alternative
Asset-Wiedergabe. Es stand frueher in der Wurzel-README.

`Vehicles/WiesbadenHelicopterAudioComponent` erzeugt den Flugsound aus dem
Flugzustand - zwei Betriebsarten:

- **Prozeduraler Fallback** (Standard, keine Assets noetig):
  `FWiesbadenHelicopterAudioModel` (rein + deterministisch) erzeugt
  int16-PCM - Rotor-Rauschen durch einen Tiefpass (Grenzfrequenz folgt
  Drehzahl/Blattlast), "Wop-Wop"-Amplitudenmodulation mit der
  Blattpassfrequenz (Blattzahl * U/min), plus Motor-Ton (Drehzahl * 8
  Zylinder) bei laufendem Triebwerk. Die Samples werden in einen
  `USoundWaveProcedural` gepusht (Queue-Limit gegen Pufferdrift).
- **Asset-basiert**: `RotorSound`/`EngineSound` (USoundWave) mit
  Pitch/Volume aus Drehzahl und Blattlast (Rotor: Pitch ~ RPM/420, Volume ~
  Last; Motor: Pitch ~ RPM/3000).

Der Heli-Pawn speist die Komponente pro Tick (Rotor-Drehzahl, Collective,
Triebwerksdrehzahl an/aus, Fahrtgeschwindigkeit); bei Triebwerksausfall ist
nur noch das Rotor-Geraeusch zu hoeren (Autorotation).

**Kampfheli-Charakter** (Referenz: Mi-35/28/Ka-52/Mi-8 - Hover & Departure):
der Heli nutzt `BladeSlapDepth` (0.55 statt 0.38) und `RotorCutoffBaseHz`
(110 statt 180) - ein schwerer, tiefer Rotorschlag statt zivilem Surren;
`BladeCount = 3` (Ka-52: 3 Blaetter je Rotor).

