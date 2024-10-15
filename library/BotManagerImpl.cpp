#include "BotManagerImpl.h"

#include "Log.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <functional>
#include <limits>
#include <random>
#include <ranges>
#include <string_view>
#include <thread>
#include <unordered_set>
#include <vector>
using namespace std::chrono_literals;

using namespace rlbot::detail;

namespace
{
/// @brief Socket buffer large enough to hold at least 4 messages
constexpr auto SOCKET_BUFFER_SIZE = 4 * std::numeric_limits<std::uint16_t>::max ();
}

///////////////////////////////////////////////////////////////////////////
BotManagerImpl::~BotManagerImpl () noexcept
{
	join ();
}

BotManagerImpl::BotManagerImpl (
    std::unique_ptr<Bot> (&spawn_) (int, int, std::string) noexcept) noexcept
    : m_spawn (spawn_)
{
}

bool rlbot::detail::BotManagerImpl::run (char const *const host_, char const *const port_) noexcept
{
	if (m_running.load (std::memory_order_relaxed))
	{
		error ("BotManager is already running\n");
		return false;
	}

	if (!m_wsaData.init ())
		return false;

	m_readerEvent = Event::create ();
	m_writerEvent = Event::create ();
	if (!m_readerEvent || !m_writerEvent)
		return false;

	// resolve host/port
	auto const addr = SockAddr::lookup (host_, port_);
	if (!addr.has_value ())
	{
		error ("Failed to lookup [%s]:%s\n", host_, port_);
		return false;
	}

	m_sock = TCPSocket::create (addr->family ());
	if (!m_sock || !m_sock.connect (addr.value ()) || !m_sock.setNoDelay ())
		return false;

	m_sockEvent = Event::create (m_sock);
	if (!m_sockEvent)
		return false;

	m_sock.setRecvBufferSize (2 * BUFFER_SIZE);
	m_sock.setSendBufferSize (2 * BUFFER_SIZE);

	m_outputQueue.reserve (128);

	m_readerThread = std::thread (&BotManagerImpl::readerService, this);
	m_writerThread = std::thread (&BotManagerImpl::writerService, this);

	m_running.store (true, std::memory_order_relaxed);

	return true;
}

void rlbot::detail::BotManagerImpl::waitForWriterIdle () noexcept
{
	auto lock = std::unique_lock (m_writerIdleMutex);
	while (!m_writerIdle.load (std::memory_order_relaxed))
		m_writerIdleCv.wait (lock);
}

void rlbot::detail::BotManagerImpl::terminate () noexcept
{
	m_writerIdle.store (true, std::memory_order_relaxed);
	m_writerIdleCv.notify_all ();

	m_quit.store (true, std::memory_order_relaxed);
	m_readerEvent.signal ();
	m_writerEvent.signal ();
}

void rlbot::detail::BotManagerImpl::join () noexcept
{
	if (m_running.load (std::memory_order_relaxed))
	{
		m_readerThread.join ();
		m_writerThread.join ();
	}

	clearBots ();

	m_readerEvent = {};
	m_writerEvent = {};

	m_sock      = {};
	m_sockEvent = {};

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

	auto buffer = m_bufferPool->getObject ();
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

	m_writerIdle.store (false, std::memory_order_relaxed);

	{
		auto const lock = std::scoped_lock (m_outputQueueMutex);
		m_outputQueue.emplace_back (message_);
	}

	m_writerEvent.signal ();

	if (message_.type () == MessageType::MatchComm) [[unlikely]]
	{
		// send to other bots in this manager
		for (auto &[_, context] : m_bots)
			context.addMatchComm (message_);
	}
}

void BotManagerImpl::enqueueMessage (MessageType type_,
    Pool<flatbuffers::FlatBufferBuilder>::Ref builder_) noexcept
{
	enqueueMessage (buildMessage (type_, std::move (builder_)));
}

