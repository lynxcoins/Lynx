#ifndef BITCOIN_LYNX_VALIDATION_H
#define BITCOIN_LYNX_VALIDATION_H

#include <map>
#include "amount.h"
#include "validation.h"
#include "consensus/params.h"
#include "script/standard.h"


class CBlock;
class CBlockIndex;

CAmount GetThresholdBalance(const CBlockIndex* pindex, const Consensus::Params& consensusParams);

bool CheckLynxRule1(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams);
bool CheckLynxRule2(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams);
bool CheckLynxRule3(const CBlock* pblock, int nHeight, const Consensus::Params& consensusParams);

bool CheckLynxRules(const CBlock* pblock, const CBlockIndex* pindex, const Consensus::Params& consensusParams, CValidationState& state);

#endif // BITCOIN_LYNX_VALIDATION_H
