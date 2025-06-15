#include "SockAddr.h"

#include "Log.h"

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <memory>

using namespace rlbot::detail;

namespace
{
in_addr inaddr_any = {.s_addr = htonl (INADDR_ANY)};

std::strong_ordering
    strongMemCompare (void const *const a_, void const *const b_, std::size_t const size_)
{
	auto const cmp = std::memcmp (a_, b_, size_);
	if (cmp < 0)
		return std::strong_ordering::less;
	if (cmp > 0)
		return std::strong_ordering::greater;
	return std::strong_ordering::equal;
}
}

///////////////////////////////////////////////////////////////////////////
SockAddr const SockAddr::AnyIPv4{inaddr_any};

SockAddr const SockAddr::AnyIPv6{in6addr_any};

SockAddr::~SockAddr () noexcept = default;

SockAddr::SockAddr () noexcept = default;

SockAddr::SockAddr (Domain const domain_) noexcept
{
	switch (domain_)
	{
	case Domain::IPv4:
		*this = AnyIPv4;
		break;

	case Domain::IPv6:
		*this = AnyIPv6;
		break;

	default:
		std::abort ();
	}
}

SockAddr::SockAddr (in_addr_t const addr_, std::uint16_t const port_) noexcept
    : SockAddr (in_addr{.s_addr = addr_}, port_)
{
}

SockAddr::SockAddr (in_addr const &addr_, std::uint16_t const port_) noexcept
{
	std::memset (&m_addr, 0, sizeof (m_addr));
	m_addr.ss_family = AF_INET;
	setAddr (addr_);
	setPort (port_);
}

SockAddr::SockAddr (in6_addr const &addr_, std::uint16_t const port_) noexcept
{
	std::memset (&m_addr, 0, sizeof (m_addr));
	m_addr.ss_family = AF_INET6;
	setAddr (addr_);
	setPort (port_);
}

SockAddr::SockAddr (SockAddr const &that_) noexcept = default;

SockAddr::SockAddr (SockAddr &&that_) noexcept = default;

SockAddr &SockAddr::operator= (SockAddr const &that_) noexcept = default;

SockAddr &SockAddr::operator= (SockAddr &&that_) noexcept = default;

SockAddr::SockAddr (sockaddr_in const &addr_) noexcept
{
	assert (addr_.sin_family == AF_INET);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
}

SockAddr::SockAddr (sockaddr_in6 const &addr_) noexcept
{
	assert (addr_.sin6_family == AF_INET6);
	std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in6));
}

SockAddr::SockAddr (sockaddr_storage const &addr_) noexcept
{
	switch (addr_.ss_family)
	{
	case AF_INET:
		std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in));
		break;

	case AF_INET6:
		std::memcpy (&m_addr, &addr_, sizeof (sockaddr_in6));
		break;

	default:
		std::abort ();
	}
}

SockAddr::operator sockaddr_in const & () const noexcept
{
	assert (m_addr.ss_family == AF_INET);
	return reinterpret_cast<sockaddr_in const &> (m_addr);
}

SockAddr::operator sockaddr_in6 const & () const noexcept
{
	assert (m_addr.ss_family == AF_INET6);
	return reinterpret_cast<sockaddr_in6 const &> (m_addr);
}

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

bool SockAddr::operator== (SockAddr const &that_) const noexcept
{
	if (m_addr.ss_family != that_.m_addr.ss_family)
		return false;

	switch (m_addr.ss_family)
	{
	case AF_INET:
		if (port () != that_.port ())
			return false;

		// ignore sin_zero
		return static_cast<sockaddr_in const &> (*this).sin_addr.s_addr ==
		       static_cast<sockaddr_in const &> (that_).sin_addr.s_addr;

	case AF_INET6:
		return std::memcmp (&m_addr, &that_.m_addr, sizeof (sockaddr_in6)) == 0;

	default:
		std::abort ();
	}
}

