#pragma once

#include <rlbot/BotManager.h>

#include "BotContext.h"
#include "Message.h"
#include "Socket.h"

#ifdef _WIN32
#include "WsaData.h"
#else
#include <liburing.h>
#endif

#include <rlbot_generated.h>

#include <tracy/Tracy.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
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
	/// @brief Number of preallocated buffers
	static constexpr auto PREALLOCATED_BUFFERS = 32;

	/// @brief Spawn bots
	void spawnBots () noexcept;

	/// @brief Clear bots
	void clearBots () noexcept;

	/// @brief Handle incoming message
	/// @param message_ Message to handle
	void handleMessage (Message message_) noexcept;

	/// @brief Request read
	void requestRead () noexcept;

	/// @brief Handle read
	/// @param count_ Number of bytes read
	void handleRead (std::size_t count_) noexcept;

	/// @brief Request write
	void requestWrite () noexcept;

	/// @brief Request write with lock held
	/// @param lock_ Held lock
	void requestWriteLocked (std::unique_lock<std::mutex> &lock_) noexcept;

	/// @brief Handle write
	/// @param count_ Number of bytes written
	void handleWrite (std::size_t count_) noexcept;

	/// @brief Service thread
	void serviceThread () noexcept;

	/// @brief Get buffer from pool
	Pool<Buffer>::Ref getBuffer () noexcept;

#ifdef _WIN32
	/// @brief WSA data
	WsaData m_wsaData;

	/// @brief IOCP handle
	HANDLE m_iocpHandle = nullptr;
	/// @brief WSARecv overlapped
	WSAOVERLAPPED m_inOverlapped;
	/// @brief WSASend overlapped
	WSAOVERLAPPED m_outOverlapped;
#else
	/// @brief Read iov
	iovec m_readIov;
	/// @brief io uring
	std::unique_ptr<io_uring, void (*) (io_uring *)> m_ring = {nullptr, nullptr};
	/// @brief io uring submission queue mutex
	std::mutex m_ringSQMutex;
	/// @brief io uring completion queue mutex
	std::mutex m_ringCQMutex;
	/// @brief io uring completion queue condition variable
	std::condition_variable m_ringCQCv;
	/// @brief Whether io uring completion queue is being waiting on
	std::atomic_bool m_ringCQBusy = false;
	/// @brief Whether io uring supports read
	bool m_ringRead = false;
	/// @brief Whether io uring supports write
	bool m_ringWrite = false;
	/// @brief Registered socket index
	SOCKET m_ringSocketFd;
	/// @brief Whether socket was registered
	int m_ringSocketFlag;
	/// @brief Discriminator for read event
	int m_inOverlapped;
	/// @brief Discriminator for write event
	int m_outOverlapped;
	/// @brief Discriminator for write queue event
	int m_writeQueueOverlapped;
	/// @brief Discriminator for quit event
	int m_quitOverlapped;
#endif

	/// @brief Bot spawner
	std::unique_ptr<Bot> (&m_spawn) (int, int, std::string) noexcept;

	/// @brief Service threads
	std::vector<std::thread> m_serviceThreads;
	/// @brief Signal to quit
	std::atomic_bool m_quit = false;
	/// @brief Whether manager is running
	std::atomic_bool m_running = false;

	std::condition_variable m_writerIdleCv;
	bool m_writerIdle = false;

	/// @brief Socket connected to RLBotServer
	SOCKET m_sock;

	/// @brief Bots
	std::deque<BotContext> m_bots;

	/// @brief Output queue mutex
	std::mutex m_writerMutex;

	/// @brief Buffer pool
	std::array<std::shared_ptr<Pool<Buffer>>, 4> m_bufferPools;
	/// @brief Buffer pool index for round-robining
	std::atomic_uint m_bufferPoolIndex = 0;

	/// @brief Flatbuffer builder pool
	std::shared_ptr<Pool<flatbuffers::FlatBufferBuilder>> m_fbbPool =
	    Pool<flatbuffers::FlatBufferBuilder>::create ("FBB");

	/// @brief Current read buffer
	Pool<Buffer>::Ref m_inBuffer;
	/// @brief Input begin pointer
	std::size_t m_inStartOffset = 0;
	/// @brief Input end pointer
	std::size_t m_inEndOffset = 0;

	/// @brief Current write buffer
	std::vector<IOVector> m_iov;
	/// @brief Output begin pointer
	std::size_t m_outStartOffset = 0;

	/// @brief Output queue
	std::vector<Message> m_outputQueue;

	/// @brief Controllable team info
	Message m_controllableTeamInfo;
	/// @brief Field info
	Message m_fieldInfo;
	/// @brief Match settings
	Message m_matchSettings;
};
}
