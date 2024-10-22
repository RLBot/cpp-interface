#pragma once

#include <WinSock2.h>

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
	std::optional<WSADATA> m_wsaData;
};
}
