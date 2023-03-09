/* Copyright 2006-2021 Christian Schneider

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

// version information
#define V_MAJOR 0
#define V_MINOR 4
#define V_MINOR2 8
//#define V_STATE "ALPHA"
#define V_STATE "DEVELOPMENT"
//#define V_MSG "USE FOR TESTING ONLY"
#define V_MSG "USE AT YOUR OWN RISK!"
#ifdef __unix
  #define V_OS "Unix"
#else
  #define V_OS "Windows"
#endif
#ifdef BIT64
  #define V_BIT "64-bit"
#else
  #define V_BIT "32-bit"
#endif

#define NOMINMAX  // This is to prevent min/max implementations from windows.h, so we can use std::min and std::max and so on, without problems

#include <cstdio>
#include <iostream>
#include <string>
#include <array>
#include <random>
#include <fcntl.h>
#include <filesystem>
#include <set>
#include <span>

#ifndef __unix
#include <windows.h>
#include <io.h>
#else
#include <time.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#endif

#include "precomp_dll.h"

#include "formats/deflate.h"
#include "formats/zlib.h"
#include "formats/zip.h"
#include "formats/gzip.h"
#include "formats/bzip2.h"
#include "formats/mp3.h"
#include "formats/pdf.h"
#include "formats/jpeg.h"
#include "formats/gif.h"
#include "formats/png.h"
#include "formats/swf.h"
#include "formats/base64.h"

// This I shamelessly lifted from https://web.archive.org/web/20090907131154/http://www.cs.toronto.edu:80/~ramona/cosmin/TA/prog/sysconf/
// (credit to this StackOverflow answer for pointing me to it https://stackoverflow.com/a/1613677)
// It allows us to portably (at least for Windows/Linux/Mac) set a std stream as binary
#define STDIN  0
#define STDOUT 1
#define STDERR 2
#ifndef __unix
# define SET_BINARY_MODE(handle) setmode(handle, O_BINARY)
#else
# define SET_BINARY_MODE(handle) ((void)0)
#endif

PrecompLoggingLevels PRECOMP_VERBOSITY_LEVEL = PRECOMP_NORMAL_LOG;

std::function<void(PrecompLoggingLevels, char*)> logging_callback;

void PrecompSetLoggingCallback(void(*callback)(PrecompLoggingLevels, char*)) {
  logging_callback = callback;
}

void print_to_log(PrecompLoggingLevels log_level, std::string format) {
  if (PRECOMP_VERBOSITY_LEVEL < log_level || !logging_callback) return;
  logging_callback(log_level, format.data());
}

void precompression_result::dump_header_to_outfile(Precomp& precomp_mgr) const {
  // write compressed data header
  precomp_mgr.ctx->fout->put(static_cast<char>(flags));
  precomp_mgr.ctx->fout->put(format);
}

void precompression_result::dump_penaltybytes_to_outfile(Precomp& precomp_mgr) const {
  if (penalty_bytes.empty()) return;
  print_to_log(PRECOMP_DEBUG_LOG, "Penalty bytes were used: %i bytes\n", penalty_bytes.size());
  fout_fput_vlint(*precomp_mgr.ctx->fout, penalty_bytes.size());

  for (auto& chr: penalty_bytes) {
    precomp_mgr.ctx->fout->put(chr);
  }
}

void precompression_result::dump_stream_sizes_to_outfile(Precomp& precomp_mgr) const {
  fout_fput_vlint(*precomp_mgr.ctx->fout, original_size);
  fout_fput_vlint(*precomp_mgr.ctx->fout, precompressed_size);
}

void precompression_result::dump_precompressed_data_to_outfile(Precomp& precomp_mgr) {
  fast_copy(*precompressed_stream, *precomp_mgr.ctx->fout, precompressed_size);
}

void precompression_result::dump_to_outfile(Precomp& precomp_mgr) {
  dump_header_to_outfile(precomp_mgr);
  dump_penaltybytes_to_outfile(precomp_mgr);
  dump_stream_sizes_to_outfile(precomp_mgr);
  dump_precompressed_data_to_outfile(precomp_mgr);
}

ResultStatistics::ResultStatistics(): CResultStatistics() {
  recompressed_streams_count = 0;
  recompressed_pdf_count = 0;
  recompressed_pdf_count_8_bit = 0;
  recompressed_pdf_count_24_bit = 0;
  recompressed_zip_count = 0;
  recompressed_gzip_count = 0;
  recompressed_png_count = 0;
  recompressed_png_multi_count = 0;
  recompressed_gif_count = 0;
  recompressed_jpg_count = 0;
  recompressed_jpg_prog_count = 0;
  recompressed_mp3_count = 0;
  recompressed_swf_count = 0;
  recompressed_base64_count = 0;
  recompressed_bzip2_count = 0;
  recompressed_zlib_count = 0;    // intense mode
  recompressed_brute_count = 0;   // brute mode

  decompressed_streams_count = 0;
  decompressed_pdf_count = 0;
  decompressed_pdf_count_8_bit = 0;
  decompressed_pdf_count_24_bit = 0;
  decompressed_zip_count = 0;
  decompressed_gzip_count = 0;
  decompressed_png_count = 0;
  decompressed_png_multi_count = 0;
  decompressed_gif_count = 0;
  decompressed_jpg_count = 0;
  decompressed_jpg_prog_count = 0;
  decompressed_mp3_count = 0;
  decompressed_swf_count = 0;
  decompressed_base64_count = 0;
  decompressed_bzip2_count = 0;
  decompressed_zlib_count = 0;    // intense mode
  decompressed_brute_count = 0;   // brute mode
}

void PrecompSetInputStream(CPrecomp* precomp_mgr, CPrecompIStream istream, const char* input_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->input_file_name = input_file_name;
  internal_precomp_ptr->set_input_stream(static_cast<std::istream*>(istream));
}
void PrecompSetInputFile(CPrecomp* precomp_mgr, FILE* fhandle, const char* input_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->input_file_name = input_file_name;
  internal_precomp_ptr->set_input_stream(fhandle);
}

void PrecompSetOutStream(CPrecomp* precomp_mgr, CPrecompOStream ostream, const char* output_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->output_file_name = output_file_name;
  internal_precomp_ptr->set_output_stream(static_cast<std::ostream*>(ostream));
}
void PrecompSetOutputFile(CPrecomp* precomp_mgr, FILE* fhandle, const char* output_file_name) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  internal_precomp_ptr->output_file_name = output_file_name;
  internal_precomp_ptr->set_output_stream(fhandle);
}

const char* PrecompGetOutputFilename(CPrecomp* precomp_mgr) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  return internal_precomp_ptr->output_file_name.c_str();
}

//Switches constructor
Switches::Switches(): CSwitches() {
  intense_mode = false;
  intense_mode_depth_limit = -1;
  fast_mode = false;
  brute_mode = false;
  brute_mode_depth_limit = -1;
  pdf_bmp_mode = false;
  prog_only = false;
  use_mjpeg = true;
  use_brunsli = true;
  use_packjpg_fallback = true;
  min_ident_size = 4;

  use_pdf = true;
  use_zip = true;
  use_gzip = true;
  use_png = true;
  use_gif = true;
  use_jpg = true;
  use_mp3 = true;
  use_swf = true;
  use_base64 = true;
  use_bzip2 = true;
  level_switch_used = false;

  // preflate config
  preflate_meta_block_size = 1 << 21; // 2 MB blocks by default
  preflate_verify = false;
}

RecursionContext::RecursionContext(float min_percent, float max_percent, Precomp& precomp_):
    CRecursionContext(), precomp(precomp_), global_min_percent(min_percent), global_max_percent(max_percent) {}


void RecursionContext::set_input_stream(std::istream* istream, bool take_ownership) {
  this->fin = std::make_unique<WrappedIStream>(istream, take_ownership);
}

void RecursionContext::set_input_stream(FILE* fhandle, bool take_ownership) {
  this->fin = std::make_unique<FILEIStream>(fhandle, take_ownership);
}

void RecursionContext::set_output_stream(std::ostream* ostream, bool take_ownership) {
  this->fout = std::unique_ptr<ObservableOStream>(new ObservableWrappedOStream(ostream, take_ownership));
}

void RecursionContext::set_output_stream(FILE* fhandle, bool take_ownership) {
  this->fout = std::unique_ptr<ObservableOStream>(new ObservableFILEOStream(fhandle, take_ownership));
}

std::unique_ptr<RecursionContext>&  Precomp::get_original_context() {
  if (recursion_contexts_stack.empty()) return ctx;
  return recursion_contexts_stack[0];
}

Precomp::Precomp(): CPrecomp() {
  header_already_read = false;
  recursion_depth = 0;
  max_recursion_depth = 10;
  max_recursion_depth_used = 0;
  max_recursion_depth_reached = false;
}

void Precomp::set_input_stdin() {
  // Read binary to stdin
  SET_BINARY_MODE(STDIN);
  auto new_fin = std::make_unique<WrappedIStream>(new std::ifstream(), true);
  new_fin->rdbuf(std::cin.rdbuf());
  this->get_original_context()->fin = std::move(new_fin);
}

void Precomp::set_input_stream(std::istream* istream, bool take_ownership) {
  if (istream == &std::cin) { set_input_stdin(); }
  else {
    this->get_original_context()->set_input_stream(istream, take_ownership);
  }
}
void Precomp::set_input_stream(FILE* fhandle, bool take_ownership) {
  if (fhandle == stdin) { set_input_stdin(); }
  else {
    this->get_original_context()->set_input_stream(fhandle, take_ownership);
  }
}

void Precomp::set_output_stdout() {
  // Read binary to stdin
  // Write binary to stdout
  SET_BINARY_MODE(STDOUT);
  auto new_fout = std::make_unique<ObservableWrappedOStream>(new std::ofstream(), true);
  new_fout->rdbuf(std::cout.rdbuf());
  this->get_original_context()->fout = std::move(new_fout);
}

void Precomp::register_output_observer_callbacks() {
  // Set write observer to update progress when writing to output file, based on how much of the input file we have read
  this->get_original_context()->fout->register_observer(ObservableOStream::observable_methods::write_method, [this]()
  {
    this->call_progress_callback();
  });
}

void Precomp::set_output_stream(std::ostream* ostream, bool take_ownership) {
  auto& orig_context = this->get_original_context();
  if (ostream == &std::cout) { set_output_stdout(); }
  else {
    orig_context->set_output_stream(ostream, take_ownership);
  }
  register_output_observer_callbacks();
}

void Precomp::set_output_stream(FILE* fhandle, bool take_ownership) {
  auto& orig_context = this->get_original_context();
  if (fhandle == stdout) { set_output_stdout(); }
  else {
    orig_context->set_output_stream(fhandle, take_ownership);
  }
  register_output_observer_callbacks();
}

void Precomp::set_progress_callback(std::function<void(float)> callback) {
  progress_callback = callback;
}
void Precomp::call_progress_callback() {
  if (!this->progress_callback) return;
  auto context_progress_range = this->ctx->global_max_percent - this->ctx->global_min_percent;
  auto inner_context_progress_percent = static_cast<float>(this->ctx->input_file_pos) / this->ctx->fin_length;
  this->progress_callback(this->ctx->global_min_percent + (context_progress_range * inner_context_progress_percent));
}

// get copyright message
// msg = Buffer for error messages (256 bytes buffer size are enough)
LIBPRECOMP void PrecompGetCopyrightMsg(char* msg) {
  if (V_MINOR2 == 0) {
    sprintf(msg, "Precomp DLL v%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR);
  } else {
    sprintf(msg, "Precomp DLL v%i.%i.%i (c) 2006-2021 by Christian Schneider",V_MAJOR,V_MINOR,V_MINOR2);
  }
}

// Brute mode detects a bit less than intense mode to avoid false positives
// and slowdowns, so both can be active. Also, both of them can have a level
// limit, so two helper functions make things easier to handle.

bool intense_mode_is_active(Precomp& precomp_mgr) {
  if (!precomp_mgr.switches.intense_mode) return false;
  if ((precomp_mgr.switches.intense_mode_depth_limit == -1) || (precomp_mgr.recursion_depth <= precomp_mgr.switches.intense_mode_depth_limit)) return true;

  return false;
}

bool brute_mode_is_active(Precomp& precomp_mgr) {
  if (!precomp_mgr.switches.brute_mode) return false;
  if ((precomp_mgr.switches.brute_mode_depth_limit == -1) || (precomp_mgr.recursion_depth <= precomp_mgr.switches.brute_mode_depth_limit)) return true;

  return false;
}

unsigned long long compare_files(Precomp& precomp_mgr, IStreamLike& file1, IStreamLike& file2, unsigned int pos1, unsigned int pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  long long size1, size2, minsize;
  int i;
  bool endNow = false;

  file1.seekg(pos1, std::ios_base::beg);
  file2.seekg(pos2, std::ios_base::beg);

  do {
    precomp_mgr.call_progress_callback();

    file1.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
    size1 = file1.gcount();
    file2.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
    size1 = file2.gcount();

    minsize = std::min(size1, size2);
    for (i = 0; i < minsize; i++) {
      if (input_bytes1[i] != input_bytes2[i]) {
        endNow = true;
        break;
      }
      same_byte_count++;
    }
  } while ((minsize == COMP_CHUNK) && (!endNow));

  return same_byte_count;
}

void end_uncompressed_data(Precomp& precomp_mgr) {
  if (!precomp_mgr.ctx->uncompressed_length.has_value()) return;

  fout_fput_vlint(*precomp_mgr.ctx->fout, *precomp_mgr.ctx->uncompressed_length);

  // fast copy of uncompressed data
  precomp_mgr.ctx->fin->seekg(precomp_mgr.ctx->uncompressed_pos, std::ios_base::beg);
  fast_copy(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, *precomp_mgr.ctx->uncompressed_length);

  precomp_mgr.ctx->uncompressed_length = std::nullopt;
}

void write_header(Precomp& precomp_mgr) {
  // write the PCF file header, beware that this needs to be done before wrapping the output file with a CompressedOStreamBuffer
  char* input_file_name_without_path = new char[precomp_mgr.input_file_name.length() + 1];

  ostream_printf(*precomp_mgr.ctx->fout, "PCF");

  // version number
  precomp_mgr.ctx->fout->put(V_MAJOR);
  precomp_mgr.ctx->fout->put(V_MINOR);
  precomp_mgr.ctx->fout->put(V_MINOR2);

  // compression-on-the-fly method used, 0, as OTF compression no longer supported
  precomp_mgr.ctx->fout->put(0);

  // write input file name without path
  const char* last_backslash = strrchr(precomp_mgr.input_file_name.c_str(), PATH_DELIM);
  if (last_backslash != nullptr) {
    strcpy(input_file_name_without_path, last_backslash + 1);
  }
  else {
    strcpy(input_file_name_without_path, precomp_mgr.input_file_name.c_str());
  }

  ostream_printf(*precomp_mgr.ctx->fout, input_file_name_without_path);
  precomp_mgr.ctx->fout->put(0);

  delete[] input_file_name_without_path;
}

int compress_file_impl(Precomp& precomp_mgr) {

  precomp_mgr.ctx->comp_decomp_state = P_PRECOMPRESS;
  if (precomp_mgr.recursion_depth == 0) write_header(precomp_mgr);

  precomp_mgr.ctx->uncompressed_bytes_total = 0;

  precomp_mgr.ctx->fin->seekg(0, std::ios_base::beg);
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
  long long in_buf_pos = 0;
  std::span<unsigned char> checkbuf;

  precomp_mgr.ctx->anything_was_used = false;
  precomp_mgr.ctx->non_zlib_was_used = false;

  for (long long input_file_pos = 0; input_file_pos < precomp_mgr.ctx->fin_length; input_file_pos++) {
  precomp_mgr.ctx->input_file_pos = input_file_pos;
  bool compressed_data_found = false;

  bool ignore_this_pos = false;

  if ((in_buf_pos + IN_BUF_SIZE) <= (input_file_pos + CHECKBUF_SIZE)) {
    precomp_mgr.ctx->fin->seekg(input_file_pos, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(precomp_mgr.ctx->in_buf), IN_BUF_SIZE);
    in_buf_pos = input_file_pos;
  }
  auto cb_pos = input_file_pos - in_buf_pos;
  checkbuf = std::span(&precomp_mgr.ctx->in_buf[cb_pos], IN_BUF_SIZE - cb_pos);

  ignore_this_pos = precomp_mgr.switches.ignore_set.find(input_file_pos) != precomp_mgr.switches.ignore_set.end();

  if (!ignore_this_pos) {

    // ZIP header?
    if (precomp_mgr.switches.use_zip && zip_header_check(precomp_mgr, checkbuf, input_file_pos)) {
      auto result = try_decompression_zip(precomp_mgr, checkbuf, input_file_pos);
      compressed_data_found = result.success;

      if (result.success) {
        end_uncompressed_data(precomp_mgr);

        result.dump_to_outfile(precomp_mgr);

        // start new uncompressed data

        // set input file pointer after recompressed data
        input_file_pos += result.input_pos_add_offset();
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_gzip)) { // no ZIP header -> GZip header?
      if (gzip_header_check(precomp_mgr, checkbuf)) {
        auto result = try_decompression_gzip(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;

        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_pdf)) { // no Gzip header -> PDF FlateDecode?
      if (pdf_header_check(checkbuf)) {
        auto result = precompress_pdf(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;

        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_png)) { // no PDF header -> PNG IDAT?
      if (png_header_check(checkbuf)) {
        auto result = precompress_png(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;

        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }

    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_gif)) { // no PNG header -> GIF header?
      if (gif_header_check(checkbuf)) {
        auto result = precompress_gif(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_jpg)) { // no GIF header -> JPG header?
      if (jpeg_header_check(checkbuf)) {
        auto result = precompress_jpeg(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_mp3)) { // no JPG header -> MP3 header?
      if (mp3_header_check(checkbuf)) { // frame start
        auto result = precompress_mp3(precomp_mgr, input_file_pos, checkbuf);
        compressed_data_found = result.success;
        if (result.success) {
          // end uncompressed data
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // start new uncompressed data

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_swf)) { // no MP3 header -> SWF header?
      // CWS = Compressed SWF file
      if (swf_header_check(checkbuf)) {
        auto result = try_decompression_swf(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_base64)) { // no SWF header -> Base64?
      if (base64_header_check(checkbuf)) {
        auto result = precompress_base64(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }

    if ((!compressed_data_found) && (precomp_mgr.switches.use_bzip2)) { // no Base64 header -> bZip2?
      if (bzip2_header_check(checkbuf)) {
        auto result = try_decompression_bzip2(precomp_mgr, checkbuf, input_file_pos);
        compressed_data_found = result.success;
        if (result.success) {
          end_uncompressed_data(precomp_mgr);

          result.dump_to_outfile(precomp_mgr);

          // set input file pointer after recompressed data
          input_file_pos += result.input_pos_add_offset();
        }
      }
    }


   // nothing so far -> if intense mode is active, look for raw zLib header
   if (intense_mode_is_active(precomp_mgr)) {
    if (!compressed_data_found) {
      bool ignore_this_position = false;
      if (!precomp_mgr.ctx->intense_ignore_offsets.empty()) {
        auto first = precomp_mgr.ctx->intense_ignore_offsets.begin();
        while (*first < input_file_pos) {
          precomp_mgr.ctx->intense_ignore_offsets.erase(first);
          if (precomp_mgr.ctx->intense_ignore_offsets.empty()) break;
          first = precomp_mgr.ctx->intense_ignore_offsets.begin();
        }

        if (!precomp_mgr.ctx->intense_ignore_offsets.empty()) {
          if (*first == input_file_pos) {
            ignore_this_position = true;
            precomp_mgr.ctx->intense_ignore_offsets.erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        if (zlib_header_check(checkbuf)) {
          auto result = try_decompression_zlib(precomp_mgr, checkbuf, input_file_pos);
          compressed_data_found = result.success;
          if (result.success) {
            compressed_data_found = true;

            end_uncompressed_data(precomp_mgr);

            result.dump_to_outfile(precomp_mgr);

            // set input file pointer after recompressed data
            input_file_pos += result.input_pos_add_offset();
          }
        }
      }
    }
   }

   // nothing so far -> if brute mode is active, brute force for zLib streams
    if (brute_mode_is_active(precomp_mgr)) {
    if (!compressed_data_found) {
      bool ignore_this_position = false;
      if (!precomp_mgr.ctx->brute_ignore_offsets.empty()) {
        auto first = precomp_mgr.ctx->brute_ignore_offsets.begin();
        while (*first < input_file_pos) {
          precomp_mgr.ctx->brute_ignore_offsets.erase(first);
          if (precomp_mgr.ctx->brute_ignore_offsets.empty()) break;
          first = precomp_mgr.ctx->brute_ignore_offsets.begin();
        }

        if (!precomp_mgr.ctx->brute_ignore_offsets.empty()) {
          if (*first == input_file_pos) {
            ignore_this_position = true;
            precomp_mgr.ctx->brute_ignore_offsets.erase(first);
          }
        }
      }

      if (!ignore_this_position) {
        if (check_raw_deflate_stream_start(precomp_mgr, checkbuf, input_file_pos)) {
          auto result = try_decompression_raw_deflate(precomp_mgr, checkbuf, input_file_pos);
          compressed_data_found = result.success;
          if (result.success) {
            compressed_data_found = true;

            end_uncompressed_data(precomp_mgr);

            result.dump_to_outfile(precomp_mgr);

            // set input file pointer after recompressed data
            input_file_pos += result.input_pos_add_offset();
          }
        }
      }
    }
   }

  }

    if (!compressed_data_found) {
      if (!precomp_mgr.ctx->uncompressed_length.has_value()) {
          precomp_mgr.ctx->uncompressed_length = 0;
          precomp_mgr.ctx->uncompressed_pos = input_file_pos;

          // uncompressed data
          precomp_mgr.ctx->fout->put(0);
      }
      (*precomp_mgr.ctx->uncompressed_length)++;
      precomp_mgr.ctx->uncompressed_bytes_total++;
    }

  }

  end_uncompressed_data(precomp_mgr);

  precomp_mgr.ctx->fout = nullptr; // To close the outfile TODO: maybe we should just make sure the whole last context gets destroyed if at recursion_depth == 0?

  return (precomp_mgr.ctx->anything_was_used || precomp_mgr.ctx->non_zlib_was_used) ? RETURN_SUCCESS : RETURN_NOTHING_DECOMPRESSED;
}

int wrap_with_exception_catch(std::function<int()> func)
{
  // Didn't want to wrap those big functions on a try catch, and also didn't want to leak out our exceptions to the outside, because we want to mantain a C API
  try
  {
    return func();
  }
  catch (const PrecompError& exc)
  {
    return exc.error_code;
  }
  catch (const std::exception& exc)
  {
    return ERR_GENERIC_OR_UNKNOWN;
  }
}

int compress_file(Precomp& precomp_mgr)
{
  return wrap_with_exception_catch([&]() { return compress_file_impl(precomp_mgr); });
}

int decompress_file_impl(RecursionContext& precomp_ctx) {
  precomp_ctx.comp_decomp_state = P_RECOMPRESS;

  long long fin_pos = precomp_ctx.fin->tellg();

while (precomp_ctx.fin->good()) {

  std::byte header1 = static_cast<std::byte>(precomp_ctx.fin->get());
  if (!precomp_ctx.fin->good()) break;
  if (header1 == std::byte{ 0 }) { // uncompressed data
    long long uncompressed_data_length;
    uncompressed_data_length = fin_fget_vlint(*precomp_ctx.fin);

    if (uncompressed_data_length == 0) break; // end of PCF file, used by bZip2 compress-on-the-fly

    print_to_log(PRECOMP_DEBUG_LOG, "Uncompressed data, length=%lli\n");
    fast_copy(*precomp_ctx.fin, *precomp_ctx.fout, uncompressed_data_length);

  } else { // decompressed data, recompress

    unsigned char headertype = precomp_ctx.fin->get();

    switch (headertype) {
    case D_PDF: { // PDF recompression
      recompress_pdf(precomp_ctx, header1);
      break;
    }     
    case D_ZIP: { // ZIP recompression
      recompress_zip(precomp_ctx, header1);
      break;
    }
    case D_GZIP: { // GZip recompression
      recompress_gzip(precomp_ctx, header1);
      break;
    }
    case D_PNG: { // PNG recompression
      recompress_png(precomp_ctx, header1);
      break;
    }
    case D_MULTIPNG: { // PNG multi recompression
      recompress_multipng(precomp_ctx, header1);
      break;
    }
    case D_GIF: { // GIF recompression
      print_to_log(PRECOMP_DEBUG_LOG, "Decompressed data - GIF\n");
      try_recompression_gif(precomp_ctx, header1);
      break;
    }
    case D_JPG: { // JPG recompression
      recompress_jpg(precomp_ctx, header1);
      break;
    }
    case D_SWF: { // SWF recompression
      recompress_swf(precomp_ctx, header1);
      break;
    }
    case D_BASE64: { // Base64 recompression
      recompress_base64(precomp_ctx, header1);
      break;
    }
    case D_BZIP2: { // bZip2 recompression
      recompress_bzip2(precomp_ctx, header1);
      break;
    }
    case D_MP3: { // MP3 recompression
      recompress_mp3(precomp_ctx);
      break;
    }
    case D_BRUTE: { // brute mode recompression
      recompress_raw_deflate(precomp_ctx, header1);
      break;
    }
    case D_RAW: { // raw zLib recompression
      recompress_zlib(precomp_ctx, header1);
      break;
    }
    default:
      throw std::runtime_error(make_cstyle_format_string("ERROR: Unsupported stream type %i\n", headertype));
    }

  }

  fin_pos = precomp_ctx.fin->tellg();
}

  return RETURN_SUCCESS;
}

int decompress_file(RecursionContext& precomp_ctx)
{
  return wrap_with_exception_catch([&]() { return decompress_file_impl(precomp_ctx); });
}

void read_header(Precomp& precomp_mgr) {
  if (precomp_mgr.header_already_read) throw std::runtime_error("Attempted to read the input stream header twice");
  unsigned char hdr[3];
  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(hdr), 3);
  if ((hdr[0] == 'P') && (hdr[1] == 'C') && (hdr[2] == 'F')) {
  } else {
    throw PrecompError(ERR_NO_PCF_HEADER);
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(hdr), 3);
  if ((hdr[0] == V_MAJOR) && (hdr[1] == V_MINOR) && (hdr[2] == V_MINOR2)) {
  } else {
    throw PrecompError(
      ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
      make_cstyle_format_string("PCF version info: %i.%i.%i\n", hdr[0], hdr[1], hdr[2])
    );
  }

  precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(hdr), 1);
  if (hdr[0] != 0) throw PrecompError(
    ERR_PCF_HEADER_INCOMPATIBLE_VERSION,
    "OTF compression no longer supported, use original Precomp and use the -nn conversion option to get an uncompressed Precomp stream that should work here"
  );

  std::string header_filename;
  char c;
  do {
    c = precomp_mgr.ctx->fin->get();
    if (c != 0) header_filename += c;
  } while (c != 0);

  if (precomp_mgr.output_file_name.empty()) {
    precomp_mgr.output_file_name = header_filename;
  }
  precomp_mgr.header_already_read = true;
}

std::tuple<long long, std::vector<char>> compare_files_penalty(Precomp& precomp_mgr, RecursionContext& context, IStreamLike& file1, IStreamLike& file2, long long pos1, long long pos2) {
  unsigned char input_bytes1[COMP_CHUNK];
  unsigned char input_bytes2[COMP_CHUNK];
  long long same_byte_count = 0;
  long long same_byte_count_penalty = 0;
  long long rek_same_byte_count = 0;
  long long rek_same_byte_count_penalty = -1;
  long long size1, size2, minsize;
  long long i;
  bool endNow = false;

  bool use_penalty_bytes = false;

  std::vector<char> penalty_bytes;

  file1.seekg(0, std::ios_base::end);
  file2.seekg(0, std::ios_base::end);
  long long compare_end = std::min(file1.tellg() - pos1, file2.tellg() - pos2);

  file1.seekg(pos1, std::ios_base::beg);
  file2.seekg(pos2, std::ios_base::beg);

  do {
    precomp_mgr.call_progress_callback();

    file1.read(reinterpret_cast<char*>(input_bytes1), COMP_CHUNK);
    size1 = file1.gcount();
    file2.read(reinterpret_cast<char*>(input_bytes2), COMP_CHUNK);
    size2 = file2.gcount();

    minsize = std::min(size1, size2);
    for (i = 0; i < minsize; i++) {
      if (input_bytes1[i] != input_bytes2[i]) {

        same_byte_count_penalty -= 5; // 4 bytes = position, 1 byte = new byte

        // if same_byte_count_penalty is too low, stop
        if ((long long)(same_byte_count_penalty + (compare_end - same_byte_count)) < 0) {
          endNow = true;
          break;
        }
        // stop, if penalty_bytes len gets too big
        if ((penalty_bytes.size() + 5) >= MAX_PENALTY_BYTES) {
          endNow = true;
          break;
        }

        // position
        penalty_bytes.push_back((same_byte_count >> 24) % 256);
        penalty_bytes.push_back((same_byte_count >> 16) % 256);
        penalty_bytes.push_back((same_byte_count >> 8) % 256);
        penalty_bytes.push_back(same_byte_count % 256);
        // new byte
        penalty_bytes.push_back(input_bytes1[i]);
      } else {
        same_byte_count_penalty++;
      }

      same_byte_count++;

      if (same_byte_count_penalty > rek_same_byte_count_penalty) {
        use_penalty_bytes = true;
        rek_same_byte_count = same_byte_count;
        rek_same_byte_count_penalty = same_byte_count_penalty;
      }

    }
  } while ((minsize == COMP_CHUNK) && (!endNow));

  return { rek_same_byte_count, use_penalty_bytes ? penalty_bytes: std::vector<char>()};
}

// JPG routines
void packjpg_mp3_dll_msg() {
  print_to_log(PRECOMP_NORMAL_LOG, "Using packJPG for JPG recompression, packMP3 for MP3 recompression.\n");
  print_to_log(PRECOMP_NORMAL_LOG, "%s\n", packjpg_version_info());
  print_to_log(PRECOMP_NORMAL_LOG, "%s\n", packmp3_version_info());
  print_to_log(PRECOMP_NORMAL_LOG, "More about packJPG and packMP3 here: http://www.matthiasstirner.com\n\n");
}

uintmax_t fileSize64(const char* filename, int* error_code) {
  std::error_code ec;
  auto size = std::filesystem::file_size(filename, ec);
  *error_code = ec.value();
  return size;
}
void print_to_terminal(const char* fmt, ...) {
  va_list args;
  va_start(args, fmt);
  va_list args_copy;
  va_copy(args_copy, args);
  int length = std::vsnprintf(nullptr, 0, fmt, args);
  va_end(args);
  assert(length >= 0);

  char* buf = new char[length + 1];
  std::vsnprintf(buf, length + 1, fmt, args_copy);
  va_end(args_copy);

  std::string str(buf);
  delete[] buf;
  print_to_console(str);
}

RecursionContext& recursion_push(RecursionContext& precomp_ctx, long long recurse_stream_length) {
  auto context_progress_range = precomp_ctx.global_max_percent - precomp_ctx.global_min_percent;
  auto current_context_progress_percent = static_cast<float>(precomp_ctx.input_file_pos) / precomp_ctx.fin_length;
  auto recursion_end_progress_percent = static_cast<float>(precomp_ctx.input_file_pos + recurse_stream_length) / precomp_ctx.fin_length;

  auto new_minimum = precomp_ctx.global_min_percent + (context_progress_range * current_context_progress_percent);
  auto new_maximum = precomp_ctx.global_min_percent + (context_progress_range * recursion_end_progress_percent);

  Precomp& precomp_mgr = precomp_ctx.precomp;
  precomp_mgr.recursion_contexts_stack.push_back(std::move(precomp_mgr.ctx));
  precomp_mgr.ctx = std::make_unique<RecursionContext>(new_minimum, new_maximum, precomp_mgr);
  return *precomp_mgr.ctx;
}

void recursion_pop(Precomp& precomp_mgr) {
  precomp_mgr.ctx = std::move(precomp_mgr.recursion_contexts_stack.back());
  precomp_mgr.recursion_contexts_stack.pop_back();
}

recursion_result recursion_compress(Precomp& precomp_mgr, long long compressed_bytes, long long decompressed_bytes, PrecompTmpFile& tmpfile, bool deflate_type, std::vector<unsigned char> in_memory) {
  recursion_result tmp_r;
  tmp_r.success = false;

  bool rescue_anything_was_used = false;
  bool rescue_non_zlib_was_used = false;

  if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.max_recursion_depth) {
    precomp_mgr.max_recursion_depth_reached = true;
    return tmp_r;
  }

  if (deflate_type && !in_memory.empty()) {
    auto decomp_io_buf_ptr = in_memory.data();
    auto memstream = memiostream::make(decomp_io_buf_ptr, decomp_io_buf_ptr + decompressed_bytes);
    fast_copy(*memstream, tmpfile, decompressed_bytes);
  }
  tmpfile.close();

  recursion_push(*precomp_mgr.ctx, compressed_bytes);

  if (!deflate_type) {
    // shorten tempfile1 to decompressed_bytes
    std::filesystem::resize_file(tmpfile.file_path, decompressed_bytes);
  }

  precomp_mgr.ctx->fin_length = std::filesystem::file_size(tmpfile.file_path.c_str());
  auto fin = new std::ifstream();
  fin->open(tmpfile.file_path, std::ios_base::in | std::ios_base::binary);
  if (!fin->is_open()) {
    throw std::runtime_error(make_cstyle_format_string("ERROR: Recursion input file \"%s\" doesn't exist\n", tmpfile.file_path.c_str()));
  }
  precomp_mgr.ctx->set_input_stream(fin);

  tmp_r.file_name = tmpfile.file_path;
  tmp_r.file_name += '_';
  auto fout = new std::ofstream();
  fout->open(tmp_r.file_name.c_str(), std::ios_base::out | std::ios_base::binary);
  precomp_mgr.ctx->set_output_stream(fout, true);

  precomp_mgr.recursion_depth++;
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);
  const auto ret_code = compress_file(precomp_mgr);
  if (ret_code != RETURN_SUCCESS && ret_code != RETURN_NOTHING_DECOMPRESSED) throw PrecompError(ret_code);
  tmp_r.success = ret_code == RETURN_SUCCESS;

  // TODO CHECK: Delete ctx?

  if (precomp_mgr.ctx->anything_was_used)
    rescue_anything_was_used = true;

  if (precomp_mgr.ctx->non_zlib_was_used)
    rescue_non_zlib_was_used = true;

  precomp_mgr.recursion_depth--;
  recursion_pop(precomp_mgr);

  if (rescue_anything_was_used)
    precomp_mgr.ctx->anything_was_used = true;

  if (rescue_non_zlib_was_used)
    precomp_mgr.ctx->non_zlib_was_used = true;

  if (tmp_r.success) {
    print_to_log(PRECOMP_DEBUG_LOG, "Recursion streams found\n");
  } else {
    print_to_log(PRECOMP_DEBUG_LOG, "No recursion streams found\n");
  }
  print_to_log(PRECOMP_DEBUG_LOG, "Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);

  if (!tmp_r.success) {
    remove(tmp_r.file_name.c_str());
    tmp_r.file_name = "";
  } else {
    if ((precomp_mgr.recursion_depth + 1) > precomp_mgr.max_recursion_depth_used)
      precomp_mgr.max_recursion_depth_used = (precomp_mgr.recursion_depth + 1);
    // get recursion file size
    tmp_r.file_length = std::filesystem::file_size(tmp_r.file_name.c_str());
  }

  return tmp_r;
}


void RecursionPasstroughStream::unlock_everything() {
  // Any locked thread that wakes up after EOF should not lock again on read/write, and should hopefully check eof() and realize it can't continue and fail gracefully
  write_eof = true;
  read_eof = true;
  data_needed_cv.notify_all();
  data_available_cv.notify_all();
}


RecursionPasstroughStream::RecursionPasstroughStream(std::unique_ptr<RecursionContext>&& ctx_) : ctx(std::move(ctx_)), owner_thread_id(std::this_thread::get_id()) {
  buffer.reserve(CHUNK);

  ctx->fout = std::make_unique<ObservableOStreamWrapper>(this, false);

  thread = std::thread([this] {
    thread_return_code = decompress_file(*ctx);
    unlock_everything();
  });
}

RecursionPasstroughStream::~RecursionPasstroughStream() {
  unlock_everything();
  if (thread.joinable()) thread.join();
}

int RecursionPasstroughStream::get_recursion_return_code(bool throw_on_failure) {
  unlock_everything();
  if (thread.joinable()) thread.join();
  if (throw_on_failure && thread_return_code != RETURN_SUCCESS) throw PrecompError(thread_return_code);
  return thread_return_code;
}

RecursionPasstroughStream& RecursionPasstroughStream::read(char* buff, std::streamsize count) {
  if (read_eof) {
    if (std::this_thread::get_id() != owner_thread_id) throw PrecompError(thread_return_code);
    _gcount = 0;
    return *this;
  }
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: TAKING LOCK FOR READ!\n\n", static_cast<void*>(this));
  std::unique_lock lock(mtx);
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: LOCK TAKEN FOR READ!\n\n", static_cast<void*>(this));
  std::streamsize already_read_count = 0;

  while (already_read_count < count) {
    std::streamsize remaining_count = count - already_read_count;
    std::streamsize iteration_read_count = data_available() >= remaining_count ? remaining_count : data_available();
    if (iteration_read_count == 0) {
      // If we don't have any data in the buffer we need to wait for more
      // unless we already reached the write_eof, in which case we know no more data will ever arrive
      // or if read_eof is true, which given that we were already in the middle of reading it probably means the stream was forcefully truncated (most likely to destroy it)
      if (read_eof || write_eof) { break; }

      data_needed_cv.notify_one();
      //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WAITING FOR DATA AVAILABLE FOR READ!\n\n", static_cast<void*>(this));
      data_available_cv.wait(lock);
      //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WOKE UP FROM WAITING FOR DATA AVAILABLE FOR READ!\n\n", static_cast<void*>(this));

      // now that we are back from getting more data, recalculate the iteration_read_count
      iteration_read_count = data_available() >= remaining_count ? remaining_count : data_available();
      if (iteration_read_count == 0) { continue; }
    }

    memcpy(buff + already_read_count, buffer_current_pos(), iteration_read_count);
    already_read_count += iteration_read_count;
    buffer_already_read_count += iteration_read_count;
  }

  // if we didn't read enough to satisfy the count (or we did but write_eof has been reached and there is no more data available) we set read_eof
  if (already_read_count < count || (write_eof && !data_available())) {
    read_eof = true;
  }
  _gcount = already_read_count;
  //data_needed_cv.notify_one();
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: EXITING READ, RELEASING LOCK!\n\n", static_cast<void*>(this));
  return *this;
}

std::istream::int_type RecursionPasstroughStream::get() {
  unsigned char chr[1];
  read(reinterpret_cast<char*>(&chr[0]), 1);
  return _gcount == 1 ? chr[0] : EOF;
}

std::streamsize RecursionPasstroughStream::gcount() { return _gcount; }
RecursionPasstroughStream& RecursionPasstroughStream::seekg(std::istream::off_type offset, std::ios_base::seekdir dir) {
  throw std::runtime_error("CANT SEEK ON A RecursionPassthroughStream!");
}
std::istream::pos_type RecursionPasstroughStream::tellg() { return accumulated_already_read_count + buffer_already_read_count; }
bool RecursionPasstroughStream::eof() { return write_eof && read_eof; }
bool RecursionPasstroughStream::good() { return true; }
bool RecursionPasstroughStream::bad() { return eof(); }
void RecursionPasstroughStream::clear() { throw std::runtime_error("CANT CLEAR ON A RecursionPassthroughStream!"); }

RecursionPasstroughStream& RecursionPasstroughStream::write(const char* buf, std::streamsize count) {
  if (write_eof) {
    if (std::this_thread::get_id() != owner_thread_id) throw PrecompError(thread_return_code);
    return *this;
  }
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: TAKING LOCK FOR WRITE!\n\n", static_cast<void*>(this));
  std::unique_lock lock(mtx);
  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: LOCK TAKEN FOR WRITE!\n\n", static_cast<void*>(this));
  std::streamsize data_already_written = 0;

  while (data_already_written < count) {
    std::streamsize remaining_data_to_write = count - data_already_written;
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WAITING FOR MORE DATA NEEDED FOR WRITE!\n\n", static_cast<void*>(this));
    if (data_available()) {
      data_available_cv.notify_one();
      data_needed_cv.wait(lock);
      // Check if data is actually needed or we were woken up to be terminated
      if (write_eof || read_eof) {
        if (std::this_thread::get_id() != owner_thread_id) throw PrecompError(thread_return_code);
        write_eof = true;
        return *this;
      }
    }
    //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: WOKE UP FROM WAITING FOR MORE DATA NEEDED FOR WRITE!\n\n", static_cast<void*>(this));

    // at this point, all data available was consumed and we are ready to replenish the buffer with new data
    accumulated_already_read_count += buffer_already_read_count;
    buffer_already_read_count = 0;

    std::streamsize iteration_data_to_write = remaining_data_to_write >= CHUNK ? CHUNK : remaining_data_to_write;
    if (buffer.size() != iteration_data_to_write) buffer.resize(iteration_data_to_write);
    memcpy(buffer.data(), buf + data_already_written, iteration_data_to_write);
    data_already_written += iteration_data_to_write;
  }

  //print_to_log(PRECOMP_NORMAL_LOG, "\n\n%p: EXITING WRITE, RELEASING LOCK!\n\n", static_cast<void*>(this));
  if (data_available()) { data_available_cv.notify_one(); }
  return *this;
}

RecursionPasstroughStream& RecursionPasstroughStream::put(char chr) {
  write(&chr, 1);
  return *this;
}

void RecursionPasstroughStream::flush() { throw std::runtime_error("CANT FLUSH ON A RecursionPassthroughStream!"); }
std::ostream::pos_type RecursionPasstroughStream::tellp() { return accumulated_already_read_count + buffer.size(); }
OStreamLike& RecursionPasstroughStream::seekp(std::ostream::off_type offset, std::ios_base::seekdir dir) {
  throw std::runtime_error("CANT SEEK ON A RecursionPassthroughStream!");
}

std::unique_ptr<RecursionPasstroughStream> recursion_decompress(RecursionContext& context, long long recursion_data_length, std::string tmpfile) {
  auto original_pos = context.fin->tellg();
  auto& precomp_mgr = context.precomp;

  // This is just a way for getting a new context, as we are not going to really use our context stack for decompression.
  // Is pretty dumb, could be made more straightforward but works for now
  recursion_push(context, recursion_data_length);
  auto new_ctx = std::move(precomp_mgr.ctx);
  recursion_pop(precomp_mgr);

  long long recursion_end_pos = original_pos + recursion_data_length;
  new_ctx->fin_length = recursion_data_length;
  // We create a view of the recursion data from the input stream, this way the recursive call to decompress can work as if it were working with a copy of the data
  // on a temporary file, processing it from pos 0 to EOF, while actually only reading from original_pos to recursion_end_pos
  auto fin_view = std::make_unique<IStreamLikeView>(context.fin.get(), recursion_end_pos);
  new_ctx->fin = std::move(fin_view);

  std::unique_ptr<RecursionPasstroughStream> passthrough = std::make_unique<RecursionPasstroughStream>(std::move(new_ctx));

  //print_to_log(PRECOMP_DEBUG_LOG, "Recursion start - new recursion depth %i\n", precomp_mgr.recursion_depth);

  //print_to_log(PRECOMP_DEBUG_LOG, "Recursion end - back to recursion depth %i\n", precomp_mgr.recursion_depth);

  return passthrough;
}

void fout_fput32_little_endian(OStreamLike& output, unsigned int v) {
  output.put(v % 256);
  output.put((v >> 8) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 24) % 256);
}

void fout_fput32(OStreamLike& output, unsigned int v) {
  output.put((v >> 24) % 256);
  output.put((v >> 16) % 256);
  output.put((v >> 8) % 256);
  output.put(v % 256);
}

void fout_fput_vlint(OStreamLike& output, unsigned long long v) {
  while (v >= 128) {
    output.put((v & 127) + 128);
    v = (v >> 7) - 1;
  }
  output.put(v);
}

int32_t fin_fget32(IStreamLike& input) {
  int32_t result = 0;
  result += ((long)input.get() << 24);
  result += ((long)input.get() << 16);
  result += ((long)input.get() << 8);
  result += (long)input.get();
  return result;
}
long long fin_fget_vlint(IStreamLike& input) {
  unsigned char c;
  long long v = 0, o = 0, s = 0;
  while ((c = input.get()) >= 128) {
    v += (((long long)(c & 127)) << s);
    s += 7;
    o = (o + 1) << 7;
  }
  return v + o + (((long long)c) << s);
}

// C API STUFF
CPrecomp* PrecompCreate() { return new Precomp(); }
void PrecompSetProgressCallback(CPrecomp* precomp_mgr, void(*callback)(float)) {
  reinterpret_cast<Precomp*>(precomp_mgr)->set_progress_callback(callback);
}
void PrecompDestroy(CPrecomp* precomp_mgr) {
  delete precomp_mgr;
}
CSwitches* PrecompGetSwitches(CPrecomp* precomp_mgr) { return &reinterpret_cast<Precomp*>(precomp_mgr)->switches; }
void PrecompSwitchesSetIgnoreList(CSwitches* precomp_switches, const long long* ignore_pos_list, size_t ignore_post_list_count) {
  reinterpret_cast<Switches*>(precomp_switches)->ignore_set = std::set(ignore_pos_list, ignore_pos_list + ignore_post_list_count);
}
CRecursionContext* PrecompGetRecursionContext(CPrecomp* precomp_mgr) { return reinterpret_cast<Precomp*>(precomp_mgr)->ctx.get(); }
CResultStatistics* PrecompGetResultStatistics(CPrecomp* precomp_mgr) { return &reinterpret_cast<Precomp*>(precomp_mgr)->statistics; }

int PrecompPrecompress(CPrecomp* precomp_mgr) {
  precomp_mgr->start_time = get_time_ms();
  return compress_file(*reinterpret_cast<Precomp*>(precomp_mgr));
}

int PrecompRecompress(CPrecomp* precomp_mgr) {
  precomp_mgr->start_time = get_time_ms();
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  if (!precomp_mgr->header_already_read) read_header(*internal_precomp_ptr);
  return decompress_file(*internal_precomp_ptr->ctx);
}

int PrecompReadHeader(CPrecomp* precomp_mgr, bool seek_to_beg) {
  auto internal_precomp_ptr = reinterpret_cast<Precomp*>(precomp_mgr);
  if (seek_to_beg) internal_precomp_ptr->ctx->fin->seekg(0, std::ios_base::beg);
  try {
    read_header(*internal_precomp_ptr);
  }
  catch (const PrecompError& err) {
    return err.error_code;
  }
  return 0;
}
