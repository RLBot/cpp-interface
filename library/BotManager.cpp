#include <rlbot/BotManager.h>

#include <rlbot/Bot.h>

#include "BotContext.h"
#include "Log.h"
#include "TracyHelper.h"

#include <cinttypes>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <ranges>
#include <unordered_set>
#include <vector>

using namespace rlbot;
using namespace rlbot::detail;

///////////////////////////////////////////////////////////////////////////
class rlbot::detail::BotManagerImpl
{
public:
	~BotManagerImpl () noexcept;

	/// @brief Parameterized constructor
	/// @param connection_ Connection to the RLBot server
	/// @param batchHivemind_ Batch hivemind
	/// @param spawn_ Bot spawning function
	BotManagerImpl (Client &connection_,
	    bool const batchHivemind_,
	    std::unique_ptr<Bot> (
	        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept;

	/// @brief Spawn bots
	void spawnBots () noexcept;

	/// @brief Clear bots
	void clearBots () noexcept;

	/// @brief Connection to the RLBot server
	Client &connection;

	/// @brief Bot spawner
	std::unique_ptr<Bot> (&spawn) (std::unordered_set<unsigned>, unsigned, std::string) noexcept;

	/// @brief Bots
	std::deque<BotContext> bots;

	/// @brief Controllable team info message
	Message controllableTeamInfoMessage;
	/// @brief Field info message
	Message fieldInfoMessage;
	/// @brief Match settings message
	Message matchConfigurationMessage;

	/// @brief Batch hivemind
	bool const batchHivemind;
};

BotManagerImpl::~BotManagerImpl () noexcept = default;

BotManagerImpl::BotManagerImpl (Client &connection_,
    bool const batchHivemind_,
    std::unique_ptr<Bot> (
        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept
    : connection (connection_), spawn (spawn_), batchHivemind (batchHivemind_)
{
}

/// @brief Spawn bots
void BotManagerImpl::spawnBots () noexcept
{
	if (!controllableTeamInfoMessage || !fieldInfoMessage || !matchConfigurationMessage)
		return;

	auto const controllableTeamInfo =
	    controllableTeamInfoMessage.corePacket ()->message_as_ControllableTeamInfo ();
	auto const fieldInfo = fieldInfoMessage.corePacket ()->message_as_FieldInfo ();
	auto const matchConfiguration =
	    matchConfigurationMessage.corePacket ()->message_as_MatchConfiguration ();
	if (!controllableTeamInfo || !fieldInfo || !matchConfiguration)
		return;

	clearBots ();

	assert (bots.empty ());

	auto const configs = matchConfiguration->player_configurations ();

	auto const team = controllableTeamInfo->team ();

	std::unordered_set<unsigned> botIndices;
	std::string name;
	for (auto const &controllableInfo : *controllableTeamInfo->controllables ())
	{
		// find player in match settings with matching spawn id
		auto const player = std::ranges::find (*configs,
		    controllableInfo->identifier (),
		    [] (auto const &config_) { return config_->player_id (); });
		if (player == std::end (*configs))
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

		auto const customBot = player->variety_as_CustomBot ();
		if (!customBot)
		{
			warning ("ControllableInfo player is not a bot\n");
			continue;
		}

		botIndices.emplace (index);

		if (batchHivemind)
		{
			if (!name.empty ())
				name = customBot->name ()->str ();
			continue; // defer bot creation
		}

		auto bot = spawn (botIndices, team, customBot->name ()->str ());

		auto loadout = bot->getLoadout (index);

		bots.emplace_back (std::move (botIndices),
		    std::move (bot),
		    controllableTeamInfoMessage,
		    fieldInfoMessage,
		    matchConfigurationMessage,
		    connection);

		if (!loadout.has_value ())
			continue;

		rlbot::flat::SetLoadoutT loadoutMessage{};
		loadoutMessage.index = index;
		loadoutMessage.loadout =
		    std::make_unique<rlbot::flat::PlayerLoadoutT> (std::move (loadout.value ()));
		connection.sendSetLoadout (std::move (loadoutMessage));
	}

	if (batchHivemind)
	{
		auto bot = spawn (botIndices, team, std::move (name));

		for (auto const &index : botIndices)
		{
			auto loadout = bot->getLoadout (index);
			if (!loadout.has_value ())
				continue;

			rlbot::flat::SetLoadoutT loadoutMessage{};
			loadoutMessage.index = index;
			loadoutMessage.loadout =
			    std::make_unique<rlbot::flat::PlayerLoadoutT> (std::move (loadout.value ()));
			connection.sendSetLoadout (std::move (loadoutMessage));
		}

		bots.emplace_back (std::move (botIndices),
		    std::move (bot),
		    controllableTeamInfoMessage,
		    fieldInfoMessage,
		    matchConfigurationMessage,
		    connection);
	}

	// handle the first bot on the reader thread
	for (auto &bot : bots | std::views::drop (1))
		bot.startService ();

	if (!bots.empty ())
		std::begin (bots)->initialize ();

	for (auto &bot : bots)
		bot.waitInitialized ();

	connection.sendInitComplete ({});
}

/// @brief Clear bots
void BotManagerImpl::clearBots () noexcept
{
	for (auto &bot : bots | std::views::drop (1))
		bot.terminate ();

	bots.clear ();
}

///////////////////////////////////////////////////////////////////////////
BotManagerBase::~BotManagerBase () noexcept
{
	join ();
}

BotManagerBase::BotManagerBase (bool const batchHivemind_,
    std::unique_ptr<Bot> (
        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept
    : m_impl (std::make_unique<BotManagerImpl> (*this, batchHivemind_, spawn_))
{
}

bool BotManagerBase::connect (char const *const host_,
    char const *const service_,
    char const *agentId_,
    bool const ballPrediction_) noexcept
{
	if (connected ())
	{
		error ("Already connected\n");
		return false;
	}

	if (!agentId_)
	{
		agentId_ = std::getenv ("RLBOT_AGENT_ID");
		if (!agentId_ || std::strlen (agentId_) == 0)
		{
			error ("No agent id provided\n");
			return false;
		}
	}

	if (!Client::connect (host_, service_))
		return false;

	sendConnectionSettings ({
	    .agent_id               = agentId_,
	    .wants_ball_predictions = ballPrediction_,
	    .wants_comms            = true,
	    .close_between_matches  = true,
	});

	return true;
}

void BotManagerBase::handleMessage (detail::Message &message_) noexcept
{
	assert (message_);

	auto const packet = message_.corePacket (true);
	if (!packet) [[unlikely]]
	{
		error ("Invalid core packet received\n");
		return;
	}

	switch (packet->message_type ())
	{
	case rlbot::flat::CoreMessage::BallPrediction:
	case rlbot::flat::CoreMessage::GamePacket:
		debug ("Received %s\n", rlbot::flat::EnumNameCoreMessage (packet->message_type ()));
		break;

	default:
		info ("Received %s\n", rlbot::flat::EnumNameCoreMessage (packet->message_type ()));
		break;
	}

	if (packet->message_type () == rlbot::flat::CoreMessage::DisconnectSignal) [[unlikely]]
	{
		terminate ();
		return;
	}

	if (packet->message_type () == rlbot::flat::CoreMessage::ControllableTeamInfo) [[unlikely]]
	{
		ZoneScopedNS ("handle ControllableTeamInfo", 16);

		m_impl->controllableTeamInfoMessage = message_;
		m_impl->spawnBots ();
		return;
	}

	if (packet->message_type () == rlbot::flat::CoreMessage::FieldInfo) [[unlikely]]
	{
		ZoneScopedNS ("handle FieldInfo", 16);

		m_impl->fieldInfoMessage = message_;
		m_impl->spawnBots ();
		return;
	}

	if (packet->message_type () == rlbot::flat::CoreMessage::MatchConfiguration) [[unlikely]]
	{
		ZoneScopedNS ("handle MatchConfiguration", 16);

		m_impl->matchConfigurationMessage = message_;
		m_impl->spawnBots ();
		return;
	}

	if (m_impl->bots.empty ()) [[unlikely]]
		return;

	if (packet->message_type () == rlbot::flat::CoreMessage::BallPrediction) [[likely]]
	{
		ZoneScopedNS ("handle BallPrediction", 16);

		for (auto &bot : m_impl->bots)
			bot.setBallPrediction (message_);
		return;
	}

	if (packet->message_type () == rlbot::flat::CoreMessage::GamePacket) [[likely]]
	{
		FrameMark;
		ZoneScopedNS ("handle GamePacket", 16);

		for (auto &bot : m_impl->bots | std::views::drop (1))
			bot.setGamePacket (message_, true);

		// handle the first bot on the reader thread
		auto &bot = m_impl->bots.front ();
		bot.setGamePacket (message_, false);
		bot.loopOnce ();

		return;
	}

	if (packet->message_type () == rlbot::flat::CoreMessage::MatchComm) [[unlikely]]
	{
		ZoneScopedNS ("handle MatchComm", 16);

		auto const matchComm = packet->message_as_MatchComm ();
		assert (matchComm);

		auto const team  = matchComm->team ();
		auto const index = matchComm->index ();

		if (auto const display = matchComm->display (); display && !display->empty ())
			info ("\tTeam %" PRIu32 " Index %" PRIu32 ": %s\n", team, index, display->c_str ());

		if (auto const content = matchComm->content (); content && !content->empty ())
		{
			auto const &data = *content;
			auto const size  = content->size ();

			std::vector<char> buf (2 * size + 1);
			auto const p = buf.data ();

			for (std::size_t i = 0; i < size; ++i)
				std::sprintf (p + 2 * i, "%02x", data[i]);

			info ("\tTeam %" PRIu32 " Index %" PRIu32 ": %s\n", team, index, p);
		}

		for (auto &bot : m_impl->bots | std::views::drop (1))
			bot.addMatchComm (message_, true);

		// handle the first bot on the reader thread
		auto &bot = m_impl->bots.front ();
		bot.loopOnce ();

		return;
	}
}
