#pragma once

#include <algorithm>
#include <cstdint>
#include <immintrin.h>

namespace SIMD {

#if defined(__AVX512F__) && defined(__AVX512BW__)

  using VecI = __m512i;

  inline VecI addEpi16(VecI x, VecI y) {
    return _mm512_add_epi16(x, y);
  }

  inline VecI addEpi32(VecI x, VecI y) {
    return _mm512_add_epi32(x, y);
  }

  inline VecI subEpi16(VecI x, VecI y) {
    return _mm512_sub_epi16(x, y);
  }

  inline VecI minEpi16(VecI x, VecI y) {
    return _mm512_min_epi16(x, y);
  }

  inline VecI maxEpi16(VecI x, VecI y) {
    return _mm512_max_epi16(x, y);
  }

  inline VecI mulloEpi16(VecI x, VecI y) {
    return _mm512_mullo_epi16(x, y);
  }

  inline VecI maddEpi16(VecI x, VecI y) {
    return _mm512_madd_epi16(x, y);
  }

  inline VecI VecISetZero() {
    return _mm512_setzero_si512();
  }

  inline VecI VecISet1Epi16(int16_t x) {
    return _mm512_set1_epi16(x);
  }

  inline int HaddEpi32(VecI VecI) {
    return _mm512_reduce_add_epi32(VecI);
  }

#elif defined(__AVX2__)

  using VecI = __m256i;
  using VecF = __m256;

  inline VecI addEpi16(VecI x, VecI y) {
    return _mm256_add_epi16(x, y);
  }

  inline VecI addEpi32(VecI x, VecI y) {
    return _mm256_add_epi32(x, y);
  }

  inline VecI subEpi16(VecI x, VecI y) {
    return _mm256_sub_epi16(x, y);
  }

  inline VecI minEpi16(VecI x, VecI y) {
    return _mm256_min_epi16(x, y);
  }

  inline VecI maxEpi16(VecI x, VecI y) {
    return _mm256_max_epi16(x, y);
  }

  inline VecI mulloEpi16(VecI x, VecI y) {
    return _mm256_mullo_epi16(x, y);
  }

  inline VecI maddEpi16(VecI x, VecI y) {
    return _mm256_madd_epi16(x, y);
  }

  inline VecI vecSet1Epi16(int16_t x) {
    return _mm256_set1_epi16(x);
  }

#else

  using VecI = __m128i;

  inline VecI addEpi16(VecI x, VecI y) {
    return _mm_add_epi16(x, y);
  }

  inline VecI addEpi32(VecI x, VecI y) {
    return _mm_add_epi32(x, y);
  }

  inline VecI subEpi16(VecI x, VecI y) {
    return _mm_sub_epi16(x, y);
  }

  inline VecI minEpi16(VecI x, VecI y) {
    return _mm_min_epi16(x, y);
  }

  inline VecI maxEpi16(VecI x, VecI y) {
    return _mm_max_epi16(x, y);
  }

  inline VecI mulloEpi16(VecI x, VecI y) {
    return _mm_mullo_epi16(x, y);
  }

  inline VecI maddEpi16(VecI x, VecI y) {
    return _mm_madd_epi16(x, y);
  }

  inline VecI VecISetZero() {
    return _mm_setzero_si128();
  }

  inline VecI VecISet1Epi16(int16_t x) {
    return _mm_set1_epi16(x);
  }

  inline int HaddEpi32(VecI VecI) {
    const VecI high64 = _mm_unpackhi_epi64(VecI, VecI);
    const VecI sum64 = _mm_add_epi32(VecI, high64);

    const VecI high32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
    const VecI sum32 = _mm_add_epi32(sum64, high32);

    return _mm_cvtsi128_si32(sum32);
  }

#endif

  constexpr int Alignment = std::max<int>(8, sizeof(VecI));

}