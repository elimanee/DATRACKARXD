// Tracker application: editor state and all ImGui windows.
#pragma once
#include <SDL.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "engine.h"
#include "song.h"

struct KeyEvent {
  SDL_Scancode sc;
  SDL_Keycode key;
  uint16_t mod;
  bool down;
  bool repeat;
};

// A snapshot of some patterns, taken before an edit.
struct UndoStep {
  struct Entry {
    int ch, pat;
    std::vector<Cell> rows;
  };
  std::vector<Entry> entries;
  int order, row, ch, col;
};

// Copied block of cells: [channel][row], with the column range it covers.
struct Clipboard {
  int startCol = 0, endCol = 0;  // sub-columns of the first / last channel
  std::vector<std::vector<Cell>> cells;
};

// Physical key -> semitone offset from the current octave, or -1.
int scancodeToNote(SDL_Scancode sc);

enum class FileDialogMode { None, Open, Save, ExportWav, LoadSample };
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
  std::vector<KeyEvent> keys_;
  std::map<SDL_Scancode, int> jamming_;  // held key -> channel

  std::string filePath_;
  bool dirty_ = false;
  std::string status_;
  double statusTime_ = 0;

  // Windows.
  bool showPattern_ = true, showOrders_ = true, showInstruments_ = true, showInsEditor_ = true;
  bool showSamples_ = true;
  bool showScroller_ = false;
  int curSample_ = 0;
  int loadSampleIntoIns_ = -1;  // instrument that receives the next loaded WAV
  bool showSong_ = true, showScope_ = true, showEffects_ = false, showKeys_ = false, showAbout_ = false;
  bool layoutDone_ = false;
  bool patternFocused_ = false;

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

  // ui_pattern.cpp
  void patternWindow();
  void patternKey(const KeyEvent& ev);
  int colCount(int ch) const;
  void moveCursor(int dRow, int dCol);
  void moveRows(int d);
  void setCursorFromPlayback();
  Cell* cursorCell(bool create);
  void pushUndo(int ch0, int ch1);
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

  // ui_instrument.cpp
  void instrumentsWindow();
  void instrumentEditor();
  bool macroEditor(Instrument& ins, int type);
  bool fmEditor(Instrument& ins);
  bool sampleInsEditor(Instrument& ins);
  void samplesWindow();
  bool wavetableEditor(Instrument& ins);

  // ui_windows.cpp
  void ordersWindow();
  void songWindow();
  void scopeWindow();
  void scrollerWindow();
  void saveSettings();
  void effectsHelp();
  void keysHelp();
  void aboutWindow();
};
