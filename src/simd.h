#pragma once

#include <algorithm>
#include <cstdint>
#include <immintrin.h>

namespace SIMD {

#if defined(__AVX512F__) && defined(__AVX512BW__)

  using VecI = __m512i;
  using VecF = __m512;

  constexpr int PackusOrder[8] = {0, 2, 4, 6, 1, 3, 5, 7};

  inline VecI addEpi16(VecI x, VecI y) { return _mm512_add_epi16(x, y); }

  inline VecI addEpi32(VecI x, VecI y) { return _mm512_add_epi32(x, y); }

  inline VecF addPs(VecF x, VecF y) { return _mm512_add_ps(x, y); }

  inline VecI subEpi16(VecI x, VecI y) { return _mm512_sub_epi16(x, y); }

  inline VecI minEpi16(VecI x, VecI y) { return _mm512_min_epi16(x, y); }

  inline VecI maxEpi16(VecI x, VecI y) { return _mm512_max_epi16(x, y); }

  inline VecI mulloEpi16(VecI x, VecI y) { return _mm512_mullo_epi16(x, y); }

  inline VecI maddEpi16(VecI x, VecI y) { return _mm512_madd_epi16(x, y); }

  inline VecI set1Epi16(int16_t x) { return _mm512_set1_epi16(x); }

  inline VecI set1Epi32(int x) { return _mm512_set1_epi32(x); }

  inline VecI setzeroSi() { return _mm512_setzero_si512(); }

  inline VecI maddubsEpi16(VecI x, VecI y) { return _mm512_maddubs_epi16(x, y); }

  inline VecI slliEpi16(VecI x, int y) { return _mm512_slli_epi16(x, y); }

  inline VecI mulhiEpi16(VecI x, VecI y) { return _mm512_mulhi_epi16(x, y); }

  inline VecI packusEpi16(VecI x, VecI y) { return _mm512_packus_epi16(x, y); }

  inline uint16_t getNnzMask(VecI x) { return _mm512_cmpgt_epi32_mask(x, _mm512_setzero_si512()); }

  inline VecF setzeroPs() { return _mm512_setzero_ps(); }

  inline VecF set1Ps(float x) { return _mm512_set1_ps(x); }

  inline VecF minPs(VecF x, VecF y) { return _mm512_min_ps(x, y); }
  
  inline VecF maxPs(VecF x, VecF y) { return _mm512_max_ps(x, y); }

  inline VecF mulPs(VecF x, VecF y) { return _mm512_mul_ps(x, y); }

  inline VecF mulAddPs(VecF x, VecF y, VecF z) { return _mm512_fmadd_ps(x, y, z); }

  inline VecF castEpi32ToPs(VecI x) { return _mm512_cvtepi32_ps(x); }

  inline float reduceAddPs(VecF vec) {
    return _mm512_reduce_add_ps(vec);
  }

#elif defined(__AVX2__)

  using VecI = __m256i;
  using VecF = __m256;

  constexpr int PackusOrder[4] = {0, 2, 1, 3};

  inline VecI addEpi16(VecI x, VecI y) { return _mm256_add_epi16(x, y); }

  inline VecI addEpi32(VecI x, VecI y) { return _mm256_add_epi32(x, y); }

  inline VecF addPs(VecF x, VecF y) { return _mm256_add_ps(x, y); }

  inline VecI subEpi16(VecI x, VecI y) { return _mm256_sub_epi16(x, y); }

  inline VecI minEpi16(VecI x, VecI y) { return _mm256_min_epi16(x, y); }

  inline VecI maxEpi16(VecI x, VecI y) { return _mm256_max_epi16(x, y); }

  inline VecI mulloEpi16(VecI x, VecI y) { return _mm256_mullo_epi16(x, y); }

  inline VecI maddEpi16(VecI x, VecI y) { return _mm256_madd_epi16(x, y); }

  inline VecI set1Epi16(int16_t x) { return _mm256_set1_epi16(x); }

  inline VecI set1Epi32(int x) { return _mm256_set1_epi32(x); }

  inline VecI setzeroSi() { return _mm256_setzero_si256(); }

  inline VecI maddubsEpi16(VecI x, VecI y) { return _mm256_maddubs_epi16(x, y); }

  inline VecI slliEpi16(VecI x, int y) { return _mm256_slli_epi16(x, y); }

  inline VecI mulhiEpi16(VecI x, VecI y) { return _mm256_mulhi_epi16(x, y); }

  inline VecI packusEpi16(VecI x, VecI y) { return _mm256_packus_epi16(x, y); }

  inline uint8_t getNnzMask(VecI x) {
    return _mm256_movemask_ps(_mm256_castsi256_ps(
              _mm256_cmpgt_epi32(x, _mm256_setzero_si256())));
  }

