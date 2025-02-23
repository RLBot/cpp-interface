#include "Simulator.h"

#include <Log.h>

#ifdef _WIN32
#include <WinSock2.h>
#else
#include <netinet/in.h>
#include <netinet/tcp.h>
#endif

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <numeric>

using namespace rlbot::detail;

#ifndef _WIN32
int (&closesocket) (int) = ::close;
#endif

namespace
{
constexpr auto NUM_TICKS = 60 * 60 * 120;

SOCKET waitForConnection () noexcept
{
	auto listenSocket = ::socket (AF_INET, SOCK_STREAM, 0);
	if (listenSocket == INVALID_SOCKET)
	{
		error ("socket: %s\n", errorMessage (true));
		return INVALID_SOCKET;
	}

	int yes = 1;
	if (::setsockopt (listenSocket,
	        SOL_SOCKET,
	        SO_REUSEADDR,
	        reinterpret_cast<char const *> (&yes),
	        sizeof (yes)) != 0)
	{
		error ("SO_REUSEADDR: %s\n", errorMessage (true));
		::closesocket (listenSocket);
		return INVALID_SOCKET;
	}

#ifdef SO_REUSEPORT
	if (::setsockopt (listenSocket,
	        SOL_SOCKET,
	        SO_REUSEPORT,
	        reinterpret_cast<char const *> (&yes),
	        sizeof (yes)) != 0)
	{
		error ("SO_REUSEPORT: %s\n", errorMessage (true));
		::closesocket (listenSocket);
		return INVALID_SOCKET;
	}
#endif

	if (::setsockopt (listenSocket,
	        IPPROTO_TCP,
	        TCP_NODELAY,
	        reinterpret_cast<char const *> (&yes),
	        sizeof (yes)) != 0)
	{
		error ("TCP_NODELAY: %s\n", errorMessage (true));
		::closesocket (listenSocket);
		return INVALID_SOCKET;
	}

	sockaddr_in addr;
	std::memset (&addr, 0, sizeof (addr));
	addr.sin_family      = AF_INET;
	addr.sin_addr.s_addr = htonl (INADDR_ANY);
	addr.sin_port        = htons (23234);

	if (::bind (listenSocket, reinterpret_cast<sockaddr const *> (&addr), sizeof (addr)) != 0)
	{
		error ("bind: %s\n", errorMessage (true));
		::closesocket (listenSocket);
		return INVALID_SOCKET;
	}

	if (::listen (listenSocket, 1) != 0)
	{
		error ("listen: %s\n", errorMessage (true));
		::closesocket (listenSocket);
		return INVALID_SOCKET;
	}

	socklen_t addrLen = sizeof (addr);
	auto const sock   = ::accept (listenSocket, reinterpret_cast<sockaddr *> (&addr), &addrLen);
	if (sock == INVALID_SOCKET)
	{
		error ("accept: %s\n", errorMessage (true));
		::closesocket (listenSocket);
		return INVALID_SOCKET;
	}

	::closesocket (listenSocket);

	return sock;
}

rlbot::flat::Vector3 fromRocketSim (RocketSim::Vec const &vec_) noexcept
{
	return {vec_.x, vec_.y, vec_.z};
}

rlbot::flat::Rotator fromRocketSim (RocketSim::RotMat const &rotMat_) noexcept
{
	auto const angle = RocketSim::Angle::FromRotMat (rotMat_);

	return {angle.pitch, angle.yaw, angle.roll};
}

bool readAll (SOCKET const sock_, void *const buffer_, std::size_t const size_) noexcept
{
	std::size_t count = 0;
	while (count < size_)
	{
		auto const rc = ::recv (sock_, static_cast<char *> (buffer_) + count, size_ - count, 0);
		if (rc <= 0)
		{
			if (rc == 0)
				error ("recv: disconnected\n");
			else
				error ("recv: %s\n", errorMessage (true));

			return false;
		}

		count += rc;
	}

	return true;
}

bool writeAll (SOCKET const sock_, void const *const buffer_, std::size_t const size_) noexcept
{
	std::size_t count = 0;
	while (count < size_)
	{
		auto const rc =
		    ::send (sock_, static_cast<char const *> (buffer_) + count, size_ - count, 0);
		if (rc <= 0)
		{
			if (rc == 0)
				error ("send: sent 0 bytes\n");
			else
				error ("send: %s\n", errorMessage (true));

			return false;
		}

		count += rc;
	}

	return true;
}
}

