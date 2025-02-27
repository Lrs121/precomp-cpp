cmake_minimum_required(VERSION 3.15)
cmake_policy(SET CMP0091 NEW)

project(precomp)

set(SRCDIR ${CMAKE_CURRENT_SOURCE_DIR})
set(OBJDIR ${CMAKE_CURRENT_BINARY_DIR})

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
  message(STATUS "Build type: Release")
else()
  if (NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(WARNING "CMake build type is set to ${CMAKE_BUILD_TYPE}! This might result in bad performance!")
  else()
    message(STATUS "Build type: Release")
  endif()
endif()

set(CMAKE_CXX_STANDARD 20)
if (UNIX)
  set(CMAKE_C_STANDARD 99)
  set(THREADS_PREFER_PTHREAD_FLAG ON)
  find_package(Threads REQUIRED)
endif()

if (MSVC)
    # Disabling Incremental Linking might be useful for example to use SizeBench to analyze compiled binary size contributions from different parts of the code
    add_link_options($<$<CONFIG:RelWithDebInfo>:/INCREMENTAL:NO>)
    #add_link_options($<$<CONFIG:Debug>:/INCREMENTAL:NO>)

    if (WIN32 AND NOT MSVC_VERSION VERSION_LESS 142)
        # This should enable hot reload for VS2022
        add_link_options($<$<CONFIG:Debug>:/INCREMENTAL>)
        add_compile_options($<$<CONFIG:Debug>:/ZI>)
    endif()
endif()

add_compile_options("$<$<CONFIG:DEBUG>:-DDEBUG>")

include_directories(AFTER ${SRCDIR})

function(add_stem2file VAR pattern stem)
    set(TMPV ${${VAR}})
    foreach(IND ${stem})
        string(REGEX REPLACE "^[ ]*([0-9a-zA-Z_%]+)[ ]*$" \\1 IND ${IND})
        string(REPLACE "%STEM%" ${IND} NEXT ${pattern})
        set(TMPV "${TMPV};${NEXT}")
    endforeach()
    set(${VAR} "${TMPV}" PARENT_SCOPE)
endfunction()

set(GIF_SRC "")
add_stem2file(GIF_SRC "${SRCDIR}/contrib/giflib/%STEM%.c" 
              "gifalloc;gif_err;dgif_lib_gcc;egif_lib_gcc")

set(BZIP_SRC "")
add_stem2file(BZIP_SRC "${SRCDIR}/contrib/bzip2/%STEM%.c"
              "bzlib;blocksort;crctable;compress;decompress;huffman;randtable")

set(ZLIB_SRC "")
add_stem2file(ZLIB_SRC "${SRCDIR}/contrib/zlib/%STEM%.c" 
              "adler32;crc32;zutil;trees;inftrees;inffast;inflate;deflate")

set(PACKARI_SRC "")
add_stem2file(PACKARI_SRC "${SRCDIR}/contrib/packjpg/%STEM%.cpp" 
              "aricoder;bitops")

set(PACKJPG_SRC "")
add_stem2file(PACKJPG_SRC "${SRCDIR}/contrib/packjpg/%STEM%.cpp" 
              "packjpg")
              
set(PACKMP3_SRC "")
add_stem2file(PACKMP3_SRC "${SRCDIR}/contrib/packmp3/%STEM%.cpp" 
              "huffmp3;packmp3")
add_definitions(-DBUILD_LIB)

add_definitions(-DHAVE_BOOL)
if (MSVC)
  include_directories(AFTER "msinttypes")
  add_definitions(-D_UNICODE -DUNICODE)
endif (MSVC)

# Not the cleanest thing ever, but we just make Mac compilation use unix/linux code, for now it works
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    add_definitions(-D__unix)
    add_definitions(-D__linux)
endif()

set(BRUNSLI_SRC "")
add_stem2file(BRUNSLI_SRC "${SRCDIR}/contrib/brunsli/c/common/%STEM%.cc"
              "ans_params;constants;context;huffman_tree;lehmer_code;platform;\
               predict;quant_matrix")
add_stem2file(BRUNSLI_SRC "${SRCDIR}/contrib/brunsli/c/dec/%STEM%.cc"
              "ans_decode;bit_reader;brunsli_decode;context_map_decode;histogram_decode;\
               huffman_decode;huffman_table;jpeg_data_writer")
add_stem2file(BRUNSLI_SRC "${SRCDIR}/contrib/brunsli/c/enc/%STEM%.cc"
              "ans_encode;brunsli_encode;context_map_encode;histogram_encode;jpeg_data_reader;\
               jpeg_huffman_decode;write_bits")
include_directories(AFTER "${SRCDIR}/contrib/brunsli/c/common"
                          "${SRCDIR}/contrib/brunsli/c/dec"
                          "${SRCDIR}/contrib/brunsli/c/enc"
                          "${SRCDIR}/contrib/brunsli/c/include")

set(BROTLI_SRC "")
add_stem2file(BROTLI_SRC "${SRCDIR}/contrib/brotli/c/dec/%STEM%.c"
              "decode;")
add_stem2file(BROTLI_SRC "${SRCDIR}/contrib/brotli/c/enc/%STEM%.c"
              "encode;")
include_directories(AFTER "${SRCDIR}/contrib/brotli/c/include")

if ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")
  add_definitions(-DBIT64)
endif ("${CMAKE_SIZEOF_VOID_P}" EQUAL "8")


set(PREFLATE_SRC "")
add_stem2file(PREFLATE_SRC "${SRCDIR}/contrib/preflate/preflate_%STEM%.cpp" 
              "block_decoder;block_reencoder;block_trees;\
               complevel_estimator;constants;decoder;hash_chain;info;\
               parameter_estimator;parser_config;predictor_state;reencoder;\
               seq_chain;statistical_codec;statistical_model;token;\
               token_predictor;tree_predictor")
add_stem2file(PREFLATE_SRC "${SRCDIR}/contrib/preflate/support/%STEM%.cpp"
              "arithmetic_coder;array_helper;bit_helper;bitstream;const_division;\
               filestream;huffman_decoder;huffman_encoder;huffman_helper;memstream;\
               outputcachestream;task_pool")
include_directories(AFTER "${SRCDIR}/contrib/preflate")

set(PRECOMP_SRC "${SRCDIR}/precomp.cpp")

set(PRECOMP_UTILS_SRC "${SRCDIR}/precomp_utils.cpp")

set(PRECOMP_IO_SRC "${SRCDIR}/precomp_io.cpp")

set(LIBPRECOMP_HDR "${SRCDIR}/libprecomp.h")
set(PRECOMP_DLL_HDR "${SRCDIR}/precomp_dll.h")
set(PRECOMP_DLL_SRC "${SRCDIR}/precomp_dll.cpp")

set(DLLTEST_SRC "${SRCDIR}/dlltest.c")

set(FORMAT_HANDLERS_SRC "")
add_stem2file(FORMAT_HANDLERS_SRC "${SRCDIR}/formats/%STEM%.cpp" 
              "base64;bzip2;deflate;gif;gzip;jpeg;mp3;pdf;png;swf;zip;zlib;")
include_directories(AFTER "${SRCDIR}/formats")

add_library(precomp_dll_shared SHARED ${GIF_SRC} ${BZIP_SRC} ${ZLIB_SRC} ${PACKARI_SRC}
                               ${PACKJPG_SRC} ${PACKMP3_SRC} ${PREFLATE_SRC}
                               ${BRUNSLI_SRC} ${BROTLI_SRC} ${PRECOMP_UTILS_SRC} ${PRECOMP_IO_SRC} ${FORMAT_HANDLERS_SRC} ${PRECOMP_DLL_SRC} ${LIBPRECOMP_HDR} ${PRECOMP_DLL_HDR})
target_compile_definitions(precomp_dll_shared PRIVATE -DPRECOMPDLL)
add_library(precomp_dll_static STATIC ${GIF_SRC} ${BZIP_SRC} ${ZLIB_SRC} ${PACKARI_SRC}
                               ${PACKJPG_SRC} ${PACKMP3_SRC} ${PREFLATE_SRC}
                               ${BRUNSLI_SRC} ${BROTLI_SRC} ${PRECOMP_UTILS_SRC} ${PRECOMP_IO_SRC} ${FORMAT_HANDLERS_SRC} ${PRECOMP_DLL_SRC} ${LIBPRECOMP_HDR} ${PRECOMP_DLL_HDR})
target_compile_definitions(precomp_dll_static PRIVATE -DPRECOMPSTATIC)

add_executable(dlltest ${LIBPRECOMP_HDR} ${DLLTEST_SRC})
target_link_libraries(dlltest PRIVATE precomp_dll_shared)

add_executable(precomp ${LIBPRECOMP_HDR} ${PRECOMP_SRC} )
target_link_libraries(precomp PRIVATE precomp_dll_static)

if (UNIX)
    target_link_libraries(precomp PRIVATE Threads::Threads precomp_dll_static)
else()
    target_link_libraries(precomp PRIVATE precomp_dll_static)
endif()

install(TARGETS precomp DESTINATION bin)
