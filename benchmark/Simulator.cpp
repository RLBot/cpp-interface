#include "Simulator.h"

#include <Log.h>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numeric>
#include <optional>

using namespace rlbot::detail;

namespace
{
rlbot::flat::Vector3 fromRocketSim (RocketSim::Vec const &vec_) noexcept
{
	return {vec_.x, vec_.y, vec_.z};
}

rlbot::flat::Rotator fromRocketSim (RocketSim::RotMat const &rotMat_) noexcept
{
	auto const angle = RocketSim::Angle::FromRotMat (rotMat_);

	return {angle.pitch, angle.yaw, angle.roll};
}

// https://github.com/RLGym/rlgym-tools/blob/main/rlgym_tools/rocket_league/misc/car_bodies.py
std::unordered_map<unsigned, RocketSim::CarConfig const *> const carConfigs = {
    {21, &RocketSim::CAR_CONFIG_OCTANE},
    {22, &RocketSim::CAR_CONFIG_BREAKOUT},
    {23, &RocketSim::CAR_CONFIG_OCTANE},
    {24, &RocketSim::CAR_CONFIG_PLANK},
    {25, &RocketSim::CAR_CONFIG_OCTANE},
    {26, &RocketSim::CAR_CONFIG_OCTANE},
    {27, &RocketSim::CAR_CONFIG_OCTANE},
    {28, &RocketSim::CAR_CONFIG_HYBRID},
    {29, &RocketSim::CAR_CONFIG_DOMINUS},
    {30, &RocketSim::CAR_CONFIG_MERC},
    {31, &RocketSim::CAR_CONFIG_HYBRID},
    {402, &RocketSim::CAR_CONFIG_OCTANE},
    {403, &RocketSim::CAR_CONFIG_DOMINUS},
    {404, &RocketSim::CAR_CONFIG_OCTANE},
    {523, &RocketSim::CAR_CONFIG_OCTANE},
    {597, &RocketSim::CAR_CONFIG_DOMINUS},
    {600, &RocketSim::CAR_CONFIG_DOMINUS},
    {607, &RocketSim::CAR_CONFIG_OCTANE},
    {625, &RocketSim::CAR_CONFIG_OCTANE},
    {723, &RocketSim::CAR_CONFIG_OCTANE},
    {803, &RocketSim::CAR_CONFIG_PLANK},
    {1018, &RocketSim::CAR_CONFIG_DOMINUS},
    {1159, &RocketSim::CAR_CONFIG_HYBRID},
    {1171, &RocketSim::CAR_CONFIG_DOMINUS},
    {1172, &RocketSim::CAR_CONFIG_OCTANE},
    {1286, &RocketSim::CAR_CONFIG_DOMINUS},
    {1295, &RocketSim::CAR_CONFIG_OCTANE},
    {1300, &RocketSim::CAR_CONFIG_OCTANE},
    {1317, &RocketSim::CAR_CONFIG_HYBRID},
    {1416, &RocketSim::CAR_CONFIG_BREAKOUT},
    {1475, &RocketSim::CAR_CONFIG_OCTANE},
    {1478, &RocketSim::CAR_CONFIG_OCTANE},
    {1533, &RocketSim::CAR_CONFIG_OCTANE},
    {1568, &RocketSim::CAR_CONFIG_OCTANE},
    {1603, &RocketSim::CAR_CONFIG_PLANK},
    {1623, &RocketSim::CAR_CONFIG_OCTANE},
    {1624, &RocketSim::CAR_CONFIG_HYBRID},
    {1675, &RocketSim::CAR_CONFIG_DOMINUS},
    {1689, &RocketSim::CAR_CONFIG_DOMINUS},
    {1691, &RocketSim::CAR_CONFIG_PLANK},
    {1856, &RocketSim::CAR_CONFIG_HYBRID},
    {1883, &RocketSim::CAR_CONFIG_DOMINUS},
    {1894, &RocketSim::CAR_CONFIG_BREAKOUT},
    {1919, &RocketSim::CAR_CONFIG_PLANK},
    {1932, &RocketSim::CAR_CONFIG_BREAKOUT},
    {2070, &RocketSim::CAR_CONFIG_DOMINUS},
    {2268, &RocketSim::CAR_CONFIG_DOMINUS},
    {2269, &RocketSim::CAR_CONFIG_HYBRID},
    {2298, &RocketSim::CAR_CONFIG_DOMINUS},
    {2313, &RocketSim::CAR_CONFIG_OCTANE},
    {2665, &RocketSim::CAR_CONFIG_OCTANE},
    {2666, &RocketSim::CAR_CONFIG_DOMINUS},
    {2853, &RocketSim::CAR_CONFIG_OCTANE},
    {2919, &RocketSim::CAR_CONFIG_OCTANE},
    {2949, &RocketSim::CAR_CONFIG_OCTANE},
    {2950, &RocketSim::CAR_CONFIG_DOMINUS},
    {2951, &RocketSim::CAR_CONFIG_DOMINUS},
    {3031, &RocketSim::CAR_CONFIG_BREAKOUT},
    {3155, &RocketSim::CAR_CONFIG_DOMINUS},
    {3156, &RocketSim::CAR_CONFIG_DOMINUS},
    {3157, &RocketSim::CAR_CONFIG_DOMINUS},
    {3265, &RocketSim::CAR_CONFIG_DOMINUS},
    {3311, &RocketSim::CAR_CONFIG_BREAKOUT},
    {3426, &RocketSim::CAR_CONFIG_DOMINUS},
    {3451, &RocketSim::CAR_CONFIG_HYBRID},
    {3582, &RocketSim::CAR_CONFIG_HYBRID},
    {3594, &RocketSim::CAR_CONFIG_PLANK},
    {3614, &RocketSim::CAR_CONFIG_PLANK},
    {3622, &RocketSim::CAR_CONFIG_PLANK},
    {3702, &RocketSim::CAR_CONFIG_HYBRID},
    {3875, &RocketSim::CAR_CONFIG_DOMINUS},
    {3879, &RocketSim::CAR_CONFIG_DOMINUS},
    {3880, &RocketSim::CAR_CONFIG_DOMINUS},
    {4014, &RocketSim::CAR_CONFIG_DOMINUS},
    {4155, &RocketSim::CAR_CONFIG_DOMINUS},
    {4268, &RocketSim::CAR_CONFIG_PLANK},
    {4284, &RocketSim::CAR_CONFIG_OCTANE},
    {4318, &RocketSim::CAR_CONFIG_OCTANE},
    {4319, &RocketSim::CAR_CONFIG_OCTANE},
    {4320, &RocketSim::CAR_CONFIG_OCTANE},
    {4367, &RocketSim::CAR_CONFIG_DOMINUS},
    {4472, &RocketSim::CAR_CONFIG_DOMINUS},
    {4473, &RocketSim::CAR_CONFIG_DOMINUS},
    {4745, &RocketSim::CAR_CONFIG_DOMINUS},
    {4770, &RocketSim::CAR_CONFIG_DOMINUS},
    {4780, &RocketSim::CAR_CONFIG_MERC},
    {4781, &RocketSim::CAR_CONFIG_DOMINUS},
    {4782, &RocketSim::CAR_CONFIG_OCTANE},
    {4861, &RocketSim::CAR_CONFIG_DOMINUS},
    {4864, &RocketSim::CAR_CONFIG_DOMINUS},
    {4906, &RocketSim::CAR_CONFIG_OCTANE},
    {5020, &RocketSim::CAR_CONFIG_OCTANE},
    {5039, &RocketSim::CAR_CONFIG_OCTANE},
    {5188, &RocketSim::CAR_CONFIG_OCTANE},
    {5265, &RocketSim::CAR_CONFIG_PLANK},
    {5361, &RocketSim::CAR_CONFIG_OCTANE},
    {5470, &RocketSim::CAR_CONFIG_HYBRID},
    {5488, &RocketSim::CAR_CONFIG_HYBRID},
    {5547, &RocketSim::CAR_CONFIG_OCTANE},
    {5709, &RocketSim::CAR_CONFIG_DOMINUS},
    {5713, &RocketSim::CAR_CONFIG_OCTANE},
    {5773, &RocketSim::CAR_CONFIG_DOMINUS},
    {5823, &RocketSim::CAR_CONFIG_DOMINUS},
    {5837, &RocketSim::CAR_CONFIG_OCTANE},
    {5858, &RocketSim::CAR_CONFIG_DOMINUS},
    {5879, &RocketSim::CAR_CONFIG_HYBRID},
    {5951, &RocketSim::CAR_CONFIG_OCTANE},
    {5964, &RocketSim::CAR_CONFIG_DOMINUS},
    {5979, &RocketSim::CAR_CONFIG_DOMINUS},
    {6122, &RocketSim::CAR_CONFIG_DOMINUS},
    {6243, &RocketSim::CAR_CONFIG_BREAKOUT},
    {6244, &RocketSim::CAR_CONFIG_DOMINUS},
    {6247, &RocketSim::CAR_CONFIG_DOMINUS},
    {6260, &RocketSim::CAR_CONFIG_DOMINUS},
    {6489, &RocketSim::CAR_CONFIG_BREAKOUT},
    {6836, &RocketSim::CAR_CONFIG_DOMINUS},
    {6939, &RocketSim::CAR_CONFIG_OCTANE},
    {7012, &RocketSim::CAR_CONFIG_HYBRID},
    {7052, &RocketSim::CAR_CONFIG_PLANK},
    {7211, &RocketSim::CAR_CONFIG_DOMINUS},
    {7336, &RocketSim::CAR_CONFIG_MERC},
    {7337, &RocketSim::CAR_CONFIG_DOMINUS},
    {7338, &RocketSim::CAR_CONFIG_DOMINUS},
    {7341, &RocketSim::CAR_CONFIG_DOMINUS},
    {7343, &RocketSim::CAR_CONFIG_DOMINUS},
    {7415, &RocketSim::CAR_CONFIG_DOMINUS},
    {7477, &RocketSim::CAR_CONFIG_MERC},
    {7512, &RocketSim::CAR_CONFIG_DOMINUS},
    {7532, &RocketSim::CAR_CONFIG_DOMINUS},
    {7593, &RocketSim::CAR_CONFIG_DOMINUS},
    {7651, &RocketSim::CAR_CONFIG_BREAKOUT},
    {7696, &RocketSim::CAR_CONFIG_BREAKOUT},
    {7772, &RocketSim::CAR_CONFIG_DOMINUS},
    {7815, &RocketSim::CAR_CONFIG_MERC},
    {7890, &RocketSim::CAR_CONFIG_BREAKOUT},
    {7901, &RocketSim::CAR_CONFIG_BREAKOUT},
    {7947, &RocketSim::CAR_CONFIG_OCTANE},
    {7948, &RocketSim::CAR_CONFIG_OCTANE},
    {7979, &RocketSim::CAR_CONFIG_MERC},
    {8006, &RocketSim::CAR_CONFIG_BREAKOUT},
    {8360, &RocketSim::CAR_CONFIG_BREAKOUT},
    {8361, &RocketSim::CAR_CONFIG_BREAKOUT},
    {8383, &RocketSim::CAR_CONFIG_OCTANE},
    {8454, &RocketSim::CAR_CONFIG_DOMINUS},
    {8524, &RocketSim::CAR_CONFIG_PLANK},
    {8565, &RocketSim::CAR_CONFIG_BREAKOUT},
    {8566, &RocketSim::CAR_CONFIG_BREAKOUT},
    {8669, &RocketSim::CAR_CONFIG_BREAKOUT},
    {8806, &RocketSim::CAR_CONFIG_OCTANE},
    {8807, &RocketSim::CAR_CONFIG_OCTANE},
    {9053, &RocketSim::CAR_CONFIG_DOMINUS},
    {9084, &RocketSim::CAR_CONFIG_HYBRID},
    {9085, &RocketSim::CAR_CONFIG_HYBRID},
    {9088, &RocketSim::CAR_CONFIG_DOMINUS},
    {9089, &RocketSim::CAR_CONFIG_DOMINUS},
    {9140, &RocketSim::CAR_CONFIG_DOMINUS},
    {9357, &RocketSim::CAR_CONFIG_BREAKOUT},
    {9388, &RocketSim::CAR_CONFIG_DOMINUS},
    {9427, &RocketSim::CAR_CONFIG_HYBRID},
    {9894, &RocketSim::CAR_CONFIG_DOMINUS},
    {10044, &RocketSim::CAR_CONFIG_HYBRID},
    {10440, &RocketSim::CAR_CONFIG_DOMINUS},
    {10441, &RocketSim::CAR_CONFIG_DOMINUS},
    {10689, &RocketSim::CAR_CONFIG_MERC},
    {10694, &RocketSim::CAR_CONFIG_DOMINUS},
    {10697, &RocketSim::CAR_CONFIG_BREAKOUT},
    {10698, &RocketSim::CAR_CONFIG_BREAKOUT},
    {10805, &RocketSim::CAR_CONFIG_HYBRID},
    {10817, &RocketSim::CAR_CONFIG_BREAKOUT},
    {10822, &RocketSim::CAR_CONFIG_BREAKOUT},
    {10896, &RocketSim::CAR_CONFIG_OCTANE},
    {10897, &RocketSim::CAR_CONFIG_OCTANE},
    {10900, &RocketSim::CAR_CONFIG_OCTANE},
    {10901, &RocketSim::CAR_CONFIG_OCTANE},
    {11016, &RocketSim::CAR_CONFIG_DOMINUS},
    {11038, &RocketSim::CAR_CONFIG_BREAKOUT},
    {11095, &RocketSim::CAR_CONFIG_DOMINUS},
    {11098, &RocketSim::CAR_CONFIG_MERC},
    {11138, &RocketSim::CAR_CONFIG_HYBRID},
    {11141, &RocketSim::CAR_CONFIG_HYBRID},
    {11314, &RocketSim::CAR_CONFIG_MERC},
    {11315, &RocketSim::CAR_CONFIG_DOMINUS},
    {11336, &RocketSim::CAR_CONFIG_DOMINUS},
    {11379, &RocketSim::CAR_CONFIG_HYBRID},
    {11394, &RocketSim::CAR_CONFIG_BREAKOUT},
    {11505, &RocketSim::CAR_CONFIG_BREAKOUT},
    {11603, &RocketSim::CAR_CONFIG_OCTANE},
    {11800, &RocketSim::CAR_CONFIG_BREAKOUT},
};
}

