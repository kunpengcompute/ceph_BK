/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2015 Mirantis, Inc.
 *
 * Author: Alyona Kiseleva <akiselyova@mirantis.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 */

#include "ZstdCompressor.h"
#include "osd/osd_types.h"

void ZstdCompressor::zstd_init_logger()
{
    PerfCountersBuilder b(cct, "zstd_compress", l_zstd_first, l_zstd_last);
    b.add_time_avg(l_zstd_createCStream_latency, "zstd_createCStream_latency", "zstd createCStream latency");
    b.add_time_avg(
        l_zstd_initCStream_srcSize_latency, "zstd_initCStream_srcSize_latency", "zstd initCStream_srcSize latency");
    b.add_time_avg(l_zstd_compressBound_latency, "zstd_compressBound_latency", "zstd compressBound latency");
    b.add_time_avg(l_zstd_compressStream2_latency, "zstd_compressStream2_latency", "zstd compressStream2 latency");
    b.add_time_avg(l_zstd_freeCStream_latency, "zstd_freeCStream_latency", "zstd freeCStream latency");
    b.add_time_avg(l_zstd_encode_latency, "zstd_encode_latency", "zstd encode latency");
    b.add_time_avg(l_zstd_decode_latency, "zstd_decode_latency", "zstd decode latency");
    b.add_time_avg(l_zstd_createDStream_latency, "zstd_createDStream_latency", "zstd createDStream latency");
    b.add_time_avg(l_zstd_initDStream_latency, "zstd_initDStream_latency", "zstd initDStream latency");
    b.add_time_avg(l_zstd_decompressStream_latency, "zstd_decompressStream_latency", "zstd decompressStream latency");
    b.add_time_avg(l_zstd_freeDStream_latency, "zstd_freeDStream_latency", "zstd freeDStream latency");

    logger = b.create_perf_counters();
    cct->get_perfcounters_collection()->add(logger);
}

void ZstdCompressor::zstd_shutdown_logger()
{
    cct->get_perfcounters_collection()->remove(logger);
    delete logger;
}

ZstdCompressor::~ZstdCompressor()
{
    zstd_shutdown_logger();
}