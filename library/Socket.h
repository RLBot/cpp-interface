#pragma once

#include "SockAddr.h"

#ifdef _WIN32
#include <WinSock2.h>
using IOVectorBase = WSABUF;
#else
#include <poll.h>
#include <sys/socket.h>
#include <sys/uio.h>
using SOCKET                  = int;
using IOVectorBase            = iovec;
constexpr auto INVALID_SOCKET = -1;
#endif

#include <chrono>
#include <cstddef>
#include <memory>

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

class Socket;
using UniqueSocket = std::unique_ptr<Socket>;
using SharedSocket = std::shared_ptr<Socket>;

/// \brief Socket object
class Socket
{
public:
	enum Type
	{
		eStream   = SOCK_STREAM, ///< Stream socket
		eDatagram = SOCK_DGRAM,  ///< Datagram socket
	};

	/// \brief Poll info
	struct PollInfo
	{
		/// \brief Socket to poll
		std::reference_wrapper<Socket> socket;

		/// \brief Input events
		int events;

		/// \brief Output events
		int revents;
	};

	~Socket ();

	/// \brief Get socket fd
	SOCKET fd () const noexcept;

	/// \brief Accept connection
	UniqueSocket accept ();

	/// \brief Whether socket is at out-of-band mark
	int atMark ();

	/// \brief Bind socket to address
	/// \param addr_ Address to bind
	bool bind (SockAddr const &addr_);

	/// \brief Connect to a peer
	/// \param addr_ Peer address
	bool connect (SockAddr const &addr_);

	/// \brief Listen for connections
	/// \param backlog_ Queue size for incoming connections
	bool listen (int backlog_);

	/// \brief Shutdown socket
	/// \param how_ Type of shutdown (\sa ::shutdown)
	bool shutdown (int how_);

	/// \brief Set linger option
	/// \param enable_ Whether to enable linger
	/// \param time_ Linger timeout
	bool setLinger (bool enable_, std::chrono::seconds time_);

	/// \brief Set non-blocking
	/// \param nonBlocking_ Whether to set non-blocking
	bool setNonBlocking (bool nonBlocking_ = true);

	/// \brief Set no delay
	/// \param noDelay_ Whether to set no delay
	bool setNoDelay (bool noDelay_ = true);

	/// \brief Set reuse address in subsequent bind
	/// \param reuse_ Whether to reuse address
	bool setReuseAddress (bool reuse_ = true);

	/// \brief Set reuse port in subsequent bind
	/// \param reuse_ Whether to reuse port
	bool setReusePort (bool reuse_ = true);

	/// \brief Set recv buffer size
	/// \param size_ Buffer size
	bool setRecvBufferSize (std::size_t size_);

	/// \brief Set send buffer size
	/// \param size_ Buffer size
	bool setSendBufferSize (std::size_t size_);

	/// \brief Join multicast group
	/// \param addr_ Multicast group address
	/// \param iface_ Interface address
	bool joinMulticastGroup (SockAddr const &addr_, SockAddr const &iface_);

	/// \brief Drop multicast group
	/// \param addr_ Multicast group address
	/// \param iface_ Interface address
	bool dropMulticastGroup (SockAddr const &addr_, SockAddr const &iface_);

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param size_ Size to read
	/// \param oob_ Whether to read from out-of-band
	std::make_signed_t<std::size_t> read (void *buffer_, std::size_t size_, bool oob_ = false);

	/// \brief Read data
	/// \param buffer_ Output buffer
	/// \param size_ Size to read
	/// \param[out] addr_ Source address
	std::make_signed_t<std::size_t> readFrom (void *buffer_, std::size_t size_, SockAddr &addr_);

	/// \brief Write data
	/// \param buffer_ Input buffer
	/// \param size_ Size to write
	std::make_signed_t<std::size_t> write (void const *buffer_, std::size_t size_);

	/// \brief Write data
	/// \param buffer_ Input buffer
	/// \param size_ Size to write
	/// \param[out] addr_ Destination address
	std::make_signed_t<std::size_t>
	    writeTo (void const *buffer_, std::size_t size_, SockAddr const &addr_);

	/// \brief Local name
	SockAddr const &sockName () const;
	/// \brief Peer name
	SockAddr const &peerName () const;

	/// \brief Create socket
	/// \param domain_ Socket domain
	/// \param type_ Socket type
	static UniqueSocket create (SockAddr::Domain domain_, Type type_);

	/// \brief Poll sockets
	/// \param info_ Poll info
	/// \param count_ Number of poll entries
	/// \param timeout_ Poll timeout
	static int poll (PollInfo *info_, std::size_t count_, std::chrono::milliseconds timeout_);

	/// \brief Get last error
	static int lastError () noexcept;

private:
	Socket () = delete;

	/// \brief Parameterized constructor
	/// \param fd_ Socket fd
	Socket (SOCKET fd_);

	/// \brief Parameterized constructor
	/// \param fd_ Socket fd
	/// \param sockName_ Local name
	/// \param peerName_ Peer name
	Socket (SOCKET fd_, SockAddr const &sockName_, SockAddr const &peerName_);

	Socket (Socket const &that_) = delete;

	Socket (Socket &&that_) = delete;

	Socket &operator= (Socket const &that_) = delete;

	Socket &operator= (Socket &&that_) = delete;

	/// \param Local name
	SockAddr m_sockName;
	/// \param Peer name
	SockAddr m_peerName;

	/// \param Socket fd
	SOCKET const m_fd = INVALID_SOCKET;

	/// \param Whether listening
	bool m_listening : 1;

	/// \param Whether connected
	bool m_connected : 1;
};
}
