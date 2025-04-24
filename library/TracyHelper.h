#pragma once

#ifdef TRACY_ENABLE
#include <tracy/Tracy.hpp>
#else
#define ZoneScopedNS(...)
#define FrameMark
#define TracyNoop
#endif
