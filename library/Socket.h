#pragma once

#include "SockAddr.h"

#ifdef _WIN32
#include <WinSock2.h>
using IOVectorBase = WSABUF;
using PollFd       = WSAPOLLFD;
#else
#include <poll.h>
#if __has_include(<sys/uio.h>)
#include <sys/uio.h>
#endif
using IOVectorBase = iovec;
using PollFd       = pollfd;
#endif

#include <atomic>
#include <chrono>
#include <span>

#ifndef _WIN32
using SOCKET                  = int;
constexpr auto INVALID_SOCKET = -1;
#endif

namespace rlbot::detail
{
/// @brief Socket object
class Socket
{
public:
	/// @brief Poll info
	struct PollInfo : public PollFd
	{
		PollInfo () noexcept = default;

		PollInfo (SOCKET const sock_, int const events_, int const revents_ = 0) noexcept
		    : pollfd{.fd   = sock_,
		          .events  = static_cast<decltype (pollfd::events)> (events_),
		          .revents = static_cast<decltype (pollfd::revents)> (revents_)}
		{
		}

		PollInfo (Socket const &sock_, int const events_, int const revents_ = 0) noexcept
		    : PollInfo (sock_.m_fd, events_, revents_)
		{
		}
	};
	static_assert (sizeof (PollInfo) == sizeof (PollFd));
	static_assert (alignof (PollInfo) == alignof (PollFd));

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

	static int lastError () noexcept;

	/// @brief Get socket error message
	static char const *errorMessage () noexcept;

	virtual ~Socket () noexcept;

	Socket () noexcept;

	Socket (Socket const &that_) noexcept = delete;

	Socket &operator= (Socket const &that_) noexcept = delete;

	/// @brief bool cast operator
	/// Determines if socket is valid
	explicit operator bool () const noexcept;

	/// @brief Get native socket handle
	SOCKET nativeHandle () const noexcept;

	/// @brief Whether socket is at out-of-band mark
	int atMark () noexcept;

	/// @brief Bind socket to address
	/// @param addr_ Address to bind
	bool bind (SockAddr const &addr_) noexcept;

	/// @brief Shutdown socket
	/// @param how_ Type of shutdown (@sa ::shutdown)
	bool shutdown (int how_) noexcept;

	/// @brief Set linger option
	/// @param enable_ Whether to enable linger
	/// @param time_ Linger timeout
	bool setLinger (bool enable_, std::chrono::seconds time_) noexcept;

	/// @brief Set non-blocking
	/// @param nonBlocking_ Whether to set non-blocking
	bool setNonBlocking (bool nonBlocking_ = true) noexcept;

	/// @brief Set reuse address in subsequent bind
	/// @param reuse_ Whether to reuse address
	bool setReuseAddress (bool reuse_ = true) noexcept;

	/// @brief Set recv buffer size
	/// @param size_ Buffer size
	bool setRecvBufferSize (std::size_t size_) noexcept;

	/// @brief Set send buffer size
	/// @param size_ Buffer size
	bool setSendBufferSize (std::size_t size_) noexcept;

	/// @brief Read data
	/// @param buffer_ Output buffer
	/// @param size_ Size to read
	/// @param oob_ Whether to read from out-of-band
	std::make_signed_t<std::size_t>
	    read (void *buffer_, std::size_t size_, bool oob_ = false) noexcept;

	/// @brief Read data
	/// @param iov_ Output vector
	/// @param oob_ Whether to read from out-of-band
	std::make_signed_t<std::size_t> readv (std::span<IOVector> iov_, bool oob_ = false) noexcept;

	/// @brief Write data
	/// @param buffer_ Input buffer
	/// @param size_ Size to write
	std::make_signed_t<std::size_t> write (void const *buffer_, std::size_t size_) noexcept;

	/// @brief Write data
	/// @param iov_ Input vector
	/// @param size_ Size to write
	std::make_signed_t<std::size_t> writev (std::span<IOVector> iov_, bool oob_ = false) noexcept;

	/// @brief Local name
	SockAddr const &sockName () const noexcept;

	/// @brief Peer name
	SockAddr const &peerName () const noexcept;

