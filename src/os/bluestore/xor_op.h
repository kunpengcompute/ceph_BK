#ifndef EC_ISA_XOR_OP_H
#define EC_ISA_XOR_OP_H

#include <assert.h>
#include <stdint.h>

#define EC_ISA_ADDRESS_ALIGNMENT 32u
#define EC_ISA_VECTOR_SSE2_WORDSIZE 64u
#define EC_ISA_VECTOR_NEON_WORDSIZE 64u

#if __GNUC__ > 4 || \
  ( (__GNUC__ == 4) && (__GNUC_MINOR__ >=4) ) ||\
  (__clang__ == 1 )
#ifdef EC_ISA_VECTOR_OP_DEBUG
#pragma message "* using 128-bit vector operations in " __FILE__
#endif

typedef long vector_op_t __attribute__((vector_size(16)));
#define EC_ISA_VECTOR_OP_WORDSIZE 16
#else

typedef unsigned long long vector_op_t;
#define EC_ISA_VECTOR_OP_WORDSIZE 8
#endif

#define is_aligned(POINTER, BYTE_COUNT) \
   (((uintptr_t)(const void *)(POINTER)) % (BYTE_COUNT) == 0)

void
byte_xor(unsigned char* cw, unsigned char* dw, unsigned char* ew);

void
vector_xor(vector_op_t* cw, vector_op_t* dw, vector_op_t* ew);

void
region_xor(unsigned char** src, unsigned char* parity, int src_size, unsigned size);

void
region_sse2_xor(char** src /* array of 64-byte aligned surce pointer to xor */,
	       	char* parity,
	       	int src_size,
	       	unsigned size);

void
region_neon_xor(char** src, 
		char* parity,
	       	int src_size,
		unsigned size);

#endif //EC_ISA_XOR_OP_H


