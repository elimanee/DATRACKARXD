// Tracker application: editor state and all ImGui windows.
#pragma once
#include <SDL.h>

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#include "keygen_fx.h"
#include "midi.h"
#include "recorder.h"
#include "shortcuts.h"
#include "song.h"

struct KeyEvent {
  SDL_Scancode sc;
  SDL_Keycode key;
  uint16_t mod;
  bool down;
  bool repeat;
};

// One undo step: either some patterns (note editing), the song without its
// pattern and sample data (instrument, order and setting edits), or the
// whole song (structural changes like the channel count).
struct UndoStep {
  enum Kind { Patterns, Meta, Full } kind = Patterns;
  struct Entry {
    int ch, pat;
    std::vector<Cell> rows;
  };
  std::vector<Entry> entries;
  int order = 0, row = 0, ch = 0, col = 0;
  std::shared_ptr<Song> song;
};

// Copied block of cells: [channel][row], with the column range it covers.
struct Clipboard {
  int startCol = 0, endCol = 0;  // sub-columns of the first / last channel
  std::vector<std::vector<Cell>> cells;
};

// Physical key -> semitone offset from the current octave, or -1.
int scancodeToNote(SDL_Scancode sc);

// A note as the piano roll sees it: a start row and a length in rows.
struct PRNote {
  int start, len, note, ins, vol;
  int ch = 0;  // channel the note lives in
  // Per-note expression (MPE style), one entry per row of the note. They
  // live in the note's channel as E8/E9 bends, 13xx cutoffs and volumes,
  // and move with the note.
  std::vector<float> bend{};    // semitones from the note, NaN = keeps the last value
  std::vector<int16_t> press{}; // volume 00-7F on the rows after the first, -1 = none
  std::vector<int16_t> timbre{};  // filter cutoff 00-FF (13xx), -1 = none
};

enum class PRDrag { None, Move, Resize, Select, Erase, Velocity, Pitch, PitchErase, Bend, BendErase, Lane, LaneErase, Key };

enum class FileDialogMode { None, Open, Save, ExportWav, LoadSample, LoadInstrument, SaveInstrument, ExportMidi, ExportOgg, ExportPlayer, ExportModule, ExportVgm };
enum class PendingAction { None, New, Open, Demo, Quit, OpenPath };

class App {
 public:
  App();
  ~App();

  void openAudio();
  void beforeFrame();  // call before ImGui's NewFrame, without the engine mutex
  void dropFile(const std::string& path);  // a file dropped on the window
  void checkRecovery();  // at startup: an autosave left by a session that didn't save
  void queueKey(const KeyEvent& ev) { keys_.push_back(ev); }
  void requestQuit();
  void loadSettings();
  // Builds the whole UI. Call between ImGui::NewFrame and ImGui::Render
  // while holding engine().mutex.
  void frame();

  bool quit = false;
  Engine& engine() { return *engine_; }
  std::string windowTitle() const;
  bool loadFile(const std::string& path);

 private:
  Song song_;
  std::unique_ptr<Engine> engine_;
  SDL_AudioDeviceID audioDev_ = 0;
  std::string audioStatus_;

  // Cursor. Sub-columns: 0 note, 1-2 instrument, 3-4 volume,
  // then 4 per effect: command hi/lo, value hi/lo.
  int curOrder_ = 0, curRow_ = 0, curCh_ = 0, curCol_ = 0;
  int octave_ = 4, editStep_ = 1, curIns_ = 0;
  bool editMode_ = false, follow_ = true;
  int firstVisibleCh_ = 0;

  bool selecting_ = false, hasSel_ = false, mouseSelecting_ = false;
  int selCh0_ = 0, selCol0_ = 0, selRow0_ = 0, selCh1_ = 0, selCol1_ = 0, selRow1_ = 0;
  Clipboard clipboard_;

  std::vector<UndoStep> undo_, redo_;
  Song metaShadow_;            // song settings as of the last undo step
  bool metaTouched_ = false;   // settings edited since metaShadow_
  bool shadowStale_ = false;   // a full snapshot was just taken
  std::vector<KeyEvent> keys_;
  std::map<SDL_Scancode, int> jamming_;  // held key -> channel

