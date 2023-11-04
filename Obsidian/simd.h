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

#else

  constexpr int Alignment = 8;

  using Vec = int16_t;

  inline Vec addEpi16(Vec x, Vec y) {
    return x + y;
  }

  inline Vec subEpi16(Vec x, Vec y) {
    return x - y;
  }

  inline void copyMemory(Vec dest, Vec src) {

#endif

}