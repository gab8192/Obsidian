#pragma once

#if defined(__linux__)
#include <sys/mman.h>
#endif
#include <cstdlib>

namespace Util {

  inline void* allocAlign(size_t size) {
#if defined(__linux__)
    constexpr size_t align = 2 * 1024 * 1024;
#else
    constexpr size_t align = 4096;
#endif
    size = ((size + align - 1) / align) * align; // not actually required
    void* result = _mm_malloc(size, align);
#if defined(__linux__)
    madvise(result, size, MADV_HUGEPAGE);
#endif
    return result;
  }

  inline void freeAlign(void* ptr) {
    _mm_free(ptr);
  }
}