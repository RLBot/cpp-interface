#include <rlbot/Connection.h>

#include "Log.h"
#include "Message.h"
#include "Pool.h"
#include "Socket.h"
#include "TracyHelper.h"

#ifdef _WIN32
#include "WsaData.h"

#include <WS2tcpip.h>
#else
#include <liburing.h>

#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
#endif

#include <atomic>
#include <condition_variable>
#include <thread>

using namespace rlbot;
using namespace rlbot::detail;

namespace
{
/// @brief Socket buffer large enough to hold at least 4 messages
constexpr auto SOCKET_BUFFER_SIZE = 4 * (std::numeric_limits<std::uint16_t>::max () + 1u);

constexpr auto PREALLOCATED_BUFFERS = 32;

constexpr auto COMPLETION_KEY_SOCKET      = 0;
constexpr auto COMPLETION_KEY_WRITE_QUEUE = 1;
constexpr auto COMPLETION_KEY_QUIT        = 2;

#ifndef _WIN32
int (&closesocket) (int) = ::close;
#endif

template <typename T>
rlbot::flat::InterfacePacketT buildInterfacePacket (T &&packet_) noexcept
{
	rlbot::flat::InterfacePacketT interfacePacket;
	interfacePacket.message.Set (std::move (packet_));
	return interfacePacket;
}
}

///////////////////////////////////////////////////////////////////////////
class rlbot::detail::ConnectionImpl
{
public:
	~ConnectionImpl () noexcept;

	/// @brief Request service thread to terminate
	void terminate () noexcept;

	/// @brief Join service thread
	void join () noexcept;

	/// @brief Request read
	void requestRead () noexcept;

	/// @brief Request write
	void requestWrite () noexcept;

	/// @brief Request write with lock held
	/// @param lock_ Held lock
	void requestWriteLocked (std::unique_lock<std::mutex> &lock_) noexcept;
	/// @brief Get buffer from pool
	Pool<Buffer>::Ref getBuffer () noexcept;

	/// @brief Push event
	/// @param event_ Event to push
	void pushEvent (int event_) noexcept;

#ifdef _WIN32
	/// @brief WSA data
	WsaData wsaData;

	/// @brief IOCP handle
	HANDLE iocpHandle = nullptr;
	/// @brief WSARecv overlapped
	WSAOVERLAPPED inOverlapped;
	/// @brief WSASend overlapped
	WSAOVERLAPPED outOverlapped;
#else
	/// @brief Read iov
	iovec readIov;
	/// @brief io uring
	io_uring ring;
	/// @brief io uring destructor
	std::unique_ptr<io_uring, void (*) (io_uring *)> ringDestructor = {nullptr, nullptr};
	/// @brief io uring submission queue mutex
	std::mutex ringSQMutex;
	/// @brief Whether io uring supports read
	bool ringRead = false;
	/// @brief Whether io uring supports write
	bool ringWrite = false;
	/// @brief Registered socket index
	SOCKET ringSocketFd;
	/// @brief Whether socket was registered
	int ringSocketFlag;
	/// @brief Discriminator for read event
	int inOverlapped;
	/// @brief Discriminator for write event
	int outOverlapped;
	/// @brief Discriminator for write queue event
	int writeQueueOverlapped;
	/// @brief Discriminator for bot wakeup event
	int botWakeupOverlapped;
	/// @brief Discriminator for quit event
	int quitOverlapped;
#endif

	/// @brief Service thread
	std::thread serviceThread;
	/// @brief Signal to quit
	std::atomic_bool quit = false;
	/// @brief Whether manager is running
	std::atomic_bool running = false;

	std::condition_variable writerIdleCv;
	bool writerIdle = false;

	/// @brief Socket connected to RLBotServer
	SOCKET sock;

	/// @brief Output queue mutex
	std::mutex writerMutex;

	/// @brief Buffer pool
	std::array<std::shared_ptr<Pool<Buffer>>, 4> bufferPools;
	/// @brief Buffer pool index for round-robining
	std::atomic_uint bufferPoolIndex = 0;

