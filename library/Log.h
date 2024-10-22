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
/// @param sock_ Whether to get socket error message
char const *errorMessage (bool sock_ = false) noexcept;

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

#ifdef NDEBUG
#define debug(format_, ...)                                                                        \
	do                                                                                             \
	{                                                                                              \
	} while (0)
#else
/// @brief Log debug message
/// @param format_ Message format
ATTR_PRINTF (printf, 1, 2)
void debug (char const *format_, ...) noexcept;
#endif
}
