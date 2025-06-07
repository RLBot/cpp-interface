#include "Message.h"

#include "Log.h"

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>

using namespace rlbot::detail;

namespace
{
template <typename T>
T const *decodeFlatbuffer (Pool<Buffer>::Ref const &buffer_,
    std::size_t const offset_,
    bool const verify_)
{
	if (!buffer_) [[unlikely]]
		return nullptr;

	assert (Message::HEADER_SIZE <= buffer_->size ());
	assert (offset_ <= buffer_->size () - Message::HEADER_SIZE);

	auto const size = static_cast<unsigned> (buffer_->operator[] (offset_ + 0) << CHAR_BIT) |
	                  buffer_->operator[] (offset_ + 1);

	assert (size <= buffer_->size () - Message::HEADER_SIZE);
	assert (offset_ <= buffer_->size () - Message::HEADER_SIZE - size);

	auto const payload =
	    std::span<std::uint8_t const>{&buffer_->operator[] (offset_ + Message::HEADER_SIZE), size};

	auto const root = flatbuffers::GetRoot<T> (payload.data ());
	if (verify_)
	{
		auto verifier = flatbuffers::Verifier (
		    payload.data (), payload.size (), flatbuffers::Verifier::Options{});

		if (!root->Verify (verifier)) [[unlikely]]
		{
			warning ("Invalid flatbuffer\n");
			return nullptr;
		}
	}

	return root;
}
}

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

unsigned Message::size () const noexcept
{
	assert (m_buffer);
	assert (m_offset + HEADER_SIZE <= m_buffer->size ());
	return static_cast<unsigned> (m_buffer->operator[] (m_offset + 0) << CHAR_BIT) |
	       m_buffer->operator[] (m_offset + 1);
}

unsigned Message::sizeWithHeader () const noexcept
{
	return size () + HEADER_SIZE;
}

std::span<std::uint8_t const> Message::span () const noexcept
{
	assert (m_buffer);
	return {&m_buffer->operator[] (m_offset), sizeWithHeader ()};
}

rlbot::flat::InterfacePacket const *Message::interfacePacket (bool const verify_) const noexcept
{
	return decodeFlatbuffer<rlbot::flat::InterfacePacket> (m_buffer, m_offset, verify_);
}

rlbot::flat::CorePacket const *Message::corePacket (bool const verify_) const noexcept
{
	return decodeFlatbuffer<rlbot::flat::CorePacket> (m_buffer, m_offset, verify_);
}

Pool<Buffer>::Ref Message::buffer () const noexcept
{
	return m_buffer;
}

void Message::reset () noexcept
{
	m_buffer.reset ();
}
