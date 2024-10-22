#include "Socket.h"

#include "Log.h"

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#endif

#include <cassert>
#include <cstring>
#include <memory>

bool rlbot::detail::resolve (char const *const host_,
    char const *const service_,
    sockaddr &addr_,
    socklen_t &len_) noexcept
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
		if (len_ >= p->ai_addrlen)
		{
			std::memcpy (&addr_, p->ai_addr, p->ai_addrlen);
			len_ = p->ai_addrlen;
			return true;
		}
	}

	return false;
}
