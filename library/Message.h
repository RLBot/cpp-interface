#pragma once

#include "Pool.h"

#include <rlbot_generated.h>

#include <cstddef>
#include <cstdint>
#include <span>

namespace rlbot::detail
{
/// @brief Message type
enum class MessageType
{
	None,
	GamePacket,
	FieldInfo,
	StartCommand,
	MatchSettings,
	PlayerInput,
	DesiredGameState,
	RenderGroup,
	RemoveRenderGroup,
	MatchComm,
	BallPrediction,
	ConnectionSettings,
	StopCommand,
	SetLoadout,
	InitComplete,
	ControllableTeamInfo,
};

/// @brief message
class Message
{
public:
	~Message () noexcept;

	Message () noexcept;

	/// @brief Parameterized constructor
	/// @param buffer_ Buffer pool reference
	/// @param offset_ Offset into buffer where message header starts
	explicit Message (Pool<Buffer>::Ref buffer_, std::size_t offset_ = 0) noexcept;

	Message (Message const &that_) noexcept;

	Message (Message &&that_) noexcept;

	Message &operator= (Message const &that_) noexcept;

	Message &operator= (Message &&that_) noexcept;

	/// @brief bool cast operator
	/// Determines whether this message points into a valid buffer
	explicit operator bool () const noexcept;

	/// @brief Get message type
	MessageType type () const noexcept;

	/// @brief Get message size (excluding header)
	unsigned size () const noexcept;

	/// @brief Get message size (including header)
	unsigned sizeWithHeader () const noexcept;

	/// @brief Get message span (including header)
	std::span<std::uint8_t const> span () const noexcept;

	/// @brief Get flatbuffer which points into this message
	/// @tparam T Flatbuffer type
	/// @param verify_ Whether to verify contents
	/// @note Returns nullptr for invalid message
	template <typename T>
	T const *flatbuffer (bool verify_ = false) const noexcept;

	/// @brief Get buffer reference
	Pool<Buffer>::Ref buffer () const noexcept;

	/// @brief Reset message
	/// This makes the message invalid and releases the underlying buffer
	void reset () noexcept;

private:
	/// @brief Referenced buffer
	Pool<Buffer>::Ref m_buffer;
	/// @brief Offset into buffer where message header starts
	std::size_t m_offset = 0;
};

extern template rlbot::flat::GamePacket const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::FieldInfo const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::StartCommand const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::MatchSettings const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::PlayerInput const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::DesiredGameState const *Message::flatbuffer (
    bool verify_) const noexcept;
extern template rlbot::flat::RenderGroup const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::RemoveRenderGroup const *Message::flatbuffer (
    bool verify_) const noexcept;
extern template rlbot::flat::MatchComm const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::BallPrediction const *Message::flatbuffer (
    bool verify_) const noexcept;
extern template rlbot::flat::ConnectionSettings const *Message::flatbuffer (
    bool verify_) const noexcept;
extern template rlbot::flat::StopCommand const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::SetLoadout const *Message::flatbuffer (bool verify_) const noexcept;
extern template rlbot::flat::ControllableTeamInfo const *Message::flatbuffer (
    bool verify_) const noexcept;
}
