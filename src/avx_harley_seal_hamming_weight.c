#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include <x86intrin.h>

#ifdef HAVE_AVX2_INSTRUCTIONS

#include "avx_harley_seal_hamming_weight.h"

static __m256i popcount_1(const __m256i v) {
  const __m256i m1 = _mm256_set1_epi8(0x55);
  const __m256i m2 = _mm256_set1_epi8(0x33);
  const __m256i m4 = _mm256_set1_epi8(0x0F);

  const __m256i t1 = _mm256_sub_epi8(v,       (_mm256_srli_epi16(v,  1) & m1));
  const __m256i t2 = _mm256_add_epi8(t1 & m2, (_mm256_srli_epi16(t1, 2) & m2));
  const __m256i t3 = _mm256_add_epi8(t2, _mm256_srli_epi16(t2, 4)) & m4;
  return _mm256_sad_epu8(t3, _mm256_setzero_si256());
}


static __m256i popcount_2(__m256i v) {

    const __m256i lookup = _mm256_setr_epi8(
        /* 0 */ 0, /* 1 */ 1, /* 2 */ 1, /* 3 */ 2,
        /* 4 */ 1, /* 5 */ 2, /* 6 */ 2, /* 7 */ 3,
        /* 8 */ 1, /* 9 */ 2, /* a */ 2, /* b */ 3,
        /* c */ 2, /* d */ 3, /* e */ 3, /* f */ 4,

        /* 0 */ 0, /* 1 */ 1, /* 2 */ 1, /* 3 */ 2,
        /* 4 */ 1, /* 5 */ 2, /* 6 */ 2, /* 7 */ 3,
        /* 8 */ 1, /* 9 */ 2, /* a */ 2, /* b */ 3,
        /* c */ 2, /* d */ 3, /* e */ 3, /* f */ 4
    );

    const __m256i low_mask = _mm256_set1_epi8(0x0f);

    const __m256i lo  = _mm256_and_si256(v, low_mask);
    const __m256i hi  = _mm256_and_si256(_mm256_srli_epi16(v, 4), low_mask);
    const __m256i popcnt1 = _mm256_shuffle_epi8(lookup, lo);
    const __m256i popcnt2 = _mm256_shuffle_epi8(lookup, hi);

    return _mm256_sad_epu8(_mm256_add_epi8(popcnt1, popcnt2), _mm256_setzero_si256());
}


static inline void CSA(__m256i* h, __m256i* l, __m256i a, __m256i b, __m256i c) {
  const __m256i u = _mm256_xor_si256(a , b);
  *h = _mm256_or_si256(_mm256_and_si256(a , b) , _mm256_and_si256(u , c) );
  *l = _mm256_xor_si256(u , c);
}

#if 1
#   define popcount popcount_1
#else
#   define popcount popcount_2
#endif

static uint64_t popcnt(const __m256i* data, const uint64_t size) {
  __m256i total     = _mm256_setzero_si256();
  __m256i ones      = _mm256_setzero_si256();
  __m256i twos      = _mm256_setzero_si256();
  __m256i fours     = _mm256_setzero_si256();
  __m256i eights    = _mm256_setzero_si256();
  __m256i sixteens  = _mm256_setzero_si256();
  __m256i twosA, twosB, foursA, foursB, eightsA, eightsB;

  const uint64_t limit = size - size % 16;
  uint64_t i = 0;

  for(; i < limit; i += 16) {
    CSA(&twosA, &ones, ones, data[i+0], data[i+1]);
    CSA(&twosB, &ones, ones, data[i+2], data[i+3]);
    CSA(&foursA, &twos, twos, twosA, twosB);
    CSA(&twosA, &ones, ones, data[i+4], data[i+5]);
    CSA(&twosB, &ones, ones, data[i+6], data[i+7]);
    CSA(&foursB,& twos, twos, twosA, twosB);
    CSA(&eightsA,&fours, fours, foursA, foursB);
    CSA(&twosA, &ones, ones, data[i+8], data[i+9]);
    CSA(&twosB, &ones, ones, data[i+10], data[i+11]);
    CSA(&foursA, &twos, twos, twosA, twosB);
    CSA(&twosA, &ones, ones, data[i+12], data[i+13]);
    CSA(&twosB, &ones, ones, data[i+14], data[i+15]);
    CSA(&foursB, &twos, twos, twosA, twosB);
    CSA(&eightsB, &fours, fours, foursA, foursB);
    CSA(&sixteens, &eights, eights, eightsA, eightsB);

    total = _mm256_add_epi64(total, popcount(sixteens));
  }

  total = _mm256_slli_epi64(total, 4);     // * 16
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(eights), 3)); // += 8 * ...
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(fours),  2)); // += 4 * ...
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(twos),   1)); // += 2 * ...
  total = _mm256_add_epi64(total, popcount(ones));
  for(; i < size; i++)
    total = _mm256_add_epi64(total, popcount(data[i]));


  return (uint64_t)(_mm256_extract_epi64(total, 0))
       + (uint64_t)(_mm256_extract_epi64(total, 1))
       + (uint64_t)(_mm256_extract_epi64(total, 2))
       + (uint64_t)(_mm256_extract_epi64(total, 3));
}