	/// @brief Poll sockets
	/// @param info_ Poll info
	static int poll (std::span<PollInfo> info_) noexcept;

	/// @brief Poll sockets
	/// @param info_ Poll info
	/// @param count_ Number of poll entries
	/// @param timeout_ Poll timeout
	static int poll (std::span<PollInfo> info_, std::chrono::milliseconds timeout_) noexcept;

protected:
	/// @brief Parameterized constructor
	/// @param fd_ Socket fd
	/// @param family_ Socket family
	Socket (SOCKET fd_, SockAddr::Family family_) noexcept;

	Socket (Socket &&that_) noexcept;

	/// @brief Parameterized constructor
	/// @param fd_ Socket fd
	/// @param sockName_ Local name
	/// @param peerName_ Peer name
	Socket (SOCKET fd_, SockAddr const &sockName_, SockAddr const &peerName_) noexcept;

	Socket &operator= (Socket &&that_) noexcept;

	/// @param Local name
	SockAddr m_sockName;
	/// @param Peer name
	SockAddr m_peerName;

	/// @param Socket fd
	SOCKET m_fd = INVALID_SOCKET;
	SockAddr::Family m_family{};

private:
	/// @brief Close socket
	void close () noexcept;
};

/// @brief TCP socket object
class TCPSocket final : public Socket
{
public:
	~TCPSocket () noexcept override;

	TCPSocket () noexcept;

	TCPSocket (TCPSocket const &that_) noexcept = delete;

	TCPSocket (TCPSocket &&that_) noexcept;

	TCPSocket &operator= (TCPSocket const &that_) noexcept = delete;

	TCPSocket &operator= (TCPSocket &&that_) noexcept;

	/// @brief Accept connection
	TCPSocket accept () noexcept;

	/// @brief Connect to a peer
	/// @param addr_ Peer address
	bool connect (SockAddr const &addr_) noexcept;

	/// @brief Listen for connections
	/// @param backlog_ Queue size for incoming connections
	bool listen (int backlog_) noexcept;

	/// @brief Set no-delay
	bool setNoDelay (bool noDelay_ = true) noexcept;

	/// @brief Create TCP socket
	static TCPSocket create (SockAddr::Family family_ = SockAddr::eIPv4) noexcept;

private:
	/// @brief Parameterized constructor
	/// @param fd_ Socket fd
	/// @param family_ Socket family
	TCPSocket (SOCKET fd_, SockAddr::Family family_) noexcept;

	/// @brief Parameterized constructor
	/// @param fd_ Socket fd
	/// @param sockName_ Local name
	/// @param peerName_ Peer name
	TCPSocket (SOCKET fd_, SockAddr const &sockName_, SockAddr const &peerName_) noexcept;

	/// @param Whether listening
	bool m_listening : 1 = false;

	/// @param Whether connected
	bool m_connected : 1 = false;
};

/// @brief UDP socket object
class UDPSocket final : public Socket
{
public:
	~UDPSocket () noexcept override;

	UDPSocket () noexcept;

	UDPSocket (UDPSocket const &that_) noexcept = delete;

	UDPSocket (UDPSocket &&that_) noexcept;

	UDPSocket &operator= (UDPSocket const &that_) noexcept = delete;

	UDPSocket &operator= (UDPSocket &&that_) noexcept;

	/// @brief Create UDP socket
	static UDPSocket create (SockAddr::Family family_ = SockAddr::eIPv4) noexcept;

	/// @brief Read data
	/// @param buffer_ Output buffer
	/// @param size_ Size to read
	/// @param addr_[out] Sender
	std::make_signed_t<std::size_t>
	    readFrom (void *buffer_, std::size_t size_, SockAddr &addr_) noexcept;

	/// @brief Write data
	/// @param buffer_ Input buffer
	/// @param size_ Size to write
	/// @param addr_ Recipient
	std::make_signed_t<std::size_t>
	    writeTo (void const *buffer_, std::size_t size_, SockAddr const &addr_) noexcept;

private:
	/// @brief Parameterized constructor
	/// @param fd_ Socket fd
	/// @param family_ Socket family
	UDPSocket (SOCKET fd_, SockAddr::Family family_) noexcept;
};
}
