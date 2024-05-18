#pragma once

#include "position.h"

namespace Stockfish {

namespace Eval {

  extern bool useNNUE;
  extern std::string currentEvalFileName;

  // The default net name MUST follow the format nn-[SHA256 first 12 digits].nnue
  // for the build process (profile-build and fishtest) to work. Do not change the
  // name of the macro, as it is used in the Makefile.
  #define EvalFileDefaultName   "nn-a3d1bfca1672.nnue"

  namespace NNUE {

    void init();
    void verify();

  } // namespace NNUE

  /// <returns> A value relative to the side to move </returns>
  Score evaluate(Position& pos);
}

}