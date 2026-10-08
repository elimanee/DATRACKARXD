// Ogg Vorbis export (libvorbis).
#include <vorbis/vorbisenc.h>

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>

#include "engine.h"

bool exportOgg(const Song& song, const std::string& path, int sampleRate, int loops, int quality, std::string& err) {
  std::ofstream f(path, std::ios::binary);
  if (!f) {
    err = "Cannot open " + path + " for writing";
    return false;
  }
  std::vector<float> mix = renderSong(song, sampleRate, loops);

  vorbis_info vi;
  vorbis_info_init(&vi);
  if (vorbis_encode_init_vbr(&vi, 2, sampleRate, std::clamp(quality, 0, 10) / 10.0f) != 0) {
    vorbis_info_clear(&vi);
    err = "Vorbis encoder setup failed";
    return false;
  }
  vorbis_comment vc;
  vorbis_comment_init(&vc);
  vorbis_comment_add_tag(&vc, "TITLE", song.name.c_str());
  if (!song.author.empty()) vorbis_comment_add_tag(&vc, "ARTIST", song.author.c_str());
  vorbis_comment_add_tag(&vc, "ENCODER", "DATRACKARXD " DATRACKARXD_VERSION);

  vorbis_dsp_state vd;
  vorbis_block vb;
  vorbis_analysis_init(&vd, &vi);
  vorbis_block_init(&vd, &vb);
  ogg_stream_state os;
  std::srand((unsigned)std::time(nullptr));
  ogg_stream_init(&os, std::rand());

  ogg_page og;
  ogg_packet op;
  auto writePage = [&]() {
    f.write((const char*)og.header, og.header_len);
    f.write((const char*)og.body, og.body_len);
  };
  {
    ogg_packet header, headerComm, headerCode;
    vorbis_analysis_headerout(&vd, &vc, &header, &headerComm, &headerCode);
    ogg_stream_packetin(&os, &header);
    ogg_stream_packetin(&os, &headerComm);
    ogg_stream_packetin(&os, &headerCode);
    while (ogg_stream_flush(&os, &og)) writePage();
  }

  const size_t frames = mix.size() / 2;
  const size_t block = 1024;
  size_t pos = 0;
  bool eos = false;
  while (!eos) {
    if (pos < frames) {
      size_t n = std::min(block, frames - pos);
      float** buf = vorbis_analysis_buffer(&vd, (int)n);
      for (size_t i = 0; i < n; i++) {
        buf[0][i] = mix[(pos + i) * 2];
        buf[1][i] = mix[(pos + i) * 2 + 1];
      }
      vorbis_analysis_wrote(&vd, (int)n);
      pos += n;
    } else {
      vorbis_analysis_wrote(&vd, 0);  // end of stream
    }
    while (vorbis_analysis_blockout(&vd, &vb) == 1) {
      vorbis_analysis(&vb, nullptr);
      vorbis_bitrate_addblock(&vb);
      while (vorbis_bitrate_flushpacket(&vd, &op)) {
        ogg_stream_packetin(&os, &op);
        while (!eos && ogg_stream_pageout(&os, &og)) {
          writePage();
          if (ogg_page_eos(&og)) eos = true;
        }
      }
    }
  }

  ogg_stream_clear(&os);
  vorbis_block_clear(&vb);
  vorbis_dsp_clear(&vd);
  vorbis_comment_clear(&vc);
  vorbis_info_clear(&vi);
  if (!f) {
    err = "Write error on " + path;
    return false;
  }
  return true;
}
