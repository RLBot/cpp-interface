#include "Event.h"

#include "Log.h"

#include <tracy/Tracy.hpp>

#ifdef EVENT_USE_EVENTFD
#include <sys/eventfd.h>
#endif

#ifdef EVENT_USE_EPOLL
#include <sys/epoll.h>
#endif

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstring>

using namespace rlbot::detail;

namespace
{
#ifdef _WIN32
int (&poll) (LPWSAPOLLFD, ULONG, INT) = ::WSAPoll;
#endif
}

///////////////////////////////////////////////////////////////////////////
Event::~Event () noexcept
{
#ifdef EVENT_USE_HANDLES
	if (m_wsaHandle && !WSACloseEvent (m_wsaHandle)) [[unlikely]]
		error ("WSACloseEvent: %s\n", Socket::errorMessage ());

	if (m_handle && !CloseHandle (m_handle)) [[unlikely]]
		error ("CloseHandle: %s\n", errorMessage ());

#elif defined(EVENT_USE_EVENTFD)
	if (m_eventFd != INVALID_SOCKET) [[unlikely]]
	{
		if (::close (m_eventFd) != 0)
			error ("close: %s\n", errorMessage ());
	}
#endif
}

Event::Event () noexcept = default;

Event::Event (Event &&that_) noexcept
{
	swap (that_);
}

Event &Event::operator= (Event &&that_) noexcept
{
	if (this != &that_) [[likely]]
		swap (that_);

	return *this;
}

Event::operator bool () const noexcept
{
	return valid ();
}

bool Event::valid () const noexcept
{
#ifdef EVENT_USE_HANDLES
	assert (!m_wsaHandle || !m_handle);
	if (m_wsaHandle || m_handle)
		return true;
#else
	if (m_externalSocket != INVALID_SOCKET)
		return true;

#ifdef EVENT_USE_EVENTFD
	assert (m_externalSocket == INVALID_SOCKET || m_eventFd == INVALID_SOCKET);
	return m_eventFd != INVALID_SOCKET;
#else
	assert (m_externalSocket == INVALID_SOCKET || m_internalSocket);
	if (m_internalSocket)
		return true;
#endif
#endif

	return false;
}

void Event::swap (Event &that_) noexcept
{
#ifdef EVENT_USE_HANDLES
	std::swap (m_socket, that_.m_socket);
	std::swap (m_wsaHandle, that_.m_wsaHandle);
	std::swap (m_handle, that_.m_handle);
#else
	std::swap (m_externalSocket, that_.m_externalSocket);

#ifdef EVENT_USE_EVENTFD
	std::swap (m_eventFd, that_.m_eventFd);
#else
	std::swap (m_internalSocket, that_.m_internalSocket);
#endif
#endif
}

EventHandle Event::handle () const noexcept
{
#ifdef EVENT_USE_HANDLES
	if (m_wsaHandle)
		return m_wsaHandle;

	if (m_handle)
		return m_handle;

	return nullptr;
#else
	if (m_externalSocket != INVALID_SOCKET)
		return m_externalSocket;

#ifdef EVENT_USE_EVENTFD
	if (m_eventFd != INVALID_SOCKET)
		return m_eventFd;
#else
	if (m_internalSocket)
		return m_internalSocket.nativeHandle ();
#endif

	return INVALID_SOCKET;
#endif
}

void Event::signal () noexcept
{
	assert (valid ());
	if (!valid ()) [[unlikely]]
		return;

	ZoneScopedNS ("event signal", 16);

#ifdef EVENT_USE_HANDLES
	assert (!m_wsaHandle);
	if (!SetEvent (m_handle)) [[unlikely]]
		error ("SetEvent: %s\n", errorMessage ());
#else
	assert (m_externalSocket == INVALID_SOCKET);

#ifdef EVENT_USE_EVENTFD
	if (::eventfd_write (m_eventFd, 1) != 0) [[unlikely]]
		error ("eventfd_write: %s\n", errorMessage ());
#else
	// write to self
	char dummy = 0;
	m_internalSocket.writeTo (&dummy, sizeof (dummy), m_internalSocket.sockName ());
#endif
#endif
}