void rlbot::detail::BotManagerImpl::spawnBots () noexcept
{
	if (!m_controllableTeamInfo || !m_fieldInfo || !m_matchSettings)
		return;

	clearBots ();

	auto const agentId = std::getenv ("RLBOT_AGENT_ID");
	if (!agentId || std::strlen (agentId) == 0)
		return;

	auto const controllableTeamInfo =
	    m_controllableTeamInfo.flatbuffer<rlbot::flat::ControllableTeamInfo> (true);
	auto const fieldInfo     = m_fieldInfo.flatbuffer<rlbot::flat::FieldInfo> (true);
	auto const matchSettings = m_matchSettings.flatbuffer<rlbot::flat::MatchSettings> (true);
	if (!controllableTeamInfo || !fieldInfo || !matchSettings)
		return;

	assert (m_bots.empty ());

	auto const &configs = *matchSettings->player_configurations ();

	auto const team = controllableTeamInfo->team ();
	rlbot::flat::SetLoadoutT loadoutMessage{};
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

		auto bot = m_spawn (index, team, player->name ()->str ());

		auto loadout = bot->getLoadout ();

		assert (!m_bots.contains (index));
		m_bots.emplace (std::piecewise_construct,
		    std::forward_as_tuple (index),
		    std::forward_as_tuple (index, std::move (bot), m_fieldInfo, m_matchSettings, *this));

		if (!loadout.has_value ())
			continue;

		if (!loadoutMessage.loadout)
			loadoutMessage.loadout = std::make_unique<rlbot::flat::PlayerLoadoutT> ();

		loadoutMessage.index    = index;
		*loadoutMessage.loadout = std::move (loadout.value ());
		enqueueMessage (loadoutMessage);
	}

	enqueueMessage (MessageType::InitComplete, {});
}

void BotManagerImpl::clearBots () noexcept
{
	for (auto &[_, context] : m_bots)
		context.terminate ();

	m_bots.clear ();
}

