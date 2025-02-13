#include "pdf.h"
#include "deflate.h"

#include <cstddef>
#include <cstring>
#include <memory>

enum BMP_HEADER_TYPE {
    BMP_HEADER_NONE = 0,
    BMP_HEADER_8BPP = 1,
    BMP_HEADER_24BPP = 2
};

class pdf_precompression_result : public deflate_precompression_result {
private:
    unsigned int img_width;
    unsigned int img_height;

    unsigned int img_width_bytes() const {
        return bmp_header_type == BMP_HEADER_24BPP ? img_width * 3 : img_width;
    }

    void dump_bmp_hdr_to_outfile(OStreamLike& outfile) const {
        if (bmp_header_type == BMP_HEADER_NONE) return;
        int i;

        outfile.put('B');
        outfile.put('M');
        // BMP size in bytes
        unsigned int bmp_size = ((img_width + 3) & -4) * img_height;
        if (bmp_header_type == BMP_HEADER_24BPP) bmp_size *= 3;
        if (bmp_header_type == BMP_HEADER_8BPP) {
            bmp_size += 54 + 1024;
        }
        else {
            bmp_size += 54;
        }
        fout_fput32_little_endian(outfile, bmp_size);

        for (i = 0; i < 4; i++) {
            outfile.put(0);
        }
        outfile.put(54);
        if (bmp_header_type == BMP_HEADER_8BPP) {
            outfile.put(4);
        }
        else {
            outfile.put(0);
        }
        outfile.put(0);
        outfile.put(0);
        outfile.put(40);
        outfile.put(0);
        outfile.put(0);
        outfile.put(0);

        fout_fput32_little_endian(outfile, img_width);
        fout_fput32_little_endian(outfile, img_height);

        outfile.put(1);
        outfile.put(0);

        if (bmp_header_type == BMP_HEADER_8BPP) {
            outfile.put(8);
        }
        else {
            outfile.put(24);
        }
        outfile.put(0);

        for (i = 0; i < 4; i++) {
            outfile.put(0);
        }

        auto datasize = ((img_width_bytes() + 3) & -4) * img_height;
        if (bmp_header_type == BMP_HEADER_24BPP) datasize *= 3;
        fout_fput32_little_endian(outfile, datasize);

        for (i = 0; i < 16; i++) {
            outfile.put(0);
        }

        if (bmp_header_type == BMP_HEADER_8BPP) {
            // write BMP palette
            for (i = 0; i < 1024; i++) {
                outfile.put(0);
            }
        }
    }
public:
    BMP_HEADER_TYPE bmp_header_type = BMP_HEADER_NONE;

    explicit pdf_precompression_result(unsigned int img_width, unsigned int img_height)
        : deflate_precompression_result(D_PDF), img_width(img_width), img_height(img_height) {}

    void dump_precompressed_data_to_outfile(OStreamLike& outfile) const override {
        bool must_pad_bmp = false;
        unsigned int width_bytes = img_width_bytes();
        if ((bmp_header_type != BMP_HEADER_NONE) && ((width_bytes % 4) != 0)) {
            must_pad_bmp = true;
        }
        if (!must_pad_bmp) {
            deflate_precompression_result::dump_precompressed_data_to_outfile(outfile);
        }
        else {
            for (int y = 0; y < img_height; y++) {
                fast_copy(*precompressed_stream, outfile, width_bytes);

                for (int i = 0; i < (4 - (width_bytes % 4)); i++) {
                    outfile.put(0);
                }
            }
        }
    }
    void dump_to_outfile(OStreamLike& outfile) const override {
        dump_header_to_outfile(outfile);
        dump_penaltybytes_to_outfile(outfile);
        dump_recon_data_to_outfile(outfile);
        dump_stream_sizes_to_outfile(outfile);
        dump_bmp_hdr_to_outfile(outfile);
        dump_precompressed_data_to_outfile(outfile);
    }
};

bool PdfFormatHandler::quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) {
  return memcmp(buffer.data(), "/FlateDecode", 12) == 0;
}

