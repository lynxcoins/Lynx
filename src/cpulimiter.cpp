#include <assert.h>

#include <algorithm>
#include <chrono>

#include <pthread.h>
#include <time.h>

#ifdef WIN32
#include <windows.h>
#endif

#include "cpulimiter.h"

namespace
{
    using ThreadId = std::thread::id;
    using ThreadHandle = std::thread::native_handle_type;
    using namespace std::chrono;

#ifdef WIN32
    const nanoseconds TimeSlot = milliseconds(1000);
#else
    const nanoseconds TimeSlot = milliseconds(100);
#endif

    const nanoseconds MinDT = milliseconds(20);
    const double Alfa = 0.08;

    bool getCpuTime(ThreadHandle thread, nanoseconds& res)
    {
#ifdef WIN32
        FILETIME createTime = {0, 0};
        FILETIME exitTime = {0, 0};
        FILETIME kernetTime = {0, 0};
        FILETIME userTime = {0, 0};
        HANDLE hThread = pthread_gethandle(thread);
        if (!GetThreadTimes(hThread, &createTime, &exitTime, &kernetTime, &userTime))
            return false;
        int64_t totalTime = static_cast<int64_t>(userTime.dwLowDateTime)
                + static_cast<int64_t>(kernetTime.dwLowDateTime)
                + (static_cast<int64_t>(userTime.dwHighDateTime) << 32)
                + (static_cast<int64_t>(kernetTime.dwHighDateTime) << 32);
        res = nanoseconds(totalTime * 100);
        return true;
#else
        clockid_t clock;
        if (pthread_getcpuclockid(thread, &clock) != 0)
            return false;

        timespec ts;
        if (clock_gettime(clock, &ts) != 0)
            return false;

        res = seconds(ts.tv_sec) + nanoseconds(ts.tv_nsec);
        return true;
#endif
    }
}


CCpuLimiter::LimitedThread::LimitedThread(std::thread& th) :
    id(th.get_id()),
    handle(th.native_handle())
{
}

CCpuLimiter::CCpuLimiter(double limit) :
    limit(limit)
{
    assert(limit >= 0);
    assert(limit <= 1);

    this->watcher = std::thread([this]() {
        this->main();
    });
}

CCpuLimiter::~CCpuLimiter()
{
    this->stop();
}

bool CCpuLimiter::contains(const std::thread& th) const
{
    std::lock_guard<std::mutex> lock(this->mutex);
    return this->checkContainsWithoutLock(th.get_id());
}

void CCpuLimiter::add(std::thread& th)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    if (this->checkContainsWithoutLock(th.get_id()))
        return;

    this->limitedThreads.push_back(LimitedThread(th));
}

void CCpuLimiter::remove(std::thread& th)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->limitedThreads.remove_if([&th](const LimitedThread& lt) {
        return lt.id == th.get_id();
    });
}

void CCpuLimiter::suspendMe()
{
    if (this->suspendFlag)
    {
        std::unique_lock<std::mutex> lock(this->mutex);
        // FIXME: Return back assert
        // The problem is caused by the fact that the thread itself can not add itself to CCpuLimiter,
        // and it can not be suspended either.
        // assert(this->checkContainsWithoutLock(std::this_thread::get_id()));
        while (this->suspendFlag)
            this->resumeCV.wait(lock);
    }
}

void CCpuLimiter::stop()
{
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->exitFlag = true;
        this->exitCV.notify_one();
    }

    if (watcher.joinable())
        watcher.join();
}

int CCpuLimiter::cpuCount()
{
    return static_cast<int>(std::thread::hardware_concurrency());
}

void CCpuLimiter::main()
{
    // Rate at which we are keeping active the processes (range 0-1)
	// 1 means that the process are using all the twork slice
    double totalLimit = this->limit * cpuCount();
    double workingRate = -1;
    this->lastUpdate = steady_clock::now();
    while (!this->exitFlag)
    {
        double cpuUsage = this->getTotalCpuUsage();
        if (cpuUsage < 0)
            workingRate = this->limit; // It's the 1st cycle, initialize workingrate
        else
            workingRate = std::min(workingRate / cpuUsage * totalLimit, 1.0);

        auto twork = duration_cast<nanoseconds>(TimeSlot * workingRate);
        this->resumeLimitedThreads();
        this->sleep(twork);

        auto tsleep = TimeSlot - twork;
        this->suspendLimitedThreads();
        this->sleep(tsleep);
    }

    this->resumeLimitedThreads();
}

bool CCpuLimiter::checkContainsWithoutLock(const ThreadId& id) const
{
    auto pred = [id](const LimitedThread& lt) {
        return lt.id == id;
    };
    auto it = std::find_if(this->limitedThreads.begin(),
                           this->limitedThreads.end(),
                           pred);
    return it != this->limitedThreads.end();
}

double CCpuLimiter::getTotalCpuUsage()
{
    std::lock_guard<std::mutex> lock(this->mutex);
    double fullCpuUsage = -1;
    auto now = steady_clock::now();
    nanoseconds dt = now - this->lastUpdate;
    if (dt >= MinDT)
    {
        for (auto it = this->limitedThreads.begin();
            it != this->limitedThreads.end();
            ++it)
        {
            if (it->cpuTime == nanoseconds())
            {
                if (!getCpuTime(it->handle, it->cpuTime))
                {
                    /* TODO: */
                }
                it->cpuUsage = -1;
                continue;
            }

            nanoseconds curCpuTime;
            if (!getCpuTime(it->handle, curCpuTime))
            {
                /* TODO Log */
                it->cpuTime = nanoseconds();
                it->cpuUsage = -1;
                continue;
            }

            auto cpudt = curCpuTime - it->cpuTime;
            auto sample = (1.0*cpudt.count()) / dt.count();
            if (it->cpuUsage == -1)
                it->cpuUsage = sample;
            else
                it->cpuUsage = (1.0 - Alfa) * it->cpuUsage + Alfa * sample;
            it->cpuTime = curCpuTime;

            if (fullCpuUsage < 0)
                fullCpuUsage = 0;
            fullCpuUsage += it->cpuUsage;
        }
        this->lastUpdate = now;
    }

    return fullCpuUsage;
}

void CCpuLimiter::resumeLimitedThreads()
{
    std::unique_lock<std::mutex> lock(this->mutex);
    this->suspendFlag = false;
    this->resumeCV.notify_all();
}

void CCpuLimiter::suspendLimitedThreads()
{
    std::unique_lock<std::mutex> lock(this->mutex);
    this->suspendFlag = true;
}

void CCpuLimiter::sleep(nanoseconds duration)
{
    std::unique_lock<std::mutex> lock(this->mutex);
    auto awakeningTime = steady_clock::now() + duration;
    while (!this->exitFlag)
    {
        if (this->exitCV.wait_until(lock, awakeningTime) == std::cv_status::timeout)
            return;
    }
}
