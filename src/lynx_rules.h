#ifndef BITCOIN_LYNX_VALIDATION_H
#define BITCOIN_LYNX_VALIDATION_H

#include <map>
#include <set>
#include "amount.h"
#include "validation.h"
#include "consensus/params.h"
#include "script/standard.h"


class CBlock;
class CBlockIndex;

CAmount GetMinBalanceForMining(const CBlockIndex* pBestBlockIndex, const Consensus::Params& consensusParams);
bool GetAddressesProhibitedForMining(const CBlockIndex* pBestBlockIndex, const Consensus::Params& consensusParams, std::set<std::string>& result);
bool GetScriptForMiningFromCandidates(const std::vector<std::string>& address_candidates, std::shared_ptr<CReserveScript>& coinbase_script);

const CTxDestination* FindAddressForMining(const std::map<CTxDestination, CAmount>& balances, const CBlockIndex* pBestBlockIndex, const Consensus::Params& consensusParams);
bool IsValidAddressForMining(const CTxDestination& address, CAmount balance, const CBlockIndex* pBestBlockIndex, const Consensus::Params& consensusParams, std::string& errorString);

bool CheckLynxRule1(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams);
bool CheckLynxRule2(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams);
bool CheckLynxRule3(const CBlock* pblock, int nHeight, const Consensus::Params& consensusParams, bool from_builtin_miner = false);

bool CheckLynxRules(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams, CValidationState& state);

bool GetLynxHardForkParam(int best_block_height, const std::vector<Consensus::HFLynxParams>& params, int& param);
#endif // BITCOIN_LYNX_VALIDATION_H
