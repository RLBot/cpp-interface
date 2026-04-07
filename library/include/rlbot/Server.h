#pragma once

#include <rlbot/RLBot.h>

#include <corepacket_generated.h>
#include <interfacepacket_generated.h>

#include <cstdint>
#include <memory>

namespace rlbot
{
namespace detail
{
class ServerImpl;
class Message;
}

class RLBot_API Server
{
public:
	virtual ~Server () noexcept;

	Server () noexcept;

	Server (Server const &) = delete;

	Server &operator= (Server const &) = delete;

	Server (Server &&) = default;

	Server &operator= (Server &&) = default;

	/// @brief Start service thread
	/// @param host_ Host to listen on
	/// @param service_ Service (port) to listen on
	bool run (char const *host_ = "0.0.0.0", char const *service_ = "23234") noexcept;

	/// @brief Check if service thread is running
	bool running () const noexcept;

	/// @brief Request service thread to terminate
	void terminate () noexcept;

	/// @brief Wait for service thread to terminate
	void join () noexcept;

	/// @brief Send CorePacket
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendCorePacket (rlbot::flat::CorePacketT const &packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send DisconnectSignal
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendDisconnectSignal (rlbot::flat::DisconnectSignalT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send GamePacket
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendGamePacket (rlbot::flat::GamePacketT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send FieldInfo
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendFieldInfo (rlbot::flat::FieldInfoT packet_, std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send MatchConfiguration
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendMatchConfiguration (rlbot::flat::MatchConfigurationT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send MatchComm
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendMatchComm (rlbot::flat::MatchCommT packet_, std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send BallPrediction
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendBallPrediction (rlbot::flat::BallPredictionT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send ControllableTeamInfo
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendControllableTeamInfo (rlbot::flat::ControllableTeamInfoT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send RenderingStatus
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendRenderingStatus (rlbot::flat::RenderingStatusT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send PingRequest
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendPingRequest (rlbot::flat::PingRequestT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

	/// @brief Send PingRequest
	/// @param packet_ Packet to send
	/// @param connectionId_ Connection ID (0 for all connections)
	void sendPingResponse (rlbot::flat::PingResponseT packet_,
	    std::uint64_t connectionId_ = 0) noexcept;

protected:
	/// @brief Remove conneciton
	/// @brief connectionId_ Connection ID
	void removeConnection (std::uint64_t connectionId_) noexcept;

private:
	/// @brief Handle message
	/// @param connectionId_ Connection ID
	/// @param message_ Message to handle
	virtual void handleMessage (std::uint64_t connectionId_, detail::Message &message_) noexcept;

	/// @brief Handle InterfacePacket
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleInterfacePacket (std::uint64_t connectionId_,
	    rlbot::flat::InterfacePacket const *packet_) noexcept;

	/// @brief Handle DisconnectSignal
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleDisconnectSignal (std::uint64_t connectionId_,
	    rlbot::flat::DisconnectSignal const *packet_) noexcept;

	/// @brief Handle StartCommand
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleStartCommand (std::uint64_t connectionId_,
	    rlbot::flat::StartCommand const *packet_) noexcept;

	/// @brief Handle MatchConfiguration
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleMatchConfiguration (std::uint64_t connectionId_,
	    rlbot::flat::MatchConfiguration const *packet_) noexcept;

	/// @brief Handle PlayerInput
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handlePlayerInput (std::uint64_t connectionId_,
	    rlbot::flat::PlayerInput const *packet_) noexcept;

	/// @brief Handle DesiredGameState
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleDesiredGameState (std::uint64_t connectionId_,
	    rlbot::flat::DesiredGameState const *packet_) noexcept;

	/// @brief Handle RenderGroup
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleRenderGroup (std::uint64_t connectionId_,
	    rlbot::flat::RenderGroup const *packet_) noexcept;

	/// @brief Handle RemoveRenderGroup
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleRemoveRenderGroup (std::uint64_t connectionId_,
	    rlbot::flat::RemoveRenderGroup const *packet_) noexcept;

	/// @brief Handle MatchComm
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleMatchComm (std::uint64_t connectionId_,
	    rlbot::flat::MatchComm const *packet_) noexcept;

	/// @brief Handle ConnectionSettings
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleConnectionSettings (std::uint64_t connectionId_,
	    rlbot::flat::ConnectionSettings const *packet_) noexcept;

	/// @brief Handle StopCommand
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleStopCommand (std::uint64_t connectionId_,
	    rlbot::flat::StopCommand const *packet_) noexcept;

	/// @brief Handle SetLoadout
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleSetLoadout (std::uint64_t connectionId_,
	    rlbot::flat::SetLoadout const *packet_) noexcept;

	/// @brief Handle InitComplete
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleInitComplete (std::uint64_t connectionId_,
	    rlbot::flat::InitComplete const *packet_) noexcept;

	/// @brief Handle RenderingStatus
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handleRenderingStatus (std::uint64_t connectionId_,
	    rlbot::flat::RenderingStatus const *packet_) noexcept;

	/// @brief Handle connection removed
	/// @param connectionId_ Connection ID
	virtual void handleConnectionRemoved (std::uint64_t connectionId_) noexcept;

	/// @brief Handle ping request
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handlePingRequest (std::uint64_t connectionId_,
	    rlbot::flat::PingRequest const *packet_) noexcept;

	/// @brief Handle ping response
	/// @param connectionId_ Connection ID
	/// @param packet_ Packet to handle
	virtual void handlePingResponse (std::uint64_t connectionId_,
	    rlbot::flat::PingResponse const *packet_) noexcept;

	/// @brief Called in service thread between comms
	/// @return true to continue looping, false to terminate
	virtual bool loop () noexcept;

	/// @brief Service thread
	void serviceThread () noexcept;

	/// @brief Implementation
	std::unique_ptr<detail::ServerImpl> m_impl;
};
}
