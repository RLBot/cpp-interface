#include <rlbot/BotManager.h>

#include <rlbot/Bot.h>

#include "BotManagerImpl.h"
#include "Log.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <unordered_set>

using namespace rlbot;
using namespace rlbot::detail;

namespace
{
/// @brief Dummy spawn function
std::unique_ptr<Bot> spawnNothing (std::unordered_set<unsigned>, unsigned, std::string) noexcept
{
	return {};
}
}

///////////////////////////////////////////////////////////////////////////
BotManagerBase::~BotManagerBase () noexcept
{
	m_impl->join ();
}

BotManagerBase::BotManagerBase (bool const batchHivemind_,
    std::unique_ptr<Bot> (
        &spawn_) (std::unordered_set<unsigned>, unsigned, std::string) noexcept) noexcept
    : m_impl (std::make_unique<detail::BotManagerImpl> (batchHivemind_, spawn_))
{
}

bool BotManagerBase::run (char const *const host_,
    char const *const port_,
    char const *agentId_,
    bool const ballPrediction_) noexcept
{
	if (!agentId_)
	{
		agentId_ = std::getenv ("RLBOT_AGENT_ID");
		if (!agentId_ || std::strlen (agentId_) == 0)
		{
			error ("No agent id provided\n");
			return false;
		}
	}

	if (!m_impl->run (host_, port_))
		return false;

	rlbot::flat::ConnectionSettingsT cs{
	    //
	    .agent_id               = agentId_,
	    .wants_ball_predictions = ballPrediction_,
	    .wants_comms            = true,
	    .close_between_matches  = true,
	    //
	};

	m_impl->enqueueMessage (cs);

	return true;
}

void rlbot::BotManagerBase::terminate () noexcept
{
	m_impl->terminate ();
}

void rlbot::BotManagerBase::startMatch (
    rlbot::flat::MatchConfigurationT const &matchConfiguration_) noexcept
{
	m_impl->enqueueMessage (matchConfiguration_);
}

bool rlbot::BotManagerBase::startMatch (char const *const host_,
    char const *const port_,
    rlbot::flat::MatchConfigurationT const &matchConfiguration_) noexcept
{
	auto agent = std::make_unique<detail::BotManagerImpl> (false, spawnNothing);
	if (!agent->run (host_, port_))
		return false;

	agent->enqueueMessage (matchConfiguration_);

	agent->waitForWriterIdle ();
	agent->terminate ();

	return true;
}