	/// @brief Flatbuffer builder pool
	std::shared_ptr<Pool<flatbuffers::FlatBufferBuilder>> fbbPool =
	    Pool<flatbuffers::FlatBufferBuilder>::create ("FBB");

	/// @brief Current read buffer
	Pool<Buffer>::Ref inBuffer;
	/// @brief Input begin pointer
	std::size_t inStartOffset = 0;
	/// @brief Input end pointer
	std::size_t inEndOffset = 0;

	/// @brief Current write buffer
	std::vector<IOVector> iov;
	/// @brief Output begin pointer
	std::size_t outStartOffset = 0;

	/// @brief Output queue
	std::vector<Message> outputQueue;
};

ConnectionImpl::~ConnectionImpl () noexcept
{
	join ();
}

void ConnectionImpl::terminate () noexcept
{
	{
		auto const lock = std::scoped_lock (writerMutex);
		writerIdle      = true;
	}

	writerIdleCv.notify_all ();

	quit.store (true, std::memory_order_relaxed);

	pushEvent (COMPLETION_KEY_QUIT);
}

void ConnectionImpl::join () noexcept
{
	if (running.load (std::memory_order_relaxed))
		serviceThread.join ();

	if (sock != INVALID_SOCKET)
	{
		if (::closesocket (sock) != 0)
			error ("closesocket: %s\n", errorMessage (true));
		sock = INVALID_SOCKET;
	}

#ifdef _WIN32
	if (iocpHandle)
	{
		if (!CloseHandle (iocpHandle))
			error ("CloseHandle: %s\n", errorMessage ());
		iocpHandle = nullptr;
	}
#else
	ringDestructor.reset ();
#endif

	outputQueue.clear ();

	quit.store (false, std::memory_order_relaxed);

	running.store (false, std::memory_order_relaxed);
}

void ConnectionImpl::requestRead () noexcept
{
#ifdef _WIN32
	ZoneScopedNS ("requestRead", 16);

	WSABUF buffer;
	buffer.buf = reinterpret_cast<CHAR *> (inBuffer->data () + inEndOffset);
	buffer.len = inBuffer->size () - inEndOffset;

	DWORD flags = MSG_PUSH_IMMEDIATE;

	{
		ZoneScopedNS ("WSARecv", 16);
		auto const rc = WSARecv (sock, &buffer, 1, nullptr, &flags, &inOverlapped, nullptr);
		if (rc != 0 && WSAGetLastError () != WSA_IO_PENDING)
		{
			error ("WSARecv: %s\n", errorMessage (true));
			terminate ();
		}
	}
#else
	ZoneScopedNS ("io_uring_prep_readv", 16);

	auto lock = std::unique_lock (ringSQMutex);

	auto const sqe = io_uring_get_sqe (&ring);
	assert (sqe);
	if (!sqe)
	{
		lock.unlock ();
		error ("io_uring_get_sqe: Queue is full\n");
		terminate ();
		return;
	}

	auto const buffer = inBuffer->data () + inEndOffset;
	auto const size   = inBuffer->size () - inEndOffset;

	if (inBuffer.preferred ()) [[likely]]
	{
		// use registered buffer
		io_uring_prep_read_fixed (sqe, ringSocketFd, buffer, size, 0, inBuffer.tag ());
	}
	else
	{
		// fallback to unregistered buffer
		if (ringRead) [[likely]]
			io_uring_prep_read (sqe, ringSocketFd, buffer, size, 0);
		else
		{
			readIov.iov_base = buffer;
			readIov.iov_len  = size;
			io_uring_prep_readv (sqe, ringSocketFd, &readIov, 1, 0);
		}
	}

	sqe->flags |= ringSocketFlag;
	io_uring_sqe_set_data (sqe, &inOverlapped);

	auto const rc = io_uring_submit (&ring);
	lock.unlock ();
	if (rc <= 0)
	{
		if (rc < 0)
			error ("io_uring_submit: %s\n", std::strerror (-rc));
		else
			error ("io_uring_submit: not submitted\n");
		terminate ();
	}
#endif
}

