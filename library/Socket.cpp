#include "Socket.h"

#include "Log.h"

#include <tracy/Tracy.hpp>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <fcntl.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

using namespace rlbot::detail;

namespace
{
#ifdef _WIN32
int (&poll) (LPWSAPOLLFD, ULONG, INT) = ::WSAPoll;
#endif
}

///////////////////////////////////////////////////////////////////////////
int Socket::lastError () noexcept
{
#ifdef _WIN32
	auto const ec = WSAGetLastError ();
	switch (ec)
	{
	case WSAEINPROGRESS:
		// non-blocking connect is in progress
		return EINPROGRESS;

	case WSAEWOULDBLOCK:
		// socket operation would block
		return EWOULDBLOCK;
	}
	return ec;
#else
	return errno;
#endif
}

char const *Socket::errorMessage () noexcept
{
#ifdef _WIN32
	thread_local std::array<char, 1024> message;

	auto const ec = WSAGetLastError ();

	auto size = FormatMessageA (
	    FORMAT_MESSAGE_FROM_SYSTEM, nullptr, ec, 0, message.data (), message.size (), nullptr);
	if (size == 0) [[unlikely]]
		return "Unknown error";

	// remove trailing whitespace
	while (size > 0 && std::isspace (message[--size]))
		message[size] = '\0';

	return message.data ();
#else
	return std::strerror (errno);
#endif
}

Socket::~Socket () noexcept
{
	close ();
}

Socket::Socket () noexcept = default;

Socket::Socket (SOCKET const fd_, SockAddr::Family const family_) noexcept
    : m_fd (fd_), m_family (family_)
{
}

Socket::Socket (Socket &&that_) noexcept
{
	*this = std::move (that_);
}

Socket::Socket (SOCKET const fd_, SockAddr const &sockName_, SockAddr const &peerName_) noexcept
    : m_sockName (sockName_), m_peerName (peerName_), m_fd (fd_), m_family (sockName_.family ())
{
}

Socket &Socket::operator= (Socket &&that_) noexcept
{
	if (this != &that_) [[likely]]
	{
		close ();

		m_sockName = that_.m_sockName;
		m_peerName = that_.m_peerName;
		m_fd       = that_.m_fd;
		m_family   = that_.m_family;

		that_.m_sockName = {};
		that_.m_peerName = {};
		that_.m_fd       = INVALID_SOCKET;
	}

	return *this;
}

void Socket::close () noexcept
{
	if (m_fd == INVALID_SOCKET) [[unlikely]]
		return;

#ifdef _WIN32
	if (::closesocket (m_fd) != 0) [[unlikely]]
		error ("closesocket: %s\n", errorMessage ());
#else
	if (::close (m_fd) != 0) [[unlikely]]
		error ("close: %s\n", errorMessage ());
#endif

	m_fd = INVALID_SOCKET;
}

Socket::operator bool () const noexcept
{
	return m_fd != INVALID_SOCKET;
}

SOCKET Socket::nativeHandle () const noexcept
{
	return m_fd;
}

int Socket::atMark () noexcept
{
#ifdef _WIN32
	unsigned long atMark;

	auto const rc = ::ioctlsocket (m_fd, SIOCATMARK, &atMark);
	if (rc != 0) [[unlikely]]
	{
		error ("ioctlsocket(SIOCATMARK): %s\n", Socket::errorMessage ());
		return false;
	}
#else
	auto const rc = ::sockatmark (m_fd);
	if (rc < 0) [[unlikely]]
		error ("sockatmark: %s\n", errorMessage ());
#endif

	return rc;
}

bool Socket::bind (SockAddr const &addr_) noexcept
{
#ifndef NO_IPV6
	if (m_family == SockAddr::eIPv6)
	{
		// enable dual-stack
		int const ipv6Only = 0;
		if (::setsockopt (m_fd,
		        IPPROTO_IPV6,
		        IPV6_V6ONLY,
		        reinterpret_cast<char const *> (&ipv6Only),
		        sizeof (ipv6Only)) != 0) [[unlikely]]
		{
			error ("bind: %s\n", errorMessage ());
			return false;
		}
	}
#endif

	if (::bind (m_fd, addr_, addr_.size ()) != 0) [[unlikely]]
	{
		error ("bind: %s\n", errorMessage ());
		return false;
	}

	if (addr_.port () == 0)
	{
		// get socket name due to request for ephemeral port
		socklen_t addrLen = sizeof (struct sockaddr_storage);
		if (::getsockname (m_fd, m_sockName, &addrLen) != 0) [[unlikely]]
			error ("getsockname: %s\n", errorMessage ());
	}
	else
		m_sockName = addr_;

	return true;
}