///////////////////////////////////////////////////////////////////////////
Simulator::~Simulator () noexcept
{
	if (m_sock != INVALID_SOCKET)
		::closesocket (m_sock);

	if (!m_delays.empty ())
	{
		for (auto const &delay : m_delays)
			std::fprintf (stderr, "%.3f\n", delay);

		std::ranges::sort (m_delays);

		std::printf ("Min:      %8.3fµs\n", m_delays.front ());
		std::printf ("Max:      %8.3fµs\n", m_delays.back ());
		auto const mid = m_delays.size () / 2;
		if (m_delays.size () % 2 == 0)
			std::printf ("Median:   %8.3fµs\n", (m_delays[mid] + m_delays[mid + 1]) / 2.0f);
		else
			std::printf ("Median:   %8.3fµs\n", m_delays[mid]);

		auto const sum  = std::accumulate (std::begin (m_delays), std::end (m_delays), 0.0);
		auto const mean = sum / m_delays.size ();
		std::printf ("Mean:     %8.3fµs\n", mean);

		if (m_delays.size () > 1)
		{
			auto const rss = std::accumulate (std::begin (m_delays),
			    std::end (m_delays),
			    0.0,
			    [mean] (auto const sum_, auto const delay_) {
				    auto const err = delay_ - mean;
				    return sum_ + err * err;
			    });

			auto const variance = rss / (m_delays.size () - 1);

			std::printf ("Variance: %8.3fµs²\n", variance);
			std::printf ("StdDev:   %8.3fµs\n", std::sqrt (variance));
		}
	}
}

Simulator::Simulator (Private) noexcept
    : m_bufferPool (Pool<Buffer>::create ("Buffer")),
      m_fbbPool (Pool<flatbuffers::FlatBufferBuilder>::create ("FBB"))
{
	m_ballPrediction.slices.resize (6 * 120);

	constexpr auto NUM_CARS = 2;
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

	m_delays.reserve (NUM_TICKS);
}

bool Simulator::run () noexcept
{
	while (true)
	{
		auto message = readMessage ();
		if (!message)
			return false;

		if (message.type () != MessageType::ConnectionSettings)
			continue;

		auto const cs = message.flatbuffer<rlbot::flat::ConnectionSettings> (true);
		if (!cs)
		{
			error ("Invalid ConnectionSettings message\n");
			return false;
		}

		m_arena = std::unique_ptr<RocketSim::Arena> (
		    RocketSim::Arena::Create (RocketSim::GameMode::SOCCAR));
		m_ballPredTracker = std::make_unique<RocketSim::BallPredTracker> (m_arena.get (), 6 * 120);

		m_gamePacket.boost_pads.resize (m_arena->GetBoostPads ().size ());

		if (m_gamePacket.balls.empty ())
		{
			auto &ball =
			    m_gamePacket.balls.emplace_back (std::make_unique<rlbot::flat::BallInfoT> ());
			ball->physics = std::make_unique<rlbot::flat::Physics> ();
			ball->shape.Set (rlbot::flat::SphereShapeT{});
		}

		if (!m_gamePacket.match_info)
		{
			m_gamePacket.match_info                  = std::make_unique<rlbot::flat::MatchInfoT> ();
			m_gamePacket.match_info->match_phase     = rlbot::flat::MatchPhase::Active;
			m_gamePacket.match_info->world_gravity_z = m_arena->GetMutatorConfig ().gravity.z;
			m_gamePacket.match_info->game_speed      = 1.0f;
		}

		if (m_gamePacket.teams.empty ())
			m_gamePacket.teams.emplace_back ();

		m_agendId             = cs->agent_id ()->str ();
		m_wantsBallPrediction = cs->wants_ball_predictions ();
		m_wantsMatchComms     = cs->wants_comms ();

		if (!sendFieldInfo () || !sendMatchConfiguration () || !sendControllableTeamInfo ())
			return false;

		while (true)
		{
			auto message = readMessage ();
			if (!message)
				return false;

			if (message.type () != MessageType::InitComplete)
				continue;

			break;
		}

		break;
	}

	m_delays.clear ();

	for (auto &player : m_gamePacket.players)
	{
		auto const car = m_cars.emplace_back (m_arena->AddCar (RocketSim::Team::BLUE));

		player->hitbox->length = car->config.hitboxSize.x;
		player->hitbox->width  = car->config.hitboxSize.y;
		player->hitbox->height = car->config.hitboxSize.z;
		*player->hitbox_offset = fromRocketSim (car->config.hitboxPosOffset);
	}

	m_arena->ResetToRandomKickoff ();

	constexpr auto tps = 120;

	unsigned accumulator = 0;

	auto now = std::chrono::high_resolution_clock::now ();

	for (unsigned i = 0; i < NUM_TICKS; ++i)
	{
		unsigned long us = 1000000 / tps;
		accumulator += 1000000 % tps;
		if (accumulator >= tps)
		{
			accumulator -= tps;
			us += 1;
		}

		// std::this_thread::sleep_for is really really unreliable on windows
		auto const deadline = now + std::chrono::microseconds (us);
		// std::this_thread::sleep_until (deadline);
		now = deadline;

		m_arena->Step ();
		if (m_wantsBallPrediction)
		{
			m_ballPredTracker->UpdatePredFromArena (m_arena.get ());
			if (!sendBallPrediction ())
				return false;
		}

		if (!sendGamePacket ())
			return false;

		unsigned inputs = 0;
		while (inputs < m_gamePacket.players.size ())
		{
			auto message = readMessage ();
			if (!message)
				return false;

			switch (message.type ())
			{
			case MessageType::PlayerInput:
				if (!handlePlayerInput (std::move (message)))
					return false;
				++inputs;
				break;

			default:
				break;
			}
		}
	}

	return true;
}