  // MIDI keyboard.
  MidiInput midi_;
  std::string midiPort_;               // port to reopen at startup
  std::vector<std::string> midiPorts_; // last scan
  bool midiVelocity_ = true;           // velocity -> volume column
  int midiBendRange_ = 2;              // pitch wheel range, semitones
  double midiBend_ = 0;                // wheel position, semitones
  std::map<int, std::pair<int, int>> midiRecRow_;  // channel -> (order, row) of its last undo step
  std::set<int> midiBent_;  // channels with a recorded bend, reset with E880 at the note off
  // MPE controller: one note per MIDI channel, each with its own bend,
  // slide (CC74 -> filter cutoff) and pressure (-> volume).
  bool midiMpe_ = false;
  int midiMpeRange_ = 48;  // MPE per-note pitch range, semitones
  struct MpeChannel {
    int voice = -1;   // song channel of the note on this MIDI channel
    int velVol = 127; // the note's velocity as a volume
    double bend = 0;
  };
  MpeChannel mpe_[16];
  struct MidiHeld {
    int ch, order, row;  // where the note was written (row -1 = not written)
  };
  std::map<int, MidiHeld> midiHeld_;   // held MIDI note -> channel
  bool midiChord_ = false;             // a recorded chord is waiting for all keys up

  std::string filePath_;
  bool dirty_ = false;
  std::string status_;
  double statusTime_ = 0;

  // Windows.
  bool showPattern_ = true, showOrders_ = true, showInstruments_ = true, showInsEditor_ = true;
  bool showSamples_ = true;
  bool showMixer_ = true;
  bool showChanFx_ = false;
  bool showArrangement_ = true;
  // Arrangement view.
  float arrZoom_ = 1.5f;          // pixels per row
  float arrScrollX_ = 0, arrScrollReq_ = -1;
  int arrSelA_ = 0, arrSelB_ = 0;  // selected order range
  bool arrDragging_ = false, loopRange_ = false;
  int arrMenuOrder_ = 0, arrMenuCh_ = 0;
  int autoLane_ = -1, autoDrag_ = -1;
  AutomationLane newLane_;
  int fxCh_ = 0;  // channel shown in the Channel FX window
  // Recording options: count-in bars before recording, MIDI quantize grid
  // (0 off, 1 row, 2 rows, 3 beat), metronome volume.
  int countInBars_ = 1;
  int midiQuantize_ = 0;
  float metronomeVol_ = 0.5f;
  bool showScroller_ = false;
  KeygenScene keygen_;
  int curSample_ = 0;
  // Sample editor: view and selection in frames, clipboard, recording.
  int smpFor_ = -1;                  // sample the view below belongs to
  double smpViewStart_ = 0, smpViewLen_ = 0;  // 0 length = whole sample
  int smpSelA_ = 0, smpSelB_ = 0;    // selection [a, b), empty = none
  int smpCursor_ = 0;                // paste position when nothing is selected
  bool smpDraw_ = false;             // pencil mode
  int smpDrag_ = 0;                  // 1 selecting, 2 drawing
  int smpLastDrawX_ = -1;
  float smpLastDrawV_ = 0;
  std::vector<float> smpClipboard_;
  float smpGainPct_ = 150.0f;
  int smpNewRate_ = 22050;
  Recorder recorder_;
  std::string recDevice_;
  float recPeak_ = 0;
  void sampleEditor(Sample& smp);
  int loadSampleIntoIns_ = -1;  // instrument that receives the next loaded WAV
  bool showSong_ = true, showScope_ = true, showEffects_ = false, showKeys_ = false, showAbout_ = false;
  bool layoutDone_ = false;
  bool patternFocused_ = false;

