// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_CONSENSUS_PARAMS_H
#define BITCOIN_CONSENSUS_PARAMS_H

#include "amount.h"
#include "uint256.h"
#include "version.h"
#include "consensus.h"
#include <map>
#include <string>

namespace Consensus {

enum DeploymentPos
{
    DEPLOYMENT_TESTDUMMY,
    DEPLOYMENT_CSV, // Deployment of BIP68, BIP112, and BIP113.
    DEPLOYMENT_SEGWIT, // Deployment of BIP141, BIP143, and BIP147.
    // NOTE: Also add new deployments to VersionBitsDeploymentInfo in versionbits.cpp
    MAX_VERSION_BITS_DEPLOYMENTS
};

/**
 * Struct for each individual consensus rule change using BIP9.
 */
struct BIP9Deployment {
    /** Bit position to select the particular bit in nVersion. */
    int bit;
    /** Start MedianTime for version bits miner confirmation. Can be a date in the past */
    int64_t nStartTime;
    /** Timeout/expiry MedianTime for the deployment attempt. */
    int64_t nTimeout;
};

/**
 * Parameters that influence chain consensus.
 */
struct Params {
    uint256 hashGenesisBlock;
    int nSubsidyHalvingInterval;
    /** Block height and hash at which BIP34 becomes active */
    int BIP34Height;
    uint256 BIP34Hash;
    /** Block height at which BIP65 becomes active */
    int BIP65Height;
    /** Block height at which BIP66 becomes active */
    int BIP66Height;
    /** Block number at which the hard fork will be performed */
    int HardForkHeight;
    /** Block number at which the second hard fork will be performed */
    int HardFork2Height;
    /** Block number at which the third hard fork (DigiShield) will be performed */
    int HardFork3Height;
    /** Block number at which the fourth hard fork (pos rules) will be performed */
    int HardFork4Height;
    /** Minimum address balance threshold (see pos rule2) */
    CAmount HardFork4BalanceThreshold;
    /** Position of prev block that address must not win block (see rule1) */
    int HardFork4AddressPrevBlockCount;
    /** Position of prev block to get difficulty from (see rule2) */
    int HardFork4DifficultyPrevBlockCount;
    /** Numberof chars to check in address and block hash (see pos rule3) */
    int HardFork4CheckLastCharsCount;
    /**
     * Minimum blocks including miner confirmation of the total of 2016 blocks in a retargeting period,
     * (nPowTargetTimespan / nPowTargetSpacing) which is also used for BIP9 deployments.
     * Examples: 1916 for 95%, 1512 for testchains.
     */
    uint32_t nRuleChangeActivationThreshold;
    uint32_t nMinerConfirmationWindow;
    BIP9Deployment vDeployments[MAX_VERSION_BITS_DEPLOYMENTS];
    /** Proof of work parameters */
    uint256 powLimit;
    bool fPowAllowMinDifficultyBlocks;
    bool fPowNoRetargeting;
    int64_t PowTargetSpacingV1;
    int64_t PowTargetSpacingV2;
    int64_t PowTargetSpacingV3;
    int64_t GetPowTargetSpacing(int nHeight) const {
        if (nHeight <= HardForkHeight)
            return PowTargetSpacingV1;
        if (nHeight <= HardFork2Height)
            return PowTargetSpacingV2;
        return PowTargetSpacingV3;
    }
    int CoinbaseMaturity;
    int CoinbaseMaturity2;
    int GetCoinbaseMaturity(int nHeight) const {
        if  (nHeight <= HardFork2Height)
            return CoinbaseMaturity;
        return CoinbaseMaturity2;
    }
    int64_t nPowTargetTimespan;
    int64_t DifficultyAdjustmentInterval(int nHeight) const { return nPowTargetTimespan / GetPowTargetSpacing(nHeight); }
    uint256 nMinimumChainWork;
    uint256 defaultAssumeValid;
};
} // namespace Consensus

#endif // BITCOIN_CONSENSUS_PARAMS_H