///////////////////////////////////////////////////////////////////////////
Simulator::~Simulator () noexcept
{
	join ();

	if (m_matchEndTime > m_matchStartTime)
	{
		auto const duration =
		    std::chrono::duration<double> (m_matchEndTime - m_matchStartTime).count ();
		std::printf ("Match duration: %.3fs\n", duration);
		std::printf ("Match rate:     %.1f tps\n", m_arena->tickCount / duration);
	}

	for (auto &[id, connection] : m_connections)
	{
		auto &delays = connection.delays;
		if (delays.empty ())
			continue;

		std::ranges::sort (delays);

		std::printf ("Connection %" PRIu64 " delays:\n", id);
		std::printf ("\tMin:      %8.3fµs\n", delays.front ());
		std::printf ("\tMax:      %8.3fµs\n", delays.back ());
		auto const mid = delays.size () / 2;
		if (delays.size () % 2 == 0)
			std::printf ("\tMedian:   %8.3fµs\n", (delays[mid] + delays[mid + 1]) / 2.0f);
		else
			std::printf ("\tMedian:   %8.3fµs\n", delays[mid]);

		auto const sum  = std::accumulate (std::begin (delays), std::end (delays), 0.0);
		auto const mean = sum / delays.size ();
		std::printf ("\tMean:     %8.3fµs\n", mean);

		if (delays.size () > 1)
		{
			auto const rss = std::accumulate (std::begin (delays),
			    std::end (delays),
			    0.0,
			    [mean] (auto const sum_, auto const delay_) {
				    auto const err = delay_ - mean;
				    return sum_ + err * err;
			    });

			auto const variance = rss / (delays.size () - 1);

			std::printf ("\tVariance: %8.3fµs²\n", variance);
			std::printf ("\tStdDev:   %8.3fµs\n", std::sqrt (variance));
		}
	}
}

