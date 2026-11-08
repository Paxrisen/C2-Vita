C2-Vita Patcher 1.0
===================

DEUTSCH
-------
Erzeugt aus deiner eigenen civ2.exe (Civilization II Multiplayer Gold Edition 5.4.0f)
die schnelle, native C2-Vita VPK fuer die PS Vita.

Voraussetzungen
- Windows 10/11, 64 Bit, mind. 4 GB RAM, ca. 1 GB freier Speicher
- Internetverbindung beim ersten Start (Python ~11 MB und VitaSDK ~150 MB werden geladen)
- Deine eigene civ2.exe
- WICHTIG: Die "Intelligente App-Steuerung" (Smart App Control) von Windows 11 muss
  AUSGESCHALTET sein. Sonst blockiert Windows die VitaSDK-Werkzeuge und der Patcher
  bricht beim Schritt "Baue die VPK" ab ("Eine Anwendungssteuerungsrichtlinie hat
  diese Datei blockiert").
  Ausschalten: Windows-Sicherheit > App- & Browsersteuerung > Intelligente
  App-Steuerung > Einstellungen > Aus.

Intelligente App-Steuerung danach wieder einschalten (ohne Neuinstallation)
Windows bietet das Wiedereinschalten in den Einstellungen nicht mehr an. Es geht
ueber die Registrierungsdatenbank (inoffiziell, auf eigene Verantwortung):
1. Eingabeaufforderung als Administrator oeffnen.
2. Einschalten:
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v VerifiedAndReputablePolicyState /t REG_DWORD /d 1 /f
   (Wert 2 = Auswertungsmodus, 0 = aus)
3. PC neu starten.

Anleitung
1. Doppelklick auf "C2-Vita-Patcher.bat" (oder civ2.exe auf die .bat ziehen).
2. civ2.exe auswaehlen.
3. Warten: Uebersetzen und Kompilieren dauern je nach PC 10-30 Minuten.
4. "C2-Vita_1.0_nativ.vpk" liegt danach neben der .bat. Mit VitaShell installieren.
5. Den kompletten Spielordner nach ux0:data/civ2/ kopieren (falls noch nicht geschehen).

Die erzeugte VPK enthaelt Code aus DEINER civ2.exe: nur fuer dich, nicht weitergeben.
Der Patcher selbst enthaelt keinerlei Spielcode oder Spieldaten.

ENGLISH
-------
Builds the fast, native C2-Vita VPK for PS Vita from your own civ2.exe
(Civilization II Multiplayer Gold Edition 5.4.0f).

Requirements
- Windows 10/11, 64-bit, at least 4 GB RAM, about 1 GB free disk space
- Internet on first run (Python ~11 MB and VitaSDK ~150 MB are downloaded)
- Your own civ2.exe
- IMPORTANT: Windows 11 "Smart App Control" must be TURNED OFF. Otherwise Windows
  blocks the VitaSDK tools and the patcher fails at "Building the VPK"
  ("An Application Control policy has blocked this file").
  Turn it off: Windows Security > App & browser control > Smart App Control >
  Settings > Off.

Turning Smart App Control back on (without reinstalling Windows)
Windows no longer offers this in Settings, but the registry works
(unofficial, at your own risk):
1. Open Command Prompt as administrator.
2. Turn it on:
   reg add "HKLM\SYSTEM\CurrentControlSet\Control\CI\Policy" /v VerifiedAndReputablePolicyState /t REG_DWORD /d 1 /f
   (value 2 = evaluation mode, 0 = off)
3. Restart the PC.

How to use
1. Double-click "C2-Vita-Patcher.bat" (or drag civ2.exe onto it).
2. Select civ2.exe.
3. Wait: translating and compiling takes 10-30 minutes depending on your PC.
4. "C2-Vita_1.0_nativ.vpk" is created next to the .bat. Install it with VitaShell.
5. Copy the complete game folder to ux0:data/civ2/ (if not done yet).

The resulting VPK contains code from YOUR civ2.exe: personal use only, do not share.
The patcher itself contains no game code or game data.

Licenses: data/pkg/licenses/ (C2-Vita: MIT; winx86, Box86: MIT; newlib,
libstdc++, pthread-embedded from vitasdk under their free licenses).
Downloaded at runtime: Python (PSF License), VitaSDK (various free licenses).
"Civilization" is a trademark of its respective owners. Not an official product.
