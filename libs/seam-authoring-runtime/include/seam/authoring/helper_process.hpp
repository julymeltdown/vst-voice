#pragma once
#include "seam/platform/helper_process.hpp"

// Source compatibility only. The platform library owns the implementation and
// child lifecycle; this header never introduces a second process supervisor.
namespace seam::authoring {
using platform::HelperProcessRequest;
using platform::HelperProcessOutput;
using platform::HelperExited;
using platform::HelperSignaled;
using platform::CompletedHelperProcess;
using platform::runBoundedHelperProcessObserved;
using platform::runBoundedHelperProcess;
}
