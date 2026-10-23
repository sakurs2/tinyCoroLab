#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "bench_helper.hpp"
#include "benchmark/benchmark.h"
#include "coro/coro.hpp"

using namespace coro;

static const int thread_num = std::thread::hardware_concurrency();

/*************************************************************
 *                        notifyall                          *
 *************************************************************/

template<typename cv_type, typename mtx_type>
void cv_notifyall_bench(const int loop_num);

void notify_all_tp(std::condition_variable& cv, std::mutex& mtx, int& gid, const int id, const int loop_num)
{
    std::unique_lock<std::mutex> lck(mtx);
    cv.wait(lck, [&]() -> bool { return id == gid; });
    loop_add;
    gid += 1;
    cv.notify_all();
}

static void threadpool_stl_cv_notifyall(benchmark::State& state)
{
    for (auto _ : state)
    {
        const int loop_num = state.range(0);

        thread_pool             pool;
        std::condition_variable cv;
        std::mutex              mtx;
        int                     gid = 0;

        for (int i = thread_num - 1; i >= 0; i--)
        {
            pool.submit_task([&, i]() { notify_all_tp(cv, mtx, gid, i, loop_num); });
        }
        pool.start();
        pool.join();
    }
}

CORO_BENCHMARK3(threadpool_stl_cv_notifyall, 100, 100000, 100000000);

task<> notify_all(std::condition_variable& cv, std::mutex& mtx, int& gid, const int id, const int loop_num)
{
    std::unique_lock<std::mutex> lck(mtx);
    cv.wait(lck, [&]() -> bool { return id == gid; });
    loop_add;
    gid += 1;
    cv.notify_all();
    co_return;
}

static void coro_stl_cv_notifyall(benchmark::State& state)
{
    for (auto _ : state)
    {
        const int loop_num = state.range(0);
        cv_notifyall_bench<std::condition_variable, std::mutex>(loop_num);
    }
}

CORO_BENCHMARK3(coro_stl_cv_notifyall, 100, 100000, 100000000);

task<> notify_all(condition_variable& cv, mutex& mtx, int& gid, const int id, const int loop_num)
{
    auto lock = co_await mtx.lock_guard();
    co_await cv.wait(mtx, [&]() { return gid == id; });
    loop_add;
    gid += 1;
    cv.notify_all();
}

static void coro_cv_notifyall(benchmark::State& state)
{
    for (auto _ : state)
    {
        const int loop_num = state.range(0);
        cv_notifyall_bench<condition_variable, mutex>(loop_num);
    }
}

CORO_BENCHMARK3(coro_cv_notifyall, 100, 100000, 100000000);

/*************************************************************
 *                        notifyone                          *
 *************************************************************/
const int notifyone_run_cnt = 1000;

template<typename cv_type, typename mtx_type>
void cv_notifyone_bench();

void notify_one_tp(std::condition_variable& cv, std::mutex& mtx, int& gid, const int id, int run_cnt)
{
    while (run_cnt > 0)
    {
        std::unique_lock<std::mutex> lck(mtx);
        cv.wait(lck, [&]() -> bool { return id == gid; });
        gid = (gid + 1) % 2;
        run_cnt -= 1;
        cv.notify_one();
    }
}

static void threadpool_stl_cv_notifyone(benchmark::State& state)
{
    for (auto _ : state)
    {
        thread_pool             pool;
        std::condition_variable cv;
        std::mutex              mtx;
        int                     gid = 0;

        for (int i = 0; i < 2; i++)
        {
            pool.submit_task([&, i]() { notify_one_tp(cv, mtx, gid, i, notifyone_run_cnt); });
        }
        pool.start();
        pool.join();
    }
}

CORO_BENCHMARK(threadpool_stl_cv_notifyone);

task<> notify_one(std::condition_variable& cv, std::mutex& mtx, int& gid, const int id, int run_cnt)
{
    while (run_cnt > 0)
    {
        std::unique_lock<std::mutex> lck(mtx);
        cv.wait(lck, [&]() -> bool { return id == gid; });
        gid = (gid + 1) % 2;
        run_cnt -= 1;
        cv.notify_one();
    }
    co_return;
}

static void coro_stl_cv_notifyone(benchmark::State& state)
{
    for (auto _ : state)
    {
        cv_notifyone_bench<std::condition_variable, std::mutex>();
    }
}

CORO_BENCHMARK(coro_stl_cv_notifyone);

task<> notify_one(condition_variable& cv, mutex& mtx, int& gid, const int id, int run_cnt)
{
    while (run_cnt > 0)
    {
        auto lock = co_await mtx.lock_guard();
        co_await cv.wait(mtx, [&]() { return id == gid; });
        gid = (gid + 1) % 2;
        run_cnt -= 1;
        cv.notify_one();
    }
}

static void coro_cv_notifyone(benchmark::State& state)
{
    for (auto _ : state)
    {
        cv_notifyone_bench<condition_variable, mutex>();
    }
}

CORO_BENCHMARK(coro_cv_notifyone);

BENCHMARK_MAIN();

template<typename cv_type, typename mtx_type>
void cv_notifyall_bench(const int loop_num)
{
    scheduler::init();

    cv_type  cv;
    mtx_type mtx;

    int gid = 0;

    for (int i = thread_num - 1; i >= 0; i--)
    {
        submit_to_scheduler(notify_all(cv, mtx, gid, i, loop_num));
    }

    scheduler::loop();
}

template<typename cv_type, typename mtx_type>
void cv_notifyone_bench()
{
    scheduler::init();

    cv_type  cv;
    mtx_type mtx;

    int gid = 0;

    for (int i = 0; i < 2; i++)
    {
        submit_to_scheduler(notify_one(cv, mtx, gid, i, notifyone_run_cnt));
    }

    scheduler::loop();
}