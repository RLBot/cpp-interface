#include "BotContext.h"

#include "BotManagerImpl.h"
#include "Log.h"

using namespace rlbot::detail;

using namespace std::chrono_literals;

///////////////////////////////////////////////////////////////////////////
BotContext::~BotContext () noexcept
{
	// signal thread to exit
	terminate ();

	if (m_thread.joinable ())
		m_thread.join ();
}

BotContext::BotContext (std::unordered_set<unsigned> indices_,
    std::unique_ptr<Bot> bot_,
    Message fieldInfo_,
    Message matchConfig_,
    BotManagerImpl &manager_) noexcept
    : indices (std::move (indices_)),
      m_manager (manager_),
      m_bot (std::move (bot_)),
      m_fieldInfoMessage (std::move (fieldInfo_)),
      m_matchConfigMessage (std::move (matchConfig_))
{
	// decode field info and match config
	m_fieldInfo     = m_fieldInfoMessage.flatbuffer<rlbot::flat::FieldInfo> ();
	m_matchConfig = m_matchConfigMessage.flatbuffer<rlbot::flat::MatchConfiguration> ();

	assert (m_fieldInfo && m_matchConfig);

	// preallocate matchComms
	m_matchCommsIn.reserve (128);
	m_matchCommsWork.reserve (128);

	// preallocate player input
	m_input.controller_state = std::make_unique<rlbot::flat::ControllerState> ();
}

void rlbot::detail::BotContext::startService () noexcept
{
	// start service thread
	m_thread = std::thread (&BotContext::service, this);
}

void rlbot::detail::BotContext::loopOnce () noexcept
{
	auto lock = std::unique_lock (m_mutex);
	serviceLoop (lock);
}

bool BotContext::serviceLoop (std::unique_lock<std::mutex> &lock_) noexcept
{
	ZoneScopedNS ("serviceLoop", 16);

	// check if any work is available
	if (m_matchCommsIn.empty () && !m_gamePacketMessage)
		return false;

	// collect match comms
	std::swap (m_matchCommsIn, m_matchCommsWork);

	// collect game data
	auto const gamePacketMessage     = std::move (m_gamePacketMessage);
	auto const ballPredictionMessage = m_ballPredictionMessage;

	// finished grabbing the data we need
	lock_.unlock ();

	// process match comms first
	for (auto const &matchComm : m_matchCommsWork)
		m_bot->matchComm (matchComm.flatbuffer<rlbot::flat::MatchComm> ());
	m_matchCommsWork.clear ();

	// process game packet next
	auto const gamePacket     = gamePacketMessage.flatbuffer<rlbot::flat::GamePacket> ();
	auto const ballPrediction = ballPredictionMessage.flatbuffer<rlbot::flat::BallPrediction> ();
	if (gamePacket)
	{
		{
			ZoneScopedNS ("bot update", 16);
			m_bot->update (gamePacket, ballPrediction, m_fieldInfo, m_matchConfig);
		}

		for (auto const &index : this->indices)
		{
			if (gamePacket->players ()->size () <= index)
				continue;

			ZoneScopedNS ("bot output", 16);
			m_input.player_index      = index;
			*m_input.controller_state = m_bot->getOutput (index);
			m_manager.enqueueMessage (m_input);
		}
	}

	// collect output match comms
	auto const matchCommsOut = m_bot->getMatchComms ();
	if (matchCommsOut.has_value ())
	{
		for (auto const &matchComm : matchCommsOut.value ())
		{
			assert (indices.contains (matchComm.index));
			assert (matchComm.team == m_bot->team);

			m_manager.enqueueMessage (matchComm);
		}
	}

	// collect render messages
	auto const renderMessages = m_bot->getRenderMessages ();
	if (renderMessages.has_value () && m_matchConfig->enable_rendering ())
	{
		for (auto const &[group, renderMessages] : renderMessages.value ())
		{
			if (renderMessages.empty ())
			{
				// empty group indicates remove
				rlbot::flat::RemoveRenderGroupT removeRenderGroup;
				removeRenderGroup.id = group;
				m_manager.enqueueMessage (removeRenderGroup);
			}
			else
			{
				rlbot::flat::RenderGroupT renderGroup;
				renderGroup.id = group;
				renderGroup.render_messages.reserve (renderMessages.size ());
				for (auto &message : renderMessages)
				{
					renderGroup.render_messages.emplace_back (
					    std::make_unique<rlbot::flat::RenderMessageT> (std::move (message)));
				}

				m_manager.enqueueMessage (renderGroup);
			}
		}
	}

	// collect desired game state
	auto const gameState = m_bot->getDesiredGameState ();
	if (gameState.has_value () && m_matchConfig->enable_state_setting ())
		m_manager.enqueueMessage (gameState.value ());

	lock_.lock ();
	return true;
}

void BotContext::terminate () noexcept
{
	m_quit.store (true, std::memory_order_relaxed);
	m_cv.notify_one ();
}

void BotContext::setGamePacket (Message gamePacket_, bool const notify_) noexcept
{
	ZoneScopedNS ("setGamePacket", 16);
	assert (gamePacket_.type () == MessageType::GamePacket);

	{
		auto const lock     = std::scoped_lock (m_mutex);
		m_gamePacketMessage = std::move (gamePacket_);
	}

	// trigger processing
	if (notify_)
		m_cv.notify_one ();
}

void BotContext::setBallPrediction (Message ballPrediction_) noexcept
{
	assert (ballPrediction_.type () == MessageType::BallPrediction);

	auto const lock         = std::scoped_lock (m_mutex);
	m_ballPredictionMessage = std::move (ballPrediction_);
}

void rlbot::detail::BotContext::addMatchComm (Message matchComm_, bool const notify_) noexcept
{
	ZoneScopedNS ("addMatchComm", 16);
	assert (matchComm_.type () == MessageType::MatchComm);

	auto const comm = matchComm_.flatbuffer<rlbot::flat::MatchComm> ();
	assert (comm);

	// don't handle message to self
	if (m_bot->indices.contains (comm->index ()))
		return;

	// don't handle team-only message to other team
	if (comm->team_only () && comm->team () != m_bot->team)
		return;

	{
		auto const lock = std::scoped_lock (m_mutex);
		m_matchCommsIn.emplace_back (std::move (matchComm_));
	}

	// trigger processing
	if (notify_)
		m_cv.notify_one ();
}

void BotContext::service () noexcept
{
#ifdef TRACY_ENABLE
	{
		char name[32];
		std::sprintf (name, "Bot %d\n", index);
		tracy::SetThreadName (name);
	}
#endif

	while (!m_quit.load (std::memory_order_relaxed))
	{
		// wait for a processable game packet, match comms, or quit
		auto lock = std::unique_lock (m_mutex);
		while (!serviceLoop (lock) && !m_quit.load (std::memory_order_relaxed))
		{
			// wakeup is within a few microseconds on windows :)
			ZoneScopedNS ("wait", 16);
			m_cv.wait (lock);
		}
	}
}
