#pragma once

#include <rlbot/BotManager.h>

#include "BotContext.h"
#include "Event.h"
#include "Message.h"
#include "Socket.h"
#include "WsaData.h"

#include <rlbot_generated.h>

#include <tracy/Tracy.hpp>

#include <array>
#include <atomic>
#include <climits>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace rlbot::detail
{
/// @brief Bot manager implementation
class BotManagerImpl
{
public:
	~BotManagerImpl () noexcept;

	/// @brief Parameterized constructor
	/// @param spawn_ Bot spawning function
	BotManagerImpl (std::unique_ptr<Bot> (&spawn_) (int, int, std::string) noexcept) noexcept;

	/// @brief Run bot manager
	/// @param host_ RLBotServer address
	/// @param port_ RLBotServer port
	/// @return Whether bot manager was successfully started
	bool run (char const *host_, char const *port_) noexcept;

	/// @brief Wait for writer to be idle (all output data has been written to socket)
	void waitForWriterIdle () noexcept;

	/// @brief Request service threads to terminate
	void terminate () noexcept;

	/// @brief Join bot manager threads
	void join () noexcept;

	/// @brief Build message
	/// @param type_ Message type
	/// @param builder_ Flatbuffers builder
	Message buildMessage (MessageType type_,
	    Pool<flatbuffers::FlatBufferBuilder>::Ref builder_) noexcept;

	/// @brief Enqueue message to writer thread
	/// @param message_ Message to enqueue
	void enqueueMessage (Message message_) noexcept;

	/// @brief Enqueue message to writer thread
	/// @param type_ Message type
	/// @param builder_ Flatbuffers builder
	void enqueueMessage (MessageType type_,
	    Pool<flatbuffers::FlatBufferBuilder>::Ref builder_) noexcept;

/// @brief Declare enqueueMessage for message type T
/// @param type_ Message type
#define DECLARE_ENQUEUE(type_) void enqueueMessage (rlbot::flat::type_##T const &message_) noexcept

	DECLARE_ENQUEUE (GamePacket);
	DECLARE_ENQUEUE (FieldInfo);
	DECLARE_ENQUEUE (StartCommand);
	DECLARE_ENQUEUE (MatchSettings);
	DECLARE_ENQUEUE (PlayerInput);
	DECLARE_ENQUEUE (DesiredGameState);
	DECLARE_ENQUEUE (RenderGroup);
	DECLARE_ENQUEUE (RemoveRenderGroup);
	DECLARE_ENQUEUE (MatchComm);
	DECLARE_ENQUEUE (BallPrediction);
	DECLARE_ENQUEUE (ConnectionSettings);
	DECLARE_ENQUEUE (StopCommand);
	DECLARE_ENQUEUE (SetLoadout);
	DECLARE_ENQUEUE (ControllableTeamInfo);

private:
	/// @brief Spawn bots
	void spawnBots () noexcept;

	/// @brief Clear bots
	void clearBots () noexcept;

	/// @brief Handle incoming message
	/// @param message_ Message to handle
	void handleMessage (Message message_) noexcept;

	/// @brief Reader service thread
	/// Reads data from RLBotServer
	void readerService () noexcept;

	/// @brief Writer service thread
	/// Writes data to RLBotServer
	void writerService () noexcept;

	/// @brief WSA data
	WsaData m_wsaData;

	/// @brief Bot spawner
	std::unique_ptr<Bot> (&m_spawn) (int, int, std::string) noexcept;

	/// @brief Reader thread
	std::thread m_readerThread;
	/// @brief Writer thread
	std::thread m_writerThread;
	/// @brief Signal to quit
	std::atomic_bool m_quit = false;
	/// @brief Whether manager is running
	std::atomic_bool m_running = false;

	/// @brief Reader event
	Event m_readerEvent;
	/// @brief Writer event
	Event m_writerEvent;

	std::mutex m_writerIdleMutex;
	std::condition_variable m_writerIdleCv;
	std::atomic_bool m_writerIdle = false;

	/// @brief Socket connected to RLBotServer
	TCPSocket m_sock;
	/// @brief Socket event
	Event m_sockEvent;

	/// @brief Bots
	std::unordered_map<std::uint32_t, BotContext> m_bots;

	/// @brief Output queue mutex
	TracyLockableN (std::mutex, m_outputQueueMutex, "outputQueue");
	/// @brief Output queue
	std::vector<Message> m_outputQueue;

	/// @brief Buffer pool
	std::shared_ptr<Pool<Buffer>> m_bufferPool = Pool<Buffer>::create ("Buffer");

	/// @brief Flatbuffer builder pool
	std::shared_ptr<Pool<flatbuffers::FlatBufferBuilder>> m_fbbPool =
	    Pool<flatbuffers::FlatBufferBuilder>::create ("FBB");

	/// @brief Controllable team info
	Message m_controllableTeamInfo;
	/// @brief Field info
	Message m_fieldInfo;
	/// @brief Match settings
	Message m_matchSettings;
};
}
