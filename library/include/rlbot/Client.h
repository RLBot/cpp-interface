#pragma once

#include <rlbot/RLBotCPP.h>

#include <corepacket_generated.h>
#include <interfacepacket_generated.h>

#include <memory>

namespace rlbot
{
namespace detail
{
class ClientImpl;
class Message;
}

class RLBotCPP_API Client
{
public:
	virtual ~Client () noexcept;

	Client () noexcept;

	Client (Client const &) = delete;

	Client &operator= (Client const &) = delete;

	Client (Client &&) = default;

	Client &operator= (Client &&) = default;

	/// @brief Connect to server
	/// @param host_ Host to connect to
	/// @param service_ Service (port) to connect to
	bool connect (char const *host_ = "127.0.0.1", char const *service_ = "23234") noexcept;

	/// @brief Check if connected to server
	bool connected () const noexcept;

	/// @brief Request service thread to terminate
	void terminate () noexcept;

	/// @brief Wait for service thread to terminate
	void join () noexcept;

	/// @brief Send InterfacePacket
	/// @param packet_ Packet to send
	void sendInterfacePacket (rlbot::flat::InterfacePacketT const &packet_) noexcept;

	/// @brief Send DisconnectSignal
	/// @param packet_ Packet to send
	void sendDisconnectSignal (rlbot::flat::DisconnectSignalT packet_) noexcept;

	/// @brief Send StartCommand
	/// @param packet_ Packet to send
	void sendStartCommand (rlbot::flat::StartCommandT packet_) noexcept;

	/// @brief Send MatchConfiguration
	/// @param packet_ Packet to send
	void sendMatchConfiguration (rlbot::flat::MatchConfigurationT packet_) noexcept;

	/// @brief Send PlayerInput
	/// @param packet_ Packet to send
	void sendPlayerInput (rlbot::flat::PlayerInputT packet_) noexcept;

	/// @brief Send DesiredGameState
	/// @param packet_ Packet to send
	void sendDesiredGameState (rlbot::flat::DesiredGameStateT packet_) noexcept;

	/// @brief Send RenderGroup
	/// @param packet_ Packet to send
	void sendRenderGroup (rlbot::flat::RenderGroupT packet_) noexcept;

	/// @brief Send RemoveRenderGroup
	/// @param packet_ Packet to send
	void sendRemoveRenderGroup (rlbot::flat::RemoveRenderGroupT packet_) noexcept;

	/// @brief Send MatchComm
	/// @param packet_ Packet to send
	void sendMatchComm (rlbot::flat::MatchCommT packet_) noexcept;

	/// @brief Send ConnectionSettings
	/// @param packet_ Packet to send
	void sendConnectionSettings (rlbot::flat::ConnectionSettingsT packet_) noexcept;

	/// @brief Send StopCommand
	/// @param packet_ Packet to send
	void sendStopCommand (rlbot::flat::StopCommandT packet_) noexcept;

	/// @brief Send SetLoadout
	/// @param packet_ Packet to send
	void sendSetLoadout (rlbot::flat::SetLoadoutT packet_) noexcept;

	/// @brief Send InitComplete
	/// @param packet_ Packet to send
	void sendInitComplete (rlbot::flat::InitCompleteT packet_) noexcept;

	/// @brief Send RenderingStatus
	/// @param packet_ Packet to send
	void sendRenderingStatus (rlbot::flat::RenderingStatusT packet_) noexcept;

private:
	/// @brief Handle message
	/// @param message_ Message to handle
	virtual void handleMessage (detail::Message &message_) noexcept;

	/// @brief Handle CorePacket
	/// @param packet_ Packet to handle
	virtual void handleCorePacket (rlbot::flat::CorePacket const *packet_) noexcept;

	/// @brief Handle DisconnectSignal
	/// @param packet_ Packet to handle
	virtual void handleDisconnectSignal (rlbot::flat::DisconnectSignal const *packet_) noexcept;

	/// @brief Handle GamePacket
	/// @param packet_ Packet to handle
	virtual void handleGamePacket (rlbot::flat::GamePacket const *packet_) noexcept;

	/// @brief Handle FieldInfo
	/// @param packet_ Packet to handle
	virtual void handleFieldInfo (rlbot::flat::FieldInfo const *packet_) noexcept;

	/// @brief Handle MatchConfiguration
	/// @param packet_ Packet to handle
	virtual void handleMatchConfiguration (rlbot::flat::MatchConfiguration const *packet_) noexcept;

	/// @brief Handle MatchComm
	/// @param packet_ Packet to handle
	virtual void handleMatchComm (rlbot::flat::MatchComm const *packet_) noexcept;

	/// @brief Handle BallPrediction
	/// @param packet_ Packet to handle
	virtual void handleBallPrediction (rlbot::flat::BallPrediction const *packet_) noexcept;

	/// @brief Handle ControllableTeamInfo
	/// @param packet_ Packet to handle
	virtual void handleControllableTeamInfo (
	    rlbot::flat::ControllableTeamInfo const *packet_) noexcept;

	/// @brief Handle RenderingStatus
	/// @param packet_ Packet to handle
	virtual void handleRenderingStatus (rlbot::flat::RenderingStatus const *packet_) noexcept;

	/// @brief Service thread
	void serviceThread () noexcept;

	/// @brief Handle read
	/// @param count_ Number of bytes read
	void handleRead (std::size_t count_) noexcept;

	/// @brief Handle write
	/// @param count_ Number of bytes written
	void handleWrite (std::size_t count_) noexcept;

	/// @brief Implementation
	std::unique_ptr<detail::ClientImpl> m_impl;
};
}
