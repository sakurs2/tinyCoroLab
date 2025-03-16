#pragma once

#include <cassert>
#include <cstring>
#include <functional>
#include <liburing.h>
#include <sys/eventfd.h>
#ifdef ENABLE_POOLING
    #include <time.h>
#endif // ENABLE_POOLING

#include "config.h"
#include "coro/attribute.hpp"

namespace coro::uring
{
#define GETURINGNUM(number) (number & 0xffff)
#define SETTASKNUM          (uint64_t(1) << 32)

using ursptr     = io_uring_sqe*;
using urcptr     = io_uring_cqe*;
using urchandler = std::function<void(urcptr)>;

class uring_proxy
{
public:
    uring_proxy() noexcept
    {
        // must init in construct func
        m_efd = eventfd(0, 0);
        assert(m_efd >= 0 && "uring_proxy init event_fd failed");
    }

    ~uring_proxy() = default;

    auto init(unsigned int entry_length) noexcept -> void
    {
        // don't need to set m_para
        memset(&m_para, 0, sizeof(m_para));
#ifdef ENABLE_POOLING
        m_para.flags |= IORING_SETUP_SQPOLL;
        m_para.sq_thread_idle = config::kSqthreadIdle;
#endif // ENABLE_POOLING

        auto res = io_uring_queue_init_params(entry_length, &m_uring, &m_para);
        assert(res == 0 && "uring_proxy init uring failed");

        res = io_uring_register_eventfd(&m_uring, m_efd);
        assert(res == 0 && "uring_proxy bind event_fd to uring failed");
    }

    auto deinit() noexcept -> void
    {
        // this operation cost too much time, so don't call this function
        // io_uring_unregister_eventfd(&m_uring);

        close(m_efd);
        m_efd = -1;
        io_uring_queue_exit(&m_uring);
    }

    auto peek_uring() noexcept -> bool
    {
        urcptr cqe{nullptr};
        io_uring_peek_cqe(&m_uring, &cqe);
        return cqe != nullptr;
    }

    auto wait_uring(int num = 1) noexcept -> void
    {
        urcptr cqe;
        if (num == 1) [[likely]]
        {
            io_uring_wait_cqe(&m_uring, &cqe);
        }
        else
        {
            io_uring_wait_cqe_nr(&m_uring, &cqe, num);
        }
    }

    inline auto seen_cqe_entry(urcptr cqe) noexcept -> void CORO_INLINE { io_uring_cqe_seen(&m_uring, cqe); }

    inline auto get_free_sqe() noexcept -> ursptr CORO_INLINE { return io_uring_get_sqe(&m_uring); }

    inline auto submit() noexcept -> int CORO_INLINE { return io_uring_submit(&m_uring); }

    auto handle_for_each_cqe(urchandler f, bool mark_finish = false) noexcept -> size_t
    {
        urcptr   cqe;
        unsigned head;
        unsigned i = 0;
        io_uring_for_each_cqe(&m_uring, head, cqe)
        {
            f(cqe);
            i++;
        };
        if (mark_finish)
        {
            cq_advance(i);
        }
        return i;
    }

    auto wait_eventfd() noexcept -> uint64_t
    {
        uint64_t u;
        auto     ret = eventfd_read(m_efd, &u);
        assert(ret != -1 && "eventfd read error");
        return u;
    }

    inline auto peek_batch_cqe(urcptr* cqes, unsigned int num) noexcept -> int CORO_INLINE
    {
        return io_uring_peek_batch_cqe(&m_uring, cqes, num);
    }

    inline auto write_eventfd(uint64_t num) noexcept -> void CORO_INLINE
    {
        auto ret = eventfd_write(m_efd, num);
        assert(ret != -1 && "eventfd write error");
    }

    inline auto cq_advance(unsigned int num) noexcept -> void CORO_INLINE { io_uring_cq_advance(&m_uring, num); }

private:
    int             m_efd{0};
    io_uring_params m_para;
    io_uring        m_uring;
};

}; // namespace coro::uring