void ConnectionImpl::requestWrite () noexcept
{
	ZoneScopedNS ("requestWrite", 16);

	auto lock = std::unique_lock (writerMutex);
	if (outputQueue.empty ())
		return;

	requestWriteLocked (lock);
}

void ConnectionImpl::requestWriteLocked (std::unique_lock<std::mutex> &lock_) noexcept
{
	ZoneScopedNS ("requestWriteLocked", 16);

	if (!iov.empty ())
	{
		lock_.unlock ();
		return;
	}

	assert (!outputQueue.empty ());

	std::array<Pool<Buffer>::Ref, PREALLOCATED_BUFFERS> buffers;

	if (iov.capacity () < outputQueue.size ()) [[unlikely]]
		iov.reserve (outputQueue.size ());

	unsigned startOffset = outStartOffset;
	for (unsigned i = 0; auto const &message : outputQueue)
	{
		auto const span = message.span ();
		assert (span.size () > startOffset);

		iov.emplace_back (&span[startOffset], span.size () - startOffset);
		startOffset = 0;

		buffers[i++] = message.buffer ();

		if (i >= buffers.size ())
			break;
	}

	lock_.unlock ();

	if (!iov.empty ()) [[likely]]
	{
#ifdef _WIN32
		ZoneScopedNS ("WSASend", 16);
		auto const rc =
		    WSASend (sock, iov.data (), iov.size (), nullptr, 0, &outOverlapped, nullptr);
		if (rc != 0 && WSAGetLastError () != WSA_IO_PENDING)
		{
			error ("WSASend: %s\n", errorMessage (true));
			terminate ();
			return;
		}
#else
		ZoneScopedNS ("io_uring_prep_writev", 16);

		auto lock = std::unique_lock (ringSQMutex);

		io_uring_sqe *sqe = nullptr;
		for (unsigned i = 0; auto const &iov : iov)
		{
			if (sqe)
				sqe->flags |= IOSQE_IO_LINK;

			sqe = io_uring_get_sqe (&ring);
			assert (sqe);
			if (!sqe)
			{
				lock.unlock ();
				error ("io_uring_get_sqe: Queue is full\n");
				terminate ();
				return;
			}

			sqe->flags |= ringSocketFlag;
			io_uring_sqe_set_data (sqe, &outOverlapped);

			assert (i < buffers.size ());
			auto const &buffer = buffers[i++];
			if (buffer.preferred ()) [[likely]]
			{
				// use registered buffer
				io_uring_prep_write_fixed (
				    sqe, ringSocketFd, iov.iov_base, iov.iov_len, 0, buffer.tag ());
			}
			else
			{
				// fallback to unregistered buffers
				if (ringWrite) [[likely]]
				{
					io_uring_prep_write (sqe, ringSocketFd, iov.iov_base, iov.iov_len, 0);
				}
				else
				{
					io_uring_prep_writev (sqe, ringSocketFd, &iov, 1, 0);
				}
			}
		}

		auto const rc = io_uring_submit (&ring);
		lock.unlock ();
		if (rc <= 0)
		{
			if (rc < 0)
				error ("io_uring_submit: %s\n", std::strerror (-rc));
			else
				error ("io_uring_submit: not submitted\n");
			terminate ();
			return;
		}
#endif
	}
}

Pool<Buffer>::Ref ConnectionImpl::getBuffer () noexcept
{
	// reduce lock contention by spreading requests across multiple pools
	auto const index = bufferPoolIndex.fetch_add (1, std::memory_order_relaxed);
	return bufferPools[index % bufferPools.size ()]->getObject ();
}

