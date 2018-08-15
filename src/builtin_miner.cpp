#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "chain.h"
#include "chainparams.h"
#include "cpulimiter.h"
#include "univalue.h"
#include "util.h"
#include "validation.h"
#include "wallet/wallet.h"

#include "wallet/rpcwallet.h"
#include "rpc/mining.h"

#include "builtin_miner.h"


namespace
{
    std::mutex mutex;
    double cpuLimit = 0.05;
    bool running = false;
    std::list<std::thread> workThreads;
    std::unique_ptr<CCpuLimiter> cpuLimiter;
    CWallet* wallet = nullptr;

    bool getScriptForMining(std::shared_ptr<CReserveScript>& script, int& nHeight)
    {
#ifdef ENABLE_WALLET
        int newHeight;
        {
            LOCK(cs_main);
            newHeight = chainActive.Height();
        }
        if (newHeight == nHeight && script != nullptr)
            return true;

        try
        {
            GetScriptForMining(wallet, script);
            nHeight = newHeight;
            return script != nullptr;
        }
        catch (const UniValue&)
        {
            return false;
        }
#else
    return false;
#endif
    }

    void generateBlock(std::shared_ptr<CReserveScript>& script)
    {
        uint64_t nMaxTries = 0x10000;
        try
        {
            generateBlocks(script, 1, nMaxTries, true);
        }
        catch (const UniValue&)
        {
        }
    }

    void generateBlocks()
    {
        int nHeight = -1;
        std::shared_ptr<CReserveScript> script;
        
        auto& params = Params();
        auto& consensus = params.GetConsensus();
        while (running)
        {
            cpuLimiter->suspendMe();
            
            if (!getScriptForMining(script, nHeight))
                continue;

            generateBlock(script);
        }
    }

    CWallet* getWallet()
    {
#ifdef ENABLE_WALLET
        if (vpwallets.empty())
            return nullptr;
        return vpwallets[0];
#else
        return nullptr;
#endif
    }

    void doStart()
    {
        wallet = getWallet();
        if (wallet == nullptr)
            throw std::runtime_error("Unable to start the built-in miner: wallet is disabled");

        running = true;
        cpuLimiter.reset(new CCpuLimiter(cpuLimit));
        int threadCount = CCpuLimiter::cpuCount();
        for (int i = 0; i < threadCount; ++i)
        {
            workThreads.emplace_back(&generateBlocks);
            cpuLimiter->add(workThreads.back());
        }
    }

    void doStop()
    {
        running = false;
        
        if (cpuLimiter != nullptr)
            cpuLimiter->stop();        
        for (auto it = workThreads.begin(); it != workThreads.end(); ++it)
            it->join();

        workThreads.clear();
        cpuLimiter = nullptr;
    }
}

void BuiltinMiner::setCpuLimit(double limit)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (limit <= 0 || limit >= 1)
        throw std::runtime_error("Unable to set cpulimit: cpulimit must be greater than 0, but less than 1");
    if (running)
        throw std::runtime_error("Unable to set cpulimit: the built-in miner is active");
    cpuLimit = limit;
    LogPrintf("A new cpuLimit value for BuiltinMiner has been set: %lf", cpuLimit);
}

double BuiltinMiner::getCpuLimit()
{
    std::lock_guard<std::mutex> lock(mutex);
    return cpuLimit;
}

void BuiltinMiner::start()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (running)
        throw std::runtime_error("Unable to start the built-in miner: the built-in miner is active");

    try
    {
        doStart();
    }
    catch (...)
    {
        doStop();
        throw;
    }
}

void BuiltinMiner::stop()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (running)
        doStop();
}

bool BuiltinMiner::isRunning()
{
    std::lock_guard<std::mutex> lock(mutex);
    return running;
}