Simulator::Simulator (Private) noexcept
{
	m_ballPrediction.slices.resize (6 * 120);

	constexpr auto NUM_CARS = 1;
	for (unsigned i = 0; i < NUM_CARS; ++i)
	{
		auto &player =
		    m_gamePacket.players.emplace_back (std::make_unique<rlbot::flat::PlayerInfoT> ());

		player->physics       = std::make_unique<rlbot::flat::Physics> ();
		player->score_info    = std::make_unique<rlbot::flat::ScoreInfo> ();
		player->hitbox        = std::make_unique<rlbot::flat::BoxShapeT> ();
		player->hitbox_offset = std::make_unique<rlbot::flat::Vector3> ();
		player->last_input    = std::make_unique<rlbot::flat::ControllerState> ();
		player->dodge_dir     = std::make_unique<rlbot::flat::Vector2> ();
	}
}

std::unique_ptr<Simulator> Simulator::create () noexcept
{
	return std::make_unique<Simulator> (Private{});
}

Simulator::ConnectionContext *Simulator::getConnectionContext (
    std::uint64_t const connectionId_) noexcept
{
	auto const &[it, inserted] = m_connections.try_emplace (connectionId_);

	auto &context = it->second;
	if (inserted)
	{
		context.connectionId = connectionId_;
		context.delays.reserve (60 * 60 * 120);
	}

	return &context;
}