bool Event::clear () noexcept
{
	ZoneScopedNS ("event clear", 16);
	assert (valid ());
	if (!valid ()) [[unlikely]]
		return false;

#ifdef EVENT_USE_HANDLES
	if (m_wsaHandle)
	{
		WSANETWORKEVENTS netEvents;
		if (WSAEnumNetworkEvents (m_socket, m_wsaHandle, &netEvents) != 0) [[unlikely]]
		{
			error ("WSAEnumNetworkEvents: %s\n", Socket::errorMessage ());
			return false;
		}

		if (netEvents.lNetworkEvents & FD_READ)
		{
			if (netEvents.iErrorCode[FD_READ_BIT]) [[unlikely]]
			{
				error ("WSAEnumNetworkEvents: read error %#x\n", netEvents.iErrorCode[FD_READ_BIT]);
				return false;
			}

			return true;
		}

		return true; // spurious read event?
	}
	else
		return true; // m_handle is auto-reset
#else
#ifdef EVENT_USE_EVENTFD
	if (m_eventFd != INVALID_SOCKET)
	{
		eventfd_t count;
		if (::eventfd_read (m_eventFd, &count) != 0) [[unlikely]]
		{
			error ("eventfd_read: %s\n", errorMessage ());
			return false;
		}
		return true;
	}
	else
		return true; // nothing to clear on external socket
#else
	if (m_internalSocket)
	{
		std::array<std::uint8_t, 32> dummy;
		while (true)
		{
			// consume all data in the read buffer
			auto const rc = m_internalSocket.read (dummy.data (), dummy.size ());
			if (rc == dummy.size ()) [[unlikely]]
				continue; // full read so there may be more; try again
			else if (rc > 0) [[likely]]
				return true; // partial read so buffer is consumed

			if (rc < 0 && Socket::lastError () == EWOULDBLOCK) [[unlikely]]
				return true;

			return false;
		}
	}
	else
		return true; // nothing to clear on external socket
#endif
#endif

	warning ("Failed to clear event\n");
	return false;
}

Event Event::create () noexcept
{
	Event event;

#ifdef EVENT_USE_HANDLES
	event.m_handle = CreateEvent (nullptr, false, false, nullptr);
	if (!event.m_handle) [[unlikely]]
	{
		error ("CreateEvent: %s\n", errorMessage ());
		return {};
	}
#else
#ifdef EVENT_USE_EVENTFD
	event.m_eventFd = ::eventfd (0, EFD_CLOEXEC | EFD_NONBLOCK);
	if (event.m_eventFd < 0) [[unlikely]]
	{
		event.m_eventFd = INVALID_SOCKET;
		error ("eventfd: %s\n", errorMessage ());
		return {};
	}
#else
	auto sock = UDPSocket::create ();
	if (!sock || !sock.bind (SockAddr::IPv4Loopback ()) || !sock.setNonBlocking ()) [[unlikely]]
		return {};

	event.m_internalSocket = std::move (sock);
#endif
#endif

	return event;
}

Event Event::create (Socket const &socket_) noexcept
{
	assert (socket_);
	Event event;

#ifdef EVENT_USE_HANDLES
	event.m_socket = socket_.nativeHandle ();
	assert (event.m_socket != INVALID_SOCKET);
	if (event.m_socket == INVALID_SOCKET) [[unlikely]]
	{
		warning ("Invalid socket for event\n");
		return {};
	}

	event.m_wsaHandle = WSACreateEvent ();
	if (!event.m_wsaHandle) [[unlikely]]
	{
		error ("WSACreateEvent: %s\n", Socket::errorMessage ());
		return {};
	}

	if (WSAEventSelect (socket_.nativeHandle (), event.m_wsaHandle, FD_READ | FD_CLOSE) != 0)
	    [[unlikely]]
	{
		error ("WSAEventSelect: %s\n", Socket::errorMessage ());
		return {};
	}
#else
	event.m_externalSocket = socket_.nativeHandle ();
	assert (event.m_externalSocket != INVALID_SOCKET);
	if (event.m_externalSocket == INVALID_SOCKET) [[unlikely]]
	{
		warning ("Invalid socket for event\n");
		return {};
	}
#endif

	return event;
}

///////////////////////////////////////////////////////////////////////////
EventWaiter::~EventWaiter () noexcept
{
#ifdef EVENT_USE_EPOLL
	if (m_epoll >= 0)
	{
		if (::close (m_epoll) < 0) [[unlikely]]
			error ("close: %s\n", errorMessage ());
		m_epoll = -1;
	}
#endif
}

EventWaiter::EventWaiter () noexcept : EventWaiter (2)
{
}

EventWaiter::EventWaiter (unsigned const count_) noexcept
#ifdef EVENT_USE_EPOLL
    : m_epoll (epoll_create (count_))
#endif
{
#ifdef EVENT_USE_EPOLL
	if (m_epoll < 0) [[unlikely]]
		error ("epoll_create: %s\n", errorMessage ());
#else
	(void)count_;
#endif
}

