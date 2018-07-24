#include "chainparams.h"
#include "consensus/consensus.h"
#include "miner.h"
#include "lynx_rules.h"
#include "base58.h"
#include "rpc/blockchain.h"
#include "wallet/wallet.h"
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

static CBlockIndex* GetDifficultyBlockIndex()
{
    const auto& consensusParams = Params().GetConsensus();
    CBlockIndex* difficultyBlockIndex = chainActive.Tip();
    for (int i = 0; i < consensusParams.HardFork5DifficultyPrevBlockCount; ++i)
        difficultyBlockIndex = difficultyBlockIndex->pprev;
    return difficultyBlockIndex;
}

static std::string sha256(const std::string& str)
{
    unsigned char rawHash[CSHA256::OUTPUT_SIZE];
    CSHA256()
        .Write(reinterpret_cast<const unsigned char*>(str.data()), str.size())
        .Finalize(rawHash);
    return HexStr(rawHash, rawHash + CSHA256::OUTPUT_SIZE);
}

static std::string sha256(const CTxDestination& dest)
{
    return sha256(CBitcoinAddress(dest).ToString());
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
    // Let's check that the rules are still inactive
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
        // We used address, but did not use address2, address3
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
        // We used address, address2, but did not use address3
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
        // We used all addresses, but address is again available
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

BOOST_AUTO_TEST_CASE(lunx_rule2)
{
    const auto& consensusParams = Params().GetConsensus();
    const CTxDestination address = coinbaseKey.GetPubKey().GetID();
    const CTxDestination address2 = coinbaseKey2.GetPubKey().GetID();
    const CTxDestination address3 = coinbaseKey3.GetPubKey().GetID();

    const auto zeroBalances = std::map<CTxDestination, CAmount>{
        {address, 0},
        {address2, 0},
        {address3, 0}
    };
    // Let's check that the rules are still inactive
    while (chainActive.Height() < consensusParams.HardFork5Height)
    {
        BOOST_CHECK_EQUAL(GetMinBalanceForMining(chainActive.Tip(), consensusParams), 0);

        const CTxDestination* addressForMining = FindAddressForMining(zeroBalances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(addressForMining != nullptr);

        auto oldChainHeight = chainActive.Height();
        CreateAndProcessBlock({}, GetScriptForDestination(*addressForMining));
        BOOST_CHECK(chainActive.Height() > oldChainHeight);
    }

    // Let's check that the rule has turned on
    BOOST_CHECK_EQUAL(FindAddressForMining(zeroBalances, chainActive.Tip(), consensusParams), nullptr);

    // The difficulty is small, so a minimum limit is required for mining
    BOOST_CHECK_EQUAL(GetMinBalanceForMining(chainActive.Tip(), consensusParams), consensusParams.HardFork5LowerLimitMinBalance);
    {
        CBlockIndex* difficultyBlockIndex = GetDifficultyBlockIndex();
        auto savedNBits = difficultyBlockIndex->nBits;

        // Usually the minimum balance is defined as pow(DiffcultyPredN, consensusParams.HardFork5CoinAgePow)
        difficultyBlockIndex->nBits = 480000000;
        CAmount etalonMinBalance = static_cast<CAmount>(std::pow(GetDifficulty(difficultyBlockIndex), consensusParams.HardFork5CoinAgePow)*COIN);
        BOOST_CHECK_EQUAL(GetMinBalanceForMining(chainActive.Tip(), consensusParams), etalonMinBalance);

        // The difficulty is too great, the necessary limit for mining will be limited from above
        difficultyBlockIndex->nBits = 10000;
        BOOST_CHECK_EQUAL(GetMinBalanceForMining(chainActive.Tip(), consensusParams), consensusParams.HardFork5UpperLimitMinBalance);

        difficultyBlockIndex->nBits = savedNBits;
    }

    {
        // At the coinbaseKey4 there is no money, the block should not be accepted
        CKey coinbaseKey4;
        coinbaseKey4.MakeNewKey(true);
        auto oldChainHeight = chainActive.Height();
        CreateAndProcessBlock({}, GetScriptForRawPubKey(coinbaseKey4.GetPubKey()));
        BOOST_CHECK(chainActive.Height() == oldChainHeight);
    }

    {
        // Creating a block with an address with enough coins
        std::map<CTxDestination, CAmount> balances = {
            {address, pcoinsTip->GetAddressBalance(CBitcoinAddress(address).ToString())},
            {address2, pcoinsTip->GetAddressBalance(CBitcoinAddress(address2).ToString())},
            {address3, pcoinsTip->GetAddressBalance(CBitcoinAddress(address3).ToString())}
        };

        const CTxDestination* addressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(addressForMining != nullptr);
        BOOST_CHECK(balances[*addressForMining] >= GetMinBalanceForMining(chainActive.Tip(), consensusParams));

        auto oldChainHeight = chainActive.Height();
        CreateAndProcessBlock({}, GetScriptForDestination(*addressForMining));
        BOOST_CHECK(chainActive.Height() == oldChainHeight + 1);
    }
}

BOOST_AUTO_TEST_CASE(lunx_rule3)
{
    const auto& consensusParams = Params().GetConsensus();
    const CTxDestination address = coinbaseKey.GetPubKey().GetID();
    const CTxDestination address2 = coinbaseKey2.GetPubKey().GetID();
    const CTxDestination address3 = coinbaseKey3.GetPubKey().GetID();

    // Let's check that the rules are still inactive
    while (chainActive.Height() < consensusParams.HardFork6Height)
    {
        std::map<CTxDestination, CAmount> balances = {
            {address, pcoinsTip->GetAddressBalance(CBitcoinAddress(address).ToString())},
            {address2, pcoinsTip->GetAddressBalance(CBitcoinAddress(address2).ToString())},
            {address3, pcoinsTip->GetAddressBalance(CBitcoinAddress(address3).ToString())}
        };
        const CTxDestination* addressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(addressForMining != nullptr);

        // Each generated block must be successfully added to the chain
        auto oldChainHeight = chainActive.Height();
        CreateAndProcessBlock({}, GetScriptForDestination(*addressForMining));
        BOOST_CHECK(chainActive.Height() > oldChainHeight);
    }

    // Let's check that the rule works
    while (chainActive.Height() < consensusParams.HardFork6Height + 1)
    {
        std::map<CTxDestination, CAmount> balances = {
            {address, pcoinsTip->GetAddressBalance(CBitcoinAddress(address).ToString())},
            {address2, pcoinsTip->GetAddressBalance(CBitcoinAddress(address2).ToString())},
            {address3, pcoinsTip->GetAddressBalance(CBitcoinAddress(address3).ToString())}
        };
        const CTxDestination* addressForMining = FindAddressForMining(balances, chainActive.Tip(), consensusParams);
        BOOST_CHECK(addressForMining != nullptr);

        auto oldChainHeight = chainActive.Height();
        CBlock block = CreateAndProcessBlock({}, GetScriptForDestination(*addressForMining));
        auto blockHash = block.GetHash().ToString();
        auto blockHashLastChars = blockHash.substr(blockHash.size() - consensusParams.HardFork6CheckLastCharsCount);
        auto addressHash = sha256(*addressForMining);
        auto addressHashLastChars = addressHash.substr(addressHash.size() - consensusParams.HardFork6CheckLastCharsCount);
        if (chainActive.Height() > oldChainHeight)
        {
            // The block was added to the chain, making sure that the HASH is correct
            BOOST_CHECK(blockHashLastChars == addressHashLastChars);
        }
        else
        {
            // The block was not added to the chain, HASH should be incorrect
            BOOST_CHECK(blockHashLastChars != addressHashLastChars);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
