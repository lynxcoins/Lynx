#include <stdexcept>
#include <thread>
#include <boost/test/unit_test.hpp>

#include "chainparams.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "wallet/test/wallet_test_fixture.h"

#include "builtin_miner.h"


extern CWallet* pwalletMain;

namespace
{
    void enableWallet()
    {
        vpwallets.push_back(pwalletMain);
    }

    struct BuiltinMinerTestingSetup : public WalletTestingSetup
    {
        BuiltinMinerTestingSetup() :
            WalletTestingSetup(CBaseChainParams::REGTEST)
        {
        }
        ~BuiltinMinerTestingSetup()
        {
            vpwallets.clear();
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
    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(0), std::runtime_error);
    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(1), std::runtime_error);
    BOOST_CHECK_NO_THROW(BuiltinMiner::setCpuLimit(0.99));

    BuiltinMiner::start();
    BOOST_CHECK_THROW(BuiltinMiner::setCpuLimit(.5), std::runtime_error);
    BuiltinMiner::stop();
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