std::unique_ptr<precompression_result> try_decompression_pdf(Precomp& precomp_mgr, unsigned char* checkbuf, long long original_input_pos, unsigned int pdf_header_length, unsigned int img_width, unsigned int img_height, int img_bpc) {
  std::unique_ptr<pdf_precompression_result> result = std::make_unique<pdf_precompression_result>(img_width, img_height);

  std::unique_ptr<PrecompTmpFile> tmpfile = std::make_unique<PrecompTmpFile>();
  tmpfile->open(precomp_mgr.get_tempfile_name("decomp_pdf"), std::ios_base::in | std::ios_base::out | std::ios_base::app | std::ios_base::binary);

  auto deflate_stream_pos = original_input_pos + pdf_header_length;

  // try to decompress at current position
  recompress_deflate_result rdres = try_recompression_deflate(precomp_mgr, *precomp_mgr.ctx->fin, deflate_stream_pos, *tmpfile);

  if (rdres.uncompressed_stream_size > 0) { // seems to be a zLib-Stream

    precomp_mgr.statistics.decompressed_streams_count++;
    if (img_bpc == 8) {
      precomp_mgr.statistics.decompressed_pdf_count_8_bit++;
    }
    else {
      precomp_mgr.statistics.decompressed_pdf_count++;
    }

    debug_deflate_detected(*precomp_mgr.ctx, rdres, "in PDF", deflate_stream_pos);

    if (rdres.accepted) {
      result->success = true;

      result->original_size = rdres.compressed_stream_size;
      result->precompressed_size = rdres.uncompressed_stream_size;

      precomp_mgr.statistics.recompressed_streams_count++;
      precomp_mgr.statistics.recompressed_pdf_count++;

      precomp_mgr.ctx->non_zlib_was_used = true;
      debug_sums(*precomp_mgr.ctx->fin, *precomp_mgr.ctx->fout, rdres);

      if (img_bpc == 8) {
        if (rdres.uncompressed_stream_size == (img_width * img_height)) {
          result->bmp_header_type = BMP_HEADER_8BPP;
          print_to_log(PRECOMP_DEBUG_LOG, "Image size did match (8 bit)\n");
          precomp_mgr.statistics.recompressed_pdf_count_8_bit++;
          precomp_mgr.statistics.recompressed_pdf_count--;
        }
        else if (rdres.uncompressed_stream_size == (img_width * img_height * 3)) {
          result->bmp_header_type = BMP_HEADER_24BPP;
          print_to_log(PRECOMP_DEBUG_LOG, "Image size did match (24 bit)\n");
          precomp_mgr.statistics.decompressed_pdf_count_8_bit--;
          precomp_mgr.statistics.decompressed_pdf_count_24_bit++;
          precomp_mgr.statistics.recompressed_pdf_count_24_bit++;
          precomp_mgr.statistics.recompressed_pdf_count--;
        }
        else {
          print_to_log(PRECOMP_DEBUG_LOG, "Image size didn't match with stream size\n");
          precomp_mgr.statistics.decompressed_pdf_count_8_bit--;
          precomp_mgr.statistics.decompressed_pdf_count++;
        }
      }

      // write compressed data header (PDF) without 12 first bytes
      //   (/FlateDecode)

      std::byte bmp_c{ 0 };
      if (result->bmp_header_type == BMP_HEADER_8BPP) {
        // 8 Bit, Bit 7,6 = 01
        bmp_c = std::byte{ 0b01000000 };
      }
      else if (result->bmp_header_type == BMP_HEADER_24BPP) {
        // 24 Bit, Bit 7,6 = 10
        bmp_c = std::byte{ 0b10000000 };
      }

      result->flags = bmp_c;
      result->inc_last_hdr_byte = false;
      result->zlib_header = std::vector(checkbuf + 12, checkbuf + pdf_header_length);

      // write decompressed data
      if (!rdres.uncompressed_stream_mem.empty()) {
        unsigned char* buf_ptr = rdres.uncompressed_stream_mem.data();
        result->precompressed_stream = memiostream::make(buf_ptr, buf_ptr + result->precompressed_size);
      }
      else {
        tmpfile->reopen();
        result->precompressed_stream = std::move(tmpfile);
      }
      result->rdres = std::move(rdres);
    }
    else {
      if (precomp_mgr.is_format_handler_active(D_RAW)) precomp_mgr.ctx->ignore_offsets[D_RAW].insert(deflate_stream_pos - 2);
      if (precomp_mgr.is_format_handler_active(D_BRUTE)) precomp_mgr.ctx->ignore_offsets[D_BRUTE].insert(deflate_stream_pos);
      print_to_log(PRECOMP_DEBUG_LOG, "No matches\n");
    }
  }

  result->original_size_extra += pdf_header_length;  // Add PDF header length to the deflate stream length for the actual PDF stream size
  return result;
}

