#pragma once

#include <rlbot/Bot.h>
#include <rlbot/Client.h>

#include "Message.h"
#include "Pool.h"

#include <atomic>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

namespace rlbot::detail
{
/// @brief Bot context
class BotContext
{
public:
	~BotContext () noexcept;

	/// @brief Parameterized constructor
	/// @param indices_ Index into gamePacket->players ()
	/// @param bot_ Bot instance
	/// @param controllableTeamInfo_ Controllable team info
	/// @param fieldInfo_ Field info
	/// @param matchConfiguration_ Match settings
	/// @param connection Connection to the RLBot server
	explicit BotContext (std::unordered_set<unsigned> indices_,
	    std::unique_ptr<Bot> bot_,
	    Message controllableTeamInfo_,
	    Message fieldInfo_,
	    Message matchConfiguration_,
	    Client &connection_) noexcept;

	/// @brief Initialize bot
	void initialize () noexcept;

	/// @brief Wait for bot initialization
	void waitInitialized () noexcept;

	/// @brief Start bot service thread
	void startService () noexcept;

	/// @brief Run service loop once
	void loopOnce () noexcept;

	/// @brief Terminate bot service thread
	void terminate () noexcept;

	/// @brief Set game packet
	/// @param gamePacket_ Game packet
	/// @param notify_ Whether to notify thread wakeup
	/// @note This triggers the bot's getOutput()
	void setGamePacket (Message gamePacket_, bool notify_) noexcept;

	/// @brief Set ball prediction
	/// @param ballPrediction_ Ball prediction
	void setBallPrediction (Message ballPrediction_) noexcept;

	/// @brief Add match comm
	/// @param matchComm_ Match comm
	/// @param notify_ Whether to notify thread wakeup
	/// @note This triggers the bot's matchComm()
	void addMatchComm (Message matchComm_, bool notify_) noexcept;

	/// @brief index_ Index into gamePacket->players ()
	std::unordered_set<unsigned> const indices;

private:
	/// @brief Bot service loop
	bool serviceLoop (std::unique_lock<std::mutex> &lock_) noexcept;

	/// @brief Bot service thread
	void service () noexcept;

	/// @brief Connection to the RLBot server
	Client &m_connection;
	/// @brief Bot thread
	std::thread m_thread;
	/// @brief Mutex
	std::mutex m_mutex;
	/// @brief Condition variable
	std::condition_variable m_cv;
	/// @brief Bot instance
	std::unique_ptr<Bot> m_bot;

	/// @brief Initialization promise
	std::promise<void> m_intializedPromise;
	/// @brief Initialization future
	std::future<void> m_intialized;

	/// @brief Player input
	std::unique_ptr<rlbot::flat::ControllerState> m_input;

	/// @brief Pending match comms
	std::vector<Message> m_matchCommsIn;
	/// @brief Working match comms
	std::vector<Message> m_matchCommsWork;
	/// @brief Game packet message
	Message m_gamePacketMessage;
	/// @brief Ball prediction message
	Message m_ballPredictionMessage;
	/// @brief Controllable team info message
	Message m_controllableTeamInfoMessage;
	/// @brief Field info message
	Message m_fieldInfoMessage;
	/// @brief Match settings message
	Message m_matchConfigurationMessage;

	/// @brief Controllable team info
	rlbot::flat::ControllableTeamInfo const *m_controllableTeamInfo = nullptr;
	/// @brief Field info
	rlbot::flat::FieldInfo const *m_fieldInfo = nullptr;
	/// @brief Match settings
	rlbot::flat::MatchConfiguration const *m_matchConfiguration = nullptr;

	/// @brief Signal to quit
	std::atomic_bool m_quit = false;
};
}
