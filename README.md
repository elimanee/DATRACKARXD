# DATRACKARXD

Just a tracker (like keygen music lmao) for my needs. Un petit tracker dans l'esprit de
[Furnace](https://github.com/tildearrow/furnace), en C++ avec Dear ImGui et SDL2 : sons chiptune,
synthèse FM 4 opérateurs, synthèse façon SID, samples, effets master, clavier MIDI, et import de modules
Furnace, DefleMask, FamiTracker, MOD, XM, IT, S3M et MIDI.

![layout](https://img.shields.io/badge/C%2B%2B17-ImGui%20%2B%20SDL2-6d5dfc)

## Fonctionnalités

- 1 à 32 canaux, jusqu'à 8 colonnes d'effets par canal
- Trois types d'instruments :
  - **Puce** : pulse (4 duty), triangle 32 pas (NES), saw, noise LFSR (normal / métallique / périodique), sine, wavetable 32×16
  - **FM** 4 opérateurs façon Yamaha OPN (Mega Drive) : 8 algorithmes, feedback, enveloppes AR/DR/SL/D2R/RR, presets
  - **Sample** : chargement de fichiers `.wav`, boucles (avant, ping-pong, arrière), multi-samples par note, fadeout
- **Synthèse façon SID** (C64) : filtre résonant passe-bas / passe-bande / passe-haut sur n'importe quel instrument, ring mod, hard sync, largeur de pulse fine avec PWM
- **Macros** façon Furnace sur tous les instruments : volume, arpège (relatif ou note fixe), duty, waveform, pitch, cutoff (avec loop `|` et release `/`)
- **Mixer** : réverbération, delay ping-pong calé sur le tempo, filtre master, envois par canal
- **Clavier MIDI** : jouer, faire des accords et enregistrer depuis un clavier MIDI (vélocité incluse)
- **Presets d'instruments** : `.dti` (sauver / charger), import `.fui` (Furnace) et `.wav`
- **Import** des formats `.fur` (Furnace), `.dmf` (DefleMask), `.ftm` / `.dnm` / `.0cc` (FamiTracker), `.mod`, `.xm`, `.it`, `.s3m` et `.mid`
- **Export** WAV, OGG, MIDI, et **lecteur keygen** : un programme autonome qui joue le morceau avec logo, étoiles et scroller
- Thèmes : Furnace, **Keygen** (néon, avec un scroller sinus, des barres copper et un champ d'étoiles), FastTracker, Amber CRT
- Éditeur de patterns au clavier (disposition physique, fonctionne en AZERTY comme en QWERTY), sélection, copier/coller, transposition
- Piano roll façon FL Studio, avec un mode accords (« Voices ») qui répartit les notes sur plusieurs canaux
- Annuler / refaire partout : notes, instruments, orders, réglages du morceau, samples
- Liste d'orders par canal (comme Furnace), ajout / duplication / clonage
- Oscilloscope par canal, mute / solo
- Sauvegarde dans un format texte `.dtk`, export WAV (mix complet ou une piste par canal), OGG et MIDI
- Une démo keygen intégrée qui se charge au démarrage

## Télécharger

Les versions compilées pour Windows, Linux et macOS sont sur la page
[Releases](https://github.com/elimanee/DATRACKARXD/releases). Il suffit de décompresser l'archive et
de lancer `datrackarxd` (ou `datrackarxd.exe`) : rien d'autre à installer.

- **macOS** : l'application n'est pas signée. Au premier lancement, clic droit > Ouvrir, ou
  `xattr -d com.apple.quarantine datrackarxd` dans un terminal.
- **Windows** : SmartScreen peut avertir pour la même raison ; « Informations complémentaires » > « Exécuter quand même ».

Pour publier une nouvelle version, mettre à jour `VERSION` dans `CMakeLists.txt`, puis au choix :
- dans l'onglet **Actions** de GitHub, ouvrir le workflow « Build », cliquer sur « Run workflow » et
  taper la version (par exemple `v0.2.0`) ;
- ou pousser un tag `vX.Y.Z`.

GitHub Actions compile les trois systèmes, crée le tag si besoin et publie la release avec les archives.

## Compiler

Il faut CMake ≥ 3.16 et un compilateur C++17. ImGui, RtMidi, libogg et libvorbis sont téléchargés
automatiquement ; SDL2 aussi s'il n'est pas installé. Sous Linux, l'entrée MIDI utilise ALSA (`libasound2-dev`).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/datrackarxd              # ou build\Release\datrackarxd.exe sous Windows
```

Sous Linux, installer `libsdl2-dev` évite de recompiler SDL2.

## Ligne de commande

```sh
datrackarxd song.dtk                       # ouvrir un morceau (ou un .fur/.mod/.xm/.it/.s3m)
datrackarxd --export song.xm out.wav 2     # rendu en WAV (2 boucles)
datrackarxd --export song.ftm out.ogg      # rendu en OGG Vorbis
datrackarxd --export-midi song.dtk out.mid # écrire un fichier MIDI
datrackarxd --export-stems song.fur piste  # une piste WAV par canal : piste_01.wav, piste_02.wav...
datrackarxd --convert song.it song.dtk     # importer un module (ou un .mid) et l'enregistrer en .dtk
datrackarxd --convert-instrument lead.fui lead.dti   # preset d'instrument depuis un .fui / .wav
datrackarxd --export-player song.dtk intro "LOGO" "texte du scroller"   # lecteur keygen autonome
datrackarxd --export-demo demo.wav         # rendu de la démo
datrackarxd --save-demo demo.dtk           # écrire la démo dans un fichier
```

## Clavier

| Touches | Action |
|---|---|
| `Z S X D C V G B H N J M` | Notes (rangée du bas, position physique) |
| `Q 2 W 3 E R 5 T 6 Y 7 U I` | Notes, octave + 1 |
| `1` ou `²`/`` ` `` | Note off |
| `0-9 A-F` | Valeurs hexa (instrument, volume, effets) |
| `Espace` | Mode Record (édition) on/off |
| `Entrée` / `Shift+Entrée` | Play/stop depuis le début de l'order / depuis le curseur |
| `F5` / `F6` / `F8` | Jouer le morceau / jouer depuis le curseur / stop |
| Flèches, `PgUp/PgDn`, `Home/End` | Déplacement |
| `Tab` / `Shift+Tab` | Canal suivant / précédent |
| `Shift` + déplacement, glisser souris | Sélection |
| `Ctrl+C / X / V` | Copier / couper / coller |
| `Ctrl+A` | Sélectionner le canal, puis tout le pattern |
| `Ctrl+Z` / `Ctrl+Y` | Annuler / refaire |
| `Ctrl+↑/↓` (+`Shift`) | Transposer de 1 (12) demi-tons |
| `Suppr` | Effacer |
| `Inser` / `Retour arrière` | Insérer / supprimer une ligne |
| Pavé num. `/` `*` | Octave - / + |
| `Ctrl+N / O / S` | Nouveau / ouvrir / sauvegarder |
| Clic sur un nom de canal | Mute (clic droit : solo) |

Hors de l'éditeur de patterns, les touches de notes jouent l'instrument courant (pratique dans l'éditeur d'instruments).

## Effets

| Effet | Description |
|---|---|
| `00xy` | Arpège : note, +x, +y demi-tons |
| `01xx` / `02xx` | Glissement de hauteur vers le haut / bas (xx/32 demi-ton par tick) |
| `03xx` | Portamento vers la note de la ligne |
| `04xy` | Vibrato : vitesse x, profondeur y |
| `05xy` / `06xy` | Glissement de volume pendant que le vibrato / le portamento continue |
| `07xy` | Trémolo : vitesse x, profondeur y |
| `08xy` | Panoramique : volume gauche x, droite y |
| `0Axy` | Glissement de volume : +x ou -y par tick |
| `0Bxx` | Sauter à l'order xx |
| `0Cxx` | Redéclencher la note tous les xx ticks |
| `0Dxx` | Aller à la ligne xx de l'order suivant |
| `0Fxx` | Vitesse (ticks par ligne) |
| `10xx` | Forme d'onde (0 pulse … 5 wavetable ; numéro de wavetable sur un canal wavetable) |
| `12xx` | Duty (0-3), ou largeur de pulse (01-FF) avec « Fine pulse width ». Noise : 1 = métallique, 3 = périodique |
| `13xx` | Cutoff du filtre de l'instrument (00-FF) |
| `80xx` | Panoramique linéaire : 00 gauche, 80 centre, FF droite |
| `90xx`-`92xx` | Position de départ du sample (octets 0, 1, 2) |
| `C0xx`-`C3xx` | Vitesse des ticks en Hz |
| `E1xy` / `E2xy` | Glisser de y demi-tons vers le haut / bas à la vitesse x |
| `E6xx` | Boucle de pattern : `E600` marque le début, `E6xx` répète xx fois |
| `ECxx` | Couper la note après xx ticks |
| `EDxx` | Retarder la ligne de xx ticks |
| `EExx` | Répéter la ligne xx fois (pattern delay) |
| `F0xx` | Tempo en BPM |
| `F1xx` / `F2xx` | Petit glissement de hauteur, une seule fois |
| `F3xx` / `F4xx` | Monter / baisser le volume de xx, une seule fois |

Colonne volume : `00`-`7F`. Un effet à `00` arrête l'arpège, les glissements ou le vibrato.

## Piano roll

L'onglet **Piano roll** (à côté de Pattern) montre le pattern du canal courant sous forme de notes,
comme dans FL Studio :

| Action | Effet |
|---|---|
| Clic dans la grille | Ajoute une note (longueur « Length », instrument courant), qu'on peut glisser tout de suite |
| Glisser une note | La déplacer (temps et hauteur) |
| Glisser le bord droit | Changer sa durée |
| Clic droit (ou glisser) | Effacer |
| Ctrl + glisser | Sélection rectangulaire (Shift pour ajouter), Ctrl+A tout sélectionner |
| Flèches | Déplacer la sélection (Haut/Bas : demi-ton, Shift : octave), Suppr pour effacer |
| Molette | Défiler ; Shift : horizontal, Ctrl : zoom temps, Alt : zoom hauteur |
| Clavier à gauche | Écouter une note |
| Règle en haut | Placer le curseur (et la lecture) |
| Ligne « Vol » en bas | Dessiner la vélocité (colonne volume) |

Les notes grisées sont celles des autres canaux (case « Ghosts »). Un canal de tracker ne joue qu'une
note à la fois : si deux notes se chevauchent, la première s'arrête où la suivante commence.

Pour les **accords**, régler « Voices » sur 2 à 8 : le piano roll montre alors ensemble le canal courant
et les suivants, et chaque note posée va sur un canal libre. Tout est annulable avec Ctrl+Z.

## Clavier MIDI

Le menu **MIDI** liste les entrées MIDI (le choix est retenu au prochain lancement). Les notes jouent
l'instrument courant ; un accord se répartit sur les canaux « Voices » du piano roll. En mode **Record** :

- à l'arrêt, les notes s'écrivent sur la ligne du curseur, qui avance quand toutes les touches sont relâchées ;
- pendant la lecture, elles s'écrivent à la ligne jouée, avec un note off au relâchement.

« Velocity -> volume » met la vélocité dans la colonne volume ; un « program change » choisit l'instrument.

## Mixer et effets

La fenêtre **Mixer** contient la réverbération, le delay (temps en lignes, donc calé sur le tempo,
option ping-pong), le filtre master et le volume général, puis pour chaque canal : niveau, panoramique,
envoi vers la réverb et vers le delay. Tout est enregistré dans le morceau.

## Synthèse façon SID

Dans l'éditeur d'instruments, la section **SID** ajoute :

- un **filtre** résonant (passe-bas, passe-bande, passe-haut) sur tout type d'instrument, avec la macro
  **Cutoff** et l'effet `13xx` pour le faire bouger ;
- **Ring mod** et **Sync** avec le canal précédent (le canal 1 utilise le dernier), comme les voix du C64 ;
- **Fine pulse width** : largeur de pulse 1-255 au lieu de 4 duty, avec un balayage **PWM** automatique.

Les instruments C64 des fichiers Furnace et DefleMask sont importés avec leur filtre, leur ring mod et leur sync.

## Presets d'instruments

Dans la liste des instruments, **Save...** enregistre l'instrument choisi en `.dti` (avec ses samples et
wavetables) et **Load...** ajoute un instrument depuis un `.dti`, un `.fui` de Furnace ou un `.wav`.

## Export

Dans le menu Fichier :

- **Export WAV** / **Export OGG** (qualité réglable) / **Export MIDI** (une piste par canal, bruit sur le canal batterie) ;
- **Export keygen player** : choisir le logo, le titre et le texte du scroller ; on obtient un programme
  autonome qui joue le morceau en boucle avec le champ d'étoiles, les barres copper, le logo et le
  scroller (Espace : pause, F : plein écran, Échap : quitter). Le programme n'a besoin de rien d'autre.

## Import de modules

Fichier > Open / import (Ctrl+O) ouvre aussi les `.fur`, `.dmf`, `.ftm`, `.mod`, `.xm`, `.it`, `.s3m` et `.mid`. Le morceau
est converti vers le moteur de DATRACKARXD, donc le rendu est proche de l'original sans être identique :

- **Furnace (.fur)** : chaque canal de puce devient la voix la plus proche (FM, pulse, triangle, noise,
  wavetable ou sample). Les instruments FM (OPN, OPM, OPL, OPLL), les macros, les wavetables, les samples
  et l'enveloppe Game Boy ou C64 (avec filtre, ring mod et sync) sont convertis. Les effets propres à une
  puce (LFO FM, etc.) sont ignorés.
- **DefleMask (.dmf)** : Genesis, SMS, Game Boy, PC Engine, NES, C64, Arcade, Neo Geo… comme pour Furnace.
  Les samples jouent à leur vitesse d'origine.
- **FamiTracker (.ftm, .dnm, .0cc)** : 2A03 avec DPCM, et les extensions VRC6, VRC7, FDS, MMC5, N163 et
  Sunsoft 5B ; séquences, grooves et tempo.
- **MIDI (.mid)** : les notes sont alignées sur les lignes (doubles-croches, ou triples-croches si
  besoin), chaque partie reçoit un instrument puce selon son programme General MIDI et autant de canaux
  que de notes simultanées ; la batterie (canal 10) devient des percussions puce.
- **MOD / S3M / XM / IT** : samples, boucles, multi-samples, enveloppes de volume (converties en macros),
  fadeout, effets classiques (arpège, slides, vibrato, trémolo, retrigger, boucles de pattern…).
  Non gérés : enveloppes de panoramique et de pitch, filtres, volumes globaux et de canal, NNA (IT).

Une fois importé, Ctrl+S l'enregistre en `.dtk`.

## Macros

Dans l'éditeur d'instruments, chaque macro se dessine à la souris ou se tape en texte :

```
15 13 11 | 9 8 7 / 4 2 0
```

`|` marque le début de la boucle, et `/` après une valeur la maintient jusqu'au note off.
La macro volume avec un point de release donne un vrai « release » (fondu après le note off).

## Code

- `src/song.*` : modèle de données et format `.dtk`
- `src/engine.*` : lecture des patterns, effets, macros, synthèse (puce, FM, samples, SID), export WAV
- `src/effects.*` : filtre résonant, réverbération, delay
- `src/export.cpp`, `src/midifile.cpp` : export OGG, import / export MIDI
- `src/import*.cpp`, `src/fur_internal.h`, `src/inflate.cpp` : import FUR, DMF, FTM, MOD, S3M, XM, IT (et décompression zlib)
- `src/midi.*` : entrée clavier MIDI (RtMidi)
- `src/payload.*`, `src/player.cpp`, `src/keygen_fx.*` : lecteur keygen autonome et sa scène
- `src/theme.*` : thèmes de couleurs
- `src/app.*` : application, menus, raccourcis, dialogue de fichiers
- `src/ui_pattern.cpp` : éditeur de patterns
- `src/ui_instrument.cpp` : liste et éditeur d'instruments (puce, FM, sample), fenêtre Samples
- `src/ui_windows.cpp` : orders, song, mixer, oscilloscope, scroller keygen, aide
- `src/demo.cpp` : morceau de démo
