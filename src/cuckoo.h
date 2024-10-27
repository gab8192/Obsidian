#pragma once

#include "types.h"

namespace Cuckoo {

  inline int h1(Key key) {
    return key & 0x1fff;
  }

  inline int h2(Key key) {
    return (key >> 16) & 0x1fff;
  }

  extern Key keys[8192];
  extern Move moves[8192];

  void init();
}
