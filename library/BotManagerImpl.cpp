#include "BotManagerImpl.h"

#include "Log.h"

#include <cstdint>
#include <tracy/Tracy.hpp>

#ifdef _WIN32
#include <WS2tcpip.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <unistd.h>
int (&closesocket) (int) = ::close;
#endif

#include <algorithm>
#include <cassert>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <ranges>
#include <unordered_set>

using namespace rlbot::detail;

namespace
{
/// @brief Socket buffer large enough to hold at least 4 messages
constexpr auto SOCKET_BUFFER_SIZE = 4 * (std::numeric_limits<std::uint16_t>::max () + 1u);

constexpr auto COMPLETION_KEY_SOCKET      = 0;
constexpr auto COMPLETION_KEY_WRITE_QUEUE = 1;
constexpr auto COMPLETION_KEY_BOT_WAKEUP  = 2;
constexpr auto COMPLETION_KEY_QUIT        = 3;
}

///////////////////////////////////////////////////////////////////////////
BotManagerImpl::~BotManagerImpl () noexcept
{
	join ();
}

BotManagerImpl::BotManagerImpl (bool const batchHivemind_,
    std::unique_ptr<Bot> (
        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept
    : m_spawn (spawn_), m_batchHivemind (batchHivemind_)
{
#ifdef _WIN32
	m_inOverlapped.hEvent  = nullptr;
	m_outOverlapped.hEvent = nullptr;
#else
	m_inOverlapped         = COMPLETION_KEY_SOCKET;
	m_outOverlapped        = COMPLETION_KEY_SOCKET;
	m_writeQueueOverlapped = COMPLETION_KEY_WRITE_QUEUE;
	m_botWakeupOverlapped  = COMPLETION_KEY_BOT_WAKEUP;
	m_quitOverlapped       = COMPLETION_KEY_QUIT;
#endif
}

bool BotManagerImpl::run (char const *const host_, char const *const port_) noexcept
{
	if (m_running.load (std::memory_order_relaxed))
	{
		error ("BotManager is already running\n");
		return false;
	}

	// reset buffer pools
	for (unsigned i = 0; auto &pool : m_bufferPools)
		pool = Pool<Buffer>::create ("Buffer " + std::to_string (i++));

#ifdef _WIN32
	if (!m_wsaData.init ())
	{
		terminate ();
		join ();
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
			terminate ();
			join ();
			return false;
		}

		m_sock = ::socket (addr.ss_family, SOCK_STREAM, 0);
		if (m_sock == INVALID_SOCKET)
		{
			error ("socket: %s\n", errorMessage (true));
			terminate ();
			join ();
			return false;
		}

		if (::connect (m_sock, reinterpret_cast<sockaddr const *> (&addr), addrLen) != 0)
		{
			error ("connect: %s\n", errorMessage (true));
			terminate ();
			join ();
			return false;
		}
	}

	{
		int const noDelay = 1;
		if (::setsockopt (m_sock,
		        IPPROTO_TCP,
		        TCP_NODELAY,
		        reinterpret_cast<char const *> (&noDelay),
		        sizeof (noDelay)) != 0) [[unlikely]]
		{
			error ("setsockopt(TCP_NODELAY, 1): %s\n", errorMessage (true));
			terminate ();
			join ();
			return false;
		}
	}

	{
		auto const size = static_cast<int> (SOCKET_BUFFER_SIZE);
		if (::setsockopt (m_sock,
		        SOL_SOCKET,
		        SO_RCVBUF,
		        reinterpret_cast<char const *> (&size),
		        sizeof (size)) != 0) [[unlikely]]
		{
			error ("setsockopt(SO_RCVBUF, %d): %s\n", size, errorMessage (true));
			terminate ();
			join ();
			return false;
		}

		if (::setsockopt (m_sock,
		        SOL_SOCKET,
		        SO_SNDBUF,
		        reinterpret_cast<char const *> (&size),
		        sizeof (size)) != 0) [[unlikely]]
		{
			error ("setsockopt(SO_SNDBUF, %d): %s\n", size, errorMessage (true));
			terminate ();
			join ();
			return false;
		}
	}

#ifdef _WIN32
	m_iocpHandle = CreateIoCompletionPort (
	    reinterpret_cast<HANDLE> (m_sock), nullptr, COMPLETION_KEY_SOCKET, 0);
	if (!m_iocpHandle)
	{
		terminate ();
		join ();
		return false;
	}
#else
	{
		auto const rc = io_uring_queue_init (64, &m_ring, 0);
		if (rc < 0)
		{
			error ("io_uring_queue_init: %s\n", std::strerror (-rc));
			terminate ();
			join ();
			return false;
		}

		m_ringDestructor = {&m_ring, &io_uring_queue_exit};
	}

	{
		auto const probe = io_uring_get_probe_ring (&m_ring);
		if (!probe)
			error ("io_uring_get_probe_ring: failed to probe\n");
		else
		{
			if (io_uring_opcode_supported (probe, IORING_OP_READ))
				m_ringRead = true;

			if (io_uring_opcode_supported (probe, IORING_OP_WRITE))
				m_ringWrite = true;

			io_uring_free_probe (probe);
		}
	}

	{
		auto const rc = io_uring_register_files (&m_ring, &m_sock, 1);
		if (rc < 0)
		{
			// doesn't work on WSL?
			error ("io_uring_register_files: %s\n", std::strerror (-rc));
			m_ringSocketFd   = m_sock;
			m_ringSocketFlag = 0;
		}
		else
		{
			m_ringSocketFd   = 0;
			m_ringSocketFlag = IOSQE_FIXED_FILE;
		}
	}

	{
		// reset buffer pools
		for (unsigned i = 0; auto &pool : m_bufferPools)
			pool = Pool<Buffer>::create ("Buffer " + std::to_string (i++));

		// preallocate some buffers to register
		std::vector<Pool<Buffer>::Ref> buffers;
		std::vector<iovec> iovs;
		buffers.reserve (PREALLOCATED_BUFFERS);
		for (unsigned i = 0; i < PREALLOCATED_BUFFERS; ++i)
		{
			auto &buffer = buffers.emplace_back (getBuffer ());
			auto &iov    = iovs.emplace_back ();
			iov.iov_base = buffer->data ();
			iov.iov_len  = buffer->size ();

			buffer.setTag (i);
			buffer.setPreferred (true);
		}

		auto const rc = io_uring_register_buffers (&m_ring, iovs.data (), iovs.size ());
		if (rc < 0)
		{
			error ("io_uring_register_buffers: %s\n", std::strerror (-rc));
			terminate ();
			join ();
			return false;
		}
	}
#endif

	m_outputQueue.reserve (128);

	m_serviceThread = std::thread (&BotManagerImpl::serviceThread, this);

	m_inBuffer = getBuffer ();

	requestRead ();

	m_running.store (true, std::memory_order_relaxed);

	return true;
}

void BotManagerImpl::waitForWriterIdle () noexcept
{
	auto lock = std::unique_lock (m_writerMutex);
	while (!m_writerIdle)
		m_writerIdleCv.wait (lock);
}

void BotManagerImpl::terminate () noexcept
{
	{
		auto const lock = std::scoped_lock (m_writerMutex);
		m_writerIdle    = true;
	}

	m_writerIdleCv.notify_all ();

	m_quit.store (true, std::memory_order_relaxed);

	pushEvent (COMPLETION_KEY_QUIT);
}

void BotManagerImpl::join () noexcept
{
	if (m_running.load (std::memory_order_relaxed))
		m_serviceThread.join ();

	clearBots ();

	if (m_sock != INVALID_SOCKET)
	{
		if (::closesocket (m_sock) != 0)
			error ("closesocket: %s\n", errorMessage (true));
		m_sock = INVALID_SOCKET;
	}

#ifdef _WIN32
	if (m_iocpHandle)
	{
		if (!CloseHandle (m_iocpHandle))
			error ("CloseHandle: %s\n", errorMessage ());
		m_iocpHandle = nullptr;
	}
#else
	m_ringDestructor.reset ();
#endif

	m_outputQueue.clear ();

	m_quit.store (false, std::memory_order_relaxed);

	m_running.store (false, std::memory_order_relaxed);
}

Message BotManagerImpl::buildMessage (MessageType type_,
    Pool<flatbuffers::FlatBufferBuilder>::Ref builder_) noexcept
{
	auto const size = builder_ ? builder_->GetSize () : 0;
	if (size > std::numeric_limits<std::uint16_t>::max ()) [[unlikely]]
	{
		warning ("Message payload is too large to encode (%u bytes)\n", size);
		return {};
	}

	auto buffer = getBuffer ();
	assert (buffer->size () >= size + 4);

	// encode header
	buffer->operator[] (0) = static_cast<unsigned> (type_) >> CHAR_BIT;
	buffer->operator[] (1) = static_cast<unsigned> (type_);
	buffer->operator[] (2) = size >> CHAR_BIT;
	buffer->operator[] (3) = size;

	// copy payload
	if (size > 0) [[likely]]
		std::memcpy (&buffer->operator[] (4), builder_->GetBufferPointer (), size);

	return Message (std::move (buffer));
}

void BotManagerImpl::enqueueMessage (Message message_) noexcept
{
	if (!message_) [[unlikely]]
		return;

	bool fastPath = false;
	{
		auto lock    = std::unique_lock (m_writerMutex);
		m_writerIdle = false;
		m_outputQueue.emplace_back (message_);

		if (m_outputQueue.size () == 1)
		{
			assert (m_iov.empty ());
			fastPath = true;
			requestWriteLocked (lock);
		}
	}

	if (!fastPath)
	{
#if _WIN32
		auto const rc =
		    PostQueuedCompletionStatus (m_iocpHandle, 0, COMPLETION_KEY_WRITE_QUEUE, nullptr);
		if (!rc)
		{
			error ("PostQueuedCompletionStatus: %s\n", errorMessage ());
			terminate ();
		}
#else
		auto lock = std::unique_lock (m_ringSQMutex);

		auto const sqe = io_uring_get_sqe (&m_ring);
		assert (sqe);
		if (!sqe)
		{
			lock.unlock ();
			error ("io_uring_get_sqe: Queue is full\n");
			terminate ();
			return;
		}

		io_uring_prep_nop (sqe);

		io_uring_sqe_set_data (sqe, &m_writeQueueOverlapped);

		auto const rc = io_uring_submit (&m_ring);
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

	if (message_.type () == MessageType::MatchComm && !m_bots.empty ()) [[unlikely]]
	{
		// send to other bots in this manager
		for (auto &bot : m_bots | std::views::drop (1))
			bot.addMatchComm (message_, true);

		// first bot needs special handling
		auto &bot = m_bots.front ();
		bot.addMatchComm (message_, false);

		if (m_serviceThread.get_id () == std::this_thread::get_id ())
			bot.loopOnce ();
		else
			pushEvent (COMPLETION_KEY_BOT_WAKEUP);
	}
}

void BotManagerImpl::enqueueMessage (MessageType type_,
    Pool<flatbuffers::FlatBufferBuilder>::Ref builder_) noexcept
{
	enqueueMessage (buildMessage (type_, std::move (builder_)));
}

void BotManagerImpl::spawnBots () noexcept
{
	if (!m_controllableTeamInfo || !m_fieldInfo || !m_matchConfiguration)
		return;

	clearBots ();

	auto const agentId = std::getenv ("RLBOT_AGENT_ID");
	if (!agentId || std::strlen (agentId) == 0)
		return;

	auto const controllableTeamInfo =
	    m_controllableTeamInfo.flatbuffer<rlbot::flat::ControllableTeamInfo> (true);
	auto const fieldInfo = m_fieldInfo.flatbuffer<rlbot::flat::FieldInfo> (true);
	auto const matchConfiguration =
	    m_matchConfiguration.flatbuffer<rlbot::flat::MatchConfiguration> (true);
	if (!controllableTeamInfo || !fieldInfo || !matchConfiguration)
		return;

	assert (m_bots.empty ());

	auto const &configs = *matchConfiguration->player_configurations ();

	auto const team = controllableTeamInfo->team ();
	rlbot::flat::SetLoadoutT loadoutMessage{};

	std::unordered_set<unsigned> botIndices;
	std::string name;
	for (auto const &controllableInfo : *controllableTeamInfo->controllables ())
	{
		// find player in match settings with matching spawn id
		auto const player = std::ranges::find (configs,
		    controllableInfo->spawn_id (),
		    [] (auto const config_) { return config_->spawn_id (); });
		if (player == std::end (configs))
		{
			warning ("ControllableInfo player not found in match settings\n");
			continue;
		}

		if (player->team () != team)
		{
			warning ("ControllableInfo team mismatch\n");
			continue;
		}

		auto const index = controllableInfo->index ();
		if (std::ranges::find (botIndices, index) != std::end (botIndices))
		{
			warning ("ControllableInfo duplicate bot index %u\n", index);
			continue;
		}

		botIndices.emplace (index);

		if (m_batchHivemind)
		{
			if (!name.empty ())
				name = player->name ()->str ();
			continue; // defer bot creation
		}

		auto bot = m_spawn (botIndices, team, player->name ()->str ());

		auto loadout = bot->getLoadout (index);

		m_bots.emplace_back (std::move (botIndices),
		    std::move (bot),
		    m_controllableTeamInfo,
		    m_fieldInfo,
		    m_matchConfiguration,
		    *this);

		if (!loadout.has_value ())
			continue;

		if (!loadoutMessage.loadout)
			loadoutMessage.loadout = std::make_unique<rlbot::flat::PlayerLoadoutT> ();

		loadoutMessage.index    = index;
		*loadoutMessage.loadout = std::move (loadout.value ());
		enqueueMessage (loadoutMessage);
	}

	if (m_batchHivemind)
	{
		auto bot = m_spawn (botIndices, team, std::move (name));

		for (auto const &index : botIndices)
		{
			auto loadout = bot->getLoadout (index);
			if (!loadout.has_value ())
				continue;

			if (!loadoutMessage.loadout)
				loadoutMessage.loadout = std::make_unique<rlbot::flat::PlayerLoadoutT> ();

			loadoutMessage.index    = index;
			*loadoutMessage.loadout = std::move (loadout.value ());
			enqueueMessage (loadoutMessage);
		}

		m_bots.emplace_back (std::move (botIndices),
		    std::move (bot),
		    m_controllableTeamInfo,
		    m_fieldInfo,
		    m_matchConfiguration,
		    *this);
	}

	// handle the first bot on the reader thread
	for (auto &bot : m_bots | std::views::drop (1))
		bot.startService ();

	if (!m_bots.empty ())
		std::begin (m_bots)->initialize ();

	for (auto &bot : m_bots)
		bot.waitInitialized ();

	enqueueMessage (MessageType::InitComplete, {});
}

void BotManagerImpl::clearBots () noexcept
{
	for (auto &bot : m_bots | std::views::drop (1))
		bot.terminate ();

	m_bots.clear ();
}

void BotManagerImpl::handleMessage (Message message_) noexcept
{
	assert (message_);

	if (message_.type () == MessageType::None) [[unlikely]]
	{
		terminate ();
		return;
	}

	if (message_.type () == MessageType::ControllableTeamInfo) [[unlikely]]
	{
		ZoneScopedNS ("handle ControllableTeamInfo", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::ControllableTeamInfo> (true);
		if (!payload) [[unlikely]]
			return;

		info ("Received ControllableTeamInfo\n");
		m_controllableTeamInfo = std::move (message_);
		spawnBots ();
		return;
	}

	if (message_.type () == MessageType::FieldInfo) [[unlikely]]
	{
		ZoneScopedNS ("handle FieldInfo", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::FieldInfo> (true);
		if (!payload) [[unlikely]]
			return;

		info ("Received FieldInfo\n");
		m_fieldInfo = std::move (message_);
		spawnBots ();
		return;
	}

	if (message_.type () == MessageType::MatchConfiguration) [[unlikely]]
	{
		ZoneScopedNS ("handle MatchConfiguration", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::MatchConfiguration> (true);
		if (!payload) [[unlikely]]
			return;

		info ("Received MatchConfiguration\n");
		m_matchConfiguration = std::move (message_);
		spawnBots ();
		return;
	}

	if (m_bots.empty ()) [[unlikely]]
		return;

	if (message_.type () == MessageType::BallPrediction) [[likely]]
	{
		ZoneScopedNS ("handle BallPrediction", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::BallPrediction> (true);
		if (!payload) [[unlikely]]
			return;

		for (auto &bot : m_bots)
			bot.setBallPrediction (message_);
		return;
	}

	if (message_.type () == MessageType::GamePacket) [[likely]]
	{
		FrameMark;
		ZoneScopedNS ("handle GamePacket", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::GamePacket> (true);
		if (!payload) [[unlikely]]
			return;

		for (auto &bot : m_bots | std::views::drop (1))
			bot.setGamePacket (message_, true);

		// handle the first bot on the reader thread
		auto &bot = m_bots.front ();
		bot.setGamePacket (message_, false);
		bot.loopOnce ();

		return;
	}

	if (message_.type () == MessageType::MatchComm) [[unlikely]]
	{
		ZoneScopedNS ("handle MatchComm", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::MatchComm> (true);
		if (!payload) [[unlikely]]
			return;

		for (auto &bot : m_bots | std::views::drop (1))
			bot.addMatchComm (message_, true);

		// handle the first bot on the reader thread
		auto &bot = m_bots.front ();
		bot.loopOnce ();

		return;
	}
}

void BotManagerImpl::requestRead () noexcept
{
#ifdef _WIN32
	ZoneScopedNS ("requestRead", 16);

	WSABUF buffer;
	buffer.buf = reinterpret_cast<CHAR *> (m_inBuffer->data () + m_inEndOffset);
	buffer.len = m_inBuffer->size () - m_inEndOffset;

	DWORD flags = MSG_PUSH_IMMEDIATE;

	{
		ZoneScopedNS ("WSARecv", 16);
		auto const rc = WSARecv (m_sock, &buffer, 1, nullptr, &flags, &m_inOverlapped, nullptr);
		if (rc != 0 && WSAGetLastError () != WSA_IO_PENDING)
		{
			error ("WSARecv: %s\n", errorMessage (true));
			terminate ();
		}
	}
#else
	ZoneScopedNS ("io_uring_prep_readv", 16);

	auto lock = std::unique_lock (m_ringSQMutex);

	auto const sqe = io_uring_get_sqe (&m_ring);
	assert (sqe);
	if (!sqe)
	{
		lock.unlock ();
		error ("io_uring_get_sqe: Queue is full\n");
		terminate ();
		return;
	}

	auto const buffer = m_inBuffer->data () + m_inEndOffset;
	auto const size   = m_inBuffer->size () - m_inEndOffset;

	if (m_inBuffer.preferred ()) [[likely]]
	{
		// use registered buffer
		io_uring_prep_read_fixed (sqe, m_ringSocketFd, buffer, size, 0, m_inBuffer.tag ());
	}
	else
	{
		// fallback to unregistered buffer
		if (m_ringRead) [[likely]]
			io_uring_prep_read (sqe, m_ringSocketFd, buffer, size, 0);
		else
		{
			m_readIov.iov_base = buffer;
			m_readIov.iov_len  = size;
			io_uring_prep_readv (sqe, m_ringSocketFd, &m_readIov, 1, 0);
		}
	}

	sqe->flags |= m_ringSocketFlag;
	io_uring_sqe_set_data (sqe, &m_inOverlapped);

	auto const rc = io_uring_submit (&m_ring);
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

void BotManagerImpl::handleRead (std::size_t const count_) noexcept
{
	ZoneScopedNS ("handleRead", 16);

	if (count_ == 0)
	{
		// peer disconnected
		terminate ();
		return;
	}

	if (static_cast<unsigned> (count_) == m_inBuffer->size () - m_inEndOffset) [[unlikely]]
	{
		// we read all the way to the end of the buffer; there's probably more to read
		ZoneScopedNS ("partial read", 16);
		warning ("Partial read %zd bytes\n", count_);
	}

	// move end pointer
	m_inEndOffset += static_cast<unsigned> (count_);

	assert (m_inEndOffset >= m_inStartOffset);
	while (m_inEndOffset - m_inStartOffset >= 4) [[likely]] // need to read complete header
	{
		auto const available = m_inEndOffset - m_inStartOffset;

		auto message    = Message (m_inBuffer, m_inStartOffset);
		auto const size = message.sizeWithHeader ();
		if (size > available) [[unlikely]]
		{
			// need to read more data for complete message
			if (m_inEndOffset == m_inBuffer->size ()) [[unlikely]]
			{
				// our buffer is supposed to be large enough to fit any message
				assert (m_inStartOffset != 0);

				// total message exceeds buffer boundary; move to new buffer
				ZoneScopedNS ("move message", 16);
				auto buffer = getBuffer ();
				std::memcpy (buffer->data (), &m_inBuffer->operator[] (m_inStartOffset), available);
				m_inBuffer = std::move (buffer);

				m_inEndOffset -= m_inStartOffset;
				m_inStartOffset = 0;
			}

			// partial message
			break;
		}

		handleMessage (std::move (message));

		m_inStartOffset += size;
	}

	if (m_inStartOffset == m_inEndOffset) [[likely]]
	{
		// complete packet read so start next read on a new buffer to avoid partial reads
		m_inBuffer      = getBuffer ();
		m_inStartOffset = 0;
		m_inEndOffset   = 0;
	}

	requestRead ();
}

void BotManagerImpl::requestWrite () noexcept
{
	ZoneScopedNS ("requestWrite", 16);

	auto lock = std::unique_lock (m_writerMutex);
	if (m_outputQueue.empty ())
		return;

	requestWriteLocked (lock);
}

void BotManagerImpl::requestWriteLocked (std::unique_lock<std::mutex> &lock_) noexcept
{
	ZoneScopedNS ("requestWriteLocked", 16);

	if (!m_iov.empty ())
	{
		lock_.unlock ();
		return;
	}

	assert (!m_outputQueue.empty ());

	std::array<Pool<Buffer>::Ref, PREALLOCATED_BUFFERS> buffers;

	if (m_iov.capacity () < m_outputQueue.size ()) [[unlikely]]
		m_iov.reserve (m_outputQueue.size ());

	unsigned startOffset = m_outStartOffset;
	for (unsigned i = 0; auto const &message : m_outputQueue)
	{
		auto const span = message.span ();
		assert (span.size () > startOffset);

		m_iov.emplace_back (&span[startOffset], span.size () - startOffset);
		startOffset = 0;

		buffers[i++] = message.buffer ();

		if (i >= buffers.size ())
			break;
	}

	lock_.unlock ();

	if (!m_iov.empty ()) [[likely]]
	{
#ifdef _WIN32
		ZoneScopedNS ("WSASend", 16);
		auto const rc =
		    WSASend (m_sock, m_iov.data (), m_iov.size (), nullptr, 0, &m_outOverlapped, nullptr);
		if (rc != 0 && WSAGetLastError () != WSA_IO_PENDING)
		{
			error ("WSASend: %s\n", errorMessage (true));
			terminate ();
			return;
		}
#else
		ZoneScopedNS ("io_uring_prep_writev", 16);

		auto lock = std::unique_lock (m_ringSQMutex);

		io_uring_sqe *sqe = nullptr;
		for (unsigned i = 0; auto const &iov : m_iov)
		{
			if (sqe)
				sqe->flags |= IOSQE_IO_LINK;

			sqe = io_uring_get_sqe (&m_ring);
			assert (sqe);
			if (!sqe)
			{
				lock.unlock ();
				error ("io_uring_get_sqe: Queue is full\n");
				terminate ();
				return;
			}

			sqe->flags |= m_ringSocketFlag;
			io_uring_sqe_set_data (sqe, &m_outOverlapped);

			assert (i < buffers.size ());
			auto const &buffer = buffers[i++];
			if (buffer.preferred ()) [[likely]]
			{
				// use registered buffer
				io_uring_prep_write_fixed (
				    sqe, m_ringSocketFd, iov.iov_base, iov.iov_len, 0, buffer.tag ());
			}
			else
			{
				// fallback to unregistered buffers
				if (m_ringWrite) [[likely]]
				{
					io_uring_prep_write (sqe, m_ringSocketFd, iov.iov_base, iov.iov_len, 0);
				}
				else
				{
					io_uring_prep_writev (sqe, m_ringSocketFd, &iov, 1, 0);
				}
			}
		}

		auto const rc = io_uring_submit (&m_ring);
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

void BotManagerImpl::handleWrite (std::size_t count_) noexcept
{
	ZoneScopedNS ("handleWrite", 16);

	auto lock = std::unique_lock (m_writerMutex);

	assert (m_iov.size () <= m_outputQueue.size ());
	assert (!m_outputQueue.empty ());

	auto it  = std::begin (m_outputQueue);
	auto it2 = std::begin (m_iov);
	while (count_ > 0)
	{
		assert (it != std::end (m_outputQueue));
		auto const size = it->sizeWithHeader ();
		auto const rem  = size - m_outStartOffset;

		if (count_ < rem) [[unlikely]]
		{
			// partial write
			ZoneScopedNS ("partial write", 16);
			m_outStartOffset += static_cast<unsigned> (count_);
			warning ("Partial write\n");
			break;
		}

		count_ -= rem;
		m_outStartOffset = 0;

		++it;
		++it2;
	}

	if (it != std::begin (m_outputQueue)) [[likely]]
	{
		m_outputQueue.erase (std::begin (m_outputQueue), it);
		m_iov.erase (std::begin (m_iov), it2);
	}

	assert (m_iov.size () <= m_outputQueue.size ());
	if (m_outputQueue.empty ())
	{
		m_writerIdle = true;
		lock.unlock ();
		m_writerIdleCv.notify_all ();
		return;
	}

	requestWriteLocked (lock);
}

void BotManagerImpl::serviceThread () noexcept
{
#ifdef TRACY_ENABLE
	tracy::SetThreadName ("serviceThread");
#endif

	while (!m_quit.load (std::memory_order_relaxed)) [[likely]]
	{
#if _WIN32
		OVERLAPPED *overlapped = nullptr;
		ULONG_PTR key;
		DWORD count;

		{
			ZoneScopedNS ("GetQueuedCompletionStatus", 16);
			auto const rc =
			    GetQueuedCompletionStatus (m_iocpHandle, &count, &key, &overlapped, INFINITE);
			if (!rc)
			{
				error ("GetQueuedCompletionStatus: %s\n", errorMessage ());
				break;
			}
		}
#else
		io_uring_cqe *cqe;
		auto const rc = io_uring_wait_cqe (&m_ring, &cqe);
		if (rc == -EINTR)
			continue;
		else if (rc < 0)
		{
			error ("io_uring_wait_cqe: %s\n", std::strerror (-rc));
			break;
		}

		if (cqe->res == -ECANCELED)
		{
			io_uring_cqe_seen (&m_ring, cqe);
			continue;
		}

		auto const overlapped = static_cast<int const *> (io_uring_cqe_get_data (cqe));
		assert (overlapped);
		if (!overlapped)
		{
			error ("Internal error\n");
			io_uring_cqe_seen (&m_ring, cqe);
			break;
		}

		auto const key   = *overlapped;
		auto const count = cqe->res;

		io_uring_cqe_seen (&m_ring, cqe);

		if (count < 0)
		{
			error ("io_uring_wait_cqe: %s\n", std::strerror (-count));
			break;
		}
#endif

		switch (key)
		{
		case COMPLETION_KEY_SOCKET:
			if (overlapped == &m_inOverlapped)
				handleRead (count);
			else if (overlapped == &m_outOverlapped)
				handleWrite (count);
			break;

		case COMPLETION_KEY_WRITE_QUEUE:
			requestWrite ();
			break;

		case COMPLETION_KEY_BOT_WAKEUP:
			if (!m_bots.empty ())
				m_bots.front ().loopOnce ();
			break;

		case COMPLETION_KEY_QUIT:
			// chain to next thread
			terminate ();
			return;
		}
	}

	terminate ();
}

Pool<Buffer>::Ref BotManagerImpl::getBuffer () noexcept
{
	// reduce lock contention by spreading requests across multiple pools
	auto const index = m_bufferPoolIndex.fetch_add (1, std::memory_order_relaxed);
	return m_bufferPools[index % m_bufferPools.size ()]->getObject ();
}

void rlbot::detail::BotManagerImpl::pushEvent (int const event_) noexcept
{
#ifdef _WIN32
	if (m_iocpHandle)
	{
		auto const rc = PostQueuedCompletionStatus (m_iocpHandle, 0, event_, nullptr);
		if (!rc)
			error ("PostQueuedCompletionStatus: %s\n", errorMessage ());
	}
#else
	if (m_ringDestructor)
	{
		auto lock = std::unique_lock (m_ringSQMutex);

		auto const sqe = io_uring_get_sqe (&m_ring);
		assert (sqe);
		if (!sqe)
		{
			lock.unlock ();
			error ("io_uring_get_sqe: Queue is full\n");
			terminate ();
			return;
		}

		io_uring_prep_nop (sqe);

		switch (event_)
		{
		case COMPLETION_KEY_BOT_WAKEUP:
			io_uring_sqe_set_data (sqe, &m_botWakeupOverlapped);
			break;

		case COMPLETION_KEY_QUIT:
			io_uring_sqe_set_data (sqe, &m_quitOverlapped);
			break;
		}

		auto const rc = io_uring_submit (&m_ring);
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

#define DEFINE_ENQUEUE(x)                                                                          \
	void BotManagerImpl::enqueueMessage (rlbot::flat::x##T const &message_) noexcept               \
	{                                                                                              \
		ZoneScopedNS ("enqueue " #x, 16);                                                          \
		auto fbb = m_fbbPool->getObject ();                                                        \
		fbb->Finish (rlbot::flat::Create##x (*fbb, &message_));                                    \
		enqueueMessage (MessageType::x, std::move (fbb));                                          \
	}

DEFINE_ENQUEUE (GamePacket)
DEFINE_ENQUEUE (FieldInfo)
DEFINE_ENQUEUE (StartCommand)
DEFINE_ENQUEUE (MatchConfiguration)
DEFINE_ENQUEUE (PlayerInput)
DEFINE_ENQUEUE (DesiredGameState)
DEFINE_ENQUEUE (RenderGroup)
DEFINE_ENQUEUE (RemoveRenderGroup)
DEFINE_ENQUEUE (MatchComm)
DEFINE_ENQUEUE (BallPrediction)
DEFINE_ENQUEUE (ConnectionSettings)
DEFINE_ENQUEUE (StopCommand)
DEFINE_ENQUEUE (SetLoadout)
DEFINE_ENQUEUE (ControllableTeamInfo)
