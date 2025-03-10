/*
  Stockfish, a UCI chess playing engine derived from Glaurung 2.1
  Copyright (C) 2004-2023 The Stockfish developers (see AUTHORS file)

  Stockfish is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Stockfish is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

// Code for calculating NNUE evaluation function

#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string_view>

#include "../evaluate.h"
#include "../position.h"
#include "../uci.h"
#include "../types.h"

#include "evaluate_nnue.h"

namespace Stockfish::Eval::NNUE {

  // Input feature converter
  LargePagePtr<FeatureTransformer> featureTransformer;

  // Evaluation function
  AlignedPtr<Network> network[LayerStacks];

  // Evaluation function file name
  std::string fileName;
  std::string netDescription;

  namespace Detail {

  // Initialize the evaluation function parameters
  template <typename T>
  void initialize(AlignedPtr<T>& pointer) {

    pointer.reset(reinterpret_cast<T*>(std_aligned_alloc(alignof(T), sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
  }

  template <typename T>
  void initialize(LargePagePtr<T>& pointer) {

    static_assert(alignof(T) <= 4096, "aligned_large_pages_alloc() may fail for such a big alignment requirement of T");
    pointer.reset(reinterpret_cast<T*>(aligned_large_pages_alloc(sizeof(T))));
    std::memset(pointer.get(), 0, sizeof(T));
  }

  // Read evaluation function parameters
  template <typename T>
  bool read_parameters(std::istream& stream, T& reference) {

    std::uint32_t header;
    header = read_little_endian<std::uint32_t>(stream);
    if (!stream || header != T::get_hash_value()) return false;
    return reference.read_parameters(stream);
  }

  // Write evaluation function parameters
  template <typename T>
  bool write_parameters(std::ostream& stream, const T& reference) {

    write_little_endian<std::uint32_t>(stream, T::get_hash_value());
    return reference.write_parameters(stream);
  }

  }  // namespace Detail

  // Initialize the evaluation function parameters
  static void initialize() {

    Detail::initialize(featureTransformer);
    for (std::size_t i = 0; i < LayerStacks; ++i)
      Detail::initialize(network[i]);
  }

  // Read network header
  static bool read_header(std::istream& stream, std::uint32_t* hashValue, std::string* desc)
  {
    std::uint32_t version, size;

    version     = read_little_endian<std::uint32_t>(stream);
    *hashValue  = read_little_endian<std::uint32_t>(stream);
    size        = read_little_endian<std::uint32_t>(stream);
    if (!stream || version != Version) return false;
    desc->resize(size);
    stream.read(&(*desc)[0], size);
    return !stream.fail();
  }

  // Write network header
  static bool write_header(std::ostream& stream, std::uint32_t hashValue, const std::string& desc)
  {
    write_little_endian<std::uint32_t>(stream, Version);
    write_little_endian<std::uint32_t>(stream, hashValue);
    write_little_endian<std::uint32_t>(stream, (std::uint32_t)desc.size());
    stream.write(&desc[0], desc.size());
    return !stream.fail();
  }

  // Read network parameters
  static bool read_parameters(std::istream& stream) {

    std::uint32_t hashValue;
    if (!read_header(stream, &hashValue, &netDescription)) return false;
    if (hashValue != HashValue) return false;
    if (!Detail::read_parameters(stream, *featureTransformer)) return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
      if (!Detail::read_parameters(stream, *(network[i]))) return false;
    return stream && stream.peek() == std::ios::traits_type::eof();
  }

  // Write network parameters
  static bool write_parameters(std::ostream& stream) {

    if (!write_header(stream, HashValue, netDescription)) return false;
    if (!Detail::write_parameters(stream, *featureTransformer)) return false;
    for (std::size_t i = 0; i < LayerStacks; ++i)
      if (!Detail::write_parameters(stream, *(network[i]))) return false;
    return (bool)stream;
  }

  // Evaluation function. Perform differential calculation.
  Score evaluate(const Position& pos, bool adjusted, int* complexity) {

    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.

    constexpr uint64_t alignment = CacheLineSize;
    constexpr int delta = 24;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType transformedFeaturesUnaligned[
      FeatureTransformer::BufferSize + alignment / sizeof(TransformedFeatureType)];

    auto* transformedFeatures = align_ptr_up<alignment>(&transformedFeaturesUnaligned[0]);
#else
    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
#endif

    ASSERT_ALIGNED(transformedFeatures, alignment);

    const int bucket = (BitCount(pos.pieces()) - 1) / 4;
    const auto psqt = featureTransformer->transform(pos, transformedFeatures, bucket);
    const auto positional = network[bucket]->propagate(transformedFeatures);

    if (complexity)
        *complexity = abs(psqt - positional) / OutputScale;

    // Give more value to positional evaluation when adjusted flag is set
    if (adjusted)
        return static_cast<Score>(((1024 - delta) * psqt + (1024 + delta) * positional) / (1024 * OutputScale));
    else
        return static_cast<Score>((psqt + positional) / OutputScale);
  }

  struct NnueEvalTrace {
    static_assert(LayerStacks == PSQTBuckets);

    Score psqt[LayerStacks];
    Score positional[LayerStacks];
    std::size_t correctBucket;
  };

  static NnueEvalTrace trace_evaluate(const Position& pos) {

    // We manually align the arrays on the stack because with gcc < 9.3
    // overaligning stack variables with alignas() doesn't work correctly.

    constexpr uint64_t alignment = CacheLineSize;

#if defined(ALIGNAS_ON_STACK_VARIABLES_BROKEN)
    TransformedFeatureType transformedFeaturesUnaligned[
      FeatureTransformer::BufferSize + alignment / sizeof(TransformedFeatureType)];

    auto* transformedFeatures = align_ptr_up<alignment>(&transformedFeaturesUnaligned[0]);
#else
    alignas(alignment)
      TransformedFeatureType transformedFeatures[FeatureTransformer::BufferSize];
#endif

    ASSERT_ALIGNED(transformedFeatures, alignment);

    NnueEvalTrace t{};
    t.correctBucket = (BitCount(pos.pieces()) - 1) / 4;
    for (IndexType bucket = 0; bucket < LayerStacks; ++bucket) {
      const auto materialist = featureTransformer->transform(pos, transformedFeatures, bucket);
      const auto positional = network[bucket]->propagate(transformedFeatures);

      t.psqt[bucket] = static_cast<Score>( materialist / OutputScale );
      t.positional[bucket] = static_cast<Score>( positional / OutputScale );
    }

    return t;
  }

  constexpr std::string_view PieceToChar(" PNBRQK  pnbrqk");

  // Load eval, from a file stream or a memory stream
  bool load_eval(std::string name, std::istream& stream) {

    initialize();
    fileName = name;
    return read_parameters(stream);
  }

  // Save eval, to a file stream or a memory stream
  bool save_eval(std::ostream& stream) {

    if (fileName.empty())
      return false;

    return write_parameters(stream);
  }

  /// Save eval, to a file given by its name
  bool save_eval(const std::optional<std::string>& filename) {

    std::string actualFilename;
    std::string msg;

    if (filename.has_value())
        actualFilename = filename.value();
    else
    {
        if (currentEvalFileName != EvalFileDefaultName)
        {
             msg = "Failed to export a net. A non-embedded net can only be saved if the filename is specified";

             sync_cout << msg << sync_endl;
             return false;
        }
        actualFilename = EvalFileDefaultName;
    }

    std::ofstream stream(actualFilename, std::ios_base::binary);
    bool saved = save_eval(stream);

    msg = saved ? "Network saved successfully to " + actualFilename
                : "Failed to export a net";

    sync_cout << msg << sync_endl;
    return saved;
  }


} // namespace Stockfish::Eval::NNUE
