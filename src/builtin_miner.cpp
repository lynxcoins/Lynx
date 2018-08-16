#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "chain.h"
#include "chainparams.h"
#include "cpulimiter.h"
#include "tinyformat.h"
#include "ui_interface.h"
#include "univalue.h"
#include "util.h"
#include "validation.h"

#include "wallet/wallet.h"
#include "wallet/rpcwallet.h"
#include "rpc/mining.h"

#include "builtin_miner.h"


namespace
{
    using tfm::format; 

    const double DefaultCpuLimit = 0.05;

    std::mutex mutex;
    double cpuLimit = DefaultCpuLimit;
    bool running = false;
    std::list<std::thread> workThreads;
    std::unique_ptr<CCpuLimiter> cpuLimiter;
    CWallet* wallet = nullptr;
    bool checkSynckChain = true;

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

            if (checkSynckChain && IsInitialBlockDownload())
                continue;
            
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
    if (limit < 0 || limit > 1)
        throw std::runtime_error("Unable to set cpulimit: cpulimit must be greater than 0, but less than 1");
    if (running)
        throw std::runtime_error("Unable to update built-in miner settings: the built-in miner is active");
    cpuLimit = limit;
    LogPrintf("A new cpuLimit value for BuiltinMiner has been set: %1.2lf\n", cpuLimit);
}

double BuiltinMiner::getCpuLimit()
{
    std::lock_guard<std::mutex> lock(mutex);
    return cpuLimit;
}

void BuiltinMiner::setCheckSynckChainFlag(bool flag)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (running)
        throw std::runtime_error("Unable to update built-in miner settings: the built-in miner is active");
    if (flag == checkSynckChain)
        return;

    checkSynckChain = flag;
    if (checkSynckChain)
        LogPrintf("Mining without network synchronization is prohibited\n");
    else
        LogPrintf("Mining without network synchronization is allowed\n");
}

bool BuiltinMiner::getCheckSynckChainFlag()
{
    std::lock_guard<std::mutex> lock(mutex);
    return checkSynckChain;
}

void BuiltinMiner::start()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (running)
        throw std::runtime_error("Unable to start the built-in miner: the built-in miner is active");

    try
    {
        doStart();
        LogPrintf("Builtin miner started\n");

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
    {
        doStop();
        LogPrintf("Builtin miner stopped\n");
    }
}

bool BuiltinMiner::isRunning()
{
    std::lock_guard<std::mutex> lock(mutex);
    return running;
}


bool BuiltinMiner::appInit(ArgsManager& args)
{
    if (args.GetBoolArg("-disablebuiltinminer", false))
    {
        LogPrintf("Built-in miner disabled!\n");
        return true;
    }

    if (getWallet() == nullptr)
    {
        LogPrintf("Built-in miner is disabled due to the fact that the wallet is disabled!\n");
        return true;
    }

    try
    {
        auto strCpuLimit = args.GetArg("-cpulimitforbuiltinminer", std::to_string(DefaultCpuLimit));
        auto cpuLimit = std::stod(strCpuLimit);
        setCpuLimit(cpuLimit);
    }
    catch (const std::exception& e)
    {
        return InitError("-cpulimitforbuiltinminer is invalid");
    }

    try
    {
        start();
    }
    catch (const std::exception& e)
    {
        return InitError(e.what());
    }

    return true;
}

std::string BuiltinMiner::getHelpString()
{
    return HelpMessageGroup(_("Built-in miner options:"))
        + HelpMessageOpt("-disablebuiltinminer", _("Disables the built-in miner"))
        + HelpMessageOpt("-cpulimitforbuiltinminer=<0..1>", format(_("CPU limit for built-in miner (default: %1.2lf)"), DefaultCpuLimit));
}