void ConnectionImpl::pushEvent (int event_) noexcept
{
#ifdef _WIN32
	if (iocpHandle)
	{
		auto const rc = PostQueuedCompletionStatus (iocpHandle, 0, event_, nullptr);
		if (!rc)
			error ("PostQueuedCompletionStatus: %s\n", errorMessage ());
	}
#else
	if (ringDestructor)
	{
		auto lock = std::unique_lock (ringSQMutex);

		auto const sqe = io_uring_get_sqe (&ring);
		assert (sqe);
		if (!sqe)
		{
			lock.unlock ();
			error ("io_uring_get_sqe: Queue is full\n");
			terminate ();
			return;
		}

		io_uring_prep_nop (sqe);

		if (event_ == COMPLETION_KEY_QUIT)
			io_uring_sqe_set_data (sqe, &quitOverlapped);

		auto const rc = io_uring_submit (&ring);
		lock.unlock ();
		if (rc <= 0)
		{
			if (rc < 0)
				error ("io_uring_submit: %s\n", std::strerror (-rc));
			else
				error ("io_uring_submit: not submitted\n");
			terminate ();
		}
	}
#endif
}

///////////////////////////////////////////////////////////////////////////
Connection::~Connection () noexcept = default;

Connection::Connection () noexcept : m_impl (new ConnectionImpl{})
{
}

bool Connection::connect (char const *const host_, char const *const port_) noexcept
{
	if (m_impl->running.load (std::memory_order_relaxed))
	{
		error ("Already connected\n");
		return false;
	}

	// reset buffer pools
	for (unsigned i = 0; auto &pool : m_impl->bufferPools)
		pool = Pool<Buffer>::create ("Buffer " + std::to_string (i++));

#ifdef _WIN32
	if (!m_impl->wsaData.init ())
	{
		m_impl->terminate ();
		m_impl->join ();
		return false;
	}
#endif

	{
		// resolve host/port
		sockaddr_storage addr;
		socklen_t addrLen = sizeof (addr);
		if (!resolve (host_, port_, reinterpret_cast<sockaddr &> (addr), addrLen))
		{
			error ("Failed to lookup [%s]:%s\n", host_, port_);
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}

		m_impl->sock = ::socket (addr.ss_family, SOCK_STREAM, 0);
		if (m_impl->sock == INVALID_SOCKET)
		{
			error ("socket: %s\n", errorMessage (true));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}

		if (::connect (m_impl->sock, reinterpret_cast<sockaddr const *> (&addr), addrLen) != 0)
		{
			error ("connect: %s\n", errorMessage (true));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}
	}

	{
		int const noDelay = 1;
		if (::setsockopt (m_impl->sock,
		        IPPROTO_TCP,
		        TCP_NODELAY,
		        reinterpret_cast<char const *> (&noDelay),
		        sizeof (noDelay)) != 0) [[unlikely]]
		{
			error ("setsockopt(TCP_NODELAY, 1): %s\n", errorMessage (true));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}
	}

	{
		auto const size = static_cast<int> (SOCKET_BUFFER_SIZE);
		if (::setsockopt (m_impl->sock,
		        SOL_SOCKET,
		        SO_RCVBUF,
		        reinterpret_cast<char const *> (&size),
		        sizeof (size)) != 0) [[unlikely]]
		{
			error ("setsockopt(SO_RCVBUF, %d): %s\n", size, errorMessage (true));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}

		if (::setsockopt (m_impl->sock,
		        SOL_SOCKET,
		        SO_SNDBUF,
		        reinterpret_cast<char const *> (&size),
		        sizeof (size)) != 0) [[unlikely]]
		{
			error ("setsockopt(SO_SNDBUF, %d): %s\n", size, errorMessage (true));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}
	}

#ifdef _WIN32
	m_impl->iocpHandle = CreateIoCompletionPort (
	    reinterpret_cast<HANDLE> (m_impl->sock), nullptr, COMPLETION_KEY_SOCKET, 0);
	if (!m_impl->iocpHandle)
	{
		m_impl->terminate ();
		m_impl->join ();
		return false;
	}
#else
	{
		auto const rc = io_uring_queue_init (64, &m_impl->ring, 0);
		if (rc < 0)
		{
			error ("io_uring_queue_init: %s\n", std::strerror (-rc));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}

		m_impl->ringDestructor = {&m_impl->ring, &io_uring_queue_exit};
	}

	{
		auto const probe = io_uring_get_probe_ring (&m_impl->ring);
		if (!probe)
			error ("io_uring_get_probe_ring: failed to probe\n");
		else
		{
			if (io_uring_opcode_supported (probe, IORING_OP_READ))
				m_impl->ringRead = true;

			if (io_uring_opcode_supported (probe, IORING_OP_WRITE))
				m_impl->ringWrite = true;

			io_uring_free_probe (probe);
		}
	}

	{
		auto const rc = io_uring_register_files (&m_impl->ring, &m_impl->sock, 1);
		if (rc < 0)
		{
			// doesn't work on WSL?
			error ("io_uring_register_files: %s\n", std::strerror (-rc));
			m_impl->ringSocketFd   = m_impl->sock;
			m_impl->ringSocketFlag = 0;
		}
		else
		{
			m_impl->ringSocketFd   = 0;
			m_impl->ringSocketFlag = IOSQE_FIXED_FILE;
		}
	}

	{
		// reset buffer pools
		for (unsigned i = 0; auto &pool : m_impl->bufferPools)
			pool = Pool<Buffer>::create ("Buffer " + std::to_string (i++));

		// preallocate some buffers to register
		std::vector<Pool<Buffer>::Ref> buffers;
		std::vector<iovec> iovs;
		buffers.reserve (PREALLOCATED_BUFFERS);
		for (unsigned i = 0; i < PREALLOCATED_BUFFERS; ++i)
		{
			auto &buffer = buffers.emplace_back (m_impl->getBuffer ());
			auto &iov    = iovs.emplace_back ();
			iov.iov_base = buffer->data ();
			iov.iov_len  = buffer->size ();

			buffer.setTag (i);
			buffer.setPreferred (true);
		}

		auto const rc = io_uring_register_buffers (&m_impl->ring, iovs.data (), iovs.size ());
		if (rc < 0)
		{
			error ("io_uring_register_buffers: %s\n", std::strerror (-rc));
			m_impl->terminate ();
			m_impl->join ();
			return false;
		}
	}
#endif

	m_impl->outputQueue.reserve (128);

	m_impl->serviceThread = std::thread (&Connection::serviceThread, this);

	m_impl->inBuffer = m_impl->getBuffer ();

	m_impl->requestRead ();

	m_impl->running.store (true, std::memory_order_relaxed);

	return true;
}

