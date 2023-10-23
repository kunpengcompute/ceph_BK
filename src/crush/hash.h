#ifndef CEPH_CRUSH_HASH_H
#define CEPH_CRUSH_HASH_H

#ifdef __KERNEL__
# include <linux/types.h>
#else
# include "crush_compat.h"
#endif

#define CRUSH_HASH_RJENKINS1   0

#define CRUSH_HASH_DEFAULT CRUSH_HASH_RJENKINS1

#define CRUSH_SIMD_NUM 4	// for neon register width: 128bit

extern const char *crush_hash_name(int type);

extern __u32 crush_hash32(int type, __u32 a);
extern __u32 crush_hash32_2(int type, __u32 a, __u32 b);
extern __u32 crush_hash32_3(int type, __u32 a, __u32 b, __u32 c);
extern void crush_hash32_3x3(int type, __u32 ai[3], __u32 bi[3], __u32 ci[3], __u32 hashi[3]);
void crush_hash32_3_simdx2(int type, __u32 a[CRUSH_SIMD_NUM * 2],
					__u32 b[CRUSH_SIMD_NUM * 2], __u32 c[CRUSH_SIMD_NUM * 2], __u32 hash[CRUSH_SIMD_NUM * 2]);
extern __u32 crush_hash32_4(int type, __u32 a, __u32 b, __u32 c, __u32 d);
extern __u32 crush_hash32_5(int type, __u32 a, __u32 b, __u32 c, __u32 d,
			    __u32 e);

#endif
