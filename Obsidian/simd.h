#pragma once

#include <algorithm>
#include <cstdint>
#include <immintrin.h>

namespace SIMD {

#if defined(__AVX512F__) && defined(__AVX512BW__)

  using Vec = __m512i;

  inline Vec addEpi16(Vec x, Vec y) {
    return _mm512_add_epi16(x, y);
  }

  inline Vec addEpi32(Vec x, Vec y) {
    return _mm512_add_epi32(x, y);
  }

  inline Vec subEpi16(Vec x, Vec y) {
    return _mm512_sub_epi16(x, y);
  }

  inline Vec minEpi16(Vec x, Vec y) {
    return _mm512_min_epi16(x, y);
  }

  inline Vec maxEpi16(Vec x, Vec y) {
    return _mm512_max_epi16(x, y);
  }

  inline Vec mulloEpi16(Vec x, Vec y) {
    return _mm512_mullo_epi16(x, y);
  }

  inline Vec maddEpi16(Vec x, Vec y) {
    return _mm512_madd_epi16(x, y);
  }

  inline Vec vecSetZero() {
    return _mm512_setzero_si512();
  }

  inline Vec vecSet1Epi16(int16_t x) {
    return _mm512_set1_epi16(x);
  }

  inline int vecHaddEpi32(Vec vec) {
    return _mm512_reduce_add_epi32(vec);
  }

#elif defined(__AVX2__)

  using Vec = __m256;


#else

  using Vec = __m128i;

  inline Vec addEpi16(Vec x, Vec y) {
    return _mm_add_epi16(x, y);
  }

  inline Vec addEpi32(Vec x, Vec y) {
    return _mm_add_epi32(x, y);
  }

  inline Vec subEpi16(Vec x, Vec y) {
    return _mm_sub_epi16(x, y);
  }

  inline Vec minEpi16(Vec x, Vec y) {
    return _mm_min_epi16(x, y);
  }

  inline Vec maxEpi16(Vec x, Vec y) {
    return _mm_max_epi16(x, y);
  }

  inline Vec mulloEpi16(Vec x, Vec y) {
    return _mm_mullo_epi16(x, y);
  }

  inline Vec maddEpi16(Vec x, Vec y) {
    return _mm_madd_epi16(x, y);
  }

  inline Vec vecSetZero() {
    return _mm_setzero_si128();
  }

  inline Vec vecSet1Epi16(int16_t x) {
    return _mm_set1_epi16(x);
  }

  inline int vecHaddEpi32(Vec vec) {
    const Vec high64 = _mm_unpackhi_epi64(vec, vec);
    const Vec sum64 = _mm_add_epi32(vec, high64);

    const Vec high32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
    const Vec sum32 = _mm_add_epi32(sum64, high32);

    return _mm_cvtsi128_si32(sum32);
  }

#endif

  constexpr int Alignment = std::max<int>(8, sizeof(Vec));

}