#pragma once

#include <rlbot/RLBotCPP.h>

#include <rlbot_generated.h>

#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace rlbot
{
/// @brief Bot base class
class RLBotCPP_API Bot
{
public:
	virtual ~Bot () noexcept = default;

	Bot () noexcept = delete;

	Bot (Bot const &) noexcept = delete;

	Bot (Bot &&) noexcept = delete;

	Bot &operator= (Bot const &) noexcept = delete;

	Bot &operator= (Bot &&) noexcept = delete;

	/// @brief Get output from bot
	/// The bot manager will call this function on every received GamePacket
	/// @param gamePacket_ Game packet
	/// @param ballPrediction_ Ball prediction (can be null)
	/// @param fieldInfo_  Field info
	/// @param matchSettings_  Match settings
	/// @note The pointers are only valid for the duration of this call; make deep copies if
	/// necessary!
	virtual rlbot::flat::ControllerState getOutput (rlbot::flat::GamePacket const *gamePacket_,
	    rlbot::flat::BallPrediction const *ballPrediction_,
	    rlbot::flat::FieldInfo const *fieldInfo_,
	    rlbot::flat::MatchSettings const *matchSettings_) noexcept = 0;

	/// @brief Called when match comms are received for this bot
	/// @param matchComm_ Match comm
	/// @note Match comms from other bots in the same manager are also called here
	virtual void matchComm (rlbot::flat::MatchComm const *matchComm_) noexcept;

	/// @brief Get loadout for this bot
	/// This is called when the manager receives MatchSettings
	virtual std::optional<rlbot::flat::PlayerLoadoutT> getLoadout () noexcept;

	/// @brief Retrieves pending match comms
	/// This is called by the bot manager after getOutput
	std::optional<std::deque<rlbot::flat::MatchCommT>> getMatchComms () noexcept;

	/// @brief Retrieves pending desired game state
	/// This is called by the bot manager after getOutput
	/// @note If state setting is disabled, this message is discarded without being sent
	std::optional<rlbot::flat::DesiredGameStateT> getDesiredGameState () noexcept;

	/// @brief Retrieves render messages
	/// This is called by the bot manager after getOutput
	std::optional<std::unordered_map<int, std::vector<rlbot::flat::RenderMessageT>>>
	    getRenderMessages () noexcept;

	/// @brief Index into gamePacket->players ()
	unsigned const index;
	/// @brief Team (0 = Blue, 1 = Orange)
	unsigned const team;
	/// @brief Bot name
	std::string const name;

protected:
	/// @brief Parameterized constructor
	/// @param index_ Index into gamePacket->players ()
	/// @param team_ Team (0 = Blue, 1 = Orange)
	/// @param name_ Bot name
	Bot (unsigned index_, unsigned team_, std::string name_) noexcept;

	/// @brief Send match comms
	/// @param display_ Display value
	/// @param data_ Data
	/// @param teamOnly_ Whether to only send to teammates
	void sendMatchComm (std::string display_,
	    std::vector<std::uint8_t> data_,
	    bool teamOnly_) noexcept;

	/// @brief Send desired game state
	/// @param gameState_ Desired game state to send
	void sendDesiredGameState (rlbot::flat::DesiredGameStateT gameState_) noexcept;

	/// @brief Send render message
	/// @param group_ Render group id
	/// @param message_ Render message
	void sendRenderMessage (int group_, rlbot::flat::RenderMessageT message_) noexcept;

	/// @brief Clear render group
	/// @param group_ Render group id
	void clearRenderGroup (int group_) noexcept;

private:
	/// @brief Mutex
	std::mutex m_mutex;
	/// @brief Pending match comms
	std::optional<std::deque<rlbot::flat::MatchCommT>> m_matchComms;
	/// @brief Pending desired game state
	std::optional<rlbot::flat::DesiredGameStateT> m_gameState;
	/// @brief Pending render messages
	std::optional<std::unordered_map<int, std::vector<rlbot::flat::RenderMessageT>>>
	    m_renderMessages;
};
}
