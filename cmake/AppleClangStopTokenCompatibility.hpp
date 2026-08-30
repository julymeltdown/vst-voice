#pragma once

// Xcode 16.4's libc++ ships the C++20 stop-token implementation but hides it
// behind an incomplete-feature macro. Enable only that API; the broader
// -fexperimental-library mode changes unrelated libc++ behavior.
#include <__config>

#if defined(__APPLE__) && defined(__clang__) && defined(_LIBCPP_VERSION)
#  undef _LIBCPP_HAS_NO_EXPERIMENTAL_STOP_TOKEN
#endif
