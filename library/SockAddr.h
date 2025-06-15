#pragma once

#ifdef _WIN32
#include <WinSock2.h>
using socklen_t = int;
#else
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <compare>
#include <cstdint>

namespace rlbot::detail
{
/// \brief Socket address
class SockAddr
{
public:
	enum class Domain
	{
		IPv4 = AF_INET,
		IPv6 = AF_INET6,
	};

	/// \brief 0.0.0.0
	static SockAddr const AnyIPv4;

	/// \brief ::
	static SockAddr const AnyIPv6;

	~SockAddr () noexcept;

	SockAddr () noexcept;

	/// \brief Parameterized constructor
	/// \param domain_ Socket domain
	/// \note Initial address is INADDR_ANY/in6addr_any
	SockAddr (Domain domain_) noexcept;

	/// \brief Parameterized constructor
	/// \param addr_ Socket address (network byte order)
	/// \param port_ Socket port (host byte order)
	SockAddr (in_addr_t addr_, std::uint16_t port_ = 0) noexcept;

	/// \brief Parameterized constructor
	/// \param addr_ Socket address (network byte order)
	/// \param port_ Socket port (host byte order)
	SockAddr (in_addr const &addr_, std::uint16_t port_ = 0) noexcept;

	/// \brief Parameterized constructor
	/// \param addr_ Socket address
	/// \param port_ Socket port (host byte order)
	SockAddr (in6_addr const &addr_, std::uint16_t port_ = 0) noexcept;

	/// \brief Copy constructor
	/// \param that_ Object to copy
	SockAddr (SockAddr const &that_) noexcept;

	/// \brief Move constructor
	/// \param that_ Object to move from
	SockAddr (SockAddr &&that_) noexcept;

	/// \brief Copy assignment
	/// \param that_ Object to copy
	SockAddr &operator= (SockAddr const &that_) noexcept;

	/// \brief Move assignment
	/// \param that_ Object to move from
	SockAddr &operator= (SockAddr &&that_) noexcept;

	/// \brief Parameterized constructor
	/// \param addr_ Address (network byte order)
	SockAddr (sockaddr_in const &addr_) noexcept;

	/// \brief Parameterized constructor
	/// \param addr_ Address (network byte order)
	SockAddr (sockaddr_in6 const &addr_) noexcept;

	/// \brief Parameterized constructor
	/// \param addr_ Address (network byte order)
	SockAddr (sockaddr_storage const &addr_) noexcept;

	/// \brief sockaddr_in cast operator (network byte order)
	operator sockaddr_in const & () const noexcept;

	/// \brief sockaddr_in6 cast operator (network byte order)
	operator sockaddr_in6 const & () const noexcept;

	/// \brief sockaddr_storage cast operator (network byte order)
	operator sockaddr_storage const & () const noexcept;

	/// \brief sockaddr* cast operator (network byte order)
	operator sockaddr * () noexcept;

	/// \brief sockaddr const* cast operator (network byte order)
	operator sockaddr const * () const noexcept;

	/// \brief Equality operator
	bool operator== (SockAddr const &that_) const noexcept;

	/// \brief Comparison operator
	std::strong_ordering operator<=> (SockAddr const &that_) const noexcept;

	/// \brief sockaddr domain
	Domain domain () const noexcept;

	/// \brief sockaddr size
	socklen_t size () const noexcept;

	/// \brief Set address
	/// \param addr_ Address to set (network byte order)
	void setAddr (in_addr_t addr_) noexcept;

	/// \brief Set address
	/// \param addr_ Address to set (network byte order)
	void setAddr (in_addr const &addr_) noexcept;

	/// \brief Set address
	/// \param addr_ Address to set (network byte order)
	void setAddr (in6_addr const &addr_) noexcept;

	/// \brief Address port (host byte order)
	std::uint16_t port () const noexcept;

	/// \brief Set address port
	/// \param port_ Port to set (host byte order)
	void setPort (std::uint16_t port_) noexcept;

	/// \brief Address name
	/// \param buffer_ Buffer to hold name
	/// \param size_ Size of buffer_
	/// \retval buffer_ success
	/// \retval nullptr failure
	char const *name (char *buffer_, std::size_t size_) const noexcept;

	/// \brief Address name
	/// \retval nullptr failure
	/// \note This function is not reentrant
	char const *name () const noexcept;

	/// @brief Resolve address
	/// @param host_ Host name
	/// @param service_ Service name
	/// @param[out] addr_ Address result
	static bool resolve (char const *host_, char const *service_, SockAddr &addr_) noexcept;

private:
	/// \brief Address storage (network byte order)
	sockaddr_storage m_addr = {};
};
}
