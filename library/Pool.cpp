#include "Pool.h"

#include "Log.h"

#include <tracy/Tracy.hpp>

#include <algorithm>
#include <cassert>

using namespace rlbot::detail;

///////////////////////////////////////////////////////////////////////////
template <typename T>
Pool<T>::Ref::~Ref () noexcept
{
	reset ();
}

template <typename T>
Pool<T>::Ref::Ref () noexcept = default;

template <typename T>
Pool<T>::Ref::Ref (Ref const &that_) noexcept
{
	*this = that_;
}

template <typename T>
Pool<T>::Ref::Ref (Ref &&that_) noexcept
{
	*this = std::move (that_);
}

template <typename T>
Pool<T>::Ref &Pool<T>::Ref::operator= (Ref const &that_) noexcept
{
	if (this != &that_) [[likely]]
	{
		reset ();

		// this is a new reference
		if (that_.m_object)
			that_.m_object->count.fetch_add (1, std::memory_order_relaxed);

		m_pool   = that_.m_pool;
		m_object = that_.m_object;
	}

	return *this;
}

template <typename T>
Pool<T>::Ref &Pool<T>::Ref::operator= (Ref &&that_) noexcept
{
	if (this != &that_) [[likely]]
	{
		reset ();

		// refcount unchanged

		m_pool   = std::move (that_.m_pool);
		m_object = std::move (that_.m_object);
	}

	return *this;
}

template <typename T>
Pool<T>::Ref::Ref (std::shared_ptr<Pool> pool_, Ref::CountedRef object_) noexcept
    : m_pool (std::move (pool_)), m_object (std::move (object_))
{
}

template <typename T>
Pool<T>::Ref::operator bool () const noexcept
{
	return static_cast<bool> (m_object);
}

template <typename T>
T *Pool<T>::Ref::operator->() noexcept
{
	assert (m_object);
	assert (m_object->count.load (std::memory_order_relaxed) > 0);
	return &m_object->ref;
}

template <typename T>
T const *Pool<T>::Ref::operator->() const noexcept
{
	assert (m_object);
	assert (m_object->count.load (std::memory_order_relaxed) > 0);
	return &m_object->ref;
}

template <typename T>
T &Pool<T>::Ref::operator* () noexcept
{
	assert (m_object);
	assert (m_object->count.load (std::memory_order_relaxed) > 0);
	return m_object->ref;
}

template <typename T>
T const &Pool<T>::Ref::operator* () const noexcept
{
	assert (m_object);
	assert (m_object->count.load (std::memory_order_relaxed) > 0);
	return m_object->ref;
}

template <typename T>
void Pool<T>::Ref::reset () noexcept
{
	assert (m_pool || !m_object);

	if (m_pool && m_object) [[likely]]
	{
		// pool will manage reference count
		m_pool->putObject (std::move (m_object));
	}
	else if (m_object) [[unlikely]]
	{
		// not going back into a pool; so manage reference count ourself
		assert (m_object->count.load (std::memory_order_relaxed) > 0);
		m_object->count.fetch_sub (1, std::memory_order_relaxed);
		m_object.reset ();
	}
}

#ifndef _WIN32
template <typename T>
std::uintptr_t Pool<T>::Ref::tag () const noexcept
{
	if (!m_object)
		return 0;

	return m_object->tag;
}

template <typename T>
void Pool<T>::Ref::setTag (std::uintptr_t const tag_) noexcept
{
	if (!m_object)
		return;

	m_object->tag = tag_;
}

template <typename T>
bool Pool<T>::Ref::preferred () const noexcept
{
	if (!m_object)
		return false;

	return m_object->preferred;
}

template <typename T>
void Pool<T>::Ref::setPreferred (bool const preferred_) noexcept
{
	if (!m_object)
		return;

	m_object->preferred = preferred_;
}
#endif

///////////////////////////////////////////////////////////////////////////
template <typename T>
Pool<T>::Private::Private () noexcept = default;

///////////////////////////////////////////////////////////////////////////
template <typename T>
Pool<T>::~Pool () noexcept
{
	debug ("Pool %s watermark %zu\n", m_name.c_str (), m_watermark);
}

template <typename T>
Pool<T>::Pool (Private, std::string name_, unsigned const reservations_) noexcept
    : m_name (std::move (name_)), m_watermark (reservations_)
{
	// preallocate reservations
	for (unsigned i = 0; i < reservations_; ++i)
		m_pool.emplace_back (std::make_shared<typename Ref::CountedRef::element_type> ());
}

template <typename T>
std::shared_ptr<Pool<T>> Pool<T>::create (std::string name_, unsigned const reservations_) noexcept
{
	auto ptr = std::make_shared<Pool<T>> (Private{}, std::move (name_), reservations_);
	return ptr;
}

template <typename T>
Pool<T>::Ref Pool<T>::getObject () noexcept
{
	ZoneScopedNS ("getObject", 16);
	typename Ref::CountedRef object;

	{
		auto const lock = std::scoped_lock (m_mutex);
#ifndef _WIN32
		if (!m_preferredPool.empty ()) [[likely]]
		{
			// collect object from preferred pool
			object = std::move (m_preferredPool.back ());
			m_preferredPool.pop_back ();
			assert (object->count.load (std::memory_order_relaxed) == 0);
			assert (object->preferred);
		}
		else
#endif
		    if (!m_pool.empty ()) [[likely]]
		{
			// collect object from preferred pool
			object = std::move (m_pool.back ());
			m_pool.pop_back ();
			assert (object->count.load (std::memory_order_relaxed) == 0);
			assert (!object->preferred);
		}
		else
		{
			// pool is empty; construct a new object
			object = std::make_shared<typename Ref::CountedRef::element_type> ();
		}
	}

	if constexpr (std::is_same_v<T, flatbuffers::FlatBufferBuilder>)
		object->ref.Clear ();

	object->count.fetch_add (1, std::memory_order_relaxed);
	return {this->shared_from_this (), std::move (object)};
}

template <typename T>
void Pool<T>::putObject (typename Ref::CountedRef object_) noexcept
{
	// check if we are the last reference
	assert (object_);
	assert (object_->count.load (std::memory_order_relaxed) > 0);
	if (object_->count.fetch_sub (1, std::memory_order_relaxed) > 1)
		return;

	assert (object_->count.load (std::memory_order_relaxed) == 0);

	// recycle object
	ZoneScopedNS ("putObject", 16);
	auto const lock = std::scoped_lock (m_mutex);

#ifdef _WIN32
	m_pool.emplace_back (std::move (object_));
	m_watermark = std::max (m_watermark, m_pool.size ());
#else
	(object_->preferred ? m_preferredPool : m_pool).emplace_back (std::move (object_));
	m_watermark = std::max (m_watermark, m_preferredPool.size () + m_pool.size ());
#endif
}

template class rlbot::detail::Pool<Buffer>;
template class rlbot::detail::Pool<flatbuffers::FlatBufferBuilder>;