  inline VecF setzeroPs() { return _mm256_setzero_ps(); }

  inline VecF set1Ps(float x) { return _mm256_set1_ps(x); }

  inline VecF minPs(VecF x, VecF y) { return _mm256_min_ps(x, y); }
  
  inline VecF maxPs(VecF x, VecF y) { return _mm256_max_ps(x, y); }

  inline VecF mulPs(VecF x, VecF y) { return _mm256_mul_ps(x, y); }

  inline VecF mulAddPs(VecF x, VecF y, VecF z) { return _mm256_fmadd_ps(x, y, z); }

  inline VecF castEpi32ToPs(VecI x) { return _mm256_cvtepi32_ps(x); }

  inline float reduceAddPs(VecF vec) {
    __m128 sum_128 = _mm_add_ps(_mm256_castps256_ps128(vec), _mm256_extractf128_ps(vec, 1));

    __m128 upper_64 = _mm_movehl_ps(sum_128, sum_128);
    __m128 sum_64 = _mm_add_ps(sum_128, upper_64);

    __m128 upper_32 = _mm_shuffle_ps(sum_64, sum_64, 1);
    __m128 sum_32 = _mm_add_ss(sum_64, upper_32);

    return _mm_cvtss_f32(sum_32);
  }

#elif defined(__SSSE3__)

  using VecI = __m128i;
  using VecF = __m128;

  constexpr int PackusOrder[2] = {0, 1};

  inline VecI addEpi16(VecI x, VecI y) { return _mm_add_epi16(x, y); }

  inline VecI addEpi32(VecI x, VecI y) { return _mm_add_epi32(x, y); }

  inline VecF addPs(VecF x, VecF y) { return _mm_add_ps(x, y); }

  inline VecI subEpi16(VecI x, VecI y) { return _mm_sub_epi16(x, y); }

  inline VecI minEpi16(VecI x, VecI y) { return _mm_min_epi16(x, y); }

  inline VecI maxEpi16(VecI x, VecI y) { return _mm_max_epi16(x, y); }

  inline VecI mulloEpi16(VecI x, VecI y) { return _mm_mullo_epi16(x, y); }

  inline VecI maddEpi16(VecI x, VecI y) { return _mm_madd_epi16(x, y); }

  inline VecI set1Epi16(int16_t x) { return _mm_set1_epi16(x); }

  inline VecI set1Epi32(int x) { return _mm_set1_epi32(x); }

  inline VecI setzeroSi() { return _mm_setzero_si128(); }

  inline VecI maddubsEpi16(VecI x, VecI y) { return _mm_maddubs_epi16(x, y); }

  inline VecI slliEpi16(VecI x, int y) { return _mm_slli_epi16(x, y); }

  inline VecI mulhiEpi16(VecI x, VecI y) { return _mm_mulhi_epi16(x, y); }

  inline VecI packusEpi16(VecI x, VecI y) { return _mm_packus_epi16(x, y); }

  inline uint8_t getNnzMask(VecI x) {
    return _mm_movemask_ps(_mm_castsi128_ps(
              _mm_cmpgt_epi32(x, _mm_setzero_si128())));
  }

  inline VecF setzeroPs() { return _mm_setzero_ps(); }

  inline VecF set1Ps(float x) { return _mm_set1_ps(x); }

  inline VecF minPs(VecF x, VecF y) { return _mm_min_ps(x, y); }
  
  inline VecF maxPs(VecF x, VecF y) { return _mm_max_ps(x, y); }

  inline VecF mulPs(VecF x, VecF y) { return _mm_mul_ps(x, y); }

  inline VecF mulAddPs(VecF x, VecF y, VecF z) { return _mm_add_ps(z, _mm_mul_ps(x, y)); }

  inline VecF castEpi32ToPs(VecI x) { return _mm_cvtepi32_ps(x); }

  inline float reduceAddPs(VecF vec) {
    __m128 upper_64 = _mm_movehl_ps(vec, vec);
    __m128 sum_64 = _mm_add_ps(vec, upper_64);

    __m128 upper_32 = _mm_shuffle_ps(sum_64, sum_64, 1);
    __m128 sum_32 = _mm_add_ss(sum_64, upper_32);

    return _mm_cvtss_f32(sum_32);
  }

#endif

  constexpr int Alignment = sizeof(VecI);

  inline VecI dpbusdEpi32(VecI sum, VecI x, VecI y) {
    VecI prod16 = maddubsEpi16(x, y);
    VecI prod32 = maddEpi16(prod16, set1Epi16(1));
    return addEpi32(sum, prod32);
  }

}