std::unique_ptr<precompression_result> PdfFormatHandler::attempt_precompression(Precomp& precomp_mgr, std::span<unsigned char> checkbuf_span, long long original_input_pos) {
  auto checkbuf = checkbuf_span.data();
  std::unique_ptr<pdf_precompression_result> result = std::make_unique<pdf_precompression_result>(0, 0);
  long long act_search_pos = 12;
  bool found_stream = false;
  do {
    if (*(checkbuf + act_search_pos) == 's') {
      if (memcmp(checkbuf + act_search_pos, "stream", 6) == 0) {
        found_stream = true;
        break;
      }
    }
    act_search_pos++;
  } while (act_search_pos < (CHECKBUF_SIZE - 6));
  if (!found_stream) return result;

  // check if the stream is an image and width and height are given

  // read 4096 bytes before stream

  unsigned char type_buf[4097];
  long long int type_buf_length;

  type_buf[4096] = 0;

  if ((original_input_pos + act_search_pos) >= 4096) {
    precomp_mgr.ctx->fin->seekg((original_input_pos + act_search_pos) - 4096, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(type_buf), 4096);
    type_buf_length = 4096;
  }
  else {
    precomp_mgr.ctx->fin->seekg(0, std::ios_base::beg);
    precomp_mgr.ctx->fin->read(reinterpret_cast<char*>(type_buf), original_input_pos + act_search_pos);
    type_buf_length = original_input_pos + act_search_pos;
  }

  // find "<<"

  long long start_pos = -1;

  for (long long i = type_buf_length; i > 0; i--) {
    if ((type_buf[i] == '<') && (type_buf[i - 1] == '<')) {
      start_pos = i;
      break;
    }
  }

  int width_val = 0, height_val = 0, bpc_val = 0;

  if ((start_pos > -1) && (precomp_mgr.switches.pdf_bmp_mode)) {

    unsigned int width_pos, height_pos, bpc_pos;

    // find "/Width"
    width_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/Width") - type_buf;

    if (width_pos > 0)
      for (auto i = width_pos + 7; i < type_buf_length; i++) {
        if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
          if (type_buf[i] != ' ') {
            width_val = width_val * 10 + (type_buf[i] - '0');
          }
        }
        else {
          break;
        }
      }

    // find "/Height"
    height_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/Height") - type_buf;

    if (height_pos > 0)
      for (auto i = height_pos + 8; i < type_buf_length; i++) {
        if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
          if (type_buf[i] != ' ') {
            height_val = height_val * 10 + (type_buf[i] - '0');
          }
        }
        else {
          break;
        }
      }

    // find "/BitsPerComponent"
    bpc_pos = (unsigned char*)strstr((const char*)(type_buf + start_pos), "/BitsPerComponent") - type_buf;

    if (bpc_pos > 0)
      for (auto i = bpc_pos + 18; i < type_buf_length; i++) {
        if (((type_buf[i] >= '0') && (type_buf[i] <= '9')) || (type_buf[i] == ' ')) {
          if (type_buf[i] != ' ') {
            bpc_val = bpc_val * 10 + (type_buf[i] - '0');
          }
        }
        else {
          break;
        }
      }

    if ((width_val != 0) && (height_val != 0) && (bpc_val != 0)) {
      print_to_log(PRECOMP_DEBUG_LOG, "Possible image in PDF found: %i * %i, %i bit\n", width_val, height_val, bpc_val);
    }
  }

  if ((*(checkbuf + act_search_pos + 6) == 13) || (*(checkbuf + act_search_pos + 6) == 10)) {
    if ((*(checkbuf + act_search_pos + 7) == 13) || (*(checkbuf + act_search_pos + 7) == 10)) {
      // seems to be two byte EOL - zLib Header present?
      if (((((*(checkbuf + act_search_pos + 8) << 8) + *(checkbuf + act_search_pos + 9)) % 31) == 0) &&
        ((*(checkbuf + act_search_pos + 9) & 32) == 0)) { // FDICT must not be set
        int compression_method = (*(checkbuf + act_search_pos + 8) & 15);
        if (compression_method == 8) {
          return try_decompression_pdf(precomp_mgr, checkbuf, original_input_pos, act_search_pos + 10, width_val, height_val, bpc_val);
        }
      }
    }
    else {
      // seems to be one byte EOL - zLib Header present?
      if ((((*(checkbuf + act_search_pos + 7) << 8) + *(checkbuf + act_search_pos + 8)) % 31) == 0) {
        int compression_method = (*(checkbuf + act_search_pos + 7) & 15);
        if (compression_method == 8) {
          return try_decompression_pdf(precomp_mgr, checkbuf, original_input_pos, act_search_pos + 9, width_val, height_val, bpc_val);
        }
      }
    }
  }

  return result;
}

