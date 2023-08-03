#ifndef PRECOMP_GZIP_HANDLER_H
#define PRECOMP_GZIP_HANDLER_H
#include "precomp_dll.h"
#include "formats/deflate.h"

#include <span>

class GZipFormatHandler : public PrecompFormatHandler {
public:
	bool quick_check(std::span<unsigned char> buffer) override;

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	void recompress(RecursionContext& context, std::byte precomp_hdr_flags) override;

	SupportedFormats get_header_byte() override { return D_GZIP; }

	static GZipFormatHandler* create() {
		return new GZipFormatHandler();
	}
};

#endif //PRECOMP_GZIP_HANDLER_H