std::strong_ordering SockAddr::operator<=> (SockAddr const &that_) const noexcept
{
	if (m_addr.ss_family != that_.m_addr.ss_family)
		return m_addr.ss_family <=> that_.m_addr.ss_family;

	switch (m_addr.ss_family)
	{
	case AF_INET:
	{
		auto const cmp =
		    strongMemCompare (&static_cast<sockaddr_in const &> (*this).sin_addr.s_addr,
		        &static_cast<sockaddr_in const &> (that_).sin_addr.s_addr,
		        sizeof (in_addr_t));

		if (cmp != std::strong_ordering::equal)
			return cmp;

		return port () <=> that_.port ();
	}

	case AF_INET6:
	{
		auto const &addr1 = static_cast<sockaddr_in6 const &> (*this);
		auto const &addr2 = static_cast<sockaddr_in6 const &> (that_);

		if (auto const cmp =
		        strongMemCompare (&addr1.sin6_addr, &addr2.sin6_addr, sizeof (in6_addr));
		    cmp != std::strong_ordering::equal)
			return cmp;

		auto const p1 = port ();
		auto const p2 = that_.port ();

		if (p1 < p2)
			return std::strong_ordering::less;
		else if (p1 > p2)
			return std::strong_ordering::greater;

		if (auto const cmp = strongMemCompare (
		        &addr1.sin6_flowinfo, &addr2.sin6_flowinfo, sizeof (std::uint32_t));
		    cmp != std::strong_ordering::equal)
			return cmp;

		return strongMemCompare (
		    &addr1.sin6_flowinfo, &addr2.sin6_flowinfo, sizeof (std::uint32_t));
	}

	default:
		std::abort ();
	}
}

void SockAddr::setAddr (in_addr_t const addr_) noexcept
{
	setAddr (in_addr{.s_addr = addr_});
}

void SockAddr::setAddr (in_addr const &addr_) noexcept
{
	if (m_addr.ss_family != AF_INET)
		std::abort ();

	std::memcpy (&reinterpret_cast<sockaddr_in &> (m_addr).sin_addr, &addr_, sizeof (addr_));
	;
}

void SockAddr::setAddr (in6_addr const &addr_) noexcept
{
	if (m_addr.ss_family != AF_INET6)
		std::abort ();

	std::memcpy (&reinterpret_cast<sockaddr_in6 &> (m_addr).sin6_addr, &addr_, sizeof (addr_));
	;
}

std::uint16_t SockAddr::port () const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return ntohs (reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_port);

	case AF_INET6:
		return ntohs (reinterpret_cast<sockaddr_in6 const *> (&m_addr)->sin6_port);

	default:
		std::abort ();
	}
}

void SockAddr::setPort (std::uint16_t const port_) noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		reinterpret_cast<sockaddr_in *> (&m_addr)->sin_port = htons (port_);
		break;

	case AF_INET6:
		reinterpret_cast<sockaddr_in6 *> (&m_addr)->sin6_port = htons (port_);
		break;

	default:
		std::abort ();
	}
}

SockAddr::Domain SockAddr::domain () const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
	case AF_INET6:
		return static_cast<Domain> (m_addr.ss_family);

	default:
		std::abort ();
	}
}

socklen_t SockAddr::size () const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return sizeof (sockaddr_in);

	case AF_INET6:
		return sizeof (sockaddr_in6);

	default:
		std::abort ();
	}
}

char const *SockAddr::name (char *buffer_, std::size_t size_) const noexcept
{
	switch (m_addr.ss_family)
	{
	case AF_INET:
		return inet_ntop (
		    AF_INET, &reinterpret_cast<sockaddr_in const *> (&m_addr)->sin_addr, buffer_, size_);

	case AF_INET6:
		return inet_ntop (
		    AF_INET6, &reinterpret_cast<sockaddr_in6 const *> (&m_addr)->sin6_addr, buffer_, size_);

	default:
		std::abort ();
	}
}

char const *SockAddr::name () const noexcept
{
	thread_local static char buffer[INET6_ADDRSTRLEN];

	return name (buffer, sizeof (buffer));
}

bool SockAddr::resolve (char const *const host_,
    char const *const service_,
    SockAddr &addr_) noexcept
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
		return false;
	}

	// free on all exit paths
	auto freeResult =
	    std::unique_ptr<addrinfo, decltype (&::freeaddrinfo)> (result, &::freeaddrinfo);

	for (auto p = result; p; p = p->ai_next)
	{
		switch (p->ai_family)
		{
		case AF_INET:
			addr_ = SockAddr (*reinterpret_cast<sockaddr_in const *> (p->ai_addr));
			return true;

		case AF_INET6:
			addr_ = SockAddr (*reinterpret_cast<sockaddr_in6 const *> (p->ai_addr));
			return true;

		default:
			continue;
		}
	}

	return false;
}