void Simulator::sendFieldInfo (std::uint64_t const connectionId_) noexcept
{
	if (m_connections.empty ())
		return;

	rlbot::flat::FieldInfoT fieldInfo;

	for (auto const &arenaPad : m_arena->GetBoostPads ())
	{
		auto &fieldPad =
		    fieldInfo.boost_pads.emplace_back (std::make_unique<rlbot::flat::BoostPadT> ());

		fieldPad->location =
		    std::make_unique<rlbot::flat::Vector3> (fromRocketSim (arenaPad->config.pos));
		fieldPad->is_full_boost = arenaPad->config.isBig;
	}

	if (m_arena->gameMode == RocketSim::GameMode::SOCCAR ||
	    m_arena->gameMode == RocketSim::GameMode::HEATSEEKER ||
	    m_arena->gameMode == RocketSim::GameMode::SNOWDAY)
	{
		for (unsigned i = 0; i < 2; ++i)
		{
			auto &goal = fieldInfo.goals.emplace_back (std::make_unique<rlbot::flat::GoalInfoT> ());

			goal->team_num = i;

			if (i == 0)
			{
				goal->location = std::make_unique<rlbot::flat::Vector3> (
				    0.0f, -RocketSim::RLConst::SOCCAR_GOAL_SCORE_BASE_THRESHOLD_Y, 642.775f);
				goal->direction = std::make_unique<rlbot::flat::Vector3> (0.0f, 1.0f, 0.0f);
			}
			else
			{
				goal->location = std::make_unique<rlbot::flat::Vector3> (
				    0.0f, RocketSim::RLConst::SOCCAR_GOAL_SCORE_BASE_THRESHOLD_Y, 642.775f);
				goal->direction = std::make_unique<rlbot::flat::Vector3> (0.0f, -1.0f, 0.0f);
			}

			goal->width  = 2.0f * 892.755f;
			goal->height = 642.755f;
		}
	}
	else if (m_arena->gameMode == RocketSim::GameMode::HOOPS)
	{
		for (unsigned i = 0; i < 2; ++i)
		{
			auto &goal = fieldInfo.goals.emplace_back (std::make_unique<rlbot::flat::GoalInfoT> ());

			goal->team_num  = 0;
			goal->location  = std::make_unique<rlbot::flat::Vector3> (0.0f,
                i == 0 ? -2770.0f : 2770.0f,
                RocketSim::RLConst::HOOPS_GOAL_SCORE_THRESHOLD_Z);
			goal->direction = std::make_unique<rlbot::flat::Vector3> (0.0f, 0.0f, 1.0f);
			goal->width     = 2.0f * 716.0f;
			goal->height    = 2.0f * 716.0f;
		}
	}

	rlbot::flat::CorePacketT corePacket{};
	corePacket.message.Set (std::move (fieldInfo));
	_sendCorePacket (corePacket, connectionId_);
}

void Simulator::sendMatchConfiguration (std::uint64_t const connectionId_) noexcept
{
	if (m_connections.empty () || !m_matchConfiguration.has_value ())
		return;

	rlbot::flat::CorePacketT corePacket{};
	corePacket.message.Set (std::move (m_matchConfiguration.value ()));

	_sendCorePacket (corePacket, connectionId_);

	m_matchConfiguration = std::move (*corePacket.message.AsMatchConfiguration ());
}

void Simulator::sendControllableTeamInfo (std::uint64_t const connectionId_) noexcept
{
	if (connectionId_ == 0)
	{
		for (auto const &[id, connection] : m_connections)
			sendControllableTeamInfo (id);

		return;
	}

	auto const it = m_connections.find (connectionId_);
	if (it == std::end (m_connections))
		return;

	auto const &connection = it->second;
	if (connection.cars.empty ())
		return;

	rlbot::flat::ControllableTeamInfoT controllableTeamInfo;
	controllableTeamInfo.team = static_cast<int> (std::begin (connection.cars)->second->team);

	controllableTeamInfo.controllables.reserve (connection.cars.size ());
	for (auto const &[id, _] : connection.cars)
	{
		auto &controllable = controllableTeamInfo.controllables.emplace_back (
		    std::make_unique<rlbot::flat::ControllableInfoT> ());
		controllable->index      = id;
		controllable->identifier = id;
	}

	rlbot::flat::CorePacketT corePacket{};
	corePacket.message.Set (std::move (controllableTeamInfo));
	_sendCorePacket (corePacket, connectionId_);
}

void Simulator::sendBallPrediction () noexcept
{
	std::optional<rlbot::flat::CorePacketT> corePacket;

	auto const all = std::ranges::all_of (m_connections, [] (auto const &pair_) {
		return pair_.second.connectionSettings.has_value () &&
		       pair_.second.connectionSettings->wants_ball_predictions;
	});

	for (auto const &[id, connection] : m_connections)
	{
		if (!connection.connectionSettings.has_value () ||
		    !connection.connectionSettings->wants_ball_predictions)
			continue;

		if (!corePacket.has_value ())
		{
			for (unsigned i = 0; auto &slice : m_ballPrediction.slices)
			{
				auto const time  = (m_arena->tickCount + i) / 120.0f;
				auto const &pred = m_ballPredTracker->predData[i++];

				slice = rlbot::flat::PredictionSlice (time,
				    rlbot::flat::Physics (fromRocketSim (pred.pos),
				        fromRocketSim (pred.rotMat),
				        fromRocketSim (pred.vel),
				        fromRocketSim (pred.angVel)));
			}

			// move into core packet
			corePacket.emplace ().message.Set (std::move (m_ballPrediction));
		}

		if (all)
		{
			_sendCorePacket (corePacket.value ());
			break;
		}
		else
			_sendCorePacket (corePacket.value (), id);
	}

	// move back to local copy
	if (corePacket.has_value ())
		m_ballPrediction = std::move (*corePacket->message.AsBallPrediction ());
}

