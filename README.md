# DATRACKARXD

Just a tracker (like keygen music lmao) for my needs. Un petit tracker chiptune dans l'esprit de
[Furnace](https://github.com/tildearrow/furnace), en C++ avec Dear ImGui et SDL2.

![layout](https://img.shields.io/badge/C%2B%2B17-ImGui%20%2B%20SDL2-6d5dfc)

## Fonctionnalités

- 1 à 16 canaux, jusqu'à 4 colonnes d'effets par canal
- Ondes : pulse (4 duty), triangle 32 pas (NES), saw, noise LFSR (normal / métallique), sine, wavetable 32×16
- Instruments avec **macros** façon Furnace : volume, arpège, duty, waveform, pitch (avec loop `|` et release `/`)
- Éditeur de patterns au clavier (disposition physique, fonctionne en AZERTY comme en QWERTY), sélection, copier/coller, undo/redo, transposition
- Liste d'orders par canal (comme Furnace), ajout / duplication / clonage
- Oscilloscope par canal, mute / solo
- Sauvegarde dans un format texte `.dtk`, export WAV
- Une démo keygen intégrée qui se charge au démarrage

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
datrackarxd song.dtk                      # ouvrir un morceau
datrackarxd --export song.dtk out.wav 2   # rendu en WAV (2 boucles)
datrackarxd --export-demo demo.wav        # rendu de la démo
datrackarxd --save-demo demo.dtk          # écrire la démo dans un fichier
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
| `08xy` | Panoramique : volume gauche x, droite y |
| `0Axy` | Glissement de volume : +x ou -y par tick |
| `0Bxx` | Sauter à l'order xx |
| `0Dxx` | Aller à la ligne xx de l'order suivant |
| `0Fxx` | Vitesse (ticks par ligne) |
| `10xx` | Forme d'onde (0 pulse … 5 wavetable) |
| `12xx` | Duty (0-3) |
| `ECxx` | Couper la note après xx ticks |
| `EDxx` | Retarder la ligne de xx ticks |

Colonne volume : `00`-`7F`. Un effet à `00` arrête l'arpège, les glissements ou le vibrato.

## Macros

Dans l'éditeur d'instruments, chaque macro se dessine à la souris ou se tape en texte :

```
15 13 11 | 9 8 7 / 4 2 0
```

`|` marque le début de la boucle, et `/` après une valeur la maintient jusqu'au note off.
La macro volume avec un point de release donne un vrai « release » (fondu après le note off).

## Code

- `src/song.*` : modèle de données et format `.dtk`
- `src/engine.*` : lecture des patterns, effets, macros, synthèse, export WAV
- `src/app.*` : application, menus, raccourcis, dialogue de fichiers
- `src/ui_pattern.cpp` : éditeur de patterns
- `src/ui_instrument.cpp` : liste et éditeur d'instruments
- `src/ui_windows.cpp` : orders, song, oscilloscope, aide
- `src/demo.cpp` : morceau de démo
