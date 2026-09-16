#pragma once

#include <cstdio>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace seam::core {

// Windows CRT text mode rewrites newlines and treats some byte values as
// control input. Helper protocols are byte streams on every platform.
[[nodiscard]] inline bool useBinaryStandardStreams() noexcept {
#if defined(_WIN32)
  return ::_setmode(::_fileno(stdin), _O_BINARY) != -1 &&
         ::_setmode(::_fileno(stdout), _O_BINARY) != -1 &&
         ::_setmode(::_fileno(stderr), _O_BINARY) != -1;
#else
  return true;
#endif
}

}  // namespace seam::core
