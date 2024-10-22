#include "WsaData.h"

#include "Log.h"
#include "Socket.h"

using namespace rlbot::detail;

///////////////////////////////////////////////////////////////////////////
WsaData::~WsaData () noexcept
{
	if (m_wsaData.has_value ())
	{
		if (WSACleanup () != 0)
			error ("WSACleanup: %s\n", errorMessage (true));
	}
}

bool WsaData::init () noexcept
{
	if (m_wsaData.has_value ())
		return true;

	auto const wsaResult = WSAStartup (MAKEWORD (2, 2), &m_wsaData.emplace ());
	if (wsaResult != 0)
	{
		error ("Failed to start winsocks: 0x%x\n", wsaResult);
		m_wsaData.reset ();
		return false;
	}

	return true;
}