// this is simply the above function, with the body repeated twice
static uint64_t popcnt_unrolled_twice(const __m256i* data, const uint64_t size) {
  __m256i total     = _mm256_setzero_si256();
  __m256i ones0     = _mm256_setzero_si256();
  __m256i twos0     = _mm256_setzero_si256();
  __m256i fours0    = _mm256_setzero_si256();
  __m256i eights0   = _mm256_setzero_si256();
  __m256i sixteens0 = _mm256_setzero_si256();
  __m256i twosA0, twosB0, foursA0, foursB0, eightsA0, eightsB0;
  __m256i ones1     = _mm256_setzero_si256();
  __m256i twos1     = _mm256_setzero_si256();
  __m256i fours1    = _mm256_setzero_si256();
  __m256i eights1   = _mm256_setzero_si256();
  __m256i sixteens1 = _mm256_setzero_si256();
  __m256i twosA1, twosB1, foursA1, foursB1, eightsA1, eightsB1;

  const uint64_t limit = size - size % 32;
  uint64_t i = 0;

  for(; i < limit; i += 32) {
    CSA(&twosA0,    &ones0,   ones0,   data[i+0], data[i+1]);
    CSA(&twosA1,    &ones1,   ones1,   data[i+16+0], data[i+16+1]);
    CSA(&twosB0,    &ones0,   ones0,   data[i+2], data[i+3]);
    CSA(&twosB1,    &ones1,   ones1,   data[i+16+2], data[i+16+3]);
    CSA(&foursA0,   &twos0,   twos0,   twosA0, twosB0);
    CSA(&foursA1,   &twos1,   twos1,   twosA1, twosB1);
    CSA(&twosA0,    &ones0,   ones0,   data[i+4], data[i+5]);
    CSA(&twosA1,    &ones1,   ones1,   data[i+16+4], data[i+16+5]);
    CSA(&twosB0,    &ones0,   ones0,   data[i+6], data[i+7]);
    CSA(&twosB1,    &ones1,   ones1,   data[i+16+6], data[i+16+7]);
    CSA(&foursB0,   &twos0,   twos0,   twosA0, twosB0);
    CSA(&foursB1,   &twos1,   twos1,   twosA1, twosB1);
    CSA(&eightsA0,  &fours0,  fours0,  foursA0, foursB0);
    CSA(&eightsA1,  &fours1,  fours1,  foursA1, foursB1);
    CSA(&twosA0,    &ones0,   ones0,   data[i+8], data[i+9]);
    CSA(&twosA1,    &ones1,   ones1,   data[i+16+8], data[i+16+9]);
    CSA(&twosB0,    &ones0,   ones0,   data[i+10], data[i+11]);
    CSA(&twosB1,    &ones1,   ones1,   data[i+16+10], data[i+16+11]);
    CSA(&foursA0,   &twos0,   twos0,   twosA0, twosB0);
    CSA(&foursA1,   &twos1,   twos1,   twosA1, twosB1);
    CSA(&twosA0,    &ones0,   ones0,   data[i+12], data[i+13]);
    CSA(&twosA1,    &ones1,   ones1,   data[i+16+12], data[i+16+13]);
    CSA(&twosB0,    &ones0,   ones0,   data[i+14], data[i+15]);
    CSA(&twosB1,    &ones1,   ones1,   data[i+16+14], data[i+16+15]);
    CSA(&foursB0,   &twos0,   twos0,   twosA0, twosB0);
    CSA(&foursB1,   &twos1,   twos1,   twosA1, twosB1);
    CSA(&eightsB0,  &fours0,  fours0,  foursA0, foursB0);
    CSA(&eightsB1,  &fours1,  fours1,  foursA1, foursB1);
    CSA(&sixteens0, &eights0, eights0, eightsA0, eightsB0);
    CSA(&sixteens1, &eights1, eights1, eightsA1, eightsB1);

    total = _mm256_add_epi64(total, popcount(sixteens0));
    total = _mm256_add_epi64(total, popcount(sixteens1));
  }

  total = _mm256_slli_epi64(total, 4);     // * 16
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(eights0), 3)); // += 8 * ...
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(fours0),  2)); // += 4 * ...
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(twos0),   1)); // += 2 * ...
  total = _mm256_add_epi64(total, popcount(ones0));
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(eights1), 3)); // += 8 * ...
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(fours1),  2)); // += 4 * ...
  total = _mm256_add_epi64(total, _mm256_slli_epi64(popcount(twos1),   1)); // += 2 * ...
  total = _mm256_add_epi64(total, popcount(ones1));
  for(; i < size; i++)
    total = _mm256_add_epi64(total, popcount(data[i]));


  return (uint64_t)(_mm256_extract_epi64(total, 0))
       + (uint64_t)(_mm256_extract_epi64(total, 1))
       + (uint64_t)(_mm256_extract_epi64(total, 2))
       + (uint64_t)(_mm256_extract_epi64(total, 3));
}

int avx2_harley_seal_bitset64_weight(const uint64_t * data, size_t size) {
  const unsigned int wordspervector = sizeof(__m256i) / sizeof(uint64_t);
  const unsigned int minvit = 16 * wordspervector;
  int total = size >= minvit ? popcnt((const __m256i*) data, size / wordspervector) : 0;
  for (size_t i = size - size % minvit; i < size; i++) {
    total += _mm_popcnt_u64(data[i]);
  }
  return total;
}

int avx2_harley_seal_unrolled_twice(const uint64_t * data, size_t size) {
  const unsigned int wordspervector = sizeof(__m256i) / sizeof(uint64_t);
  const unsigned int minvit = 16 * wordspervector;
  int total = size >= minvit ? popcnt_unrolled_twice((const __m256i*) data, size / wordspervector) : 0;
  for (size_t i = size - size % minvit; i < size; i++) {
    total += _mm_popcnt_u64(data[i]);
  }
  return total;
}

#endif // HAVE_AVX2_INSTRUCTIONS
