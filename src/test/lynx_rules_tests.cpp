#include "chainparams.h"
#include "consensus/consensus.h"
#include "miner.h"
#include "lynx_rules.h"
#include "base58.h"
#include "test/test_bitcoin.h"
#include <boost/test/unit_test.hpp>


static CKey coinbaseKey2;
static CKey coinbaseKey3;

static CBlock CreateBlock(const std::vector<CMutableTransaction>& txns, const CScript& scriptPubKey)
{
    const CChainParams& chainparams = Params();
    std::unique_ptr<CBlockTemplate> pblocktemplate = BlockAssembler(chainparams).CreateNewBlock(scriptPubKey);
    CBlock& block = pblocktemplate->block;

    // Replace mempool-selected txns with just coinbase plus passed-in txns:
    block.vtx.resize(1);
    for (const CMutableTransaction& tx : txns)
        block.vtx.push_back(MakeTransactionRef(tx));
    // IncrementExtraNonce creates a valid coinbase and merkleRoot
    unsigned int extraNonce = 0;
    IncrementExtraNonce(&block, chainActive.Tip(), extraNonce);

    while (!CheckProofOfWork(block.GetPoWHash(), block.nBits, chainparams.GetConsensus())) ++block.nNonce;
    return block;
}

BOOST_FIXTURE_TEST_SUITE(lynx_rules_tests, TestChain100Setup)

BOOST_AUTO_TEST_CASE(lunx_rule1)
{
    coinbaseKey2.MakeNewKey(true);
    coinbaseKey3.MakeNewKey(true);

    const auto& consensusParams = Params().GetConsensus();
    const CTxDestination address = coinbaseKey.GetPubKey().GetID();
    const CTxDestination address2 = coinbaseKey2.GetPubKey().GetID();
    const CTxDestination address3 = coinbaseKey3.GetPubKey().GetID();

    auto balances = std::map<CTxDestination, CAmount>{
        {address, 0},
        {address2, 0},
        {address3, 0}
    };

    while (chainActive.Height() < consensusParams.HardFork4Height)
    {
        std::string errorString;
        BOOST_CHECK(IsValidAddressForMining(address, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(IsValidAddressForMining(address2, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(IsValidAddressForMining(address3, 0, chainActive.Tip(), consensusParams, errorString));

        std::set<std::string> addressesProhibitedForMining;
        BOOST_CHECK(GetAddressesProhibitedForMining(chainActive.Tip(), consensusParams, addressesProhibitedForMining));
        BOOST_CHECK(addressesProhibitedForMining.empty());
        
        const CTxDestination* foundAddressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(&balances.begin()->first == foundAddressForMining);

        CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey()));
    }

    {
        std::string errorString;
        BOOST_CHECK(!IsValidAddressForMining(address, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(IsValidAddressForMining(address2, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(IsValidAddressForMining(address3, 0, chainActive.Tip(), consensusParams, errorString));

        std::set<std::string> addressesProhibitedForMining;
        BOOST_CHECK(GetAddressesProhibitedForMining(chainActive.Tip(), consensusParams, addressesProhibitedForMining));
        BOOST_CHECK(addressesProhibitedForMining == std::set<std::string>{
            CBitcoinAddress(coinbaseKey.GetPubKey().GetID()).ToString()
        });

        const CTxDestination* foundAddressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(foundAddressForMining != nullptr);
        BOOST_CHECK(*foundAddressForMining == address2 || *foundAddressForMining == address3);
    }

    {
        auto pblock = std::make_shared<CBlock>(CreateBlock({}, GetScriptForRawPubKey(coinbaseKey.GetPubKey())));
        CBlockIndex blockIndex(pblock->GetBlockHeader());
        blockIndex.nHeight = chainActive.Height() + 1;
        blockIndex.pprev = chainActive.Tip();
        // The coinbaseKey key was used the last time.
        BOOST_CHECK(!CheckLynxRule1(pblock.get(), &blockIndex, consensusParams));
        BOOST_CHECK(chainActive.Height() < blockIndex.nHeight);
    }

    {
        auto pblock = std::make_shared<CBlock>(CreateBlock({}, GetScriptForRawPubKey(coinbaseKey2.GetPubKey())));
        CBlockIndex blockIndex(pblock->GetBlockHeader());
        blockIndex.nHeight = chainActive.Height() + 1;
        blockIndex.pprev = chainActive.Tip();
        // A new address (coinbaseKey2) was used to generate the block
        BOOST_CHECK(CheckLynxRule1(pblock.get(), &blockIndex, consensusParams));
        BOOST_CHECK(ProcessNewBlock(Params(), pblock, true, nullptr));
        BOOST_CHECK(chainActive.Height() == blockIndex.nHeight);
    }

    {
        std::set<std::string> addressesProhibitedForMining;
        BOOST_CHECK(GetAddressesProhibitedForMining(chainActive.Tip(), consensusParams, addressesProhibitedForMining));
        auto etalonAddressesProhibitedForMining = std::set<std::string>{
            CBitcoinAddress(address).ToString(),
            CBitcoinAddress(address2).ToString()
        };
        BOOST_CHECK(addressesProhibitedForMining == etalonAddressesProhibitedForMining);
        
        std::string errorString;
        BOOST_CHECK(!IsValidAddressForMining(address, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(!IsValidAddressForMining(address2, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(IsValidAddressForMining(address3, 0, chainActive.Tip(), consensusParams, errorString));

        const CTxDestination* foundAddressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(foundAddressForMining != nullptr);
        BOOST_CHECK(*foundAddressForMining == address3);
    }

    {
        auto pblock = std::make_shared<CBlock>(CreateBlock({}, GetScriptForRawPubKey(coinbaseKey3.GetPubKey())));
        CBlockIndex blockIndex(pblock->GetBlockHeader());
        blockIndex.nHeight = chainActive.Height() + 1;
        blockIndex.pprev = chainActive.Tip();
        // A new address (coinbaseKey3) was used to generate the block
        BOOST_CHECK(CheckLynxRule1(pblock.get(), &blockIndex, consensusParams));
        BOOST_CHECK(ProcessNewBlock(Params(), pblock, true, nullptr));
        BOOST_CHECK(chainActive.Height() == blockIndex.nHeight);
    }

    {
        std::set<std::string> addressesProhibitedForMining;
        BOOST_CHECK(GetAddressesProhibitedForMining(chainActive.Tip(), consensusParams, addressesProhibitedForMining));
        auto etalonAddressesProhibitedForMining = std::set<std::string>{
            CBitcoinAddress(address2).ToString(),
            CBitcoinAddress(address3).ToString()
        };
        BOOST_CHECK(addressesProhibitedForMining == etalonAddressesProhibitedForMining);
        std::string errorString;
        BOOST_CHECK(IsValidAddressForMining(address, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(!IsValidAddressForMining(address2, 0, chainActive.Tip(), consensusParams, errorString));
        BOOST_CHECK(!IsValidAddressForMining(address3, 0, chainActive.Tip(), consensusParams, errorString));

        const CTxDestination* foundAddressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(foundAddressForMining != nullptr);
        BOOST_CHECK(*foundAddressForMining == address);
    }
}

BOOST_AUTO_TEST_SUITE_END()
