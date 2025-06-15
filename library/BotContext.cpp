#include "BotContext.h"

#include "Log.h"
#include "TracyHelper.h"

#include <chrono>

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
    Message controllableTeamInfo_,
    Message fieldInfo_,
    Message matchConfiguration_,
    Client &connection_) noexcept
    : indices (std::move (indices_)),
      m_connection (connection_),
      m_bot (std::move (bot_)),
      m_intialized (m_intializedPromise.get_future ()),
      m_controllableTeamInfoMessage (std::move (controllableTeamInfo_)),
      m_fieldInfoMessage (std::move (fieldInfo_)),
      m_matchConfigurationMessage (std::move (matchConfiguration_))
{
	// decode controllable team info and field info and match settings
	m_controllableTeamInfo =
	    m_controllableTeamInfoMessage.corePacket ()->message_as_ControllableTeamInfo ();
	m_fieldInfo = m_fieldInfoMessage.corePacket ()->message_as_FieldInfo ();
	m_matchConfiguration =
	    m_matchConfigurationMessage.corePacket ()->message_as_MatchConfiguration ();

	assert (m_controllableTeamInfo && m_fieldInfo && m_matchConfiguration);

	// preallocate matchComms
	m_matchCommsIn.reserve (128);
	m_matchCommsWork.reserve (128);

	// preallocate player input
	m_input = std::make_unique<rlbot::flat::ControllerState> ();
}

void BotContext::initialize () noexcept
{
	m_bot->initialize (m_controllableTeamInfo, m_fieldInfo, m_matchConfiguration);
	m_intializedPromise.set_value ();
}

void rlbot::detail::BotContext::waitInitialized () noexcept
{
	m_intialized.get ();
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
		m_bot->matchComm (matchComm.corePacket ()->message_as_MatchComm ());
	m_matchCommsWork.clear ();

	// process game packet next
	if (gamePacketMessage)
	{
		auto const gamePacket = gamePacketMessage.corePacket ()->message_as_GamePacket ();
		auto const ballPrediction =
		    ballPredictionMessage
		        ? ballPredictionMessage.corePacket ()->message_as_BallPrediction ()
		        : nullptr;
		{
			ZoneScopedNS ("bot update", 16);
			m_bot->update (gamePacket, ballPrediction);
		}

		rlbot::flat::InterfacePacketT interfacePacket;
		interfacePacket.message.Set (rlbot::flat::PlayerInputT{});

		auto const playerInput        = interfacePacket.message.AsPlayerInput ();
		playerInput->controller_state = std::move (m_input);

		for (auto const &index : this->indices)
		{
			if (gamePacket->players ()->size () <= index)
				continue;

			ZoneScopedNS ("bot output", 16);
			playerInput->player_index      = index;
			*playerInput->controller_state = m_bot->getOutput (index);
			m_connection.sendInterfacePacket (interfacePacket);
		}

		m_input = std::move (playerInput->controller_state);
	}

	// collect output match comms
	auto matchCommsOut = m_bot->getMatchComms ();
	if (matchCommsOut.has_value ())
	{
		rlbot::flat::InterfacePacketT interfacePacket;
		for (auto &matchComm : matchCommsOut.value ())
		{
			assert (indices.contains (matchComm.index));
			assert (matchComm.team == m_bot->team);

			interfacePacket.message.Set (std::move (matchComm));
			m_connection.sendInterfacePacket (interfacePacket);
		}
	}

	// collect render messages
	auto const renderMessages = m_bot->getRenderMessages ();
	if (renderMessages.has_value () &&
	    m_matchConfiguration->enable_rendering () != rlbot::flat::DebugRendering::AlwaysOff)
	{
		for (auto const &[group, renderMessages] : renderMessages.value ())
		{
			if (renderMessages.empty ())
			{
				// empty group indicates remove
				rlbot::flat::RemoveRenderGroupT removeRenderGroup;
				removeRenderGroup.id = group;
				m_connection.sendRemoveRenderGroup (std::move (removeRenderGroup));
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

				m_connection.sendRenderGroup (std::move (renderGroup));
			}
		}
	}

	// collect desired game state
	auto const gameState = m_bot->getDesiredGameState ();
	if (gameState.has_value () && m_matchConfiguration->enable_state_setting ())
		m_connection.sendDesiredGameState (std::move (gameState.value ()));

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
	assert (gamePacket_.corePacket (true));
	assert (gamePacket_.corePacket ()->message_type () == rlbot::flat::CoreMessage::GamePacket);

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
	assert (ballPrediction_.corePacket (true));
	assert (
	    ballPrediction_.corePacket ()->message_type () == rlbot::flat::CoreMessage::BallPrediction);

	auto const lock         = std::scoped_lock (m_mutex);
	m_ballPredictionMessage = std::move (ballPrediction_);
}

void rlbot::detail::BotContext::addMatchComm (Message matchComm_, bool const notify_) noexcept
{
	ZoneScopedNS ("addMatchComm", 16);
	assert (matchComm_.corePacket (true));
	assert (matchComm_.corePacket ()->message_type () == rlbot::flat::CoreMessage::MatchComm);

	auto const comm = matchComm_.corePacket ()->message_as_MatchComm ();
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
		std::sprintf (name, "Bot %d\n", indices.empty () ? -1 : *std::begin (indices));
		tracy::SetThreadName (name);
	}
#endif

	initialize ();

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
