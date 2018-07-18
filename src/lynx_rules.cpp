#include <vector>
#include <set>
#include "consensus/validation.h"
#include "rpc/blockchain.h"
#include "validation.h"
#include "util.h"
#include "utilstrencodings.h"
#include "base58.h"
#include "lynx_rules.h"


CAmount GetThresholdBalance(const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    double thresholdDifficulty = GetDifficultyPrevN(pindex, consensusParams.HardFork4AddressPrevBlockCount);
    return static_cast<CAmount>(consensusParams.HardFork4BalanceThreshold * thresholdDifficulty);
}

static bool GetLastCoinbaseDestinations(const CBlockIndex* pindex, const Consensus::Params& consensusParams, std::set<std::string>& result)
{
    for (int i = 0; i < consensusParams.HardFork4AddressPrevBlockCount && pindex != NULL; i++, pindex = pindex->pprev)
    {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, consensusParams))
            return false;

        std::vector<std::string> blockDests = GetTransactionDestinations(block.vtx[0]);
        result.insert(blockDests.begin(), blockDests.end());
    }

    return true;
}

const CTxDestination* FindAddressForMining(const std::map<CTxDestination, CAmount>& balances, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    // rule1 prepare: get all addresses from last blocks
    std::set<std::string> lastCoinbaseDestinations;
    if (!GetLastCoinbaseDestinations(pindex, consensusParams, lastCoinbaseDestinations))
        return nullptr;

    // rule2 prepare: find amount threshold
    CAmount thresholdBalance = GetThresholdBalance(pindex, consensusParams);
    
    // Find address
    auto it  = balances.begin();
    for (; it != balances.end(); ++it)
    {
        const CTxDestination& addr = it->first;
        CAmount amount = it->second;
        std::string strAddr = CBitcoinAddress(addr).ToString();

        if (lastCoinbaseDestinations.count(strAddr) == 0  // rule1: check last blocks
            && amount >= thresholdBalance)                // rule2: check balance
        {
            return &addr;
        }
    }

    return nullptr;
}

bool IsValidAddressForMining(const CTxDestination& address, CAmount balance, const CBlockIndex* pindex, const Consensus::Params& consensusParams, std::string& errorString)
{
    // rule1:
    std::set<std::string> lastDestinations;
    if (!GetLastCoinbaseDestinations(pindex, consensusParams, lastDestinations))
    {
        errorString = "Unable to get the latest Coinbase addresses";
        return false;
    }

    if (lastDestinations.count(CBitcoinAddress(address).ToString()) != 0)
    {
        errorString = "Address get reward not long ago";
        return false;
    }

    // rule2:
    if (balance < GetThresholdBalance(pindex, consensusParams))
    {
        errorString = "Not enough coins on address";
        return false;
    }

    return true;
}

bool CheckLynxRule1(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if (pindex->nHeight <= consensusParams.HardFork4Height)
        return true; // The rule does not yet apply

    // rule1:
    // extract destination(s) of coinbase tx, for each conibase tx destination check that previous 10 blocks do not have
    // such destination in coinbase tx

    std::vector<std::string> coinbaseDestinations = GetTransactionDestinations(pblock->vtx[0]);
    CBlockIndex* prevIndex = pindex->pprev;

    for (int i = 0; i < consensusParams.HardFork4AddressPrevBlockCount && prevIndex != NULL; i++, prevIndex = prevIndex->pprev)
    {
        CBlock prevBlock;
        if (!ReadBlockFromDisk(prevBlock, prevIndex, consensusParams))
            return false;

        for (const auto& prevDestination : GetTransactionDestinations(prevBlock.vtx[0]))
        {
            if (coinbaseDestinations.end() != std::find(coinbaseDestinations.begin(), coinbaseDestinations.end(), prevDestination))
                return error("CheckLynxRule1(): new blocks with coinbase destination %s are temporarily not allowed", prevDestination);
        }
    }

    return true;
}

bool CheckLynxRule2(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams)
{
    if (pindex->nHeight <= consensusParams.HardFork4Height)
        return true; // The rule does not yet apply

    // rule2:
    // first address from coinbase transaction must have a coin age of 1000 or greater. The coin age is the product of the number of coins
    // in the miners reward address and the difficulty value of the previous 10th block

    std::vector<std::string> coinbaseDestinations = GetTransactionDestinations(pblock->vtx[0]);
    if (coinbaseDestinations.empty())
        return error("CheckLynxRule2(): GetTransactionFirstAddress failed. Address was not found");

    const std::string& addr = coinbaseDestinations[0];
    CAmount balance;
    {
        LOCK(cs_main);
        balance = pcoinsTip->GetAddressBalance(addr);
    }
    CAmount thresholdBalance = GetThresholdBalance(pindex, consensusParams);
    if (balance < thresholdBalance)
    {
        return error("CheckLynxRule2(): not enough coins on address %s: balance=%i, thresholdBalance=%i",
            coinbaseDestinations[0], balance, thresholdBalance);
    }

    return true;
}

bool CheckLynxRule3(const CBlock* pblock, int nHeight, const Consensus::Params& consensusParams)
{
    if (nHeight <= consensusParams.HardFork4Height)
        return true; // The rule does not yet apply
   
    // rule3:
    // the last 2 chars in the sha256 hash of address, must match the last
    // 2 chars of the block hash value submitted by the miner in the candidate block.

    std::vector<std::string> coinbaseDestinations = GetTransactionDestinations(pblock->vtx[0]);
    if (coinbaseDestinations.empty())
        return error("CheckLynxRule3(): GetTransactionFirstAddress failed. Address was not found");

    const std::string& addr = coinbaseDestinations[0];
    unsigned char addrSha256Raw[CSHA256::OUTPUT_SIZE];
    CSHA256().Write((const unsigned char*)addr.c_str(), addr.size()).Finalize(addrSha256Raw);
    std::string addrHex = HexStr(addrSha256Raw, addrSha256Raw + CSHA256::OUTPUT_SIZE);
    std::string blockHex = pblock->GetHash().ToString();

    LogPrintf("Reward address: %s\n", addr);
    LogPrintf("Address_hash: %s\n", addrHex);
    LogPrintf("Block hash: %s\n", blockHex);

    auto lastCharsCount = consensusParams.HardFork4CheckLastCharsCount;
    bool res = 0 == addrHex.compare(addrHex.size() - lastCharsCount, lastCharsCount,
                                    blockHex, blockHex.size() - lastCharsCount, lastCharsCount);
    if (!res)
        return error("CheckLynxRule3(): block hash and sha256 hash of the first destination should last on the same 2 chars");

    return true;
}

bool CheckLynxRules(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams, CValidationState& state)
{
    if (!CheckLynxRule1(pblock, pindex, consensusParams))
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-destination");

    if (!CheckLynxRule2(pblock, pindex, consensusParams))
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-destination");
    
    if (!CheckLynxRule3(pblock, pindex->nHeight, consensusParams))
        return state.DoS(100, false, REJECT_INVALID, "bad-cb-destination");

    return true;
}