std::unique_ptr<PrecompFormatHeaderData> PdfFormatHandler::read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) {
  return read_deflate_format_header(*context.fin, *context.fout, precomp_hdr_flags, false);
}

void PdfFormatHandler::recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) {
  auto& deflate_precomp_hdr_data = static_cast<DeflateFormatHeaderData&>(precomp_hdr_data);

  // restore PDF header
  ostream_printf(recompressed_stream, "/FlateDecode");
  
  debug_deflate_reconstruct(deflate_precomp_hdr_data.rdres, "PDF", deflate_precomp_hdr_data.stream_hdr.size(), 0);

  // Write zlib_header
  recompressed_stream.write(reinterpret_cast<char*>(deflate_precomp_hdr_data.stream_hdr.data()), deflate_precomp_hdr_data.stream_hdr.size());

  std::vector<unsigned char> in_buf{};
  in_buf.resize(CHUNK);

  int bmp_c = static_cast<int>(deflate_precomp_hdr_data.option_flags & std::byte{ 0b11000000 });
  if (bmp_c == 1) print_to_log(PRECOMP_DEBUG_LOG, "Skipping BMP header (8-Bit)\n");
  if (bmp_c == 2) print_to_log(PRECOMP_DEBUG_LOG, "Skipping BMP header (24-Bit)\n");

  // read BMP header
  int bmp_width = 0;
  switch (bmp_c) {
  case 1:
    precompressed_input.read(reinterpret_cast<char*>(in_buf.data()), 54 + 1024);
    break;
  case 2:
    precompressed_input.read(reinterpret_cast<char*>(in_buf.data()), 54);
    break;
  }
  if (bmp_c > 0) {
    bmp_width = in_buf[18] + (in_buf[19] << 8) + (in_buf[20] << 16) + (in_buf[21] << 24);
    if (bmp_c == 2) bmp_width *= 3;
  }

  uint64_t read_part, skip_part;
  if ((bmp_c == 0) || ((bmp_width % 4) == 0)) {
    // recompress directly to fout
    read_part = deflate_precomp_hdr_data.rdres.uncompressed_stream_size;
    skip_part = 0;
  }
  else { // lines aligned to 4 byte, skip those bytes
    // recompress directly to fout, but skipping bytes
    read_part = bmp_width;
    skip_part = (-bmp_width) & 3;
  }
  if (!try_reconstructing_deflate_skip(precompressed_input, recompressed_stream, deflate_precomp_hdr_data.rdres, read_part, skip_part, tools.progress_callback)) {
    throw PrecompError(ERR_DURING_RECOMPRESSION);
  }
}