std::unique_ptr<Simulator> Simulator::create () noexcept
{
	auto simulator = std::make_unique<Simulator> (Private{});

	if (!simulator->init ())
		return {};

	return simulator;
}

bool Simulator::init () noexcept
{
	m_sock = waitForConnection ();
	if (m_sock == INVALID_SOCKET)
		return {};

	return true;
}

bool Simulator::sendFieldInfo () noexcept
{
	rlbot::flat::FieldInfoT fieldInfo;

	for (auto const &arenaPad : m_arena->GetBoostPads ())
	{
		auto &fieldPad =
		    fieldInfo.boost_pads.emplace_back (std::make_unique<rlbot::flat::BoostPadT> ());

		fieldPad->is_full_boost = arenaPad->config.isBig;
		fieldPad->location =
		    std::make_unique<rlbot::flat::Vector3> (fromRocketSim (arenaPad->config.pos));
	}

	for (unsigned i = 0; i < 2; ++i)
	{
		auto &goal = fieldInfo.goals.emplace_back (std::make_unique<rlbot::flat::GoalInfoT> ());

		goal->team_num = i;
		goal->width    = 892.755f;
		goal->height   = 642.755f;

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
	}

	auto fbb = m_fbbPool->getObject ();
	fbb->Finish (rlbot::flat::CreateFieldInfo (*fbb, &fieldInfo));

	auto message = fillMessage (MessageType::FieldInfo, *fbb);
	if (!message)
		return false;

	return writeMessage (message);
}

bool Simulator::sendMatchConfiguration () noexcept
{
	rlbot::flat::MatchConfigurationT matchConfiguration;
	matchConfiguration.enable_rendering     = true;
	matchConfiguration.enable_state_setting = true;

	for (unsigned i = 0; i < m_gamePacket.players.size (); ++i)
	{
		auto &playerConfig = matchConfiguration.player_configurations.emplace_back (
		    std::make_unique<rlbot::flat::PlayerConfigurationT> ());

		playerConfig->variety.Set (rlbot::flat::CustomBotT{});
		playerConfig->agent_id = m_agendId;
		playerConfig->spawn_id = i;
	}

	matchConfiguration.mutators = std::make_unique<rlbot::flat::MutatorSettingsT> ();

	auto fbb = m_fbbPool->getObject ();
	fbb->Finish (rlbot::flat::CreateMatchConfiguration (*fbb, &matchConfiguration));

	auto message = fillMessage (MessageType::MatchConfiguration, *fbb);
	if (!message)
		return false;

	return writeMessage (message);
}

bool Simulator::sendControllableTeamInfo () noexcept
{
	rlbot::flat::ControllableTeamInfoT controllableTeamInfo;
	controllableTeamInfo.team = 0;

	for (unsigned i = 0; i < m_gamePacket.players.size (); ++i)
	{
		auto &controllable = controllableTeamInfo.controllables.emplace_back (
		    std::make_unique<rlbot::flat::ControllableInfoT> ());
		controllable->index    = i;
		controllable->spawn_id = i;
	}

	auto fbb = m_fbbPool->getObject ();
	fbb->Finish (rlbot::flat::CreateControllableTeamInfo (*fbb, &controllableTeamInfo));

	auto message = fillMessage (MessageType::ControllableTeamInfo, *fbb);
	if (!message)
		return false;

	return writeMessage (message);
}

