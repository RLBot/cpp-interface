#include "SockAddr.h"

#include "Log.h"

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#endif

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace rlbot::detail;

namespace
{
/// @brief Make IPv4 address
/// @param addr_ Address (big-endian order)
in_addr makeAddr (in_addr_t const addr_) noexcept
{
	in_addr addr{};
	addr.s_addr = addr_;
	return addr;
}

/// @brief 0.0.0.0 address
auto const inaddr_any = makeAddr (htonl (INADDR_ANY));
/// @brief 127.0.0.1 address
auto const inaddr_loopback = makeAddr (htonl (INADDR_LOOPBACK));
}

///////////////////////////////////////////////////////////////////////////
SockAddr SockAddr::IPv4Any (in_port_t const port_) noexcept
{
	return SockAddr{sockaddr_in{
	    //
	    .sin_family = AF_INET,
	    .sin_port   = htons (port_),
	    .sin_addr   = inaddr_any,
	    //
	}};
}

SockAddr SockAddr::IPv4Loopback (in_port_t const port_) noexcept
{
	return SockAddr{sockaddr_in{
	    //
	    .sin_family = AF_INET,
	    .sin_port   = htons (port_),
	    .sin_addr   = inaddr_loopback,
	    //
	}};
}

#ifndef NO_IPV6
SockAddr SockAddr::IPv6Any (in_port_t const port_) noexcept
{
	return SockAddr{sockaddr_in6{
	    //
	    .sin6_family = AF_INET6,
	    .sin6_port   = htons (port_),
	    .sin6_addr   = in6addr_any
	    //
	}};
}

SockAddr SockAddr::IPv6Loopback (in_port_t const port_) noexcept
{
	return SockAddr{sockaddr_in6{
	    //
	    .sin6_family = AF_INET6,
	    .sin6_port   = htons (port_),
	    .sin6_addr   = in6addr_loopback
	    //
	}};
}
#endif

SockAddr::~SockAddr () noexcept = default;

SockAddr::SockAddr () noexcept = default;

SockAddr::SockAddr (SockAddr const &that_) noexcept = default;

SockAddr::SockAddr (SockAddr &&that_) noexcept = default;

SockAddr &SockAddr::operator= (SockAddr const &that_) noexcept = default;

SockAddr &SockAddr::operator= (SockAddr &&that_) noexcept = default;

SockAddr::SockAddr (sockaddr_in const &addr_) noexcept
{
	assert (addr_.sin_family == AF_INET);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
}

#ifndef NO_IPV6
SockAddr::SockAddr (sockaddr_in6 const &addr_) noexcept
{
	assert (addr_.sin6_family == AF_INET6);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in6));
}
#endif

SockAddr::SockAddr (sockaddr_storage const &addr_) noexcept
{
	switch (addr_.ss_family)
	{
	case AF_INET:
		std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
		break;

#ifndef NO_IPV6
	case AF_INET6:
		std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in6));
		break;
#endif

	default:
		std::abort ();
		break;
	}
}

SockAddr::operator sockaddr_in const & () const noexcept
{
	assert (m_addr.ss_family == AF_INET);
	return reinterpret_cast<sockaddr_in const &> (m_addr);
}

#ifndef NO_IPV6
SockAddr::operator sockaddr_in6 const & () const noexcept
{
	assert (m_addr.ss_family == AF_INET6);
	return reinterpret_cast<sockaddr_in6 const &> (m_addr);
}
#endif

SockAddr::operator sockaddr_storage const & () const noexcept
{
	return m_addr;
}

SockAddr::operator sockaddr * () noexcept
{
	return reinterpret_cast<sockaddr *> (&m_addr);
}

SockAddr::operator sockaddr const * () const noexcept
{
	return reinterpret_cast<sockaddr const *> (&m_addr);
}

SockAddr::Family SockAddr::family () const noexcept
{
	return static_cast<Family> (m_addr.ss_family);
}

bool SockAddr::setPort (std::uint16_t const port_) noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		reinterpret_cast<sockaddr_in *> (&m_addr)->sin_port = htons (port_);
		return true;

#ifndef NO_IPV6
	case AF_INET6:
		reinterpret_cast<sockaddr_in6 *> (&m_addr)->sin6_port = htons (port_);
		return true;
#endif

	default:
		std::abort ();
		break;
	}
}

socklen_t SockAddr::size () const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return sizeof (sockaddr_in);

#ifndef NO_IPV6
	case AF_INET6:
		return sizeof (sockaddr_in6);
#endif

	default:
		std::abort ();
	}
}

std::uint16_t SockAddr::port () const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return ntohs (reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_port);

#ifndef NO_IPV6
	case AF_INET6:
		return ntohs (reinterpret_cast<sockaddr_in6 const *> (&m_addr)->sin6_port);
#endif

	default:
		std::abort ();
		break;
	}
}

char const *SockAddr::name (char *buffer_, std::size_t size_) const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return inet_ntop (
		    AF_INET, &reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_addr, buffer_, size_);

#ifndef NO_IPV6
	case AF_INET6:
		return inet_ntop (
		    AF_INET6, &reinterpret_cast<sockaddr_in6 const *> (&m_addr)->sin6_addr, buffer_, size_);
#endif

	default:
		std::abort ();
		break;
	}
}

char const *SockAddr::name () const noexcept
{
#ifdef NO_IPV6
	thread_local static char buffer[INET_ADDRSTRLEN];
#else
	thread_local static char buffer[INET6_ADDRSTRLEN];
#endif

	return name (buffer, sizeof (buffer));
}

std::optional<SockAddr> SockAddr::lookup (char const *host_, char const *service_) noexcept
{
	addrinfo hints;
	std::memset (&hints, 0, sizeof (hints));

	hints.ai_family = AF_UNSPEC;
	hints.ai_flags  = AI_V4MAPPED | AI_ADDRCONFIG;

	addrinfo *result = nullptr;
	auto const rc    = ::getaddrinfo (host_, service_, &hints, &result);
	if (rc < 0)
	{
		error ("getaddrinfo: [%s]:%s %s\n",
		    host_ ? host_ : "",
		    service_ ? service_ : "",
		    ::gai_strerror (rc));
		return std::nullopt;
	}

	// free on all exit paths
	auto freeResult =
	    std::unique_ptr<addrinfo, decltype (&::freeaddrinfo)> (result, &::freeaddrinfo);

	for (auto p = result; p; p = p->ai_next)
	{
		switch (p->ai_family)
		{
		case AF_INET:
			return SockAddr (*reinterpret_cast<sockaddr_in const *> (p->ai_addr));

#ifndef NO_IPV6
		case AF_INET6:
			return SockAddr (*reinterpret_cast<sockaddr_in6 const *> (p->ai_addr));
#endif

		default:
			std::abort ();
			break;
		}
	}

	return std::nullopt;
}