EventWaiter::EventWaiter (EventWaiter &&that_) noexcept
{
	*this = std::move (that_);
}

EventWaiter &EventWaiter::operator= (EventWaiter &&that_) noexcept
{
	if (this != &that_) [[likely]]
	{
		std::swap (m_events, that_.m_events);
#ifdef EVENT_USE_EPOLL
		std::swap (m_epoll, that_.m_epoll);
#endif
	}

	return *this;
}

EventWaiter::operator bool () const noexcept
{
	return valid ();
}

bool EventWaiter::valid () const noexcept
{
#ifdef EVENT_USE_EPOLL
	return m_epoll != INVALID_SOCKET;
#else
	return true;
#endif
}

bool EventWaiter::add (Event &event_) noexcept
{
	assert (valid ());
	if (!valid ()) [[unlikely]]
	{
		warning ("Tried to add event to invalid waiter\n");
		return false;
	}

	assert (event_.valid ());
	if (!event_.valid ()) [[unlikely]]
	{
		warning ("Tried to add invalid event to waiter\n");
		return false;
	}

	// make sure we aren't already monitoring this event
	auto const it = std::ranges::find (m_events, &event_);
	assert (it == std::end (m_events));
	if (it != std::end (m_events)) [[unlikely]]
	{
		warning ("Tried to add duplicate event to waiter\n");
		return false;
	}

	auto const handle = event_.handle ();
#ifdef EVENT_USE_HANDLES
	assert (handle);
	if (!handle) [[unlikely]]
	{
		warning ("Event has no handle\n");
		return false;
	}

	m_handles.emplace_back (handle);
#elif defined(EVENT_USE_EPOLL)
	assert (handle != INVALID_SOCKET);
	if (handle == INVALID_SOCKET) [[unlikely]]
	{
		warning ("Event has no handle\n");
		return false;
	}

	struct epoll_event e = {.events = EPOLLIN, .data = {.fd = handle}};
	if (::epoll_ctl (m_epoll, EPOLL_CTL_ADD, handle, &e) != 0) [[unlikely]]
	{
		error ("epoll_ctl: %s\n", errorMessage ());
		return false;
	}
#else
	m_pollInfo.emplace_back (handle, POLLIN);
#endif

	m_events.emplace_back (&event_);

	return true;
}

Event *EventWaiter::wait () noexcept
{
	ZoneScopedNS ("event wait", 16);

#ifdef EVENT_USE_HANDLES
	assert (m_handles.size () == m_events.size ());
	auto const rc =
	    WSAWaitForMultipleEvents (m_handles.size (), m_handles.data (), false, WSA_INFINITE, false);
	if (rc == WSA_WAIT_FAILED) [[unlikely]]
	{
		error ("WSAWaitForMultipleEvents: %s\n", Socket::errorMessage ());
		return nullptr;
	}

	if (rc == WSA_WAIT_TIMEOUT) [[unlikely]]
	{
		error ("WSAWaitForMultipleEvents: timed out\n");
		return nullptr;
	}

	if (rc >= m_events.size ()) [[unlikely]]
	{
		warning ("WSAWaitForMultipleEvents returned out-of-range index\n");
		return nullptr;
	}

	return m_events[rc];
#else
#ifdef EVENT_USE_EPOLL
	assert (m_epoll != INVALID_SOCKET);

	struct epoll_event events;
	while (true)
	{
		auto const rc = ::epoll_wait (m_epoll, &events, 1, -1);
		if (rc <= 0) [[unlikely]]
		{
			if (rc < 0 && errno == EINTR) [[likely]]
				continue; // interrupted; try again

			error ("epoll_wait: %s\n", errorMessage ());
			return nullptr;
		}

		break;
	}

	if (events.events & EPOLLIN) [[likely]]
	{
		for (auto const &event : m_events)
		{
			if (event->handle () == events.data.fd)
				return event;
		}
	}
#else
	assert (m_events.size () == m_pollInfo.size ());

	for (auto &info : m_pollInfo)
		info.revents = 0;

	int rc = 0;
	while (rc == 0)
	{
		rc = ::poll (m_pollInfo.data (), m_pollInfo.size (), -1);
		if (rc < 0) [[unlikely]]
		{
			error ("poll: %s\n", Socket::errorMessage ());
			return nullptr;
		}
	}

	for (unsigned i = 0; i < m_pollInfo.size (); ++i)
	{
		if (!(m_pollInfo[i].revents & POLLIN))
			continue;

		return m_events[i];
	}
#endif
#endif

	error ("Failed to wait\n");
	return nullptr;
}
