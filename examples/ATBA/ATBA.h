#pragma once

#include <rlbot/Bot.h>

#include <string>
#include <unordered_set>

/// @brief Always Towards Ball Agent
/// This bot blindly drives towards the ball
class ATBA final : public rlbot::Bot
{
public:
	~ATBA () noexcept override;

	ATBA () noexcept = delete;

	ATBA (std::unordered_set<unsigned> indices_, unsigned team_, std::string name_) noexcept;

	ATBA (ATBA const &) noexcept = delete;

	ATBA (ATBA &&) noexcept = delete;

	ATBA &operator= (ATBA const &) noexcept = delete;

	ATBA &operator= (ATBA &&) noexcept = delete;

	void update (rlbot::flat::GamePacket const *packet_,
	    rlbot::flat::BallPrediction const *ballPrediction_,
	    rlbot::flat::FieldInfo const *fieldInfo_,
	    rlbot::flat::MatchConfiguration const *matchConfig_) noexcept override;
};
