#pragma once

#include <numa.h>

inline int numaNodeCount() {
  return numa_max_node() + 1;
}