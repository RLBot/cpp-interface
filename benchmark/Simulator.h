#pragma once

#include <rlbot/Server.h>

#include <RocketSim.h>
#include <Sim/BallPredTracker/BallPredTracker.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

class Simulator : public rlbot::Server
{
private:
	struct Private
	{
	};

	struct ConnectionContext
	{
		std::uint64_t connectionId = 0;
		std::optional<rlbot::flat::ConnectionSettingsT> connectionSettings{};
		std::unordered_map<rlbot::flat::InterfaceMessage,
		    std::chrono::high_resolution_clock::time_point>
		    inTimestamps{};
		std::unordered_map<rlbot::flat::CoreMessage, std::chrono::high_resolution_clock::time_point>
		    outTimestamps{};
		std::vector<double> delays{};
		std::unordered_map<std::int32_t, RocketSim::Car *> cars;
		bool initComplete = false;
	};

public:
	~Simulator () noexcept;

	Simulator (Private) noexcept;

	static std::unique_ptr<Simulator> create () noexcept;

private:
	/// @brief Initialize
	bool init () noexcept;

	ConnectionContext *getConnectionContext (std::uint64_t connectionId_) noexcept;

	void sendFieldInfo (std::uint64_t connectionId_ = 0) noexcept;
	void sendMatchConfiguration (std::uint64_t connectionId_ = 0) noexcept;
	void sendControllableTeamInfo (std::uint64_t connectionId_ = 0) noexcept;
	void sendBallPrediction () noexcept;
	void sendGamePacket () noexcept;

	/// @brief Send CorePacket
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void _sendCorePacket (rlbot::flat::CorePacketT const &packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	void handleDisconnectSignal (std::uint64_t connectionId_,
	    rlbot::flat::DisconnectSignal const *packet_) noexcept override;

	void handleStartCommand (std::uint64_t connectionId_,
	    rlbot::flat::StartCommand const *packet_) noexcept override;

	void handleMatchConfiguration (std::uint64_t connectionId_,
	    rlbot::flat::MatchConfiguration const *packet_) noexcept override;

	void handlePlayerInput (std::uint64_t connectionId_,
	    rlbot::flat::PlayerInput const *packet_) noexcept override;

	void handleDesiredGameState (std::uint64_t connectionId_,
	    rlbot::flat::DesiredGameState const *packet_) noexcept override;

	void handleRenderGroup (std::uint64_t connectionId_,
	    rlbot::flat::RenderGroup const *packet_) noexcept override;

	void handleRemoveRenderGroup (std::uint64_t connectionId_,
	    rlbot::flat::RemoveRenderGroup const *packet_) noexcept override;

	void handleMatchComm (std::uint64_t connectionId_,
	    rlbot::flat::MatchComm const *packet_) noexcept override;

	void handleConnectionSettings (std::uint64_t connectionId_,
	    rlbot::flat::ConnectionSettings const *packet_) noexcept override;

	void handleStopCommand (std::uint64_t connectionId_,
	    rlbot::flat::StopCommand const *packet_) noexcept override;

	void handleSetLoadout (std::uint64_t connectionId_,
	    rlbot::flat::SetLoadout const *packet_) noexcept override;

	void handleInitComplete (std::uint64_t connectionId_,
	    rlbot::flat::InitComplete const *packet_) noexcept override;

	void handleRenderingStatus (std::uint64_t connectionId_,
	    rlbot::flat::RenderingStatus const *packet_) noexcept override;

	void handleConnectionRemoved (std::uint64_t connectionId_) noexcept override;

	void handlePingRequest (std::uint64_t connectionId_,
	    rlbot::flat::PingRequest const *packet_) noexcept override;

	void handlePingResponse (std::uint64_t connectionId_,
	    rlbot::flat::PingResponse const *packet_) noexcept override;

	bool loop () noexcept override;

	void allocateMatch () noexcept;

	void allocateCar (std::uint64_t connectionId_) noexcept;

	void startMatch () noexcept;

	void clearMatch () noexcept;

	std::optional<rlbot::flat::MatchConfigurationT> m_matchConfiguration;

	std::unique_ptr<RocketSim::Arena> m_arena;
	std::unique_ptr<RocketSim::BallPredTracker> m_ballPredTracker;
	std::unordered_map<std::int32_t, RocketSim::Car *> m_cars;

	std::unordered_map<std::uint64_t, ConnectionContext> m_connections;

	rlbot::flat::BallPredictionT m_ballPrediction;
	rlbot::flat::GamePacketT m_gamePacket;

	std::vector<bool> m_pendingInputs;

	std::chrono::high_resolution_clock::time_point m_matchStartTime;
	std::chrono::high_resolution_clock::time_point m_matchEndTime;
};
