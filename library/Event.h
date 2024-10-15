#pragma once

#include "Socket.h"

#ifdef _WIN32
#define EVENT_USE_HANDLES
#elif __has_include(<sys/eventfd.h>)
#define EVENT_USE_EVENTFD
#endif

#if __has_include(<sys/epoll.h>)
#define EVENT_USE_EPOLL
#endif

#ifdef EVENT_USE_HANDLES
#include <WinSock2.h>
#endif

#include <span>
#include <utility>
#include <vector>

namespace rlbot::detail
{
class EventWaiter;

#ifdef EVENT_USE_HANDLES
using EventHandle = HANDLE;
#else
using EventHandle = SOCKET;
#endif

/// @brief Event
class Event
{
public:
	~Event () noexcept;

	/// @brief Default constructor
	/// Creates an invalid event object; use Event::create() for a valid event object
	Event () noexcept;

	Event (Event const &that_) noexcept = delete;

	Event (Event &&that_) noexcept;

	Event &operator= (Event const &that_) noexcept = delete;

	Event &operator= (Event &&that_) noexcept;

	/// @brief bool cast operator
	/// Determines whether this contains an event handle
	explicit operator bool () const noexcept;

	/// @brief Check for validity
	/// Determines whether this contains an event handle
	bool valid () const noexcept;

	/// @brief Swap with another event
	/// @param that_ Event to swap
	void swap (Event &that_) noexcept;

	/// @brief Get event handle
	EventHandle handle () const noexcept;

	/// @brief Signal event
	void signal () noexcept;

	/// @brief Clear event
	bool clear () noexcept;

	/// @brief Create event
	static Event create () noexcept;

	/// @brief Create event from socket
	/// @param socket_ Socket to monitor
	static Event create (Socket const &socket_) noexcept;

private:
#ifdef EVENT_USE_HANDLES
	/// @brief Socket to monitor
	SOCKET m_socket = INVALID_SOCKET;
	/// @brief WSA event handle for socket
	WSAEVENT m_wsaHandle = nullptr;
	/// @brief Event handle for non-socket
	HANDLE m_handle = nullptr;
#else
	/// @brief Socket to monitor
	SOCKET m_externalSocket = INVALID_SOCKET;

#ifdef EVENT_USE_EVENTFD
	/// @brief Event handle for non-socket
	int m_eventFd = INVALID_SOCKET;
#else
	/// @brief Event handle for non-socket
	UDPSocket m_internalSocket;
#endif
#endif
};

/// @brief Event waiter
/// Can monitor multiple events simultaneously
class EventWaiter
{
public:
	~EventWaiter () noexcept;

	EventWaiter () noexcept;

	/// @brief Parameterized constructor
	/// @param reservations_ Initial capacity
	explicit EventWaiter (unsigned reservations_) noexcept;

	EventWaiter (EventWaiter const &that_) noexcept = delete;

	EventWaiter (EventWaiter &&that_) noexcept;

	EventWaiter &operator= (EventWaiter const &that_) noexcept = delete;

	EventWaiter &operator= (EventWaiter &&that_) noexcept;

	/// @brief bool cast operator
	/// Determines whether this contains waitable events
	explicit operator bool () const noexcept;

	/// @brief Check for validity
	/// Determines whether this contains waitable events
	bool valid () const noexcept;

	/// @brief Add event to monitor
	/// Determines whether this contains waitable events
	/// @note This keeps a shallow reference
	bool add (Event &event_) noexcept;

	/// @brief Wait for event
	/// @return Event which was triggered
	Event *wait () noexcept;

private:
	/// @brief Events to monitor
	std::vector<Event *> m_events;

#ifdef EVENT_USE_HANDLES
	/// @brief Handles to monitor
	std::vector<HANDLE> m_handles;
#elif defined(EVENT_USE_EPOLL)
	/// @brief epoll descriptor
	int m_epoll = -1;
#else
	/// @brief Handles to monitor
	std::vector<Socket::PollInfo> m_pollInfo;
#endif
};
}
