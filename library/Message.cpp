#include "Message.h"

#include "Log.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>

using namespace rlbot::detail;

///////////////////////////////////////////////////////////////////////////
Message::~Message () noexcept = default;

Message::Message () noexcept = default;

Message::Message (Pool<Buffer>::Ref buffer_, std::size_t const offset_) noexcept
    : m_buffer (std::move (buffer_)), m_offset (offset_)
{
}

Message::Message (Message const &that_) noexcept = default;

Message::Message (Message &&that_) noexcept = default;

Message &Message::operator= (Message const &that_) noexcept = default;

Message &Message::operator= (Message &&that_) noexcept = default;

Message::operator bool () const noexcept
{
	return static_cast<bool> (m_buffer);
}

MessageType Message::type () const noexcept
{
	assert (m_buffer);
	assert (m_offset <= m_buffer->size () - 2);
	return static_cast<MessageType> (
	    static_cast<unsigned> (m_buffer->operator[] (m_offset) << CHAR_BIT) |
	    m_buffer->operator[] (m_offset + 1));
}

unsigned Message::size () const noexcept
{
	assert (m_buffer);
	assert (m_offset <= m_buffer->size () - 4);
	return static_cast<unsigned> (m_buffer->operator[] (m_offset + 2) << CHAR_BIT) |
	       m_buffer->operator[] (m_offset + 3);
}

unsigned Message::sizeWithHeader () const noexcept
{
	return size () + 4;
}

std::span<std::uint8_t const> Message::span () const noexcept
{
	assert (m_buffer);
	return {&m_buffer->operator[] (m_offset), sizeWithHeader ()};
}

template <typename T>
T const *Message::flatbuffer (bool const verify_) const noexcept
{
	if (!m_buffer) [[unlikely]]
		return nullptr;

	assert (m_offset <= m_buffer->size () - 4);
	assert (m_offset < m_buffer->size ());
	assert (sizeWithHeader () < m_buffer->size ());
	assert (m_offset < m_buffer->size () - sizeWithHeader ());

	auto const root = flatbuffers::GetRoot<T> (&m_buffer->operator[] (m_offset + 4));
	if (verify_)
	{
		auto const payload = span ().subspan (4);
		auto verifier      = flatbuffers::Verifier (
            payload.data (), payload.size (), flatbuffers::Verifier::Options{});

		if (!root->Verify (verifier)) [[unlikely]]
		{
			warning ("Invalid flatbuffer\n");
			return nullptr;
		}
	}

	return root;
}

void Message::reset () noexcept
{
	m_buffer.reset ();
}

template rlbot::flat::GamePacket const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::FieldInfo const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::StartCommand const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::MatchSettings const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::PlayerInput const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::DesiredGameState const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::RenderGroup const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::RemoveRenderGroup const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::MatchComm const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::BallPrediction const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::ConnectionSettings const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::StopCommand const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::SetLoadout const *Message::flatbuffer (bool verify_) const noexcept;
template rlbot::flat::ControllableTeamInfo const *Message::flatbuffer (bool verify_) const noexcept;