void Simulator::sendGamePacket () noexcept
{
	if (m_connections.empty () || !m_matchConfiguration.has_value ())
		return;

	for (unsigned i = 0; auto &player : m_gamePacket.players)
	{
		auto const it = m_cars.find (i++);
		if (it == std::end (m_cars))
		{
			error ("Car with index %u not found in cars map\n", i - 1);
			continue;
		}

		auto const &car   = it->second;
		auto const &state = car->GetState ();

		*player->physics = rlbot::flat::Physics (fromRocketSim (state.pos),
		    fromRocketSim (state.rotMat),
		    fromRocketSim (state.vel),
		    fromRocketSim (state.angVel));

		// player->score_info
		// player->hitbox
		// player->hitbox_offset
		// player->latest_touch
		// player->air_state
		// player->dodge_timeout
		player->demolished_timeout = state.demoRespawnTimer;
		player->is_supersonic      = state.isSupersonic;
		player->is_bot             = true;
		// player->name
		player->team  = static_cast<std::uint32_t> (car->team);
		player->boost = state.boost;
		// player->player_id
		// player->accolades
		// player->last_input
		player->has_jumped        = state.hasJumped;
		player->has_double_jumped = state.hasDoubleJumped;
		player->has_dodged        = state.hasFlipped;
		player->dodge_elapsed     = state.flipTime;
		// player->dodge_dir
	}

	for (unsigned i = 0; auto &boostPad : m_gamePacket.boost_pads)
	{
		auto const &arenaPad = m_arena->GetBoostPads ()[i++];
		boostPad             = rlbot::flat::BoostPadState (
            arenaPad->_internalState.isActive, arenaPad->_internalState.cooldown);
	}

	{
		auto const &ball      = m_gamePacket.balls.front ();
		auto const &arenaBall = m_arena->ball;

		*ball->physics = rlbot::flat::Physics (fromRocketSim (arenaBall->GetState ().pos),
		    fromRocketSim (arenaBall->GetState ().rotMat),
		    fromRocketSim (arenaBall->GetState ().vel),
		    fromRocketSim (arenaBall->GetState ().angVel));

		if (arenaBall->IsSphere ())
			ball->shape.Set (
			    rlbot::flat::SphereShapeT{.diameter = m_arena->_mutatorConfig.ballRadius * 2.0f});
		else
			ball->shape.Set (
			    rlbot::flat::CylinderShapeT{.diameter = m_arena->_mutatorConfig.ballRadius * 2.0f,
			        .height                           = RocketSim::RLConst::Snowday::PUCK_HEIGHT});
	}

	{
		m_gamePacket.match_info->seconds_elapsed = m_arena->tickCount * m_arena->tickTime;

		switch (m_matchConfiguration->mutators->match_length)
		{
		case rlbot::flat::MatchLengthMutator::FiveMinutes:
			m_gamePacket.match_info->game_time_remaining =
			    300.0f - m_gamePacket.match_info->seconds_elapsed;
			break;

		case rlbot::flat::MatchLengthMutator::TenMinutes:
			m_gamePacket.match_info->game_time_remaining =
			    600.0f - m_gamePacket.match_info->seconds_elapsed;
			break;

		case rlbot::flat::MatchLengthMutator::TwentyMinutes:
			m_gamePacket.match_info->game_time_remaining =
			    1200.0f - m_gamePacket.match_info->seconds_elapsed;
			break;

		case rlbot::flat::MatchLengthMutator::Unlimited:
			m_gamePacket.match_info->game_time_remaining = INFINITY;
			break;
		}

		if (m_gamePacket.match_info->game_time_remaining < 0.0f)
		{
			m_gamePacket.match_info->game_time_remaining = 0.0f;
			m_gamePacket.match_info->is_overtime         = true;
		}
		else
			m_gamePacket.match_info->is_overtime = false;

		m_gamePacket.match_info->is_unlimited_time = m_matchConfiguration->mutators->match_length ==
		                                             rlbot::flat::MatchLengthMutator::Unlimited;

		m_gamePacket.match_info->match_phase     = rlbot::flat::MatchPhase::Active;
		m_gamePacket.match_info->world_gravity_z = m_arena->GetMutatorConfig ().gravity.z;
		m_gamePacket.match_info->game_speed      = 1.0f;
		m_gamePacket.match_info->last_spectated  = 0;
		m_gamePacket.match_info->frame_num       = m_arena->tickCount;
	}

	// move into core packet
	rlbot::flat::CorePacketT corePacket;
	corePacket.message.Set (std::move (m_gamePacket));

	_sendCorePacket (corePacket);

	// move back to local copy
	m_gamePacket = std::move (*corePacket.message.AsGamePacket ());
}

void Simulator::_sendCorePacket (rlbot::flat::CorePacketT const &packet_,
    std::uint64_t const connectionId_) noexcept
{
	auto const now = std::chrono::high_resolution_clock::now ();

	if (connectionId_ == 0)
	{
		for (auto &[_, connection] : m_connections)
			connection.outTimestamps[packet_.message.type] = now;
	}
	else
		getConnectionContext (connectionId_)->outTimestamps[packet_.message.type] = now;

	Server::sendCorePacket (packet_, connectionId_);
}

void Simulator::handleDisconnectSignal (std::uint64_t const connectionId_,
    rlbot::flat::DisconnectSignal const *const packet_) noexcept
{
	(void)packet_;

	auto const now        = std::chrono::high_resolution_clock::now ();
	auto const connection = getConnectionContext (connectionId_);
	connection->inTimestamps[rlbot::flat::InterfaceMessage::DisconnectSignal] = now;

	sendDisconnectSignal ({}, connectionId_);
}

void Simulator::handleStartCommand (std::uint64_t const connectionId_,
    rlbot::flat::StartCommand const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;

	auto const now        = std::chrono::high_resolution_clock::now ();
	auto const connection = getConnectionContext (connectionId_);
	connection->inTimestamps[rlbot::flat::InterfaceMessage::StartCommand] = now;
}

