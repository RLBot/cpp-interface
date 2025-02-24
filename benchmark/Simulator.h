#pragma once

#include <Message.h>
#include <Socket.h>

#ifdef _WIN32
#include <WinSock2.h>
#else
#include <sys/socket.h>
#endif

#include <RocketSim.h>
#include <Sim/BallPredTracker/BallPredTracker.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

class Simulator
{
private:
	struct Private
	{
	};

public:
	~Simulator () noexcept;

	Simulator (Private) noexcept;

	bool run () noexcept;

	static std::unique_ptr<Simulator> create () noexcept;

private:
	/// @brief Initialize
	bool init () noexcept;

	bool sendFieldInfo () noexcept;
	bool sendMatchConfig () noexcept;
	bool sendControllableTeamInfo () noexcept;
	bool sendBallPrediction () noexcept;
	bool sendGamePacket () noexcept;
	bool sendMatchComms () noexcept;

	bool handlePlayerInput (rlbot::detail::Message message_) noexcept;

	rlbot::detail::Message fillMessage (rlbot::detail::MessageType type_,
	    flatbuffers::FlatBufferBuilder &builder_) noexcept;

	rlbot::detail::Message readMessage () noexcept;
	bool writeMessage (rlbot::detail::Message message_) noexcept;

	std::unique_ptr<RocketSim::Arena> m_arena;
	std::unique_ptr<RocketSim::BallPredTracker> m_ballPredTracker;
	std::vector<RocketSim::Car *> m_cars;

	SOCKET m_sock = INVALID_SOCKET;

	std::shared_ptr<rlbot::detail::Pool<rlbot::detail::Buffer>> m_bufferPool;
	std::shared_ptr<rlbot::detail::Pool<flatbuffers::FlatBufferBuilder>> m_fbbPool;

	std::vector<std::chrono::high_resolution_clock::time_point> m_inTimestamps;
	std::vector<std::chrono::high_resolution_clock::time_point> m_outTimestamps;

	rlbot::flat::BallPredictionT m_ballPrediction;
	rlbot::flat::GamePacketT m_gamePacket;

	std::string m_agendId;
	bool m_wantsBallPrediction = false;
	bool m_wantsMatchComms     = false;

	std::vector<double> m_delays;
};