bool Connection::connected () const noexcept
{
	return m_impl->running.load (std::memory_order_relaxed);
}

void Connection::terminate () noexcept
{
	m_impl->terminate ();
}

void Connection::join () noexcept
{
	m_impl->join ();
}

void Connection::sendInterfacePacket (rlbot::flat::InterfacePacketT const &packet_) noexcept
{
	auto fbb = m_impl->fbbPool->getObject ();
	fbb->Finish (rlbot::flat::CreateInterfacePacket (*fbb, &packet_));

	auto const size = fbb->GetSize ();
	if (size > std::numeric_limits<std::uint16_t>::max ()) [[unlikely]]
	{
		warning ("Message payload is too large to encode (%u bytes)\n", size);
		return;
	}

	auto buffer = m_impl->getBuffer ();
	assert (buffer->size () >= size + Message::HEADER_SIZE);

	// encode header
	buffer->operator[] (0) = size >> CHAR_BIT;
	buffer->operator[] (1) = size;

	// copy payload
	if (size > 0) [[likely]]
		std::memcpy (&buffer->operator[] (Message::HEADER_SIZE), fbb->GetBufferPointer (), size);

	{
		auto lock          = std::unique_lock (m_impl->writerMutex);
		m_impl->writerIdle = false;
		m_impl->outputQueue.emplace_back (std::move (buffer));

		if (m_impl->outputQueue.size () == 1)
		{
			assert (m_impl->iov.empty ());
			m_impl->requestWriteLocked (lock);
			return;
		}
	}

#if _WIN32
	auto const rc =
	    PostQueuedCompletionStatus (m_impl->iocpHandle, 0, COMPLETION_KEY_WRITE_QUEUE, nullptr);
	if (!rc)
	{
		error ("PostQueuedCompletionStatus: %s\n", errorMessage ());
		m_impl->terminate ();
	}
#else
	auto lock = std::unique_lock (m_impl->ringSQMutex);

	auto const sqe = io_uring_get_sqe (&m_impl->ring);
	assert (sqe);
	if (!sqe)
	{
		lock.unlock ();
		error ("io_uring_get_sqe: Queue is full\n");
		m_impl->terminate ();
		return;
	}

	io_uring_prep_nop (sqe);

	io_uring_sqe_set_data (sqe, &m_impl->writeQueueOverlapped);

	auto const rc = io_uring_submit (&m_impl->ring);
	lock.unlock ();
	if (rc <= 0)
	{
		if (rc < 0)
			error ("io_uring_submit: %s\n", std::strerror (-rc));
		else
			error ("io_uring_submit: not submitted\n");
		m_impl->terminate ();
	}
#endif
}

