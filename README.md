# DATRACKARXD

Just a tracker (like keygen music lmao) for my needs. Un petit tracker dans l'esprit de
[Furnace](https://github.com/tildearrow/furnace), en C++ avec Dear ImGui et SDL2 : sons chiptune,
vraies puces émulées (AY / YM2149, SN76489, Game Boy, OPL3), synthèse FM 4 opérateurs, synthèse façon
SID, samples, effets par canal et master, clavier MIDI, et import de modules Furnace, DefleMask,
FamiTracker, MOD, XM, IT, S3M et MIDI.

![layout](https://img.shields.io/badge/C%2B%2B17-ImGui%20%2B%20SDL2-6d5dfc)

## Fonctionnalités

- 1 à 32 canaux, jusqu'à 8 colonnes d'effets par canal
- Types d'instruments :
  - **Puce** : pulse (4 duty), triangle 32 pas (NES), saw, noise LFSR (normal / métallique / périodique), sine, wavetable 32×16
  - **FM** 4 opérateurs façon Yamaha OPN (Mega Drive) : 8 algorithmes, feedback, enveloppes AR/DR/SL/D2R/RR, presets
  - **Sample** : fichiers `.wav`, `.flac`, `.mp3`, `.ogg` ou enregistrés au micro, boucles (avant, ping-pong, arrière), multi-samples par note, fadeout
  - **Vraies puces émulées** : **AY-3-8910 / YM2149** (ZX Spectrum, CPC, Atari ST, avec l'enveloppe « buzzer »),
    **SN76489** (Master System), **Game Boy** (pulse avec sweep, wave, noise) et **OPL3** (AdLib, 2 ou 4 opérateurs)
- **Enveloppe ADSR**, deux **LFO** (pitch, volume, largeur de pulse, cutoff, panoramique) et **morphing de wavetable** sur les instruments
- **Effets par canal** : bitcrusher, distorsion, chorus, EQ 3 bandes, compresseur
- **Éditeur de samples** : sélection, zoom, couper / copier / coller, rogner, normaliser, fondus, inverser,
  amplifier, rééchantillonner, dessin au crayon, boucle sur la sélection, enregistrement au micro
- **Synthèse façon SID** (C64) : filtre résonant passe-bas / passe-bande / passe-haut sur n'importe quel instrument, ring mod, hard sync, largeur de pulse fine avec PWM
- **Macros** façon Furnace sur tous les instruments : volume, arpège (relatif ou note fixe), duty, waveform, pitch, cutoff (avec loop `|` et release `/`)
- **Mixer** : réverbération, delay ping-pong calé sur le tempo, filtre master, envois par canal
- **Clavier MIDI** : jouer, faire des accords et enregistrer depuis un clavier MIDI (vélocité incluse)
- **Presets d'instruments** : `.dti` (sauver / charger), import `.fui` (Furnace) et `.wav`
- **Import** des formats `.fur` (Furnace), `.dmf` (DefleMask), `.ftm` / `.dnm` / `.0cc` (FamiTracker), `.mod`, `.xm`, `.it`, `.s3m` et `.mid`
- **Export** WAV, OGG, MIDI, et **lecteur keygen** : un programme autonome qui joue le morceau avec logo, étoiles et scroller
- **DATRACKARXD Keygen** : une appli séparée pour fabriquer ces lecteurs, avec aperçu en direct
- Thèmes : Furnace, **Keygen** (néon, avec un scroller sinus, des barres copper et un champ d'étoiles), FastTracker, Amber CRT
- Éditeur de patterns au clavier (disposition physique, fonctionne en AZERTY comme en QWERTY), sélection, copier/coller, transposition
- Piano roll façon FL Studio, avec un mode accords (« Voices ») qui répartit les notes sur plusieurs canaux
- Annuler / refaire partout : notes, instruments, orders, réglages du morceau, samples
- Liste d'orders par canal (comme Furnace), ajout / duplication / clonage
- Oscilloscope par canal, mute / solo
- Sauvegarde dans un format texte `.dtk`, export WAV (mix complet ou une piste par canal), OGG et MIDI
- Une démo keygen intégrée qui se charge au démarrage, et `songs/chips.dtk` pour entendre les puces émulées

## Télécharger

Les versions compilées pour Windows, Linux et macOS sont sur la page
[Releases](https://github.com/elimanee/DATRACKARXD/releases). Il suffit de décompresser l'archive et
de lancer `datrackarxd` (ou `datrackarxd.exe`) : rien d'autre à installer.

- **macOS** : l'application n'est pas signée. Au premier lancement, clic droit > Ouvrir, ou
  `xattr -d com.apple.quarantine datrackarxd` dans un terminal.
- **Windows** : SmartScreen peut avertir pour la même raison ; « Informations complémentaires » > « Exécuter quand même ».

L'archive contient deux programmes : `datrackarxd` (le tracker) et `datrackarxd-keygen` (l'appli
DATRACKARXD Keygen, voir plus bas).

Pour publier une nouvelle version, mettre à jour `VERSION` dans `CMakeLists.txt`, puis au choix :
- dans l'onglet **Actions** de GitHub, ouvrir le workflow « Build », cliquer sur « Run workflow » et
  taper la version (par exemple `v0.2.0`) ;
- ou pousser un tag `vX.Y.Z`.

GitHub Actions compile les trois systèmes, crée le tag si besoin et publie la release avec les archives.

## Compiler

Il faut CMake ≥ 3.16 et un compilateur C++17. ImGui, RtMidi, libogg, libvorbis, ymfm et dr_libs sont
téléchargés automatiquement ; SDL2 aussi s'il n'est pas installé. Sous Linux, l'entrée MIDI utilise ALSA (`libasound2-dev`).

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
| `10xx` | Forme d'onde (0 pulse … 5 wavetable ; numéro de wavetable sur un canal wavetable). AY : mixer (1 tone, 2 noise, 4 enveloppe). OPL : forme d'onde 0-7 |
| `12xx` | Duty (0-3), ou largeur de pulse (01-FF) avec « Fine pulse width ». Noise : 1 = métallique, 3 = périodique. AY : période du bruit. SN / Game Boy noise : mode |
| `13xx` | Cutoff du filtre de l'instrument (00-FF) |
| `80xx` | Panoramique linéaire : 00 gauche, 80 centre, FF droite |
| `90xx`-`92xx` | Position de départ du sample (octets 0, 1, 2) |
| `C0xx`-`C3xx` | Vitesse des ticks en Hz |
| `E1xy` / `E2xy` | Glisser de y demi-tons vers le haut / bas à la vitesse x |
| `E5xx` | Hauteur fine : `80` = rien, `00`-`FF` = -1 à +1 demi-ton |
| `E6xx` | Boucle de pattern : `E600` marque le début, `E6xx` répète xx fois |
| `E8xx` | Pitch bend : (xx - 80) / 16 demi-tons (`00` = -8, `80` = rien, `A0` = +2, `FF` = +7,9). Glisse sur la ligne et reste jusqu'au prochain `E8`/`E9` |
| `E9xx` | Grand pitch bend : (xx - 80) / 4 demi-tons (`00` = -32, `80` = rien, `B0` = +12, `FF` = +31,75), même glissement |
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
| Ligne du bas, « Vol » | Dessiner la vélocité (colonne volume) |
| Ligne du bas, « Pitch » | Dessiner le pitch bend du canal (`E8xx`/`E9xx`) ; clic droit pour effacer, Ctrl pour arrondir au demi-ton, « +-N » change l'échelle |
| Ligne du bas, « Press » / « Slide » | Expression de chaque note (voir MPE ci-dessous) |

Les notes grisées sont celles des autres canaux (case « Ghosts »). Un canal de tracker ne joue qu'une
note à la fois : si deux notes se chevauchent, la première s'arrête où la suivante commence.

Pour les **accords**, régler « Voices » sur 2 à 8 : le piano roll montre alors ensemble le canal courant
et les suivants, et chaque note posée va sur un canal libre. Tout est annulable avec Ctrl+Z.

### Expression par note (MPE)

Comme le MPE d'Ableton, la case **MPE** donne à chaque note ses propres courbes, qui bougent avec elle
(déplacer, copier, transposer) :

| Action (case MPE cochée) | Effet |
|---|---|
| Glisser le long d'une note | Dessiner sa courbe de hauteur (bend), affichée sur la note ; Ctrl pour arrondir au demi-ton |
| Clic droit (ou glisser) sur une note | Effacer la courbe à cet endroit |
| Double-clic sur une note | Remettre toutes ses expressions à zéro |
| Bord droit d'une note | Changer sa durée, comme d'habitude |
| Ligne « Press » | Pression : le volume de la note au fil du temps (colonne volume) |
| Ligne « Slide » | Timbre : le cutoff du filtre de l'instrument (`13xx`, filtre activé) au fil du temps |

Les lignes Press et Slide modifient les notes sélectionnées, sinon toutes les notes sous la souris.
Dans le pattern, une courbe s'écrit en `E8xx` (`E9xx` au-delà de ±8 demi-tons) sur les lignes de la
note, et un `E880` à sa fin pour que la note suivante reparte sans bend. Un canal ne joue qu'une note à
la fois : avec « Voices », chaque note d'un accord a donc son propre bend, comme en MPE.

## Clavier MIDI

Le menu **MIDI** liste les entrées MIDI (le choix est retenu au prochain lancement). Les notes jouent
l'instrument courant ; un accord se répartit sur les canaux « Voices » du piano roll. En mode **Record** :

- à l'arrêt, les notes s'écrivent sur la ligne du curseur, qui avance quand toutes les touches sont relâchées ;
- pendant la lecture, elles s'écrivent à la ligne jouée, avec un note off au relâchement.

« Velocity -> volume » met la vélocité dans la colonne volume ; un « program change » choisit l'instrument.
La **molette de pitch bend** fait glisser les notes jouées (« Pitch wheel range » règle l'amplitude,
2 demi-tons par défaut) ; en mode Record pendant la lecture, elle s'écrit en `E8xx` ligne par ligne.

**Contrôleur MPE** (Roli Seaboard, LinnStrument, Osmose, Push 3…) : cocher « MPE controller » dans le
menu MIDI. Chaque note arrive sur son propre canal MIDI et reçoit son propre canal de tracker (8 voix si
« Voices » vaut 1). Par note :

- le pitch bend du canal MIDI bend la note (« MPE pitch range », 48 demi-tons par défaut comme le
  standard MPE) → `E8xx`/`E9xx` ;
- le CC74 (slide) règle le cutoff du filtre → `13xx` ;
- la pression (aftertouch de canal) fait varier le volume entre 30 % et la vélocité → colonne volume.

En mode Record pendant la lecture, tout s'écrit ligne par ligne sur le canal de la note, avec `E880` au
note off si la note était bendée.

## Mixer et effets

La fenêtre **Mixer** contient la réverbération, le delay (temps en lignes, donc calé sur le tempo,
option ping-pong), le filtre master et le volume général, puis pour chaque canal : niveau, panoramique,
envoi vers la réverb et vers le delay. Tout est enregistré dans le morceau.

Le bouton **FX** d'un canal ouvre la fenêtre **Channel FX** : une chaîne d'effets jouée de gauche à
droite, avant le mixer (jusqu'à 8). Les flèches changent l'ordre, la case active ou coupe un effet, un
double-clic sur un réglage le remet par défaut.

| Effet | Réglages |
|---|---|
| Bitcrusher | bits (1-16), sous-échantillonnage, mix |
| Distortion | drive, tone, mix, type (douce, écrêtage dur, repli) |
| Chorus | vitesse, profondeur, mix, feedback |
| EQ | graves (200 Hz), médiums (fréquence réglable), aigus (4 kHz), de -24 à +12 dB |
| Compressor | seuil, ratio, attaque, relâchement (avec gain de compensation automatique) |

## Puces émulées

Le **Type** d'un instrument peut être une vraie puce. Chaque canal qui la joue a sa propre puce, émulée au
niveau des registres : périodes entières (les notes aiguës sont un peu fausses, comme sur la vraie
machine), volume en 16 pas, générateurs de bruit et enveloppes matérielles d'origine.

| Puce | Réglages | Macros Duty / Waveform |
|---|---|---|
| **AY-3-8910 / YM2149** | horloge (ZX, CPC, Atari ST, MSX), modèle, tone / noise / enveloppe, période du bruit, forme et vitesse de l'enveloppe | période du bruit / mixer (1 tone, 2 noise, 4 enveloppe) |
| **SN76489** | horloge (NTSC, PAL, BBC), tone, bruit blanc ou périodique | mode du bruit |
| **Game Boy** | canal pulse (duty, enveloppe, sweep), wave (la wavetable de l'instrument, 4 volumes) ou noise (court ou long) | duty / — |
| **OPL3** (AdLib) | 2 ou 4 opérateurs, algorithme, feedback, et par opérateur MULT, TL, AR, DR, SL, RR, KSL, forme d'onde (8), sustain, trémolo, vibrato, KSR | — / forme d'onde |

Astuce AY : le **buzzer** de l'Atari ST s'obtient avec Tone et Noise décochés, Envelope cochée, une forme
en dent de scie (8) ou triangle (10) et « Env. pitch » sur « Note x1 » : l'enveloppe devient la forme
d'onde et suit la note. Sur Game Boy, une macro de volume remplace l'enveloppe matérielle.

L'OPL3 est émulé par [ymfm](https://github.com/aaronsgiles/ymfm). Le filtre SID, l'ADSR, les LFO et
les effets par canal marchent aussi sur les puces.

## Enveloppe, LFO et morphing

La section **Envelope & LFO** de l'éditeur d'instruments (tous les types) ajoute :

- une enveloppe **ADSR** (attaque, déclin, maintien, relâchement en secondes) par-dessus le volume ; la
  note continue de sonner pendant le relâchement après le note off ;
- deux **LFO** relancés à chaque note : cible (pitch, volume, largeur de pulse, cutoff, panoramique),
  forme (sinus, triangle, scie, carré, aléatoire), vitesse en Hz, profondeur et délai avant qu'ils
  n'arrivent.

Sur un instrument wavetable (et le canal wave du Game Boy), **Morph** fait passer la forme d'onde de la
wavetable à une seconde wavetable, une fois, en boucle ou en aller-retour.

## Éditeur de samples

L'onglet **Samples** (à côté de Pattern) charge des `.wav`, `.flac`, `.mp3` et `.ogg` (mixés en mono)
et **Record** enregistre depuis le micro ou l'entrée ligne choisie.

| Action | Effet |
|---|---|
| Glisser sur la forme d'onde | Sélectionner (Shift+clic pour étendre) |
| Molette / Shift+molette | Zoomer / défiler |
| Cut, Copy, Paste, Delete, Crop | Couper, copier, coller (au curseur ou à la place de la sélection), effacer, ne garder que la sélection |
| Silence, Normalize, Fade in/out, Reverse, Invert, Remove DC | Sur la sélection, ou tout le sample sans sélection |
| Amplify | Multiplier par le pourcentage choisi |
| Resample | Changer la fréquence d'échantillonnage sans changer la hauteur (plus bas = plus crade et plus léger) |
| Loop selection | Boucler sur la sélection |
| Pencil | Dessiner la forme d'onde à la souris (zoomer pour être précis) |

Tout s'annule avec Ctrl+Z.

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

## DATRACKARXD Keygen (appli séparée)

`datrackarxd-keygen` sert uniquement à fabriquer des lecteurs keygen :

1. ouvrir un morceau (bouton **Open song...** ou glisser-déposer un fichier : `.dtk`, `.fur`, `.dmf`,
   `.ftm`, `.mod`, `.xm`, `.it`, `.s3m`, `.mid`), il se joue tout de suite ;
2. régler le logo, le titre de la fenêtre et le texte du scroller ;
3. régler le look : couleurs arc-en-ciel ou une seule couleur, barres copper, champ d'étoiles et sa
   vitesse, vitesse et hauteur de vague du scroller, oscilloscope ; l'aperçu montre exactement le
   résultat ;
4. **Export player...** écrit le lecteur autonome.

Le lecteur est une copie de cette petite appli avec le morceau dedans, donc plus léger qu'avec le
tracker. Si `datrackarxd-keygen` est à côté du tracker, l'export du tracker l'utilise aussi.

```sh
datrackarxd-keygen                                    # ouvrir l'appli
datrackarxd-keygen song.xm                            # l'ouvrir avec un morceau
datrackarxd-keygen --export song.fur intro "LOGO" "texte du scroller"
```

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
  que de notes simultanées ; la batterie (canal 10) devient des percussions puce. Le pitch bend
  devient des `E8xx`/`E9xx` (et l'export MIDI écrit les bends, avec une amplitude de 12 demi-tons, ou
  48 si le morceau bend plus loin).
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
- `src/effects.*` : filtre résonant, réverbération, delay, effets par canal
- `src/chips.*` : puces émulées (AY / YM2149, SN76489, Game Boy, OPL3 via ymfm)
- `src/audiofile.*`, `src/sampleops.*`, `src/recorder.*` : lecture WAV / FLAC / MP3 / OGG, outils de l'éditeur de samples, enregistrement
- `src/export.cpp`, `src/midifile.cpp` : export OGG, import / export MIDI
- `src/import*.cpp`, `src/fur_internal.h`, `src/inflate.cpp` : import FUR, DMF, FTM, MOD, S3M, XM, IT (et décompression zlib)
- `src/midi.*` : entrée clavier MIDI (RtMidi)
- `src/payload.*`, `src/player.cpp`, `src/keygen_fx.*` : lecteur keygen autonome et sa scène
- `src/keygen_main.cpp`, `src/filebrowser.*` : l'appli DATRACKARXD Keygen
- `src/theme.*` : thèmes de couleurs
- `src/app.*` : application, menus, raccourcis, dialogue de fichiers
- `src/ui_pattern.cpp` : éditeur de patterns
- `src/ui_instrument.cpp` : liste et éditeur d'instruments (puce, FM, sample), fenêtre Samples
- `src/ui_windows.cpp` : orders, song, mixer, oscilloscope, scroller keygen, aide
- `src/demo.cpp` : morceau de démo

## Bibliothèques

[Dear ImGui](https://github.com/ocornut/imgui) (MIT), [SDL2](https://libsdl.org) (zlib),
[RtMidi](https://github.com/thestk/rtmidi), [libogg et libvorbis](https://xiph.org) (BSD),
[ymfm](https://github.com/aaronsgiles/ymfm) (BSD-3-Clause, émulation OPL3) et
[dr_flac / dr_mp3](https://github.com/mackron/dr_libs) (domaine public / MIT-0). Leurs licences sont
dans le dossier `licenses` des archives publiées.