void Simulator::handleMatchConfiguration (std::uint64_t const connectionId_,
    rlbot::flat::MatchConfiguration const *const packet_) noexcept
{
	(void)connectionId_;

	auto const now        = std::chrono::high_resolution_clock::now ();
	auto const connection = getConnectionContext (connectionId_);
	connection->inTimestamps[rlbot::flat::InterfaceMessage::MatchConfiguration] = now;

	rlbot::flat::MatchConfigurationT matchConfiguration;
	packet_->UnPackTo (&matchConfiguration);

	switch (matchConfiguration.existing_match_behavior)
	{
	case rlbot::flat::ExistingMatchBehavior::Restart:
		break;

	default:
		error ("Unsupported existing match behavior: '%s'\n",
		    rlbot::flat::EnumNameExistingMatchBehavior (
		        matchConfiguration.existing_match_behavior));
		return;
	}

	std::unique_ptr<RocketSim::Arena> arena;
	switch (matchConfiguration.game_mode)
	{
	case rlbot::flat::GameMode::Soccar:
		arena.reset (RocketSim::Arena::Create (RocketSim::GameMode::SOCCAR));
		break;

	case rlbot::flat::GameMode::Hoops:
		arena.reset (RocketSim::Arena::Create (RocketSim::GameMode::HOOPS));
		break;

	case rlbot::flat::GameMode::Snowday:
		arena.reset (RocketSim::Arena::Create (RocketSim::GameMode::SNOWDAY));
		break;

	case rlbot::flat::GameMode::Heatseeker:
		arena.reset (RocketSim::Arena::Create (RocketSim::GameMode::HEATSEEKER));
		break;

	default:
		error ("Unsupported game mode: '%s'\n",
		    rlbot::flat::EnumNameGameMode (matchConfiguration.game_mode));
		return;
	}

	auto ballPredTracker = std::make_unique<RocketSim::BallPredTracker> (arena.get (), 6 * 120);

	rlbot::flat::GamePacketT gamePacket;
	gamePacket.boost_pads.resize (arena->GetBoostPads ().size ());

	if (gamePacket.balls.empty ())
	{
		auto &ball    = gamePacket.balls.emplace_back (std::make_unique<rlbot::flat::BallInfoT> ());
		ball->physics = std::make_unique<rlbot::flat::Physics> ();
		ball->shape.Set (rlbot::flat::SphereShapeT{});
	}

	if (!gamePacket.match_info)
	{
		gamePacket.match_info                  = std::make_unique<rlbot::flat::MatchInfoT> ();
		gamePacket.match_info->match_phase     = rlbot::flat::MatchPhase::Inactive;
		gamePacket.match_info->world_gravity_z = arena->GetMutatorConfig ().gravity.z;
		gamePacket.match_info->game_speed      = 1.0f;
	}

	std::unordered_map<std::int32_t, RocketSim::Car *> cars;
	gamePacket.players.clear ();
	for (auto &player : matchConfiguration.player_configurations)
	{
		auto const playerId = cars.size ();

		player->player_id = playerId;

		if (!player)
		{
			error ("Received missing player configuration %zu\n", playerId);
			return;
		}

		debug ("Player %zu is on team %u\n", playerId, player->team);

		if (player->variety.type != rlbot::flat::PlayerClass::CustomBot)
		{
			error ("Invalid player type '%s' for player %zu\n",
			    rlbot::flat::EnumNamePlayerClass (player->variety.type),
			    playerId);
			return;
		}

		if (player->team != static_cast<std::uint32_t> (RocketSim::Team::BLUE) &&
		    player->team != static_cast<std::uint32_t> (RocketSim::Team::ORANGE))
		{
			error ("Invalid team %" PRIu32 " for player %zu\n", player->team, playerId);
			return;
		}

		auto &gamePlayer =
		    gamePacket.players.emplace_back (std::make_unique<rlbot::flat::PlayerInfoT> ());

		RocketSim::CarConfig const *carConfig = &RocketSim::CAR_CONFIG_OCTANE;
		if (auto const bot = player->variety.AsCustomBot ())
		{
			if (auto const loadout = bot->loadout.get ())
			{
				auto const it = carConfigs.find (loadout->car_id);
				if (it != std::end (carConfigs))
					carConfig = it->second;
			}
		}

		auto const car = arena->AddCar (static_cast<RocketSim::Team> (player->team), *carConfig);
		cars[playerId] = car;

		gamePlayer->player_id = playerId;

		gamePlayer->physics       = std::make_unique<rlbot::flat::Physics> ();
		gamePlayer->score_info    = std::make_unique<rlbot::flat::ScoreInfo> ();
		gamePlayer->hitbox        = std::make_unique<rlbot::flat::BoxShapeT> ();
		gamePlayer->hitbox_offset = std::make_unique<rlbot::flat::Vector3> ();
		gamePlayer->last_input    = std::make_unique<rlbot::flat::ControllerState> ();
		gamePlayer->dodge_dir     = std::make_unique<rlbot::flat::Vector2> ();

		gamePlayer->hitbox->length = carConfig->hitboxSize.x;
		gamePlayer->hitbox->width  = carConfig->hitboxSize.y;
		gamePlayer->hitbox->height = carConfig->hitboxSize.z;

		*gamePlayer->hitbox_offset = fromRocketSim (carConfig->hitboxPosOffset);
	}

	m_matchConfiguration = std::move (matchConfiguration);
	m_arena              = std::move (arena);
	m_ballPredTracker    = std::move (ballPredTracker);
	m_cars               = std::move (cars);
	m_gamePacket         = std::move (gamePacket);

	allocateMatch ();
}

