#include "lmrnn.h"
#include "incbin.h"

#include <iostream>
#include <fstream>

INCBIN(EmbeddedLMR, LmrFile);

namespace LmrNN {

  struct {
    alignas(16) weight_t FeatureWeights[FeaturesWidth][HiddenWidth];
    alignas(16) weight_t FeatureBiases[HiddenWidth];
    alignas(16) weight_t OutputWeights[HiddenWidth];
                weight_t OutputBias;
  } Content;

  void init() {
    memcpy(&Content, gEmbeddedLMRData, sizeof(Content));
  }

  inline int sseHaddEpi32(__m128i vec) {
    const __m128i high64 = _mm_unpackhi_epi64(vec, vec);
    const __m128i sum64 = _mm_add_epi32(vec, high64);

    const __m128i high32 = _mm_shuffle_epi32(sum64, _MM_SHUFFLE(2, 3, 0, 1));
    const __m128i sum32 = _mm_add_epi32(sum64, high32);

    return _mm_cvtsi128_si32(sum32);
  }



  Score evaluate(bool* inputs) {

    const __m128i zero = _mm_setzero_si128();

    __m128i acc;
    memcpy(&acc, Content.FeatureBiases, sizeof(acc));

    for (int i = 0; i < FeaturesWidth; i++) {
      if (inputs[i])
        acc = _mm_add_epi16(acc, *(__m128i*)Content.FeatureWeights[i]);
    }

    __m128i sumVec = _mm_setzero_si128();

    __m128i relu = _mm_max_epi16(acc, zero);
    sumVec = _mm_add_epi32(sumVec, _mm_madd_epi16(relu, *(__m128i*)Content.OutputWeights ));

    int sum = Content.OutputBias + sseHaddEpi32(sumVec);

    return sum / 16;
  }

}
