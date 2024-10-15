#include "BotContext.h"

#include "BotManagerImpl.h"
using namespace rlbot::detail;

///////////////////////////////////////////////////////////////////////////
BotContext::~BotContext () noexcept
{
	// signal thread to exit
	terminate ();

	m_thread.join ();
}

BotContext::BotContext (unsigned const index_,
    std::unique_ptr<Bot> bot_,
    Message fieldInfo_,
    Message matchSettings_,
    BotManagerImpl &manager_) noexcept
    : m_manager (manager_),
      m_bot (std::move (bot_)),
      m_fieldInfo (std::move (fieldInfo_)),
      m_matchSettings (std::move (matchSettings_)),
      m_index (index_)
{
	// start thread
	m_thread = std::thread (&BotContext::service, this);
}

void BotContext::service () noexcept
{
	/// @todo
	// setThreadAffinity (m_index + 2);

#ifdef TRACY_ENABLE
	{
		char name[32];
		std::sprintf (name, "Bot %d\n", m_index);
		tracy::SetThreadName (name);
	}
#endif

	std::vector<Message> matchComms;
	rlbot::flat::PlayerInputT input{};
	input.player_index     = m_index;
	input.controller_state = std::make_unique<rlbot::flat::ControllerState> ();

	// preallocate matchComms
	m_matchComms.reserve (128);
	matchComms.reserve (128);

	EventWaiter waiter;
	if (!waiter.add (m_event))
		return;

	while (!m_quit.load (std::memory_order_relaxed))
	{
		// wait for a processable game packet, match comms, or quit
		auto lock = std::unique_lock (m_mutex, std::defer_lock);
		while (!m_quit.load (std::memory_order_relaxed))
		{
			lock.lock ();
			if (!m_matchComms.empty () || (m_gamePacket && m_matchSettings && m_fieldInfo))
				break;
			lock.unlock ();

			auto const event = waiter.wait ();
			if (!event)
				return;

			event->clear ();
		}

		if (m_quit.load (std::memory_order_relaxed))
			return;

		// collect match comms
		assert (lock.owns_lock ());
		if (!m_matchComms.empty ())
		{
			std::ranges::move (m_matchComms, std::back_inserter (matchComms));
			m_matchComms.clear ();
		}

		// collect game data
		auto const gamePacketBuf     = std::move (m_gamePacket);
		auto const ballPredictionBuf = m_ballPrediction;
		auto const fieldInfoBuf      = m_fieldInfo;
		auto const matchSettingsBuf  = m_matchSettings;

		// finished grabbing the data we need
		lock.unlock ();

		// process match comms first
		for (auto const &matchComm : matchComms)
			m_bot->matchComm (matchComm.flatbuffer<rlbot::flat::MatchComm> ());
		matchComms.clear ();

		// process game packet next
		auto const gamePacket     = gamePacketBuf.flatbuffer<rlbot::flat::GamePacket> ();
		auto const ballPrediction = ballPredictionBuf.flatbuffer<rlbot::flat::BallPrediction> ();
		auto const fieldInfo      = fieldInfoBuf.flatbuffer<rlbot::flat::FieldInfo> ();
		auto const matchSettings  = matchSettingsBuf.flatbuffer<rlbot::flat::MatchSettings> ();
		if (gamePacket && fieldInfo && matchSettings && gamePacket->players ()->size () > m_index)
		{
			ZoneScopedNS ("bot", 16);
			*input.controller_state =
			    m_bot->getOutput (gamePacket, ballPrediction, fieldInfo, matchSettings);

			// check for quit in case getOutput() took a while
			if (m_quit.load (std::memory_order_relaxed))
				break;

			m_manager.enqueueMessage (input);
		}

		// collect match comms
		auto const matchComms = m_bot->getMatchComms ();
		if (matchComms.has_value ())
		{
			for (auto const &matchComm : matchComms.value ())
			{
				assert (matchComm.index == m_index);
				assert (matchComm.team == m_bot->team);

				m_manager.enqueueMessage (matchComm);
			}
		}

		// collect render messages
		auto const renderMessages = m_bot->getRenderMessages ();
		if (renderMessages.has_value () && matchSettings) // && matchSettings->enable_rendering ())
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
		if (gameState.has_value () && matchSettings && matchSettings->enable_state_setting ())
			m_manager.enqueueMessage (gameState.value ());
	}
}

void BotContext::terminate () noexcept
{
	m_quit.store (true, std::memory_order_relaxed);
	m_event.signal ();
}

void BotContext::setGamePacket (Message gamePacket_) noexcept
{
	assert (gamePacket_.type () == MessageType::GamePacket);

	{
		auto const lock = std::scoped_lock (m_mutex);
		m_gamePacket    = std::move (gamePacket_);
	}

	// trigger processing
	m_event.signal ();
}

void BotContext::setBallPrediction (Message ballPrediction_) noexcept
{
	assert (ballPrediction_.type () == MessageType::BallPrediction);

	auto const lock  = std::scoped_lock (m_mutex);
	m_ballPrediction = std::move (ballPrediction_);
}

void rlbot::detail::BotContext::addMatchComm (Message matchComm_) noexcept
{
	assert (matchComm_.type () == MessageType::MatchComm);

	auto const comm = matchComm_.flatbuffer<rlbot::flat::MatchComm> ();
	assert (comm);

	// don't handle message to self
	if (comm->index () == m_bot->index)
		return;

	// don't handle team-only message to other team
	if (comm->team_only () && comm->team () != m_bot->team)
		return;

	{
		auto const lock = std::scoped_lock (m_mutex);
		m_matchComms.emplace_back (std::move (matchComm_));
	}

	// trigger processing
	m_event.signal ();
}
