#include <chrono>
#include <list>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "chain.h"
#include "chainparams.h"
#include "cpulimiter.h"
#include "lynx_rules.h"
#include "miner.h"
#include "pow.h"
#include "tinyformat.h"
#include "ui_interface.h"
#include "univalue.h"
#include "util.h"
#include "validation.h"

#include "wallet/wallet.h"
#include "wallet/rpcwallet.h"
#include "rpc/mining.h"

#include "builtin_miner.h"
#include <boost/algorithm/string.hpp>
#include <boost/program_options/detail/config_file.hpp>

namespace
{
    using tfm::format; 

    const double DefaultCpuLimit = 0.01;
    const auto Timeout = std::chrono::milliseconds(200);
    const auto ReloadConfigInterval = std::chrono::seconds(120);

    std::mutex mutex;
    double cpuLimit = DefaultCpuLimit;
    bool running = false;
    std::list<std::thread> workThreads;
    std::unique_ptr<CCpuLimiter> cpuLimiter;
    CWallet* wallet = nullptr;
    bool checkSynckChain = true;

    void updateMiningAddressesFromConf()
    {
        auto confPath = gArgs.GetArg("-conf", BITCOIN_CONF_FILENAME);

        ArgsManager tmpArgs;
        tmpArgs.ReadConfigFile(confPath);

        auto mineraddress = tmpArgs.GetArg("-mineraddress", std::string());
        if (!mineraddress.empty())
            gArgs.ForceSetArg("-mineraddress", mineraddress);
    }

    bool useWallet()
    {
#ifndef ENABLE_WALLET
        return false;
#endif
        return wallet != nullptr;
    }

    std::shared_ptr<CReserveScript> getScriptForMiningFromWallet()
    {
        std::shared_ptr<CReserveScript> script;
        try
        {
            GetScriptForMining(wallet, script);
        }
        catch (const UniValue&)
        {
            script.reset();
        }
        return script;
    }

    void sleepBeforeUpdatingMiningAddressesFromConf()
    {
        // loop here is to be able to stop faster if "running" has changed
        for (int i = 0; running && i < static_cast<int>(ReloadConfigInterval / Timeout); i++)
            std::this_thread::sleep_for(Timeout);
    }

    std::vector<std::string> getMinerAddresses()
    {
        std::string mineraddress_cfg = gArgs.GetArg("-mineraddress", std::string());
        std::vector<std::string> addresses;
        boost::split(addresses, mineraddress_cfg, boost::is_any_of(",\t "));
        auto new_end = std::remove_if(addresses.begin(), addresses.end(),
                                      std::mem_fn(&std::string::empty));
        addresses.erase(new_end, addresses.end());

        return addresses;
    }

    std::shared_ptr<CReserveScript> getScriptForMiningFromConfig()
    {
        std::vector<std::string> address_candidates = getMinerAddresses();
        if (address_candidates.empty())
        {
            // Only update the addresses for mining, script will be returned on the next call
            try
            {
                sleepBeforeUpdatingMiningAddressesFromConf();
                updateMiningAddressesFromConf();
                LogPrintf("Built-in miner reloaded config file\n");
            }
            catch (const std::exception& e)
            {
                fprintf(stderr,"Error reading configuration file: %s\n", e.what());
            }
            return nullptr;
        }

        std::shared_ptr<CReserveScript> script;
        if (!GetScriptForMiningFromCandidates(address_candidates, script))
            return nullptr;
        return script;
    }

    bool getScriptForMining(std::shared_ptr<CReserveScript>& script, int& nHeight)
    {
        int newHeight;
        {
            LOCK(cs_main);
            newHeight = chainActive.Height();
        }
        if (newHeight == nHeight && script != nullptr)
            return true;

        if (useWallet())
            script = getScriptForMiningFromWallet();
        else
            script = getScriptForMiningFromConfig();

        if (script == nullptr)
            return false;
        
        nHeight = newHeight;
        return true;
    }

    void generateBlock(std::shared_ptr<CReserveScript>& script, int nHeight)
    {
        const int nInnerLoopCount = 0x10000;
        auto& params = Params();
        auto& consensus = params.GetConsensus();

        auto pblocktemplate = BlockAssembler(params)
            .CreateNewBlock(script->reserveScript);
        if (pblocktemplate == nullptr)
            return;

        unsigned int nExtraNonce = 0;
        CBlock* pblock = &pblocktemplate->block;
        {
            LOCK(cs_main);
            IncrementExtraNonce(pblock, chainActive.Tip(), nExtraNonce);
        }

        for (; running && pblock->nNonce < nInnerLoopCount; ++pblock->nNonce)
        {
            bool isValidBlock = CheckProofOfWork(pblock->GetPoWHash(), pblock->nBits, consensus)
                && CheckLynxRule3(pblock, nHeight + 1, consensus);
            if (isValidBlock)
            {
                auto shared_pblock = std::make_shared<const CBlock>(*pblock);
                if (ProcessNewBlock(params, shared_pblock, true, nullptr))
                    script->KeepScript();
                return;
            }

            cpuLimiter->suspendMe();
        }
    }

    void waitForSyncChain()
    {
        if (checkSynckChain)
        {
            while (running && IsInitialBlockDownload())
                std::this_thread::sleep_for(Timeout);
        }
    }

    void generateBlocks()
    {
        waitForSyncChain();

        int nHeight = -1;
        std::shared_ptr<CReserveScript> script;        
        while (running)
        {
            cpuLimiter->suspendMe();            
            if (!getScriptForMining(script, nHeight))
                continue;
            generateBlock(script, nHeight);
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
            LogPrintf("Built-in miner uses -mineraddress option because wallet is disabled\n");

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
        LogPrintf("BuiltinMiner started\n");

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
        LogPrintf("BuiltinMiner stopped\n");
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
        LogPrintf("BuiltinMiner disabled!\n");
        return true;
    }

    if (args.GetBoolArg("-disablechecksyncchain", false))
        setCheckSynckChainFlag(false);

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
        + HelpMessageOpt("-cpulimitforbuiltinminer=<0..1>", format(_("CPU limit for built-in miner (default: %1.2lf)"), DefaultCpuLimit))
        + HelpMessageOpt("-disablechecksyncchain", _("Causes the built-in miner to immediately start working, without waiting for the end of the synchronization of the chain"))
        + HelpMessageOpt("-mineraddress", _("Addresses which will be used for mining if wallet is disable. Addresses whould be separated by \",\""));
}