void Simulator::handlePlayerInput (std::uint64_t const connectionId_,
    rlbot::flat::PlayerInput const *const packet_) noexcept
{
	auto const now        = std::chrono::high_resolution_clock::now ();
	auto const connection = getConnectionContext (connectionId_);
	connection->inTimestamps[rlbot::flat::InterfaceMessage::PlayerInput] = now;

	auto const state = packet_->controller_state ();
	if (!state)
		return;

	auto const index = packet_->player_index ();

	if (index >= std::min (m_cars.size (), m_gamePacket.players.size ()))
		return;

	auto const it = connection->cars.find (index);
	if (it == std::end (connection->cars))
		return;

	auto const car = it->second;

	*m_gamePacket.players[index]->last_input = *state;

	car->controls.throttle  = state->throttle ();
	car->controls.steer     = state->steer ();
	car->controls.pitch     = state->pitch ();
	car->controls.yaw       = state->yaw ();
	car->controls.roll      = state->roll ();
	car->controls.jump      = state->jump ();
	car->controls.boost     = state->boost ();
	car->controls.handbrake = state->handbrake ();

	auto const &sendTime = connection->outTimestamps[rlbot::flat::CoreMessage::GamePacket];
	auto const &recvTime = connection->inTimestamps[rlbot::flat::InterfaceMessage::PlayerInput];

	connection->delays.emplace_back (
	    std::chrono::duration<double, std::micro> (recvTime - sendTime).count ());

	m_pendingInputs[index] = true;
	if (!std::ranges::all_of (m_pendingInputs, [] (auto const value_) { return value_; }))
		return;

	m_arena->Step ();
	m_matchEndTime = std::chrono::high_resolution_clock::now ();

	unsigned maxTicks;
	switch (m_matchConfiguration->mutators->match_length)
	{
	case rlbot::flat::MatchLengthMutator::FiveMinutes:
		maxTicks = 5 * 60 * 120;
		break;

	case rlbot::flat::MatchLengthMutator::TenMinutes:
		maxTicks = 10 * 60 * 120;
		break;

	case rlbot::flat::MatchLengthMutator::TwentyMinutes:
		maxTicks = 20 * 60 * 120;
		break;

	case rlbot::flat::MatchLengthMutator::Unlimited:
	default:
		// just make it one hour
		maxTicks = 60 * 60 * 120;
		break;
	}

	if (m_arena->tickCount >= maxTicks)
	{
		terminate ();
		return;
	}

	std::fill (std::begin (m_pendingInputs), std::end (m_pendingInputs), false);

	sendBallPrediction ();
	sendGamePacket ();
}

void Simulator::handleDesiredGameState (std::uint64_t const connectionId_,
    rlbot::flat::DesiredGameState const *const packet_) noexcept
{
	auto const now        = std::chrono::high_resolution_clock::now ();
	auto const connection = getConnectionContext (connectionId_);
	connection->inTimestamps[rlbot::flat::InterfaceMessage::DesiredGameState] = now;

	if (!m_matchConfiguration.has_value () || !m_matchConfiguration->enable_state_setting)
	{
		error ("Received DesiredGameState packet from connection %" PRIu64
		       ", but state setting is disabled\n",
		    connectionId_);
		return;
	}

	/// @todo
	(void)packet_;
}

void Simulator::handleRenderGroup (std::uint64_t const connectionId_,
    rlbot::flat::RenderGroup const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;

	auto const now        = std::chrono::high_resolution_clock::now ();
	auto const connection = getConnectionContext (connectionId_);
	connection->inTimestamps[rlbot::flat::InterfaceMessage::RenderGroup] = now;
}

void Simulator::handleRemoveRenderGroup (std::uint64_t const connectionId_,
    rlbot::flat::RemoveRenderGroup const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;

	if (auto const it = m_connections.find (connectionId_); it != m_connections.end ())
		it->second.inTimestamps[rlbot::flat::InterfaceMessage::RemoveRenderGroup] =
		    std::chrono::high_resolution_clock::now ();
}

void Simulator::handleMatchComm (std::uint64_t const connectionId_,
    rlbot::flat::MatchComm const *const packet_) noexcept
{
	if (auto const it = m_connections.find (connectionId_); it != m_connections.end ())
		it->second.inTimestamps[rlbot::flat::InterfaceMessage::MatchComm] =
		    std::chrono::high_resolution_clock::now ();

	std::optional<rlbot::flat::CorePacketT> corePacket;

	for (auto &[id, connection] : m_connections)
	{
		if (id == connectionId_ || !connection.connectionSettings.has_value () ||
		    !connection.connectionSettings->wants_comms || connection.cars.empty ())
			continue;

		if (packet_->team_only () &&
		    static_cast<std::uint32_t> (std::begin (connection.cars)->second->team) !=
		        packet_->team ())
			continue;

		if (!corePacket.has_value ())
		{
			rlbot::flat::MatchCommT comm;
			packet_->UnPackTo (&comm);

			corePacket.emplace ().message.Set (std::move (comm));
		}

		_sendCorePacket (corePacket.value (), id);
	}
}

void Simulator::handleConnectionSettings (std::uint64_t const connectionId_,
    rlbot::flat::ConnectionSettings const *const packet_) noexcept
{
	rlbot::flat::ConnectionSettingsT connectionSettings;
	packet_->UnPackTo (&connectionSettings);

	auto const connection = getConnectionContext (connectionId_);
	if (connection->connectionSettings.has_value ())
	{
		if (connection->connectionSettings->agent_id != connectionSettings.agent_id)
		{
			error ("Cannot change agent id from '%s' to '%s' for connection %" PRIu64 "\n",
			    connection->connectionSettings->agent_id.c_str (),
			    connectionSettings.agent_id.c_str (),
			    connectionId_);
			return;
		}
	}

	connection->connectionSettings = std::move (connectionSettings);

	allocateCar (connectionId_);
	allocateMatch ();
}

void Simulator::handleStopCommand (std::uint64_t const connectionId_,
    rlbot::flat::StopCommand const *const packet_) noexcept
{
	(void)connectionId_;

	if (packet_->shutdown_server ())
		terminate ();
}

