// Video export: the keygen scene or a wall of oscilloscopes, drawn off
// screen frame by frame (software renderer) in step with the music. With
// ffmpeg on the computer an .mp4 (H.264 + AAC); otherwise an .avi that the
// tracker writes itself (MJPEG pictures + PCM sound).
#pragma once
#include <memory>
#include <string>

#include "keygen_fx.h"
#include "song.h"

struct VideoOptions {
  int width = 1280, height = 720, fps = 30;
  int scene = 0;     // 0 keygen scene, 1 oscilloscopes
  std::string logo, scroll, greets;
  KeygenStyle style;
  int loops = 1;
  float tail = 2.0f;  // seconds after the end, for the release and reverb tails
  int jpegQuality = 85;
};

// The ffmpeg program to run, or "" when there is none.
std::string findFfmpeg();
// .mp4 / .mkv / .mov go through ffmpeg, anything else is a built-in .avi.
bool videoNeedsFfmpeg(const std::string& path);

class VideoWriter {
 public:
  VideoWriter();
  ~VideoWriter();
  // Prepares everything (renders the sound first when ffmpeg is used).
  bool begin(const Song& song, const std::string& path, const VideoOptions& opt, std::string& err);
  // Draws and writes up to `frames` more frames. False once done or failed.
  bool step(int frames);
  float progress() const;       // 0..1
  bool done() const;
  // Closes the file; false (with err) if anything went wrong.
  bool finish(std::string& err);
  void cancel();

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};
