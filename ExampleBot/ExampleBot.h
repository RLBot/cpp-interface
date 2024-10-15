#pragma once

#include <rlbot/Bot.h>

#include <chrono>

class ExampleBot final : public rlbot::Bot
{
public:
	~ExampleBot () noexcept override;

	ExampleBot () noexcept = delete;

	ExampleBot (int index_, int team_, std::string name_) noexcept;

	ExampleBot (ExampleBot const &) noexcept = delete;

	ExampleBot (ExampleBot &&) noexcept = delete;

	ExampleBot &operator= (ExampleBot const &) noexcept = delete;

	ExampleBot &operator= (ExampleBot &&) noexcept = delete;

	rlbot::flat::ControllerState getOutput (rlbot::flat::GamePacket const *packet_,
	    rlbot::flat::BallPrediction const *ballPrediction_,
	    rlbot::flat::FieldInfo const *fieldInfo_,
	    rlbot::flat::MatchSettings const *matchSettings_) noexcept override;

	void matchComm (rlbot::flat::MatchComm const *matchComm_) noexcept override;

private:
	std::chrono::steady_clock::time_point const m_start = std::chrono::steady_clock::now ();
	bool m_comms                                        = false;
	bool m_rendered                                     = false;
	bool m_stateSet                                     = false;
};