bool Socket::shutdown (int const how_) noexcept
{
	if (::shutdown (m_fd, how_) != 0) [[unlikely]]
	{
		error ("shutdown: %s\n", errorMessage ());
		return false;
	}

	return true;
}

bool Socket::setLinger (bool const enable_, std::chrono::seconds const time_) noexcept
{
	if (time_.count () < 0 || time_.count () > std::numeric_limits<std::uint16_t>::max ())
	{
		error ("Linger time %lld is out of range\n", static_cast<long long> (time_.count ()));
		return false;
	}

	struct linger linger;
	linger.l_onoff  = enable_;
	linger.l_linger = static_cast<std::uint16_t> (time_.count ());

	auto const rc = ::setsockopt (
	    m_fd, SOL_SOCKET, SO_LINGER, reinterpret_cast<char const *> (&linger), sizeof (linger));
	if (rc != 0) [[unlikely]]
	{
		error ("setsockopt(SO_LINGER, %s, %lus): %s\n",
		    enable_ ? "on" : "off",
		    static_cast<unsigned long> (time_.count ()),
		    errorMessage ());
		return false;
	}

	return true;
}

bool Socket::setNonBlocking (bool const nonBlocking_) noexcept
{
#ifdef _WIN32
	unsigned long enable = nonBlocking_;

	auto const rc = ::ioctlsocket (m_fd, FIONBIO, &enable);
	if (rc != 0) [[unlikely]]
	{
		error ("ioctlsocket(FIONBIO, %d): %s\n", nonBlocking_, errorMessage ());
		return false;
	}
#else
	auto flags = ::fcntl (m_fd, F_GETFL, 0);
	if (flags == -1) [[unlikely]]
	{
		error ("fcntl(F_GETFL): %s\n", errorMessage ());
		return false;
	}

	if (nonBlocking_)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if (::fcntl (m_fd, F_SETFL, flags) != 0) [[unlikely]]
	{
		error ("fcntl(F_SETFL, %d): %s\n", flags, errorMessage ());
		return false;
	}
#endif

	return true;
}

bool Socket::setReuseAddress (bool const reuse_) noexcept
{
	int const reuse = reuse_;
	if (::setsockopt (m_fd,
	        SOL_SOCKET,
	        SO_REUSEADDR,
	        reinterpret_cast<char const *> (&reuse),
	        sizeof (reuse)) != 0) [[unlikely]]
	{
		error ("setsockopt(SO_REUSEADDR, %s): %s\n", reuse_ ? "yes" : "no", errorMessage ());
		return false;
	}

	return true;
}

bool Socket::setRecvBufferSize (std::size_t const size_) noexcept
{
	auto const size = static_cast<int> (size_);
	if (::setsockopt (
	        m_fd, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char const *> (&size), sizeof (size)) !=
	    0) [[unlikely]]
	{
		error ("setsockopt(SO_RCVBUF, %zu): %s\n", size_, errorMessage ());
		return false;
	}

	return true;
}

bool Socket::setSendBufferSize (std::size_t const size_) noexcept
{
	auto const size = static_cast<int> (size_);
	if (::setsockopt (
	        m_fd, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char const *> (&size), sizeof (size)) !=
	    0) [[unlikely]]
	{
		error ("setsockopt(SO_SNDBUF, %zu): %s\n", size_, errorMessage ());
		return false;
	}

	return true;
}

std::make_signed_t<std::size_t>
    Socket::read (void *const buffer_, std::size_t const size_, bool const oob_) noexcept
{
	ZoneScopedNS ("read", 16);
	assert (buffer_);
	assert (size_);

	auto const rc = ::recv (m_fd, static_cast<char *> (buffer_), size_, oob_ ? MSG_OOB : 0);
	if (rc < 0) [[unlikely]]
	{
		if (lastError () != EWOULDBLOCK)
			error ("recv: %s\n", errorMessage ());
	}

	return rc;
}

