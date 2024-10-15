#pragma once

#ifdef _WIN32
#include <WS2tcpip.h>
#include <WinSock2.h>
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <cstdint>
#include <optional>

#ifdef _WIN32
using socklen_t = int;
using in_port_t = std::uint16_t;
using in_addr_t = std::uint32_t;
#endif

namespace rlbot::detail
{
/// @brief Socket address
class SockAddr
{
public:
	enum Family
	{
		/// @brief IPv4 family
		eIPv4 = AF_INET,
#ifndef NO_IPV6
		/// @brief IPv6 family
		eIPv6 = AF_INET6,
#endif
	};

	/// @brief Get address for 0.0.0.0
	/// @param port_ Port number
	static SockAddr IPv4Any (in_port_t port_ = 0) noexcept;

	/// @brief Get address for 127.0.0.1
	/// @param port_ Port number
	static SockAddr IPv4Loopback (in_port_t port_ = 0) noexcept;

#ifndef NO_IPV6
	/// @brief Get address for ::
	/// @param port_ Port number
	static SockAddr IPv6Any (in_port_t port_ = 0) noexcept;

	/// @brief Get address for ::1
	/// @param port_ Port number
	static SockAddr IPv6Loopback (in_port_t port_ = 0) noexcept;
#endif

	~SockAddr () noexcept;

	SockAddr () noexcept;

	/// @brief Copy constructor
	/// @param that_ Object to copy
	SockAddr (SockAddr const &that_) noexcept;

	/// @brief Move constructor
	/// @param that_ Object to move from
	SockAddr (SockAddr &&that_) noexcept;

	/// @brief Copy assignment
	/// @param that_ Object to copy
	SockAddr &operator= (SockAddr const &that_) noexcept;

	/// @brief Move assignment
	/// @param that_ Object to move from
	SockAddr &operator= (SockAddr &&that_) noexcept;

	/// @brief Parameterized constructor
	/// @param addr_ Address
	explicit SockAddr (sockaddr_in const &addr_) noexcept;

#ifndef NO_IPV6
	/// @brief Parameterized constructor
	/// @param addr_ Address
	explicit SockAddr (sockaddr_in6 const &addr_) noexcept;
#endif

	/// @brief Parameterized constructor
	/// @param addr_ Address
	explicit SockAddr (sockaddr_storage const &addr_) noexcept;

	/// @brief sockaddr_in cast operator
	explicit operator sockaddr_in const & () const noexcept;

#ifndef NO_IPV6
	/// @brief sockaddr_in6 cast operator
	explicit operator sockaddr_in6 const & () const noexcept;
#endif

	/// @brief sockaddr_storage cast operator
	explicit operator sockaddr_storage const & () const noexcept;

	/// @brief sockaddr* cast operator
	operator sockaddr * () noexcept;

	/// @brief sockaddr const* cast operator
	operator sockaddr const * () const noexcept;

	/// @brief Address family
	Family family () const noexcept;

	/// @brief Address size
	socklen_t size () const noexcept;

	/// @brief Address port
	std::uint16_t port () const noexcept;

	/// @brief Set address port
	/// @param port_ Port to set
	bool setPort (std::uint16_t port_) noexcept;

	/// @brief Address name
	/// @param buffer_ Buffer to hold name
	/// @param size_ Size of buffer_
	/// @retval buffer_ success
	/// @retval nullptr failure
	char const *name (char *buffer_, std::size_t size_) const noexcept;

	/// @brief Address name
	/// @retval nullptr failure
	/// @note This function is not reentrant
	char const *name () const noexcept;

	/// @brief Lookup address
	/// @param host_ Host name
	/// @param service_ Service name
	static std::optional<SockAddr> lookup (char const *host_,
	    char const *service_ = nullptr) noexcept;

private:
	/// @brief Address storage
	sockaddr_storage m_addr = {};
};
}