void Simulator::handleSetLoadout (std::uint64_t const connectionId_,
    rlbot::flat::SetLoadout const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Simulator::handleInitComplete (std::uint64_t const connectionId_,
    rlbot::flat::InitComplete const *const packet_) noexcept
{
	(void)packet_;

	auto const connection    = getConnectionContext (connectionId_);
	connection->initComplete = true;

	startMatch ();
}

void Simulator::handleRenderingStatus (std::uint64_t const connectionId_,
    rlbot::flat::RenderingStatus const *const packet_) noexcept
{
	(void)connectionId_;
	(void)packet_;
}

void Simulator::handleConnectionRemoved (std::uint64_t const connectionId_) noexcept
{
	m_connections.erase (connectionId_);
}

void Simulator::handlePingRequest (std::uint64_t const connectionId_,
    rlbot::flat::PingRequest const *const packet_) noexcept
{
	sendPingResponse ({.cookie = packet_->cookie ()}, connectionId_);
}

void Simulator::handlePingResponse (std::uint64_t const connectionId_,
    rlbot::flat::PingResponse const *const packet_) noexcept
{
	(void)packet_;
	(void)connectionId_;
}

bool Simulator::loop () noexcept
{
	return true;
}

void Simulator::allocateMatch () noexcept
{
	if (!m_matchConfiguration.has_value ())
		return;

	// check if match is already started
	if (m_gamePacket.match_info->match_phase != rlbot::flat::MatchPhase::Inactive)
		return;

	if (m_matchConfiguration->wait_for_agents)
	{
		for (auto const &[id, connection] : m_connections)
			allocateCar (id);

		// check if all cars are assigned to a connection
		for (auto const &[playerId, car] : m_cars)
		{
			debug ("Checking if player %" PRId32 " is assigned to a connection\n", playerId);

			bool assigned = false;
			for (auto const &[id, connection] : m_connections)
			{
				debug ("Checking if player %" PRId32 " is assigned to connection %" PRIu64 "\n",
				    playerId,
				    id);
				if (connection.cars.contains (playerId))
				{
					debug (
					    "Player %" PRId32 " is assigned to connection %" PRIu64 "\n", playerId, id);
					assigned = true;
					break;
				}
			}

			if (!assigned)
			{
				debug ("Player %" PRId32 " is not assigned to any connection\n", playerId);
				return;
			}
		}
	}

	// allocate the match
	m_arena->ResetToRandomKickoff ();

	sendFieldInfo ();
	sendMatchConfiguration ();
	sendControllableTeamInfo ();

	m_gamePacket.match_info->match_phase = rlbot::flat::MatchPhase::Paused;

	startMatch ();
}

void Simulator::allocateCar (std::uint64_t const connectionId_) noexcept
{
	if (!m_matchConfiguration.has_value ())
	{
		debug ("Cannot allocate car without match configuration\n");
		return; // cannot allocate without match configuration
	}

	auto const connection = getConnectionContext (connectionId_);

	if (!connection->cars.empty ())
	{
		debug ("Cars already allocated for connection %" PRIu64 "\n", connectionId_);
		return; // already allocated
	}

	if (!connection->connectionSettings.has_value () ||
	    connection->connectionSettings->agent_id.empty ())
	{
		debug ("No agent ID for connection %" PRIu64 ", cannot allocate car\n", connectionId_);
		return; // no agent id, cannot allocate
	}

	// check if player is already assigned to a connection
	auto const carOwner = [this] (auto const index_) -> ConnectionContext const * {
		for (auto const &[id, conn] : m_connections)
		{
			if (conn.cars.contains (index_))
				return &conn;
		}

		return nullptr;
	};

	std::optional<std::uint32_t> team;

	for (auto const &player : m_matchConfiguration->player_configurations)
	{
		auto const index = &player - m_matchConfiguration->player_configurations.data ();

		assert (player);
		if (!player)
			continue;

		debug ("Player %zu is on team %u\n",
		    &player - m_matchConfiguration->player_configurations.data (),
		    player->team);

		auto const bot = player->variety.AsCustomBot ();
		assert (bot);
		if (!bot)
		{
			debug ("Invalid bot configuration for connection %" PRIu64 "\n", connectionId_);
			continue;
		}

		// check if player is already assigned to a connection
		if (auto const owner = carOwner (index))
		{
			debug ("Player %zu is already assigned to connection %" PRIu64 "\n",
			    index,
			    connection->connectionId);
			continue;
		}

		if (connection->connectionSettings->agent_id != bot->agent_id)
		{
			debug ("Connection Agent ID %s does not match bot Agent ID %s for connection %" PRIu64
			       "\n",
			    connection->connectionSettings->agent_id.c_str (),
			    bot->agent_id.c_str (),
			    connectionId_);
			continue;
		}

		if (!team.has_value ())
		{
			debug ("Assigning team %u to connection %" PRIu64 "\n", player->team, connectionId_);
			team = player->team;
		}

		if (team.value () != player->team)
		{
			debug ("Connection team %u does not match player team %u for connection %" PRIu64 "\n",
			    team.value (),
			    player->team,
			    connectionId_);

			continue;
		}

		// always add first eligible car
		if (connection->cars.empty ())
		{
			debug ("Allocating car %zu to connection %" PRIu64 "\n", index, connectionId_);
			connection->cars[index] = m_cars[index];
			if (!bot->hivemind)
				break; // if not hivemind, only assign one car
		}

		// if we already have a car, it must have been a hivemind
		if (!bot->hivemind)
		{
			debug ("Connection is not a hivemind for car %zu on connection %" PRIu64 "\n",
			    index,
			    connectionId_);
			continue;
		}

		debug ("Allocating car %zu to connection %" PRIu64 "\n", index, connectionId_);
		connection->cars[index] = m_cars[index];
	}
}

void Simulator::startMatch () noexcept
{
	if (m_gamePacket.match_info->match_phase == rlbot::flat::MatchPhase::Active)
		return;

	if (m_matchConfiguration->wait_for_agents)
	{
		// check if all cars have received InitComplete
		for (auto const &[playerId, car] : m_cars)
		{
			bool initComplete = false;
			for (auto const &[id, connection] : m_connections)
			{
				if (connection.cars.contains (playerId) && connection.initComplete)
				{
					initComplete = true;
					break;
				}
			}

			if (!initComplete)
				return;
		}
	}

	m_gamePacket.match_info->match_phase = rlbot::flat::MatchPhase::Active;

	m_pendingInputs.resize (m_cars.size (), false);
	std::fill (std::begin (m_pendingInputs), std::end (m_pendingInputs), false);

	m_matchStartTime = std::chrono::high_resolution_clock::now ();

	sendBallPrediction ();
	sendGamePacket ();
}

void Simulator::clearMatch () noexcept
{
	for (auto &[id, connection] : m_connections)
		connection.cars.clear ();

	m_gamePacket.players.clear ();
	m_gamePacket.boost_pads.clear ();
	m_gamePacket.balls.clear ();

	m_cars.clear ();
	m_ballPredTracker.reset ();
	m_arena.reset ();

	m_matchConfiguration.reset ();
}
