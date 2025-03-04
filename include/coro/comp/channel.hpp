#pragma once

#include <array>
#include <atomic>
#include <bit>
#include <cstddef>
#include <optional>

#include "coro/comp/condition_variable.hpp"
#include "coro/comp/mutex.hpp"
#include "coro/concepts/common.hpp"
#include "coro/log.hpp"
#include "coro/task.hpp"

namespace coro
{

namespace detail
{
class channel_base
{
public:
    ~channel_base() noexcept { assert(part_closed() && "detected channel destruct with no_close state"); }

    void close() noexcept
    {
        std::atomic_ref<uint8_t>(m_close_state).store(part_close, std::memory_order_release);
        m_producer_cv.notify_all();
        m_consumer_cv.notify_all();
    }

    // TODO: Implement sync_close
    // task<> sync_close() noexcept {}

    // TODO: There are too many duplicated codes in send and recv of each channel,
    // use CRTP to fix it.

protected:
    inline bool complete_closed_atomic() noexcept
    {
        return std::atomic_ref<uint8_t>(m_close_state).load(std::memory_order_acquire) <= complete_close;
    }

    inline bool part_closed_atomic() noexcept
    {
        return std::atomic_ref<uint8_t>(m_close_state).load(std::memory_order_acquire) <= part_close;
    }

    inline bool part_closed() noexcept { return m_close_state <= part_close; }

protected:
    inline static constexpr uint8_t complete_close = 0;
    inline static constexpr uint8_t part_close     = 1;
    inline static constexpr uint8_t no_close       = 2;

    mutex              m_mtx;
    condition_variable m_producer_cv;
    condition_variable m_consumer_cv;
    alignas(std::atomic_ref<uint8_t>::required_alignment) uint8_t m_close_state{no_close};
};
}; // namespace detail

template<concepts::conventional_type T, size_t capacity = 0>
class channel : public detail::channel_base
{
    using data_type = std::optional<T>;

public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    task<bool> send(value_type&& value) noexcept
    {
        if (part_closed_atomic())
        {
            co_return false;
        }

        auto lock = co_await m_mtx.lock_guard();
        co_await m_producer_cv.wait(m_mtx, [this]() -> bool { return !full() || part_closed(); });

        if (part_closed())
        {
            co_return false;
        }

        m_array[m_tail] = std::forward<value_type>(value);
        m_tail++;
        m_num++;
        if (m_tail >= capacity)
        {
            m_tail = 0;
        }

        m_consumer_cv.notify_one();
        co_return true;
    }

    task<data_type> recv() noexcept
    {
        if (complete_closed_atomic())
        {
            co_return std::nullopt;
        }

        auto lock = co_await m_mtx.lock_guard();
        co_await m_consumer_cv.wait(m_mtx, [this]() -> bool { return !empty() || part_closed(); });
        if (empty() && part_closed())
        {
            m_close_state = complete_close;
            co_return std::nullopt;
        }

        data_type p = std::make_optional<T>(std::move(m_array[m_head]));
        m_head++;
        m_num--;
        if (m_head >= capacity)
        {
            m_head = 0;
        }

        m_producer_cv.notify_one();
        co_return p;
    }

private:
    inline bool empty() noexcept { return m_num == 0; }

    inline bool full() noexcept { return m_num == capacity; }

private:
    size_t                  m_head{0};
    size_t                  m_tail{0};
    size_t                  m_num{0};
    std::array<T, capacity> m_array;
};

template<concepts::conventional_type T, size_t capacity>
    requires(std::has_single_bit(capacity))
class channel<T, capacity> : public detail::channel_base
{
    using data_type              = std::optional<T>;
    static constexpr size_t mask = capacity - 1;

public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    task<bool> send(value_type&& value) noexcept
    {
        if (part_closed_atomic())
        {
            co_return false;
        }

        auto lock = co_await m_mtx.lock_guard();
        co_await m_producer_cv.wait(m_mtx, [this]() -> bool { return !full() || part_closed(); });

        if (part_closed())
        {
            co_return false;
        }

        m_array[tail()] = std::forward<value_type>(value);
        m_tail++;

        m_consumer_cv.notify_one();
        co_return true;
    }

    task<data_type> recv() noexcept
    {
        if (complete_closed_atomic())
        {
            co_return std::nullopt;
        }

        auto lock = co_await m_mtx.lock_guard();
        co_await m_consumer_cv.wait(m_mtx, [this]() -> bool { return !empty() || part_closed(); });
        if (empty() && part_closed())
        {
            m_close_state = complete_close;
            co_return std::nullopt;
        }

        data_type p = std::make_optional<T>(std::move(m_array[head()]));
        m_head++;

        m_producer_cv.notify_one();
        co_return p;
    }

private:
    inline size_t head() noexcept { return m_head & mask; }

    inline size_t tail() noexcept { return m_tail & mask; }

    inline bool empty() noexcept { return m_head == m_tail; }

    inline bool full() noexcept { return (m_tail - m_head) == capacity; }

private:
    size_t                  m_head{0};
    size_t                  m_tail{0};
    std::array<T, capacity> m_array;
};

template<concepts::conventional_type T>
class channel<T, 0> : public detail::channel_base
{
    using data_type = std::optional<T>;

public:
    template<typename value_type>
        requires(std::is_constructible_v<T, value_type &&>)
    task<bool> send(value_type&& value) noexcept
    {
        if (part_closed_atomic())
        {
            co_return false;
        }

        auto lock = co_await m_mtx.lock_guard();
        co_await m_producer_cv.wait(m_mtx, [this]() -> bool { return !full() || part_closed(); });

        if (part_closed())
        {
            co_return false;
        }

        m_data = std::make_optional<T>(std::forward<value_type>(value));

        m_consumer_cv.notify_one();
        co_return true;
    }

    task<data_type> recv() noexcept
    {
        if (complete_closed_atomic())
        {
            co_return std::nullopt;
        }

        auto lock = co_await m_mtx.lock_guard();
        co_await m_consumer_cv.wait(m_mtx, [this]() -> bool { return !empty() || part_closed(); });
        if (empty() && part_closed())
        {
            m_close_state = complete_close;
            co_return std::nullopt;
        }

        auto p = std::move(m_data);
        m_data = std::nullopt;

        m_producer_cv.notify_one();
        co_return p;
    }

private:
    inline bool empty() noexcept { return !m_data.has_value(); }
    inline bool full() noexcept { return m_data.has_value(); }

private:
    data_type m_data{std::nullopt};
};

}; // namespace coro