std::make_signed_t<std::size_t> Socket::readv (std::span<IOVector> const iov_,
    bool const oob_) noexcept
{
#ifdef _WIN32
	DWORD count;
	DWORD flags   = oob_ ? MSG_OOB : 0;
	auto const rc = WSARecv (m_fd, iov_.data (), iov_.size (), &count, &flags, nullptr, nullptr);
	if (rc != 0) [[unlikely]]
	{
		error ("WSARecv: %s\n", errorMessage ());
		return -1;
	}

	return count;
#else
	struct msghdr hdr = {};

	hdr.msg_iov    = iov_.data ();
	hdr.msg_iovlen = iov_.size ();
	hdr.msg_flags  = oob_ ? MSG_OOB : 0;

	auto const rc = ::recvmsg (m_fd, &hdr, oob_ ? MSG_OOB : 0);
	if (rc < 0) [[unlikely]]
	{
		error ("recvmsg: %s\n", errorMessage ());
		return -1;
	}

	return rc;
#endif
}

std::make_signed_t<std::size_t> Socket::write (void const *const buffer_,
    std::size_t const size_) noexcept
{
	ZoneScopedNS ("write", 16);
	assert (buffer_);
	assert (size_ > 0);

	auto const rc = ::send (m_fd, static_cast<char const *> (buffer_), size_, 0);
	if (rc < 0 && lastError () != EWOULDBLOCK) [[unlikely]]
		error ("send: %s\n", errorMessage ());

	return rc;
}

std::make_signed_t<std::size_t> Socket::writev (std::span<IOVector> iov_, bool oob_) noexcept
{
	ZoneScopedNS ("writev", 16);
#ifdef _WIN32
	DWORD count;
	auto const rc =
	    WSASend (m_fd, iov_.data (), iov_.size (), &count, oob_ ? MSG_OOB : 0, nullptr, nullptr);
	if (rc != 0) [[unlikely]]
	{
		error ("WSASend: %s\n", errorMessage ());
		return -1;
	}

	return count;
#else
	struct msghdr hdr = {};

	hdr.msg_iov    = iov_.data ();
	hdr.msg_iovlen = iov_.size ();
	hdr.msg_flags  = oob_ ? MSG_OOB : 0;

	auto const rc = ::sendmsg (m_fd, &hdr, oob_ ? MSG_OOB : 0);
	if (rc < 0) [[unlikely]]
	{
		error ("sendmsg: %s\n", errorMessage ());
		return -1;
	}

	return rc;
#endif
}

SockAddr const &Socket::sockName () const noexcept
{
	return m_sockName;
}

SockAddr const &Socket::peerName () const noexcept
{
	return m_peerName;
}

int Socket::poll (std::span<PollInfo> info_) noexcept
{
	if (info_.empty ()) [[unlikely]]
		return 0;

	ZoneScopedNS ("poll", 16);
	auto const rc = ::poll (info_.data (), info_.size (), -1);
	if (rc < 0) [[unlikely]]
	{
		error ("poll: %s\n", errorMessage ());
		return rc;
	}

	return rc;
}

int Socket::poll (std::span<PollInfo> info_, std::chrono::milliseconds const timeout_) noexcept
{
	if (timeout_.count () < 0 || timeout_.count () > std::numeric_limits<int>::max ())
	{
		error ("Poll timeout %lld is out of range\n", static_cast<long long> (timeout_.count ()));
		return -1;
	}

	if (info_.empty ()) [[unlikely]]
		return 0;

	auto const rc = ::poll (info_.data (), info_.size (), static_cast<int> (timeout_.count ()));
	if (rc < 0) [[unlikely]]
	{
		error ("poll: %s\n", errorMessage ());
		return rc;
	}

	return rc;
}

///////////////////////////////////////////////////////////////////////////
TCPSocket::~TCPSocket () noexcept
{
	if (m_fd == INVALID_SOCKET)
		return;

	if (m_listening)
		info ("Stop listening on [%s]:%u\n", m_sockName.name (), m_sockName.port ());

	if (m_connected)
		info ("Closing connection to [%s]:%u\n", m_peerName.name (), m_peerName.port ());
}

TCPSocket::TCPSocket () noexcept = default;

TCPSocket::TCPSocket (TCPSocket &&that_) noexcept
{
	*this = std::move (that_);
}

TCPSocket::TCPSocket (SOCKET const fd_, SockAddr::Family const family_) noexcept
    : Socket (fd_, family_), m_listening (false), m_connected (false)
{
}

TCPSocket::TCPSocket (SOCKET const fd_,
    SockAddr const &sockName_,
    SockAddr const &peerName_) noexcept
    : Socket (fd_, sockName_, peerName_), m_listening (false), m_connected (true)
{
}

TCPSocket &TCPSocket::operator= (TCPSocket &&that_) noexcept
{
	if (this != &that_) [[likely]]
	{
		m_listening = that_.m_listening;
		m_connected = that_.m_connected;
		Socket::operator= (std::move (that_));

		that_.m_listening = false;
		that_.m_connected = false;
	}

	return *this;
}