void BotManagerImpl::handleMessage (Message message_) noexcept
{
	assert (message_);

	if (message_.type () == MessageType::BallPrediction) [[likely]]
	{
		ZoneScopedNS ("handle BallPrediction", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::BallPrediction> (true);
		if (!payload) [[unlikely]]
			return;

		for (auto &[_, context] : m_bots)
			context.setBallPrediction (message_);
		return;
	}

	if (message_.type () == MessageType::GamePacket) [[likely]]
	{
		FrameMark;
		ZoneScopedNS ("handle GamePacket", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::GamePacket> (true);
		if (!payload) [[unlikely]]
			return;

		for (auto &[_, context] : m_bots)
			context.setGamePacket (message_);
		return;
	}

	if (message_.type () == MessageType::MatchComm) [[unlikely]]
	{
		ZoneScopedNS ("handle MatchComm", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::MatchComm> (true);
		if (!payload) [[unlikely]]
			return;

		for (auto &[_, context] : m_bots)
			context.addMatchComm (message_);
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

	if (message_.type () == MessageType::MatchSettings) [[unlikely]]
	{
		ZoneScopedNS ("handle MatchSettings", 16);

		auto const payload = message_.flatbuffer<rlbot::flat::MatchSettings> (true);
		if (!payload) [[unlikely]]
			return;

		info ("Received MatchSettings\n");
		m_matchSettings = std::move (message_);
		spawnBots ();
		return;
	}

	if (message_.type () == MessageType::None) [[unlikely]]
	{
		terminate ();
		return;
	}
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
DEFINE_ENQUEUE (MatchSettings)
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

void BotManagerImpl::readerService () noexcept
{
#ifdef TRACY_ENABLE
	tracy::SetThreadName ("Reader");
#endif

	// may contain multiple messages per buffer
	Pool<Buffer>::Ref inBuffer = m_bufferPool->getObject ();
	unsigned startOffset       = 0; // begin pointer
	unsigned endOffset         = 0; // end pointer

	EventWaiter waiter;
	if (!waiter.add (m_readerEvent) || !waiter.add (m_sockEvent))
	{
		terminate ();
		return;
	}

	while (!m_quit.load (std::memory_order_relaxed)) [[likely]]
	{
		auto const event = waiter.wait ();
		if (!event) [[unlikely]]
			break;

		event->clear ();

		if (event == &m_readerEvent) [[unlikely]]
		{
			// only signaled to quit
			break;
		}

		assert (inBuffer);
		assert (endOffset < inBuffer->size ());
		auto const rc = m_sock.read (inBuffer->data () + endOffset, inBuffer->size () - endOffset);
		if (rc <= 0) [[unlikely]]
		{
			if (rc < 0 && Socket::lastError () == EWOULDBLOCK) [[likely]]
				continue;

			break;
		}

		if (static_cast<unsigned> (rc) == inBuffer->size () - endOffset) [[unlikely]]
		{
			// we read all the way to the end of the buffer; there's probably more to read
			ZoneScopedNS ("partial read", 16);
			warning ("Partial read %zd bytes\n", rc);
		}

		// move end pointer
		endOffset += static_cast<unsigned> (rc);

		assert (endOffset > startOffset);
		while (endOffset - startOffset >= 4) [[likely]] // need to read complete header
		{
			auto const available = endOffset - startOffset;

			auto message    = Message (inBuffer, startOffset);
			auto const size = message.sizeWithHeader ();
			if (size > available) [[unlikely]]
			{
				// need to read more data for complete message
				if (endOffset == inBuffer->size ()) [[unlikely]]
				{
					// our buffer is supposed to be large enough to fit any message
					assert (startOffset != 0);

					// total message exceeds buffer boundary; move to new buffer
					ZoneScopedNS ("move message", 16);
					auto buffer = m_bufferPool->getObject ();
					std::memcpy (buffer->data (), &inBuffer->operator[] (startOffset), available);
					inBuffer = std::move (buffer);

					endOffset -= startOffset;
					startOffset = 0;
				}

				// partial message
				break;
			}

			handleMessage (std::move (message));

			startOffset += size;
		}

		if (startOffset == endOffset) [[likely]]
		{
			// complete packet read so start next read on a new buffer to avoid partial reads
			inBuffer    = m_bufferPool->getObject ();
			startOffset = 0;
			endOffset   = 0;
		}
	}

	terminate ();
}

void BotManagerImpl::writerService () noexcept
{
#ifdef TRACY_ENABLE
	tracy::SetThreadName ("Writer");
#endif

	std::vector<Message> outMessages;
	std::vector<Socket::IOVector> iov;
	unsigned offset = 0;

	// preallocate outMessages
	outMessages.reserve (128);

	// preallocate iov
	iov.reserve (128);

	EventWaiter waiter;
	if (!waiter.add (m_writerEvent))
	{
		terminate ();
		return;
	}

	while (!m_quit.load (std::memory_order_relaxed)) [[likely]]
	{
		{
			auto const lock = std::scoped_lock (m_outputQueueMutex);
			for (auto &item : m_outputQueue)
				outMessages.emplace_back (std::move (item));

			m_outputQueue.clear ();
		}

		iov.clear ();
		if (iov.capacity () < outMessages.size ()) [[unlikely]]
			iov.reserve (outMessages.size ());

		unsigned startOffset = offset;
		for (auto const &message : outMessages)
		{
			auto const span = message.span ();
			assert (span.size () > startOffset);

			iov.emplace_back (&span[startOffset], span.size () - startOffset);
			startOffset = 0;
		}

		if (!iov.empty ()) [[likely]]
		{
			auto rc = m_sock.writev (iov);
			if (rc < 0) [[unlikely]]
				break;

			unsigned startOffset = offset;

			auto it = std::begin (outMessages);
			while (rc > 0)
			{
				assert (it != std::end (outMessages));
				auto const size = it->sizeWithHeader ();
				auto const rem  = size - startOffset;

				if (rc < rem) [[unlikely]]
				{
					// partial write
					ZoneScopedNS ("partial write", 16);
					offset += static_cast<unsigned> (rc);
					warning ("Partial write\n");
					continue;
				}

				rc -= rem;
				startOffset = 0;

				++it;
			}

			if (it != std::begin (outMessages)) [[likely]]
				outMessages.erase (std::begin (outMessages), it);

			continue;
		}

		m_writerIdle.store (true, std::memory_order_relaxed);
		m_writerIdleCv.notify_all ();

		// no data to write; so wait for signal
		auto const event = waiter.wait ();
		if (!event) [[unlikely]]
			break;

		event->clear ();
	}

	terminate ();
}
