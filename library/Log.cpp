#include "Log.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <Windows.h>
#endif

#include <array>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

namespace
{
enum class LogLevel
{
	None,
	Error,
	Warning,
	Info,
	Debug,
	Default = Warning,
};

auto const logLevel = [] {
	auto const level = std::getenv ("RLBOTCPP_LOG_LEVEL");
	if (!level || std::strlen (level) == 0)
		return LogLevel::Default;

	if (std::strcmp (level, "NONE") == 0)
		return LogLevel::None;

	if (std::strcmp (level, "ERROR") == 0)
		return LogLevel::Error;

	if (std::strcmp (level, "WARNING") == 0)
		return LogLevel::Warning;

	if (std::strcmp (level, "INFO") == 0)
		return LogLevel::Info;

	if (std::strcmp (level, "DEBUG") == 0)
		return LogLevel::Debug;

	return LogLevel::Default;
}();

std::mutex stdoutMutex;
std::mutex stderrMutex;
}

char const *rlbot::detail::errorMessage (bool const sock_) noexcept
{
#ifdef _WIN32
	thread_local std::array<char, 1024> message;

	auto const ec = sock_ ? WSAGetLastError () : GetLastError ();

	auto size = FormatMessageA (
	    FORMAT_MESSAGE_FROM_SYSTEM, nullptr, ec, 0, message.data (), message.size (), nullptr);
	if (size == 0)
		return "Unknown error";

	// remove trailing whitespace
	while (size > 0 && std::isspace (message[--size]))
		message[size] = '\0';

	return message.data ();
#else
	(void)sock_;
	return std::strerror (errno);
#endif
}

void rlbot::detail::error (char const *format_, ...) noexcept
{
	if (logLevel < LogLevel::Error)
		return;

	va_list ap;

	va_start (ap, format_);
	{
		auto const lock = std::scoped_lock (stderrMutex);
		std::fputs ("[Error  ] ", stderr);
		std::vfprintf (stderr, format_, ap);
		std::fflush (stderr);
	}
	va_end (ap);
}

void rlbot::detail::warning (char const *format_, ...) noexcept
{
	if (logLevel < LogLevel::Warning)
		return;

	va_list ap;

	va_start (ap, format_);
	{
		auto const lock = std::scoped_lock (stderrMutex);
		std::fputs ("[Warning] ", stderr);
		std::vfprintf (stderr, format_, ap);
		std::fflush (stderr);
	}
	va_end (ap);
}

void rlbot::detail::info (char const *format_, ...) noexcept
{
	if (logLevel < LogLevel::Info)
		return;

	va_list ap;

	va_start (ap, format_);
	{
		auto const lock = std::scoped_lock (stdoutMutex);
		std::fputs ("[Info   ] ", stdout);
		std::vfprintf (stdout, format_, ap);
		std::fflush (stdout);
	}
	va_end (ap);
}

#ifndef NDEBUG
void rlbot::detail::debug (char const *format_, ...) noexcept
{
	if (logLevel < LogLevel::Debug)
		return;

	va_list ap;

	va_start (ap, format_);
	{
		auto const lock = std::scoped_lock (stdoutMutex);
		std::fputs ("[Debug  ] ", stdout);
		std::vfprintf (stdout, format_, ap);
		std::fflush (stdout);
	}
	va_end (ap);
}
#endif
