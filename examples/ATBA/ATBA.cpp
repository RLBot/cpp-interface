#include "ATBA.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstdio>
#include <cstdlib>
#include <numbers>
#include <unordered_set>

///////////////////////////////////////////////////////////////////////////
ATBA::~ATBA () noexcept = default;

ATBA::ATBA (std::unordered_set<unsigned> indices_, unsigned const team_, std::string name_) noexcept
    : rlbot::Bot (std::move (indices_), team_, std::move (name_))
{
	std::set<unsigned> sorted (std::begin (indices), std::end (indices));
	for (auto const &index : sorted)
		std::printf ("Team %u Index %u: Example Bot created\n", team_, index);
}

void ATBA::update (rlbot::flat::GamePacket const *const packet_,
    rlbot::flat::BallPrediction const *const ballPrediction_) noexcept
{
	for (auto const &index : this->indices)
	{
		// If there's no ball, there's nothing to chase; don't do anything
		if (packet_->balls ()->size () == 0)
		{
			setOutput (index, {});
			continue;
		}

		// We're not in the game packet; skip this tick
		if (packet_->players ()->size () <= index)
		{
			setOutput (index, {});
			continue;
		}

		auto const target = packet_->balls ()->Get (0)->physics ();
		auto const car    = packet_->players ()->Get (index)->physics ();

		auto const botToTargetAngle = std::atan2 (target->location ().y () - car->location ().y (),
		    target->location ().z () - car->location ().z ());

		auto botFrontToTargetAngle = botToTargetAngle - car->rotation ().yaw ();
		if (botFrontToTargetAngle > std::numbers::pi_v<float>)
			botFrontToTargetAngle -= 2.0f * std::numbers::pi_v<float>;
		if (botFrontToTargetAngle < -std::numbers::pi_v<float>)
			botFrontToTargetAngle += 2.0f * std::numbers::pi_v<float>;

		auto const throttle = 1.0f;
		auto const steer    = std::copysign (1.0f, botFrontToTargetAngle);

		setOutput (index, {throttle, steer, 0.0f, 0.0f, 0.0f, false, false, false, false});
	};
}
