// Tracker application: editor state and all ImGui windows.
#pragma once
#include <SDL.h>

#include <map>
#include <set>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#include "midi.h"
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
};

enum class PRDrag { None, Move, Resize, Select, Erase, Velocity, Key };

enum class FileDialogMode { None, Open, Save, ExportWav, LoadSample, LoadInstrument, SaveInstrument, ExportMidi };
enum class PendingAction { None, New, Open, Demo, Quit };

class App {
 public:
  App();
  ~App();

  void openAudio();
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
  bool showScroller_ = false;
  int curSample_ = 0;
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

  PendingAction pending_ = PendingAction::None;
  bool confirmRequest_ = false;

  // app.cpp
  void setStatus(const std::string& s);
  void menuBar();
  void handleKeys();
  void globalShortcut(const KeyEvent& ev);
  void doAction(PendingAction a);
  void askAction(PendingAction a);
  void confirmPopup();
  void fileDialog();
  void openFileDialog(FileDialogMode mode);
  void saveSong(bool forceDialog);
  void defaultLayout(unsigned int dockId);
  void togglePlay(bool fromCursor);
  void afterSongReplaced();
  void midiMenu();
  void midiEvents();
  void midiNoteOn(int key, int velocity);
  void midiNoteOff(int key);

  // ui_pattern.cpp
  void patternWindow();
  void patternKey(const KeyEvent& ev);
  int colCount(int ch) const;
  void moveCursor(int dRow, int dCol);
  void moveRows(int d);
  void setCursorFromPlayback();
  Cell* cursorCell(bool create);
  void pushUndo(int ch0, int ch1, int order = -1);  // order -1: the cursor's
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
  void copySelection(bool cut);
  void paste();
  void clearSelection();
  void transpose(int semitones);
  void startSelection();

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

  // ui_windows.cpp
  void ordersWindow();
  void songWindow();
  void mixerWindow();
  void scopeWindow();
  void scrollerWindow();
  void saveSettings();
  void effectsHelp();
  void keysHelp();
  void aboutWindow();
};
