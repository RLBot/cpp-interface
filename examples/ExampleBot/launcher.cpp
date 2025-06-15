#include <rlbot/Client.h>

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <direct.h>
#else
#include <unistd.h>
#endif

#include <chrono>
#include <cstdio>
#include <thread>
using namespace std::chrono_literals;

#define USE_HIVEMIND true

int main (int argc_, char *argv_[])
{
	std::setvbuf (stdout, nullptr, _IONBF, 0);
	std::setvbuf (stderr, nullptr, _IONBF, 0);

	// MSVC and glibc support dynamically allocated cwd
	// If you use a different libc this won't work!
	auto const cwd = ::getcwd (nullptr, 0);
	if (!cwd)
	{
		std::fprintf (stderr, "Failed to get current directory\n");
		return EXIT_FAILURE;
	}

	if (argc_ <= 2)
	{
		std::fprintf (stderr, "Usage: %s <addr> <port>\n", argv_[0]);
		std::free (cwd);
		return EXIT_FAILURE;
	}

	auto const host = argv_[1];
	auto const port = argv_[2];

	auto connection = rlbot::Client{};
	if (!connection.connect (host, port))
	{
		std::fprintf (stderr, "Failed to connect to [%s]:%s\n", host, port);
		std::free (cwd);
		return EXIT_FAILURE;
	}

	/// map names at https://github.com/RLBot/python-interface/blob/master/rlbot/utils/maps.py
	rlbot::flat::MatchConfigurationT ms{};
	ms.auto_start_agents       = true;
	ms.wait_for_agents         = true;
	ms.game_map_upk            = "Stadium_P";
	ms.game_mode               = rlbot::flat::GameMode::Soccar;
	ms.skip_replays            = true;
	ms.instant_start           = true;
	ms.existing_match_behavior = rlbot::flat::ExistingMatchBehavior::Restart;
	ms.enable_rendering        = rlbot::flat::DebugRendering::OnByDefault;
	ms.enable_state_setting    = true;
	ms.auto_save_replay        = false;
	ms.freeplay                = false;

	for (unsigned i = 0; i < 4; ++i)
	{
		rlbot::flat::CustomBotT bot{};
		bot.root_dir = cwd;
#ifdef _WIN32
		bot.run_command = "ExampleBot.exe";
#else
		bot.run_command = "./ExampleBot";
#endif
		bot.name     = "ExampleBot";
		bot.agent_id = "RLBotCPP/ExampleBot";
		bot.hivemind = USE_HIVEMIND;

		auto player = std::make_unique<rlbot::flat::PlayerConfigurationT> ();
		player->variety.Set (std::move (bot));
		player->team = i % 2;
		ms.player_configurations.emplace_back (std::move (player));
	}

	connection.sendMatchConfiguration (std::move (ms));
	connection.sendDisconnectSignal ({});

	std::free (cwd);

	if constexpr (USE_HIVEMIND)
		std::printf ("Please run two ExampleBot processes (one for each team)\n");
	else
		std::printf ("Please run one ExampleBot process per bot\n");

	std::printf (
	    "Set the RLBOT_AGENT_ID=\"RLBotCPP/ExampleBot\" environment variable when launching\n");
}