TCPSocket TCPSocket::accept () noexcept
{
	SockAddr addr;
	socklen_t addrLen = sizeof (struct sockaddr_storage);

	auto const fd = ::accept (m_fd, addr, &addrLen);
	if (fd == INVALID_SOCKET) [[unlikely]]
	{
		error ("accept: %s\n", errorMessage ());
		return {};
	}

	info ("Accepted connection from [%s]:%u\n", addr.name (), addr.port ());
	return {fd, m_sockName, addr};
}

bool TCPSocket::connect (SockAddr const &addr_) noexcept
{
	if (::connect (m_fd, addr_, addr_.size ()) != 0)
	{
		auto const ec = lastError ();
		if (ec != EINPROGRESS && ec != EWOULDBLOCK) [[unlikely]]
			error ("connect: %s\n", errorMessage ());
		else
		{
			m_peerName  = addr_;
			m_connected = true;
			info ("Connecting to [%s]:%u\n", addr_.name (), addr_.port ());
		}

		return false;
	}

	m_peerName  = addr_;
	m_connected = true;
	info ("Connected to [%s]:%u\n", addr_.name (), addr_.port ());
	return true;
}

bool TCPSocket::listen (int const backlog_) noexcept
{
	if (::listen (m_fd, backlog_) != 0) [[unlikely]]
	{
		error ("listen: %s\n", errorMessage ());
		return false;
	}

	m_listening = true;
	return true;
}

bool TCPSocket::setNoDelay (bool const noDelay_) noexcept
{
	int const noDelay = noDelay_;
	if (::setsockopt (m_fd,
	        IPPROTO_TCP,
	        TCP_NODELAY,
	        reinterpret_cast<char const *> (&noDelay),
	        sizeof (noDelay)) != 0) [[unlikely]]
	{
		error ("setsockopt(TCP_NODELAY, %s): %s\n", noDelay_ ? "yes" : "no", errorMessage ());
		return false;
	}

	return true;
}

TCPSocket TCPSocket::create (SockAddr::Family const family_) noexcept
{
	auto const fd = ::socket (family_, SOCK_STREAM, 0);
	if (fd == INVALID_SOCKET) [[unlikely]]
	{
		error ("socket: %s\n", errorMessage ());
		return {};
	}

	return {fd, family_};
}

///////////////////////////////////////////////////////////////////////////
UDPSocket::~UDPSocket () noexcept = default;

UDPSocket::UDPSocket () noexcept = default;

UDPSocket::UDPSocket (UDPSocket &&that_) noexcept
{
	*this = std::move (that_);
}

UDPSocket &UDPSocket::operator= (UDPSocket &&that_) noexcept
{
	if (this != &that_) [[likely]]
		Socket::operator= (std::move (that_));

	return *this;
}

UDPSocket UDPSocket::create (SockAddr::Family const family_) noexcept
{
	auto const fd = ::socket (family_, SOCK_DGRAM, 0);
	if (fd == INVALID_SOCKET) [[unlikely]]
	{
		error ("socket: %s\n", errorMessage ());
		return {};
	}

	return {fd, family_};
}

std::make_signed_t<std::size_t>
    UDPSocket::readFrom (void *const buffer_, std::size_t const size_, SockAddr &addr_) noexcept
{
	ZoneScopedNS ("read", 16);
	assert (buffer_);
	assert (size_);

	socklen_t addrLen = sizeof (struct sockaddr_storage);
	auto const rc     = ::recvfrom (m_fd, static_cast<char *> (buffer_), size_, 0, addr_, &addrLen);
	if (rc < 0 && lastError () != EWOULDBLOCK) [[unlikely]]
		error ("recvfrom: %s\n", errorMessage ());

	return rc;
}

std::make_signed_t<std::size_t> UDPSocket::writeTo (void const *const buffer_,
    std::size_t const size_,
    SockAddr const &addr_) noexcept
{
	ZoneScopedNS ("write", 16);
	assert (buffer_);
	assert (size_ > 0);

	auto const rc =
	    ::sendto (m_fd, static_cast<char const *> (buffer_), size_, 0, addr_, addr_.size ());
	if (rc < 0 && lastError () != EWOULDBLOCK) [[unlikely]]
		error ("sendto: %s\n", errorMessage ());

	return rc;
}

UDPSocket::UDPSocket (SOCKET const fd_, SockAddr::Family const family_) noexcept
    : Socket (fd_, family_)
{
}
