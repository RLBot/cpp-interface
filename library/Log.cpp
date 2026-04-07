#include "Log.h"

#ifdef _WIN32
#include <WinSock2.h>
#include <Windows.h>
#endif

#include <array>
#include <cctype>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
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

std::string currentDateTime () noexcept
{
	auto const now  = std::chrono::system_clock::now ();
	auto const time = std::chrono::system_clock::to_time_t (now);
	auto const us =
	    std::chrono::duration_cast<std::chrono::microseconds> (now.time_since_epoch ()).count () %
	    1000000;

	struct tm tm;
#ifdef _WIN32
	::gmtime_s (&tm, &time);
#else
	::gmtime_r (&time, &tm);
#endif

	std::array<char, 32> buffer;
	auto const rc =
	    std::strftime (buffer.data (), buffer.size (), "%Y-%m-%d %H:%M:%S", std::gmtime (&time));

	std::snprintf (buffer.data () + rc, buffer.size () - rc, ".%06ld", static_cast<long> (us));

	return buffer.data ();
}

auto const logLevel = [] {
	auto const level = std::getenv ("RLBOT_LOG_LEVEL");
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
		std::fprintf (stderr, "%s [Error  ] ", currentDateTime ().c_str ());
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
		std::fprintf (stderr, "%s [Warning] ", currentDateTime ().c_str ());
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
		std::fprintf (stdout, "%s [Info   ] ", currentDateTime ().c_str ());
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
		std::fprintf (stdout, "%s [Debug  ] ", currentDateTime ().c_str ());
		std::vfprintf (stdout, format_, ap);
		std::fflush (stdout);
	}
	va_end (ap);
}
#endif
