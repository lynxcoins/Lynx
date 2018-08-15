#pragma once


#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <list>


class CCpuLimiter final
{
    CCpuLimiter(const CCpuLimiter&) = delete;
    CCpuLimiter& operator=(const CCpuLimiter&) = delete;

public:
    explicit CCpuLimiter(double limit);
    ~CCpuLimiter();

    bool contains(const std::thread& th) const;
    void add(std::thread& th);
    void remove(std::thread& th);

    void suspendMe();

    void stop();

public:
    static int cpuCount();

private:
    using ThreadId = std::thread::id;
    using ThreadHandle = std::thread::native_handle_type;
    struct LimitedThread
    {
        explicit LimitedThread(std::thread& th);

        std::thread::id id;
        ThreadHandle handle;
        std::chrono::nanoseconds cpuTime;
        double cpuUsage = -1;
    };
    using LimitedThreads = std::list<LimitedThread>;

private:
    void main();
    bool checkContainsWithoutLock(const ThreadId& id) const; 
    double getTotalCpuUsage();
    void resumeLimitedThreads();
    void suspendLimitedThreads();
    void sleep(std::chrono::nanoseconds duration);

private:
    const double limit;

    mutable std::mutex mutex;
    std::condition_variable exitCV;
    bool exitFlag = false;

    std::condition_variable resumeCV;
    bool suspendFlag = false;

    std::thread watcher;
    LimitedThreads limitedThreads;
    std::chrono::steady_clock::time_point lastUpdate;
};
