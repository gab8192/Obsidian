#pragma once

#include <cstdint>
#include <immintrin.h>

namespace SIMD {

#if defined(USE_AVX512)

  constexpr int Alignment = 64;

  using Vec = __m512i;

  inline Vec addEpi16(Vec x, Vec y) {
    return _mm512_add_epi16(x, y);
  }

  inline Vec subEpi16(Vec x, Vec y) {
    return _mm512_sub_epi16(x, y);
  }

#elif defined(USE_AVX2)

  constexpr int Alignment = 32;

  using Vec = __m256i;

  inline Vec addEpi16(Vec x, Vec y) {
    return _mm256_add_epi16(x, y);
}

  inline Vec subEpi16(Vec x, Vec y) {
    return _mm256_sub_epi16(x, y);
  }

  inline int vecHadd(Vec vec) {
    __m128i xmm0;
    __m128i xmm1;

    // Get the lower and upper half of the register:
    xmm0 = _mm256_castsi256_si128(vec);
    xmm1 = _mm256_extracti128_si256(vec, 1);

    // Add the lower and upper half vertically:
    xmm0 = _mm_add_epi32(xmm0, xmm1);

    // Get the upper half of the result:
    xmm1 = _mm_unpackhi_epi64(xmm0, xmm0);

    // Add the lower and upper half vertically:
    xmm0 = _mm_add_epi32(xmm0, xmm1);

    // Shuffle the result so that the lower 32-bits are directly above the second-lower 32-bits:
    xmm1 = _mm_shuffle_epi32(xmm0, _MM_SHUFFLE(2, 3, 0, 1));

    // Add the lower 32-bits to the second-lower 32-bits vertically:
    xmm0 = _mm_add_epi32(xmm0, xmm1);

    // Cast the result to the 32-bit integer type and return it:
    return _mm_cvtsi128_si32(xmm0);
  }

#else

  constexpr int Alignment = 8;

  using Vec = int16_t;

  inline Vec addEpi16(Vec x, Vec y) {
    return x + y;
  }

  inline Vec subEpi16(Vec x, Vec y) {
    return x - y;
  }

#endif

}