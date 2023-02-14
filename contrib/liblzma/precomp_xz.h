#ifndef PRECOMP_XZ_H
#define PRECOMP_XZ_H

#include "api/lzma.h"
#include "precomp_xz_params.h"

bool init_encoder_mt(lzma_stream *strm, int threads, uint64_t max_memory, 
                     uint64_t &memory_usage, uint64_t &block_size, 
                     const lzma_init_mt_extra_parameters& extra_params);

bool init_decoder(lzma_stream *strm);

#endif /* ifndef PRECOMP_XZ_H */
