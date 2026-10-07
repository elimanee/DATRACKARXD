# DATRACKARXD

Just a tracker (like keygen music lmao) for my needs. Un petit tracker dans l'esprit de
[Furnace](https://github.com/tildearrow/furnace), en C++ avec Dear ImGui et SDL2 : sons chiptune,
synthèse FM 4 opérateurs, samples, et import de modules Furnace, MOD, XM, IT et S3M.

![layout](https://img.shields.io/badge/C%2B%2B17-ImGui%20%2B%20SDL2-6d5dfc)

## Fonctionnalités

- 1 à 32 canaux, jusqu'à 8 colonnes d'effets par canal
- Trois types d'instruments :
  - **Puce** : pulse (4 duty), triangle 32 pas (NES), saw, noise LFSR (normal / métallique / périodique), sine, wavetable 32×16
  - **FM** 4 opérateurs façon Yamaha OPN (Mega Drive) : 8 algorithmes, feedback, enveloppes AR/DR/SL/D2R/RR, presets
  - **Sample** : chargement de fichiers `.wav`, boucles (avant, ping-pong, arrière), multi-samples par note, fadeout
- **Macros** façon Furnace sur tous les instruments : volume, arpège (relatif ou note fixe), duty, waveform, pitch (avec loop `|` et release `/`)
- **Import** des formats `.fur` (Furnace), `.mod`, `.xm`, `.it` et `.s3m`
- Thèmes : Furnace, **Keygen** (néon, avec un scroller sinus, des barres copper et un champ d'étoiles), FastTracker, Amber CRT
- Éditeur de patterns au clavier (disposition physique, fonctionne en AZERTY comme en QWERTY), sélection, copier/coller, undo/redo, transposition
- Liste d'orders par canal (comme Furnace), ajout / duplication / clonage
- Oscilloscope par canal, mute / solo
- Sauvegarde dans un format texte `.dtk`, export WAV (mix complet ou une piste par canal)
- Une démo keygen intégrée qui se charge au démarrage

## Télécharger

Les versions compilées pour Windows, Linux et macOS sont sur la page
[Releases](https://github.com/elimanee/DATRACKARXD/releases). Il suffit de décompresser l'archive et
de lancer `datrackarxd` (ou `datrackarxd.exe`) : rien d'autre à installer.

- **macOS** : l'application n'est pas signée. Au premier lancement, clic droit > Ouvrir, ou
  `xattr -d com.apple.quarantine datrackarxd` dans un terminal.
- **Windows** : SmartScreen peut avertir pour la même raison ; « Informations complémentaires » > « Exécuter quand même ».

Pour publier une nouvelle version : pousser un tag `vX.Y.Z`. GitHub Actions compile les trois
systèmes et crée la release avec les archives.

## Compiler

Il faut CMake ≥ 3.16 et un compilateur C++17. ImGui est téléchargé automatiquement ; SDL2 aussi s'il
n'est pas installé.

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
datrackarxd --export-stems song.fur piste  # une piste WAV par canal : piste_01.wav, piste_02.wav...
datrackarxd --convert song.it song.dtk     # importer un module et l'enregistrer en .dtk
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
| `Espace` | Mode édition on/off |
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
| `12xx` | Duty (0-3). Noise : 1 = métallique, 3 = périodique |
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

## Import de modules

Fichier > Open / import (Ctrl+O) ouvre aussi les `.fur`, `.mod`, `.xm`, `.it` et `.s3m`. Le morceau
est converti vers le moteur de DATRACKARXD, donc le rendu est proche de l'original sans être identique :

- **Furnace (.fur)** : chaque canal de puce devient la voix la plus proche (FM, pulse, triangle, noise,
  wavetable ou sample). Les instruments FM (OPN, OPM, OPL, OPLL), les macros, les wavetables, les samples
  et l'enveloppe Game Boy ou C64 sont convertis. Les effets propres à une puce (filtres, LFO FM, etc.) sont ignorés.
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
- `src/engine.*` : lecture des patterns, effets, macros, synthèse (puce, FM, samples), export WAV
- `src/import*.cpp`, `src/inflate.cpp` : import FUR, MOD, S3M, XM, IT (et décompression zlib)
- `src/theme.*` : thèmes de couleurs
- `src/app.*` : application, menus, raccourcis, dialogue de fichiers
- `src/ui_pattern.cpp` : éditeur de patterns
- `src/ui_instrument.cpp` : liste et éditeur d'instruments (puce, FM, sample), fenêtre Samples
- `src/ui_windows.cpp` : orders, song, oscilloscope, scroller keygen, aide
- `src/demo.cpp` : morceau de démo