  // Piano roll.
  bool showPianoRoll_ = true;
  unsigned int patternDockId_ = 0;  // dock node of the Pattern window (last frame)
  float prZoomX_ = 16, prZoomY_ = 11;  // pixels per row / per semitone
  float prScrollX_ = 0;                // first visible row
  float prTop_ = -1;                   // pitch at the top edge (-1 = not placed yet)
  bool prGhosts_ = true;
  int prLength_ = 4;                   // length of new notes, in rows
  int prVoices_ = 1;                   // channels used for chords (1 = this channel only)
  int prLane_ = 0;                     // lane under the notes: 0 volume, 1 pitch bend, 2 pressure, 3 slide
  int prBendView_ = 2;                 // pitch lane height, in semitones each way
  bool prMpe_ = false;                 // MPE editing: dragging a note bends it
  int prBendKey_ = -1;                 // note being bent (noteKey)
  float prLastBend_ = 0;
  int prLastRow_ = -1, prLastVal_ = 0x80;  // last row and value drawn in the pitch lane
  std::set<int> prSel_;                // selected notes (start * 256 + note)
  int prViewCh_ = -1, prViewOrder_ = -1;
  PRDrag prDrag_ = PRDrag::None;
  std::vector<PRNote> prOrig_;         // notes when the drag started
  std::vector<bool> prOrigSel_;
  int prGrab_ = -1;                    // index in prOrig_ of the note under the mouse
  float prMx0_ = 0, prMy0_ = 0;
  bool prUndone_ = false;              // undo snapshot taken for this drag
  int prPreview_ = -1;                 // note being previewed

  // Order editor selection.
  int ordCh_ = 0;

  // File dialog.
  FileDialogMode dialogMode_ = FileDialogMode::None;
  bool dialogOpenRequest_ = false;
  std::string dialogDir_;
  char dialogName_[256] = {};
  int exportLoops_ = 1;
  int oggQuality_ = 6;
  int moduleFormat_ = 0;  // 0 XM, 1 MOD, 2 IT
  int moduleRate_ = 1;    // rendered instruments: 0 11025, 1 22050, 2 44100 Hz
  float moduleHold_ = 1.0f;
  std::string exportReport_;  // what the last module export couldn't convert
  bool showExportReport_ = false;
  // Keygen player export.
  bool playerPopupRequest_ = false;
  char playerLogo_[64] = "DATRACKARXD";
  char playerTitle_[128] = {};
  char playerScroll_[2048] = {};

  // Settings (Settings window, saved in datrackarxd.cfg).
  std::string audioDevice_;  // "" = system default
  int audioBuffer_ = 512;    // frames per audio callback
  float audioLatencyMs_ = 0;
  bool audioReopen_ = false;
  std::vector<std::string> audioDevices_;
  float uiScale_ = 1.0f;
  bool fontDirty_ = true;
  int language_ = 0;         // 0 English, 1 French
  int autosaveMinutes_ = 2;  // 0 = off
  static constexpr size_t MAX_RECENT = 10;
  std::vector<std::string> recent_;
  // Autosave and crash recovery.
  double lastAutosave_ = 0;
  bool autosaved_ = false;   // the autosave file holds this song
  size_t autosaveHash_ = 0;  // of the text last autosaved
  bool recoverRequest_ = false;
  std::string recoverFrom_;  // original path of the recovered song
  std::string droppedPath_;  // song dropped on the window, waiting for "discard changes?"
  Shortcuts shortcuts_;
  bool showSettings_ = false, showManual_ = false;
  int capturing_ = -1;  // action waiting for a key in the Settings window

  PendingAction pending_ = PendingAction::None;
  bool confirmRequest_ = false;

  // app.cpp
  void setStatus(const std::string& s);
  void menuBar();
  void handleKeys();
  void globalShortcut(const KeyEvent& ev);
  bool runAction(Action a, bool inPattern);
  // Menu shortcut label of an action ("" if unbound).
  std::string keyLabel(Action a) const { return bindingText(shortcuts_.bind[(int)a]); }
  void doAction(PendingAction a);
  void askAction(PendingAction a);
  void confirmPopup();
  void fileDialog();
  void playerExportPopup();
  void openFileDialog(FileDialogMode mode);
  void saveSong(bool forceDialog);
  void defaultLayout(unsigned int dockId);
  void togglePlay(bool fromCursor);
  void afterSongReplaced();
  void midiMenu();
  void midiEvents();
  // id tells held notes apart (the key, or key + 256 * channel with MPE);
  // returns the song channel playing the note, or -1.
  int midiNoteOn(int key, int velocity, int id = -1);
  void midiNoteOff(int id);
  void midiPitchWheel(int value);
  // Writes an expression at the playing row of a channel (Record mode):
  // a bend (E8/E9), another effect, and/or a volume (-1 = none).
  void recordExpression(int ch, double bend, int cmd, int val, int vol);

