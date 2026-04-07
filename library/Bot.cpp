#include <rlbot/Bot.h>
using namespace rlbot;

///////////////////////////////////////////////////////////////////////////
Bot::Bot (std::unordered_set<unsigned> indices_, unsigned team_, std::string name_) noexcept
    : indices (std::move (indices_)), team (team_), name (std::move (name_))
{
}

void Bot::initialize (rlbot::flat::ControllableTeamInfo const *const controllableTeamInfo_,
    rlbot::flat::FieldInfo const *const fieldInfo_,
    rlbot::flat::MatchConfiguration const *const matchConfiguration_) noexcept
{
	(void)controllableTeamInfo_;
	(void)fieldInfo_;
	(void)matchConfiguration_;
}

rlbot::flat::ControllerState Bot::getOutput (unsigned const index_) noexcept
{
	auto const lock = std::scoped_lock (m_mutex);
	return m_outputs[index_];
}

void Bot::matchComm (rlbot::flat::MatchComm const *const matchComm_) noexcept
{
	(void)matchComm_;
}

void Bot::pingResponse (std::chrono::microseconds const latency_) noexcept
{
	(void)latency_;
}

std::optional<rlbot::flat::PlayerLoadoutT> Bot::getLoadout (unsigned const index_) noexcept
{
	// no loadout
	(void)index_;
	return std::nullopt;
}

std::optional<std::vector<rlbot::flat::MatchCommT>> Bot::getMatchComms () noexcept
{
	auto const lock = std::scoped_lock (m_mutex);
	if (m_matchComms.empty ())
		return std::nullopt;

	return std::move (m_matchComms);
}

std::optional<rlbot::flat::DesiredGameStateT> Bot::getDesiredGameState () noexcept
{
	std::optional<rlbot::flat::DesiredGameStateT> result;

	{
		auto const lock = std::scoped_lock (m_mutex);
		if (!m_gameState.has_value ())
			return std::nullopt;

		result = std::move (m_gameState);
		m_gameState.reset ();
	}

	return result;
}

std::optional<std::unordered_map<int, std::vector<rlbot::flat::RenderMessageT>>>
    Bot::getRenderMessages () noexcept
{
	std::optional<std::unordered_map<int, std::vector<rlbot::flat::RenderMessageT>>> result;
	{
		auto const lock = std::scoped_lock (m_mutex);
		if (!m_renderMessages.has_value ())
			return std::nullopt;

		result = std::move (m_renderMessages);
		m_renderMessages.reset ();
	}

	return result;
}

bool rlbot::Bot::getPingRequested () noexcept
{
	auto const lock = std::scoped_lock (m_mutex);

	auto const requested = m_pingRequested;

	m_pingRequested = false;

	return requested;
}

void rlbot::Bot::setOutput (unsigned index_, rlbot::flat::ControllerState const &output_) noexcept
{
	if (!indices.contains (index_))
		return;

	auto const lock   = std::scoped_lock (m_mutex);
	m_outputs[index_] = output_;
}

void Bot::sendMatchComm (unsigned const index_,
    std::string display_,
    std::vector<std::uint8_t> data_,
    bool const teamOnly_) noexcept
{
	if (!indices.contains (index_) || (display_.empty () && data_.empty ()))
		return;

	auto const lock = std::scoped_lock (m_mutex);

	auto &matchComm = m_matchComms.emplace_back ();

	matchComm.index     = index_;
	matchComm.team      = team;
	matchComm.team_only = teamOnly_;
	matchComm.display   = std::move (display_);
	matchComm.content   = std::move (data_);
}

void Bot::sendPingRequest () noexcept
{
	auto const lock = std::scoped_lock (m_mutex);

	m_pingRequested = true;
}

void Bot::sendDesiredGameState (rlbot::flat::DesiredGameStateT gameState_) noexcept
{
	auto const lock = std::scoped_lock (m_mutex);

	m_gameState = std::move (gameState_);
}

void Bot::sendRenderMessage (int const group_, rlbot::flat::RenderMessageT message_) noexcept
{
	auto const lock = std::scoped_lock (m_mutex);

	if (!m_renderMessages.has_value ())
		m_renderMessages.emplace ();

	m_renderMessages->operator[] (group_).emplace_back (std::move (message_));
}

void Bot::clearRenderGroup (int const group_) noexcept
{
	auto const lock = std::scoped_lock (m_mutex);

	if (!m_renderMessages.has_value ())
		m_renderMessages.emplace ();

	m_renderMessages->operator[] (group_).clear ();
}