bool Simulator::sendBallPrediction () noexcept
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

	auto fbb = m_fbbPool->getObject ();
	fbb->Finish (rlbot::flat::CreateBallPrediction (*fbb, &m_ballPrediction));

	auto message = fillMessage (MessageType::BallPrediction, *fbb);
	if (!message)
		return false;

	return writeMessage (message);
}

bool Simulator::sendGamePacket () noexcept
{
	for (unsigned i = 0; auto &player : m_gamePacket.players)
	{
		auto const car   = m_cars[i++];
		*player->physics = rlbot::flat::Physics (fromRocketSim (car->GetState ().pos),
		    fromRocketSim (car->GetState ().rotMat),
		    fromRocketSim (car->GetState ().vel),
		    fromRocketSim (car->GetState ().angVel));

		player->boost = car->GetState ().boost;
	}

	auto fbb = m_fbbPool->getObject ();
	fbb->Finish (rlbot::flat::CreateGamePacket (*fbb, &m_gamePacket));

	auto message = fillMessage (MessageType::GamePacket, *fbb);
	if (!message)
		return false;

	return writeMessage (message);
}

bool Simulator::sendMatchComms () noexcept
{
	return false;
}

bool Simulator::handlePlayerInput (Message message_) noexcept
{
	auto const input = message_.flatbuffer<rlbot::flat::PlayerInput> (true);
	if (!input)
		return false;

	auto const state = input->controller_state ();
	if (!state)
		return false;

	if (input->player_index () >= std::min (m_cars.size (), m_gamePacket.players.size ()))
		return false;

	auto const car = m_cars[input->player_index ()];

	*m_gamePacket.players[input->player_index ()]->last_input = *state;

	car->controls.throttle  = state->throttle ();
	car->controls.steer     = state->steer ();
	car->controls.pitch     = state->pitch ();
	car->controls.yaw       = state->yaw ();
	car->controls.roll      = state->roll ();
	car->controls.jump      = state->jump ();
	car->controls.boost     = state->boost ();
	car->controls.handbrake = state->handbrake ();

	auto const sendTime = m_outTimestamps[static_cast<unsigned> (MessageType::GamePacket)];
	auto const recvTime = m_inTimestamps[static_cast<unsigned> (MessageType::PlayerInput)];
	m_delays.emplace_back (1e6 * std::chrono::duration<double> (recvTime - sendTime).count ());

	return true;
}

Message Simulator::fillMessage (MessageType const type_,
    flatbuffers::FlatBufferBuilder &builder_) noexcept
{
	auto buffer = m_bufferPool->getObject ();

	auto const size = builder_.GetSize ();
	if (buffer->size () < size + 4)
		return {};

	buffer->operator[] (0) = static_cast<unsigned> (type_) >> CHAR_BIT;
	buffer->operator[] (1) = static_cast<unsigned> (type_);
	buffer->operator[] (2) = size >> CHAR_BIT;
	buffer->operator[] (3) = size;

	std::memcpy (&buffer->operator[] (4), builder_.GetBufferPointer (), size);

	return Message (std::move (buffer), 0);
}

Message Simulator::readMessage () noexcept
{
	auto inBuffer = m_bufferPool->getObject ();
	if (!readAll (m_sock, inBuffer->data (), 4))
		return {};

	auto const now = std::chrono::high_resolution_clock::now ();

	auto message = Message (inBuffer, 0);

	auto const type = static_cast<unsigned> (message.type ());
	if (m_inTimestamps.size () <= type) [[unlikely]]
		m_inTimestamps.resize (type + 1);

	m_inTimestamps[type] = now;

	if (!readAll (m_sock, &inBuffer->operator[] (4), message.size ()))
		return {};

	return message;
}

bool Simulator::writeMessage (Message message_) noexcept
{
	auto const type = static_cast<unsigned> (message_.type ());
	if (m_outTimestamps.size () <= type) [[unlikely]]
		m_outTimestamps.resize (type + 1);

	if (!writeAll (m_sock, message_.buffer ()->data (), message_.sizeWithHeader ()))
		return false;

	m_outTimestamps[type] = std::chrono::high_resolution_clock::now ();

	return true;
}
