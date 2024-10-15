#pragma once

#ifndef _MSC_VER
#define ATTR_PRINTF(archetype_, index_, firstToCheck_)                                             \
	__attribute__ ((format (archetype_, index_, firstToCheck_)))
#else
#define ATTR_PRINTF(archetype_, index_, firstToCheck_)
#endif

namespace rlbot::detail
{
/// @brief Get last error message, e.g. FormatMessage(GetLastError()) or std::strerror(errno)
char const *errorMessage () noexcept;

/// @brief Log error message
/// @param format_ Message format
ATTR_PRINTF (printf, 1, 2)
void error (char const *format_, ...) noexcept;

/// @brief Log warning message
/// @param format_ Message format
ATTR_PRINTF (printf, 1, 2)
void warning (char const *format_, ...) noexcept;

/// @brief Log info message
/// @param format_ Message format
ATTR_PRINTF (printf, 1, 2)
void info (char const *format_, ...) noexcept;

/// @brief Log debug message
/// @param format_ Message format
ATTR_PRINTF (printf, 1, 2)
void debug (char const *format_, ...) noexcept;
}
