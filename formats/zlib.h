#ifndef PRECOMP_ZLIB_HANDLER_H
#define PRECOMP_ZLIB_HANDLER_H
#include "precomp_dll.h"
#include "deflate.h"

#include <span>

bool zlib_header_check(const std::span<unsigned char> checkbuf_span);

class ZlibFormatHandler : public PrecompFormatHandler {
	DeflateHistogramFalsePositiveDetector falsePositiveDetector {};
public:
	explicit ZlibFormatHandler(std::vector<SupportedFormats> _header_bytes, std::optional<unsigned int> _depth_limit = std::nullopt)
		: PrecompFormatHandler(_header_bytes, _depth_limit, true) {}

	bool quick_check(const std::span<unsigned char> buffer, uintptr_t current_input_id, const long long original_input_pos) override {
		return zlib_header_check(buffer);
	}

	std::unique_ptr<precompression_result> attempt_precompression(Precomp& precomp_instance, std::span<unsigned char> buffer, long long input_stream_pos) override;

	std::unique_ptr<PrecompFormatHeaderData> read_format_header(RecursionContext& context, std::byte precomp_hdr_flags, SupportedFormats precomp_hdr_format) override;

	void recompress(IStreamLike& precompressed_input, OStreamLike& recompressed_stream, PrecompFormatHeaderData& precomp_hdr_data, SupportedFormats precomp_hdr_format, const Tools& tools) override;
	void write_pre_recursion_data(RecursionContext& context, PrecompFormatHeaderData& precomp_hdr_data) override;

	static ZlibFormatHandler* create() {
		return new ZlibFormatHandler({ D_RAW });
	}
};

#endif //PRECOMP_ZLIB_HANDLER_H