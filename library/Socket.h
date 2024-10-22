#pragma once

#ifdef _WIN32
#include <WinSock2.h>
using IOVectorBase = WSABUF;
using socklen_t    = int;
#else
#include <sys/socket.h>
#include <sys/uio.h>
using SOCKET                  = int;
using IOVectorBase            = iovec;
constexpr auto INVALID_SOCKET = -1;
#endif

#include <cstddef>

namespace rlbot::detail
{
/// @brief I/O vector
struct IOVector : public IOVectorBase
{
	IOVector (void *const buffer_, std::size_t const size_) noexcept
	    : IOVectorBase{
#ifdef _WIN32
	          .len = static_cast<ULONG> (size_),
	          .buf = static_cast<CHAR *> (buffer_),
#else
	          .iov_base = buffer_,
	          .iov_len  = size_,
#endif
	      }
	{
	}

	IOVector (void const *const buffer_, std::size_t const size_) noexcept
	    : IOVector (const_cast<void *> (buffer_), size_)
	{
	}
};
static_assert (sizeof (IOVector) == sizeof (IOVectorBase));
static_assert (alignof (IOVector) == alignof (IOVectorBase));

/// @brief Resolve address
/// @param host_ Host name
/// @param service_ Service name
/// @param[out] addr_ Address result
/// @param[inout] len_ Address length
bool resolve (char const *host_, char const *service_, sockaddr &addr_, socklen_t &len_) noexcept;
}
