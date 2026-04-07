#pragma once

#include <rlbot/Bot.h>

#include <chrono>
#include <string>
#include <unordered_set>

class ExampleBot final : public rlbot::Bot
{
public:
	~ExampleBot () noexcept override;

	ExampleBot () noexcept = delete;

	ExampleBot (std::unordered_set<unsigned> indices_, unsigned team_, std::string name_) noexcept;

	ExampleBot (ExampleBot const &) noexcept = delete;

	ExampleBot (ExampleBot &&) noexcept = delete;

	ExampleBot &operator= (ExampleBot const &) noexcept = delete;

	ExampleBot &operator= (ExampleBot &&) noexcept = delete;

	void initialize (rlbot::flat::ControllableTeamInfo const *controllableTeamInfo_,
	    rlbot::flat::FieldInfo const *fieldInfo_,
	    rlbot::flat::MatchConfiguration const *matchConfiguration_) noexcept override;

	void update (rlbot::flat::GamePacket const *packet_,
	    rlbot::flat::BallPrediction const *ballPrediction_) noexcept override;

	void matchComm (rlbot::flat::MatchComm const *matchComm_) noexcept override;

	void pingResponse (std::chrono::microseconds latency_) noexcept override;

private:
	std::chrono::steady_clock::time_point m_start    = std::chrono::steady_clock::now ();
	std::chrono::steady_clock::time_point m_lastPing = std::chrono::steady_clock::now ();

	bool m_comms    = false;
	bool m_rendered = false;
	bool m_stateSet = false;
};
