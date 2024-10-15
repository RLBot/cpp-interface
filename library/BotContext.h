#pragma once

#include <rlbot/Bot.h>

#include "Event.h"
#include "Message.h"
#include "Pool.h"

#include <tracy/Tracy.hpp>

#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace rlbot::detail
{
class BotManagerImpl;

/// @brief Bot context
class BotContext
{
public:
	~BotContext () noexcept;

	/// @brief Parameterized constructor
	/// @param index_ Index into gamePacket->players ()
	/// @param bot_ Bot instance
	/// @param fieldInfo_ Field info
	/// @param matchSettings_ Match settings
	/// @param manager_ Bot manager
	explicit BotContext (unsigned index_,
	    std::unique_ptr<Bot> bot_,
	    Message fieldInfo_,
	    Message matchSettings_,
	    BotManagerImpl &manager_) noexcept;

	/// @brief Bot service thread
	void service () noexcept;

	/// @brief Terminate bot service thread
	void terminate () noexcept;

	/// @brief Set game packet
	/// @param gamePacket_ Game packet
	/// @note This triggers the bot's getOutput()
	void setGamePacket (Message gamePacket_) noexcept;

	/// @brief Set ball prediction
	/// @param ballPrediction_ Ball prediction
	void setBallPrediction (Message ballPrediction_) noexcept;

	/// @brief Add match comm
	/// @param matchComm_ Match comm
	/// @note This triggers the bot's matchComm()
	void addMatchComm (Message matchComm_) noexcept;

private:
	/// @brief Bot manager
	BotManagerImpl &m_manager;
	/// @brief Bot thread
	std::thread m_thread;
	/// @brief Event to wake up bot thread
	Event m_event = Event::create ();
	/// @brief Mutex
	TracyLockableN (std::mutex, m_mutex, "bot");
	/// @brief Bot instance
	std::unique_ptr<Bot> m_bot;

	/// @brief Pending match comms
	std::vector<Message> m_matchComms;
	/// @brief Game packet
	Message m_gamePacket;
	/// @brief Ball prediction
	Message m_ballPrediction;
	/// @brief Field info
	Message m_fieldInfo;
	/// @brief Match settings
	Message m_matchSettings;

	/// @brief index_ Index into gamePacket->players ()
	unsigned const m_index;

	/// @brief Signal to quit
	std::atomic_bool m_quit = false;
};
}
