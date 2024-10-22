#include "ExampleBot.h"

#include <rlbot/BotManager.h>

#include <tracy/Tracy.hpp>

#include <cstdio>
#include <cstdlib>

namespace
{
#if _WIN32
void *alignedAlloc (std::size_t const alignment_, std::size_t const size_)
{
	return _aligned_malloc (size_, alignment_);
}
void (&alignedFree) (void *) = _aligned_free;
#else
void *(&alignedAlloc) (std::size_t, std::size_t) = aligned_alloc;
void (&alignedFree) (void *)                     = std::free;
#endif
}

int main (int argc_, char *argv_[])
{
	TracyNoop;

	if (argc_ <= 2)
	{
		std::fprintf (stderr, "Usage: %s <addr> <port>\n", argv_[0]);
		return EXIT_FAILURE;
	}

	auto const agentId = std::getenv ("RLBOT_AGENT_ID");
	if (!agentId || std::strlen (agentId) == 0)
	{
		std::fprintf (stderr, "Missing environment variable RLBOT_AGENT_ID\n");
		return EXIT_FAILURE;
	}

	auto const host = argv_[1];
	auto const port = argv_[2];

	rlbot::BotManager<ExampleBot> manager;
	if (!manager.run (host, port, agentId, true))
		return EXIT_FAILURE;
}

#ifdef TRACY_ENABLE
void *operator new (std ::size_t const size_)
{
	auto const ptr = std::malloc (size_);
	TracyAllocS (ptr, size_, 16);
	return ptr;
}

void *operator new[] (std::size_t const size_)
{
	auto const ptr = std::malloc (size_);
	TracyAllocS (ptr, size_, 16);
	return ptr;
}

void *operator new (std::size_t const size_, std::align_val_t const alignment_)
{
	auto const ptr = alignedAlloc (static_cast<std::size_t> (alignment_), size_);
	TracyAllocS (ptr, size_, 16);
	return ptr;
}

void *operator new[] (std::size_t const size_, std::align_val_t const alignment_)
{
	auto const ptr = alignedAlloc (static_cast<std::size_t> (alignment_), size_);
	TracyAllocS (ptr, size_, 16);
	return ptr;
}

void operator delete (void *const ptr_) noexcept
{
	TracyFreeS (ptr_, 16);
	std::free (ptr_);
}

void operator delete[] (void *const ptr_) noexcept
{
	TracyFreeS (ptr_, 16);
	std::free (ptr_);
}

void operator delete (void *const ptr_, std::align_val_t const alignment_) noexcept
{
	TracyFreeS (ptr_, 16);
	alignedFree (ptr_);
}

void operator delete[] (void *const ptr_, std::align_val_t const alignment_) noexcept
{
	TracyFreeS (ptr_, 16);
	alignedFree (ptr_);
}

#if !defined(__clang_major__) || __clang_major__ < 20
void operator delete (void *const ptr_, std::size_t const size_) noexcept
{
	TracyFreeS (ptr_, 16);
	std::free (ptr_);
}

void operator delete[] (void *const ptr_, std::size_t const size_) noexcept
{
	TracyFreeS (ptr_, 16);
	std::free (ptr_);
}

void operator delete (void *const ptr_,
    std::size_t const size_,
    std::align_val_t const alignment_) noexcept
{
	TracyFreeS (ptr_, 16);
	alignedFree (ptr_);
}

void operator delete[] (void *const ptr_,
    std::size_t const size_,
    std::align_val_t const alignment_) noexcept
{
	TracyFreeS (ptr_, 16);
	alignedFree (ptr_);
}
#endif
#endif
