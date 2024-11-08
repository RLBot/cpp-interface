#include <rlbot/Bot.h>
using namespace rlbot;

///////////////////////////////////////////////////////////////////////////
Bot::Bot (std::unordered_set<unsigned> indices_, unsigned team_, std::string name_) noexcept
    : indices (std::move (indices_)), team (team_), name (std::move (name_))
{
}

void Bot::matchComm (rlbot::flat::MatchComm const *const matchComm_) noexcept
{
	(void)matchComm_;
}

std::optional<rlbot::flat::PlayerLoadoutT> Bot::getLoadout (unsigned const index_) noexcept
{
	// no loadout
	(void)index_;
	return std::nullopt;
}

std::optional<std::deque<rlbot::flat::MatchCommT>> Bot::getMatchComms () noexcept
{
	std::optional<std::deque<rlbot::flat::MatchCommT>> result;

	{
		auto const lock = std::scoped_lock (m_mutex);
		if (!m_matchComms.has_value ())
			return std::nullopt;

		result = std::move (m_matchComms);
		m_matchComms.reset ();
	}

	return result;
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

void Bot::sendMatchComm (unsigned const index_,
    std::string display_,
    std::vector<std::uint8_t> data_,
    bool const teamOnly_) noexcept
{
	if (!indices.contains (index_) || (display_.empty () && data_.empty ()))
		return;

	auto const lock = std::scoped_lock (m_mutex);

	if (!m_matchComms.has_value ())
		m_matchComms.emplace ();

	auto &matchComm = m_matchComms->emplace_back ();

	matchComm.index     = index_;
	matchComm.team      = team;
	matchComm.team_only = teamOnly_;
	matchComm.display   = std::move (display_);
	matchComm.content   = std::move (data_);
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