  // ui_pattern.cpp
  void patternWindow();
  void patternKey(const KeyEvent& ev);
  int colCount(int ch) const;
  void moveCursor(int dRow, int dCol);
  void moveRows(int d);
  void setCursorFromPlayback();
  Cell* cursorCell(bool create);
  void pushUndo(int ch0, int ch1, int order = -1);  // order -1: the cursor's
  // Sets effect cmd in a cell of channel ch: reuses a column with the same
  // command, else the first free one, adding a column if needed.
  bool putEffect(int ch, Cell& cell, int cmd, int val);
  void pushFullUndo();
  void commitMetaEdit();
  void resetUndo();
  Song metaSnapshot();
  void restoreMeta(const Song& m);
  void trimUndo();
  void applyUndo(std::vector<UndoStep>& from, std::vector<UndoStep>& to);
  void enterNote(int note);
  void enterHex(int digit);
  void deleteAtCursor();
  void insertRow(bool insert);
  void selectionBounds(int& ch0, int& col0, int& row0, int& ch1, int& col1, int& row1) const;
  // Selection tools.
  void toolBounds(int& ch0, int& f0, int& row0, int& ch1, int& f1, int& row1) const;
  void interpolateSelection();
  void volumeTool(float scale, int jitter);
  void rowTool(int kind);  // 0 reverse, 1 expand x2, 2 shrink /2
  void selectionToolsMenu();
  struct FindReplace {
    int note = -1;  // -1 anything, -2 any note, else that note
    int ins = -1, fxCmd = -1;
    int transpose = 0, newIns = -1, newVol = -1, newFxCmd = -1;
    int scope = 0;  // 0 selection, 1 current pattern, 2 whole song
  } findReplace_;
  bool showFindReplace_ = false;
  int findReplace(bool apply);  // number of matching cells
  void findReplaceWindow();
  void copySelection(bool cut);
  void paste();
  void clearSelection();
  void transpose(int semitones);
  void startSelection();
  void selectChannelOrAll();

  // ui_pianoroll.cpp
  void pianoRollWindow();

  // ui_instrument.cpp
  void instrumentsWindow();
  void instrumentEditor();
  bool macroEditor(Instrument& ins, int type);
  bool fmEditor(Instrument& ins);
  bool sampleInsEditor(Instrument& ins);
  void samplesWindow();
  bool wavetableEditor(Instrument& ins);
  bool sidEditor(Instrument& ins);
  bool modulationEditor(Instrument& ins);
  bool chipEditor(Instrument& ins);
  bool oplEditor(Instrument& ins);
  bool morphEditor(Instrument& ins);

  // ui_windows.cpp
  void ordersWindow();
  void songWindow();
  void mixerWindow();
  void channelFxWindow();
  void arrangementWindow();
  void automationEditor(float orderW, float leftW, float playPos);
  // Order list edits (Orders and Arrangement windows); automation follows.
  int firstUnusedPattern(int ch) const;
  void orderInsertNew(int at);
  void orderDuplicate(int first, int last);
  void orderClone(int at);
  void orderDelete(int first, int last);
  void orderSwap(int a, int b);
  std::string laneLabel(const AutomationLane& lane) const;
  std::string laneValueText(const AutomationLane& lane, float v) const;
  float laneDefault(const AutomationLane& lane) const;
  void scopeWindow();
  void scrollerWindow();
  void saveSettings();
  // ui_settings.cpp
  void settingsWindow();
  void manualWindow();
  void recoveryPopup();
  void addRecent(const std::string& path);
  void autosaveTick();
  void clearAutosave();
  void effectsHelp();
  void keysHelp();
  void aboutWindow();
};
