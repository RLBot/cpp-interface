#include <rlbot/BotManager.h>

#include <cstdio>

#define USE_HIVEMIND true

int main (int argc_, char *argv_[])
{
	if (argc_ <= 2)
	{
		std::fprintf (stderr, "Usage: %s <addr> <port>\n", argv_[0]);
		return EXIT_FAILURE;
	}

	auto const host = argv_[1];
	auto const port = argv_[2];

	/// map names at https://github.com/VirxEC/python-interface/blob/master/rlbot/utils/maps.py
	rlbot::flat::MatchSettingsT ms{};
	ms.game_map_upk            = "Stadium_P";
	ms.game_mode               = rlbot::flat::GameMode::Soccer;
	ms.skip_replays            = true;
	ms.instant_start           = true;
	ms.existing_match_behavior = rlbot::flat::ExistingMatchBehavior::Restart;
	ms.enable_rendering        = true;
	ms.enable_state_setting    = true;

	for (unsigned i = 0; i < 1; ++i)
	{
		auto player = std::make_unique<rlbot::flat::PlayerConfigurationT> ();
		player->variety.Set (rlbot::flat::RLBotT{});
		player->team     = i % 2;
		player->name     = "ExampleBot";
		player->agent_id = "RLBotCPP/ExampleBot";
		player->hivemind = USE_HIVEMIND;
		ms.player_configurations.emplace_back (std::move (player));
	}

	if (!rlbot::BotManagerBase::startMatch (host, port, ms))
		return EXIT_FAILURE;

	if constexpr (USE_HIVEMIND)
		std::printf ("Please run two ExampleBot processes (one for each team)\n");
	else
		std::printf ("Please run one ExampleBot process per bot\n");

	std::printf (
	    "Set the RLBOT_AGENT_ID=\"RLBotCPP/ExampleBot\" environment variable when launching\n");
}
