#pragma once

#include <rlbot/Bot.h>

#include <rlbot/Connection.h>
#include <rlbot/RLBotCPP.h>

#include <concepts>
#include <memory>
#include <string>
#include <unordered_set>

namespace rlbot
{
namespace detail
{
class BotManagerImpl;
}

/// @brief Bot manager base class
/// This should only be derived by the BotManager template class below
class RLBotCPP_API BotManagerBase : public Connection
{
public:
	/// @brief Destructor
	/// Waits for service threads to finish, but doesn't request termination
	/// You should call disconnect() first if you don't want to wait for server-initiated shutdown
	~BotManagerBase () noexcept override;

	/// @brief Connect to server
	/// @param host_ RLBotServer address
	/// @param service_ RLBotServer service (port)
	/// @param agentId_ Agent ID (optional, defaults to RLBOT_AGENT_ID environment variable)
	/// @param ballPrediction_ Whether to request ball prediction
	bool connect (char const *const host_,
	    char const *const service_,
	    char const *agentId_,
	    bool const ballPrediction_) noexcept;

protected:
	/// @brief Parameterized constructor
	/// @param batchHivemind_ Whether to batch hivemind
	/// @param spawn_ Bot spawning function
	BotManagerBase (bool batchHivemind_,
	    std::unique_ptr<Bot> (
	        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept;

private:
	/// @sa Connection::handleMessage
	void handleMessage (detail::Message &message_) noexcept override;

	/// @brief Bot manager implementation
	std::unique_ptr<detail::BotManagerImpl> m_impl;
};

/// @brief Bot manager
/// @tparam T Bot type
template <typename T>
    requires std::derived_from<T, Bot>
class BotManager final : public BotManagerBase
{
public:
	/// @brief Parameterized constructor
	/// @param batchHivemind_ Whether to batch hivemind
	explicit BotManager (bool const batchHivemind_ = false) noexcept
	    : BotManagerBase (batchHivemind_, BotManager::spawn)
	{
	}

private:
	/// @brief Bot spawning function
	/// @param indices_ Index into gameTickPacket->players ()
	/// @param team_ Team (0 = Blue, 1 = Orange)
	/// @param name_ Bot name
	static std::unique_ptr<Bot> spawn (std::unordered_set<unsigned> indices_,
	    unsigned const team_,
	    std::string name_) noexcept
	{
		return std::make_unique<T> (std::move (indices_), team_, std::move (name_));
	}
};
}
