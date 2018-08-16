#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <boost/test/unit_test.hpp>

#include "chainparams.h"
#include "util.h"
#include "validation.h"
#include "wallet/test/wallet_test_fixture.h"
#include "wallet/wallet.h"

#include "builtin_miner.h"


extern CWallet* pwalletMain;

namespace
{
    void enableWallet()
    {
        if (vpwallets.empty())
            vpwallets.push_back(pwalletMain);
    }

    void disableWallet()
    {
        vpwallets.clear();
    }

    std::unique_ptr<ArgsManager> parse(std::vector<const char*> argv)
    {
        std::unique_ptr<ArgsManager> args(new ArgsManager());        
        int argc = static_cast<int>(argv.size());
        args->ParseParameters(argc, argv.data());
        return args;
    }

    struct BuiltinMinerTestingSetup : public WalletTestingSetup
    {
        BuiltinMinerTestingSetup() :
            WalletTestingSetup(CBaseChainParams::REGTEST)
        {
        }
        ~BuiltinMinerTestingSetup()
        {
            disableWallet();
        }
    };
}

BOOST_FIXTURE_TEST_SUITE(builtin_miner_test, BuiltinMinerTestingSetup)

BOOST_AUTO_TEST_CASE(start_stop)
{
    BOOST_CHECK_EQUAL(BuiltinMiner::isRunning(), false);
    
    // Wallet disabled
    BOOST_CHECK_THROW(BuiltinMiner::start(), std::runtime_error);

    enableWallet();

    BOOST_CHECK_NO_THROW(BuiltinMiner::start());
    BOOST_CHECK_EQUAL(BuiltinMiner::isRunning(), true);
    BOOST_CHECK_THROW(BuiltinMiner::start(), std::runtime_error);
    
    BOOST_CHECK_NO_THROW(BuiltinMiner::stop());
    BOOST_CHECK_EQUAL(BuiltinMiner::isRunning(), false);    
    BOOST_CHECK_NO_THROW(BuiltinMiner::stop());
}

BOOST_AUTO_TEST_CASE(set_cpu_limit)
{
    enableWallet();

    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(-1), std::runtime_error);
    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(-0.01), std::runtime_error);
    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(1.001), std::runtime_error);
    BOOST_CHECK_NO_THROW(BuiltinMiner::setCpuLimit(0.99));
    BOOST_CHECK_EQUAL(BuiltinMiner::getCpuLimit(), 0.99);

    BuiltinMiner::start();
    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(.5), std::runtime_error);
    BuiltinMiner::stop();
}

BOOST_AUTO_TEST_CASE(app_init)
{
    {
        enableWallet();

        auto args = parse({"program", "-disablebuiltinminer"});
        BOOST_CHECK(BuiltinMiner::appInit(*args));
        BOOST_CHECK(!BuiltinMiner::isRunning());
    }

    {
        disableWallet();

        auto args = parse({"program"});
        BOOST_CHECK(BuiltinMiner::appInit(*args));
        BOOST_CHECK(!BuiltinMiner::isRunning());
    }

    {
        enableWallet();

        // Disable error printing
        decltype(uiInterface.ThreadSafeMessageBox) tmpThreadSafeMessageBox;
        tmpThreadSafeMessageBox.connect([](const std::string&, const std::string&, unsigned) {
            return true;
        });
        swap(tmpThreadSafeMessageBox, uiInterface.ThreadSafeMessageBox);

        {
            auto args = parse({"program", "-cpulimitforbuiltinminer=string"});
            BOOST_CHECK(!BuiltinMiner::appInit(*args));
            BOOST_CHECK(!BuiltinMiner::isRunning());
        }

        {
            auto args = parse({"program", "-cpulimitforbuiltinminer=-0.01"});
            BOOST_CHECK(!BuiltinMiner::appInit(*args));
            BOOST_CHECK(!BuiltinMiner::isRunning());
        }

        {
            auto args = parse({"program", "-cpulimitforbuiltinminer=1.01"});
            BOOST_CHECK(!BuiltinMiner::appInit(*args));
            BOOST_CHECK(!BuiltinMiner::isRunning());
        }
        // Restore printing error
        swap(tmpThreadSafeMessageBox, uiInterface.ThreadSafeMessageBox);

        {
            auto args = parse({"program", "-cpulimitforbuiltinminer=0.5"});
            BOOST_CHECK(BuiltinMiner::appInit(*args));
            BOOST_CHECK(BuiltinMiner::isRunning());
            BOOST_CHECK_EQUAL(BuiltinMiner::getCpuLimit(), 0.5);
            BuiltinMiner::stop();
        }
    }
}

BOOST_AUTO_TEST_CASE(mining)
{
    enableWallet();
    BuiltinMiner::setCheckSynckChainFlag(false);
    BuiltinMiner::start();

    int baseHeight = chainActive.Height();
    int curHeight = baseHeight;
    while (curHeight < baseHeight + 10)
    {
        // TODO: Check CPU usage
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));

        {
            LOCK(cs_main);
            curHeight = chainActive.Height();
        }
    }

    BuiltinMiner::stop();
}

BOOST_AUTO_TEST_SUITE_END()
