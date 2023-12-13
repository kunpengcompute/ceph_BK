// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Haomai Wang <haomaiwang@gmail.com>
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software
 * Foundation.  See file COPYING.
 *
 */

#ifndef CEPH_ZSTDCOMPRESSOR_H
#define CEPH_ZSTDCOMPRESSOR_H

#define ZSTD_STATIC_LINKING_ONLY

#ifdef WITH_SHARED_ZSTD
#include "zstd.h"
#else
#include "zstd/lib/zstd.h"
#endif

#include "include/buffer.h"
#include "include/encoding.h"
#include "compressor/Compressor.h"
#include "common/perf_counters.h"

#define COMPRESSION_LEVEL 5

class ZstdCompressor : public Compressor {
  CephContext *const cct;
 public:
  ZstdCompressor(CephContext *cct) : Compressor(COMP_ALG_ZSTD, "zstd"), cct(cct) {
      zstd_init_logger();
  }

  ~ZstdCompressor() override;

  int compress(const bufferlist &src, bufferlist &dst, boost::optional<int32_t> &compressor_message) override {
    auto start = ceph::mono_clock::now();
    ZSTD_CStream *s = ZSTD_createCStream();
    logger->tinc(l_zstd_createCStream_latency, ceph::mono_clock::now() - start);
    ZSTD_initCStream_srcSize(s, COMPRESSION_LEVEL, src.length());
    logger->tinc(l_zstd_initCStream_srcSize_latency, ceph::mono_clock::now() - start);
    auto p = src.begin();
    size_t left = src.length();

    size_t const out_max = ZSTD_compressBound(left);
    logger->tinc(l_zstd_compressBound_latency, ceph::mono_clock::now() - start);
    bufferptr outptr = buffer::create_small_page_aligned(out_max);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = outptr.c_str();
    outbuf.size = outptr.length();
    outbuf.pos = 0;

    while (left) {
      ceph_assert(!p.end());
      struct ZSTD_inBuffer_s inbuf;
      inbuf.pos = 0;
      inbuf.size = p.get_ptr_and_advance(left, (const char**)&inbuf.src);
      left -= inbuf.size;
      ZSTD_EndDirective const zed = (left==0) ? ZSTD_e_end : ZSTD_e_continue;
      #ifdef WITH_SHARED_ZSTD
      size_t r = ZSTD_compressStream2(s, &outbuf, &inbuf, zed);
      #else
      size_t r = ZSTD_compress_generic(s, &outbuf, &inbuf, zed);
      #endif
      if (ZSTD_isError(r)) {
	return -EINVAL;
      }
    }
    ceph_assert(p.end());
    logger->tinc(l_zstd_compressStream2_latency, ceph::mono_clock::now() - start);

    ZSTD_freeCStream(s);
    logger->tinc(l_zstd_freeCStream_latency, ceph::mono_clock::now() - start);

    // prefix with decompressed length
    encode((uint32_t)src.length(), dst);
    logger->tinc(l_zstd_encode_latency, ceph::mono_clock::now() - start);
    dst.append(outptr, 0, outbuf.pos);
    return 0;
  }

  int decompress(const bufferlist &src, bufferlist &dst, boost::optional<int32_t> compressor_message) override {
    auto i = std::cbegin(src);
    return decompress(i, src.length(), dst, compressor_message);
  }

  int decompress(bufferlist::const_iterator &p,
		 size_t compressed_len,
		 bufferlist &dst,
		 boost::optional<int32_t> compressor_message) override {
    if (compressed_len < 4) {
      return -1;
    }
    auto start = ceph::mono_clock::now();
    compressed_len -= 4;
    uint32_t dst_len;
    decode(dst_len, p);
    logger->tinc(l_zstd_decode_latency, ceph::mono_clock::now() - start);

    bufferptr dstptr(dst_len);
    ZSTD_outBuffer_s outbuf;
    outbuf.dst = dstptr.c_str();
    outbuf.size = dstptr.length();
    outbuf.pos = 0;
    ZSTD_DStream *s = ZSTD_createDStream();
    logger->tinc(l_zstd_createDStream_latency, ceph::mono_clock::now() - start);
    ZSTD_initDStream(s);
    logger->tinc(l_zstd_initDStream_latency, ceph::mono_clock::now() - start);
    while (compressed_len > 0) {
      if (p.end()) {
	return -1;
      }
      ZSTD_inBuffer_s inbuf;
      inbuf.pos = 0;
      inbuf.size = p.get_ptr_and_advance(compressed_len,
					 (const char**)&inbuf.src);
      ZSTD_decompressStream(s, &outbuf, &inbuf);
      compressed_len -= inbuf.size;
    }
    logger->tinc(l_zstd_decompressStream_latency, ceph::mono_clock::now() - start);
    ZSTD_freeDStream(s);
    logger->tinc(l_zstd_freeDStream_latency, ceph::mono_clock::now() - start);

    dst.append(dstptr, 0, outbuf.pos);
    return 0;
  }

  private:
  enum {
      l_zstd_first = 736430,
      l_zstd_createCStream_latency,
      l_zstd_initCStream_srcSize_latency,
      l_zstd_compressBound_latency,
      l_zstd_compressStream2_latency,
      l_zstd_freeCStream_latency,
      l_zstd_encode_latency,
      l_zstd_decode_latency,
      l_zstd_createDStream_latency,
      l_zstd_initDStream_latency,
      l_zstd_decompressStream_latency,
      l_zstd_freeDStream_latency,
      l_zstd_last
  };

  PerfCounters *logger = nullptr;
  void zstd_init_logger();
  void zstd_shutdown_logger();
};

#endif