void Connection::sendDisconnectSignal (rlbot::flat::DisconnectSignalT packet_) noexcept
{
	ZoneScopedNS ("enqueue DisconnectSignal", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendStartCommand (rlbot::flat::StartCommandT packet_) noexcept
{
	ZoneScopedNS ("enqueue StartCommand", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendMatchConfiguration (rlbot::flat::MatchConfigurationT packet_) noexcept
{
	ZoneScopedNS ("enqueue MatchConfiguration", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendPlayerInput (rlbot::flat::PlayerInputT packet_) noexcept
{
	ZoneScopedNS ("enqueue PlayerInput", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendDesiredGameState (rlbot::flat::DesiredGameStateT packet_) noexcept
{
	ZoneScopedNS ("enqueue DesiredGameState", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendRenderGroup (rlbot::flat::RenderGroupT packet_) noexcept
{
	ZoneScopedNS ("enqueue RenderGroup", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendRemoveRenderGroup (rlbot::flat::RemoveRenderGroupT packet_) noexcept
{
	ZoneScopedNS ("enqueue RemoveRenderGroup", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendMatchComm (rlbot::flat::MatchCommT packet_) noexcept
{
	ZoneScopedNS ("enqueue MatchComm", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendConnectionSettings (rlbot::flat::ConnectionSettingsT packet_) noexcept
{
	ZoneScopedNS ("enqueue ConnectionSettings", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendStopCommand (rlbot::flat::StopCommandT packet_) noexcept
{
	ZoneScopedNS ("enqueue StopCommand", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendSetLoadout (rlbot::flat::SetLoadoutT packet_) noexcept
{
	ZoneScopedNS ("enqueue SetLoadout", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendInitComplete (rlbot::flat::InitCompleteT packet_) noexcept
{
	ZoneScopedNS ("enqueue InitComplete", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::sendRenderingStatus (rlbot::flat::RenderingStatusT packet_) noexcept
{
	ZoneScopedNS ("enqueue RenderingStatus", 16);
	sendInterfacePacket (buildInterfacePacket (std::move (packet_)));
}

void Connection::handleMessage (detail::Message &message_) noexcept
{
	auto const packet = message_.corePacket (true);
	if (!packet) [[unlikely]]
		error ("Invalid core packet received\n");
	else
		handleCorePacket (packet);
}

void Connection::handleCorePacket (rlbot::flat::CorePacket const *const packet_) noexcept
{
	switch (packet_->message_type ())
	{
	case rlbot::flat::CoreMessage::DisconnectSignal:
		handleDisconnectSignal (packet_->message_as_DisconnectSignal ());
		break;

	case rlbot::flat::CoreMessage::GamePacket:
		handleGamePacket (packet_->message_as_GamePacket ());
		break;

	case rlbot::flat::CoreMessage::FieldInfo:
		handleFieldInfo (packet_->message_as_FieldInfo ());
		break;

	case rlbot::flat::CoreMessage::MatchConfiguration:
		handleMatchConfiguration (packet_->message_as_MatchConfiguration ());
		break;

	case rlbot::flat::CoreMessage::MatchComm:
		handleMatchComm (packet_->message_as_MatchComm ());
		break;

	case rlbot::flat::CoreMessage::BallPrediction:
		handleBallPrediction (packet_->message_as_BallPrediction ());
		break;

	case rlbot::flat::CoreMessage::ControllableTeamInfo:
		handleControllableTeamInfo (packet_->message_as_ControllableTeamInfo ());
		break;

	case rlbot::flat::CoreMessage::RenderingStatus:
		handleRenderingStatus (packet_->message_as_RenderingStatus ());
		break;

	default:
		// Unknown packet type
		break;
	}
}

void Connection::handleDisconnectSignal (
    rlbot::flat::DisconnectSignal const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleGamePacket (rlbot::flat::GamePacket const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleFieldInfo (rlbot::flat::FieldInfo const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleMatchConfiguration (
    rlbot::flat::MatchConfiguration const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleMatchComm (rlbot::flat::MatchComm const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleBallPrediction (rlbot::flat::BallPrediction const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleControllableTeamInfo (
    rlbot::flat::ControllableTeamInfo const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::handleRenderingStatus (rlbot::flat::RenderingStatus const *const packet_) noexcept
{
	(void)packet_;
}

void Connection::serviceThread () noexcept
{
#ifdef TRACY_ENABLE
	tracy::SetThreadName ("serviceThread");
#endif

	while (!m_impl->quit.load (std::memory_order_relaxed)) [[likely]]
	{
#if _WIN32
		OVERLAPPED *overlapped = nullptr;
		ULONG_PTR key;
		DWORD count;

		{
			ZoneScopedNS ("GetQueuedCompletionStatus", 16);
			auto const rc =
			    GetQueuedCompletionStatus (m_impl->iocpHandle, &count, &key, &overlapped, INFINITE);
			if (!rc)
			{
				error ("GetQueuedCompletionStatus: %s\n", errorMessage ());
				break;
			}
		}
#else
		auto const [rc, cqe] = [this] {
			io_uring_cqe *cqe;
			if (io_uring_peek_cqe (&m_impl->ring, &cqe) == 0)
				return std::make_pair (0, cqe);

			auto const rc = io_uring_wait_cqe (&m_impl->ring, &cqe);
			return std::make_pair (rc, cqe);
		}();

		if (rc == -EINTR)
			continue;
		else if (rc < 0)
		{
			error ("io_uring_wait_cqe: %s\n", std::strerror (-rc));
			break;
		}

		if (cqe->res == -ECANCELED)
		{
			io_uring_cqe_seen (&m_impl->ring, cqe);
			continue;
		}

		auto const overlapped = static_cast<int const *> (io_uring_cqe_get_data (cqe));
		assert (overlapped);
		if (!overlapped)
		{
			error ("Internal error\n");
			io_uring_cqe_seen (&m_impl->ring, cqe);
			break;
		}

		auto const key   = *overlapped;
		auto const count = cqe->res;

		io_uring_cqe_seen (&m_impl->ring, cqe);

		if (count < 0)
		{
			error ("io_uring_wait_cqe: %s\n", std::strerror (-count));
			break;
		}
#endif

		switch (key)
		{
		case COMPLETION_KEY_SOCKET:
			if (overlapped == &m_impl->inOverlapped)
				handleRead (count);
			else if (overlapped == &m_impl->outOverlapped)
				handleWrite (count);
			break;

		case COMPLETION_KEY_WRITE_QUEUE:
			m_impl->requestWrite ();
			break;

		case COMPLETION_KEY_QUIT:
			// chain to next thread
			m_impl->terminate ();
			return;
		}
	}

	m_impl->terminate ();
}

void Connection::handleRead (std::size_t count_) noexcept
{
	ZoneScopedNS ("handleRead", 16);

	if (count_ == 0)
	{
		// peer disconnected
		m_impl->terminate ();
		return;
	}

	if (static_cast<unsigned> (count_) == m_impl->inBuffer->size () - m_impl->inEndOffset)
	    [[unlikely]]
	{
		// we read all the way to the end of the buffer; there's probably more to read
		ZoneScopedNS ("partial read", 16);
		warning ("Partial read %zd bytes\n", count_);
	}

	// move end pointer
	m_impl->inEndOffset += static_cast<unsigned> (count_);

	assert (m_impl->inEndOffset >= m_impl->inStartOffset);
	while (m_impl->inEndOffset - m_impl->inStartOffset >= Message::HEADER_SIZE)
	    [[likely]] // need to read complete header
	{
		auto const available = m_impl->inEndOffset - m_impl->inStartOffset;

		auto message    = Message (m_impl->inBuffer, m_impl->inStartOffset);
		auto const size = message.sizeWithHeader ();
		if (size > available) [[unlikely]]
		{
			// need to read more data for complete message
			if (m_impl->inEndOffset == m_impl->inBuffer->size ()) [[unlikely]]
			{
				// our buffer is supposed to be large enough to fit any message
				assert (m_impl->inStartOffset != 0);

				// total message exceeds buffer boundary; move to new buffer
				ZoneScopedNS ("move message", 16);
				auto buffer = m_impl->getBuffer ();
				std::memcpy (buffer->data (),
				    &m_impl->inBuffer->operator[] (m_impl->inStartOffset),
				    available);
				m_impl->inBuffer = std::move (buffer);

				m_impl->inEndOffset -= m_impl->inStartOffset;
				m_impl->inStartOffset = 0;
			}

			break;
		}

		handleMessage (message);

		m_impl->inStartOffset += size;
	}

	if (m_impl->inStartOffset == m_impl->inEndOffset) [[likely]]
	{
		// complete packet read so start next read on a new buffer to avoid partial reads
		m_impl->inBuffer      = m_impl->getBuffer ();
		m_impl->inStartOffset = 0;
		m_impl->inEndOffset   = 0;
	}

	m_impl->requestRead ();
}

void Connection::handleWrite (std::size_t count_) noexcept
{
	ZoneScopedNS ("handleWrite", 16);

	auto lock = std::unique_lock (m_impl->writerMutex);

	assert (m_impl->iov.size () <= m_impl->outputQueue.size ());
	assert (!m_impl->outputQueue.empty ());

	auto it  = std::begin (m_impl->outputQueue);
	auto it2 = std::begin (m_impl->iov);
	while (count_ > 0)
	{
		assert (it != std::end (m_impl->outputQueue));
		auto const size = it->sizeWithHeader ();
		auto const rem  = size - m_impl->outStartOffset;

		if (count_ < rem) [[unlikely]]
		{
			// partial write
			ZoneScopedNS ("partial write", 16);
			m_impl->outStartOffset += static_cast<unsigned> (count_);
			warning ("Partial write\n");
			break;
		}

		count_ -= rem;
		m_impl->outStartOffset = 0;

		++it;
		++it2;
	}

	if (it != std::begin (m_impl->outputQueue)) [[likely]]
	{
		m_impl->outputQueue.erase (std::begin (m_impl->outputQueue), it);
		m_impl->iov.erase (std::begin (m_impl->iov), it2);
	}

	assert (m_impl->iov.size () <= m_impl->outputQueue.size ());
	if (m_impl->outputQueue.empty ())
	{
		m_impl->writerIdle = true;
		lock.unlock ();
		m_impl->writerIdleCv.notify_all ();
		return;
	}

	m_impl->requestWriteLocked (lock);
}
