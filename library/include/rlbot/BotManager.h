#pragma once

#include <rlbot/Bot.h>
#include <rlbot/RLBotCPP.h>

#include <rlbot_generated.h>

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
class RLBotCPP_API BotManagerBase
{
public:
	/// @brief Destructor
	/// Waits for service threads to finish, but doesn't request termination
	/// You should call terminate() first if you don't want to wait for server-initiated shutdown
	virtual ~BotManagerBase () noexcept;

	/// @brief Run bot manager
	/// @param host_ RLBotServer address
	/// @param port_ RLBotServer port
	/// @param agentId_ Agent id for connection
	/// @param ballPrediction_ Whether ball prediction is requested
	/// @return Whether bot manager was successfully started
	bool run (char const *host_,
	    char const *port_,
	    char const *agentId_,
	    bool ballPrediction_) noexcept;

	/// @brief Request bot manager to terminate
	void terminate () noexcept;

	/// @brief Start match
	/// @param matchSettings_ Match settings
	/// @note Uses this manager's agent to start a match
	void startMatch (rlbot::flat::MatchSettingsT const &matchSettings_) noexcept;

	/// @brief Start match
	/// @param host_ RLBotServer address
	/// @param port_ RLBotServer port
	/// @param matchSettings_ Match settings
	/// @note Uses a temporary agent to start a match
	static bool startMatch (char const *host_,
	    char const *port_,
	    rlbot::flat::MatchSettingsT const &matchSettings_) noexcept;

protected:
	/// @brief Parameterized constructor
	/// @param batchHivemind_ Whether to batch hivemind
	/// @param spawn_ Bot spawning function
	BotManagerBase (bool batchHivemind_,
	    std::unique_ptr<Bot> (
	        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept;

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
