#pragma once

#ifdef _WIN32
#include <WinSock2.h>
#endif

#include <optional>

namespace rlbot::detail
{
/// @brief WSA data
class WsaData
{
public:
	~WsaData () noexcept;

	bool init () noexcept;

private:
#ifdef _WIN32
	std::optional<WSADATA> m_wsaData;
#endif
};
}
