#include "ExampleBot.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <unordered_set>
using namespace std::chrono_literals;

namespace
{
template <typename T>
    requires std::floating_point<T>
constexpr auto STEER_GAIN = 2.0f;

template <typename T>
    requires std::floating_point<T>
constexpr T pi = std::numbers::pi_v<T>;

template <typename T>
    requires std::floating_point<T>
T angleWrap (T const angle_) noexcept
{
	auto const theta = std::fmod (angle_, static_cast<T> (2.0) * pi<T>);

	if (theta > pi<T>)
		return theta - static_cast<T> (2.0) * pi<T>;

	if (theta < -pi<T>)
		return theta + static_cast<T> (2.0) * pi<T>;

	return theta;
}

template <typename T>
    requires std::floating_point<T>
float angleSteer (T const angle_) noexcept
{
	return std::clamp (
	    STEER_GAIN<T> * angleWrap (angle_), static_cast<T> (-1.0f), static_cast<T> (1.0f));
}
}

///////////////////////////////////////////////////////////////////////////
ExampleBot::~ExampleBot () noexcept = default;

ExampleBot::ExampleBot (std::unordered_set<unsigned> indices_,
    unsigned const team_,
    std::string name_) noexcept
    : rlbot::Bot (std::move (indices_), team_, std::move (name_))
{
	std::set<unsigned> sorted (std::begin (indices), std::end (indices));
	for (auto const &index : sorted)
		std::printf ("Team %u Index %u: Example Bot created\n", team_, index);
}

void ExampleBot::update (rlbot::flat::GamePacket const *const packet_,
    rlbot::flat::BallPrediction const *const ballPrediction_,
    rlbot::flat::FieldInfo const *const fieldInfo_,
    rlbot::flat::MatchConfiguration const *const matchConfiguration_) noexcept
{
	auto const now = std::chrono::steady_clock::now ();

	auto const prevComms    = m_comms;
	auto const prevRendered = m_rendered;

	for (auto const &index : this->indices)
	{
		if (!prevComms && now - m_start > 5s)
		{
			// demonstrate sending of a match comm
			sendMatchComm (index, "Here is a team message", {}, true);
			m_comms = true;
		}
		else if (!prevRendered && now - m_start > 10s)
		{
			rlbot::flat::CarAnchorT carAnchor;
			carAnchor.index = index;
			carAnchor.local = std::make_unique<rlbot::flat::Vector3> ();

			rlbot::flat::BallAnchorT ballAnchor;
			ballAnchor.index = 0;
			ballAnchor.local = std::make_unique<rlbot::flat::Vector3> ();

			rlbot::flat::Line3DT line;

			line.start        = std::make_unique<rlbot::flat::RenderAnchorT> ();
			line.start->world = std::make_unique<rlbot::flat::Vector3> ();
			line.start->relative.Set (std::move (carAnchor));

			line.end        = std::make_unique<rlbot::flat::RenderAnchorT> ();
			line.end->world = std::make_unique<rlbot::flat::Vector3> ();
			line.end->relative.Set (std::move (ballAnchor));

			line.color = std::make_unique<rlbot::flat::Color> (255, 255, 255, 255);

			rlbot::flat::RenderMessageT render;
			render.variety.Set (line);

			// demonstrate sending of a render message
			// group id is unique to a connection (one per team with hivemind, one per bot without)
			sendRenderMessage (index + 100, std::move (render));
			sendMatchComm (index, "Enabled render", {}, true);
			m_rendered = true;
		}
		else if (!m_stateSet && now - m_start > 15s)
		{
			auto ballState     = std::make_unique<rlbot::flat::DesiredBallStateT> ();
			ballState->physics = std::make_unique<rlbot::flat::DesiredPhysicsT> ();

			ballState->physics->location    = std::make_unique<rlbot::flat::Vector3PartialT> ();
			ballState->physics->location->x = std::make_unique<rlbot::flat::Float> (0.0f);
			ballState->physics->location->y = std::make_unique<rlbot::flat::Float> (0.0f);
			ballState->physics->location->z = std::make_unique<rlbot::flat::Float> (0.0f);

			rlbot::flat::DesiredGameStateT state;
			state.ball_states.emplace_back (std::move (ballState));

			// demonstrate sending of desired game state
			sendDesiredGameState (state);
			sendMatchComm (index, "State set", {}, true);
			m_stateSet = true;
		}

		auto const balls   = packet_->balls ();
		auto const players = packet_->players ();
		if (balls->size () == 0 || players->size () <= index)
			continue;

		auto const ballPos = balls->Get (0)->physics ()->location ();
		auto const carPos  = players->Get (index)->physics ()->location ();
		auto const carRot  = players->Get (index)->physics ()->rotation ();

		auto const dx    = ballPos.x () - carPos.x ();
		auto const dy    = ballPos.y () - carPos.y ();
		auto const angle = std::atan2 (dy, dx);

		auto const steer     = angleSteer (angle - carRot.yaw ());
		auto const handbrake = std::abs (steer) >= 1.0f;

		outputs[index] = {1.0f, steer, 0.0f, 0.0f, 0.0f, false, false, handbrake, false};
	}
}

void ExampleBot::matchComm (rlbot::flat::MatchComm const *const matchComm_) noexcept
{
	auto const display = matchComm_->display ();
	if (display->size () != 0)
		std::printf ("To [%d:%d] From [%d:%d]: %s\n",
		    team,
		    std::ranges::min (indices),
		    matchComm_->team (),
		    matchComm_->index (),
		    display->c_str ());
}
