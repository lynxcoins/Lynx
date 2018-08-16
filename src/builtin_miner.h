#ifndef BITCOIN_BUILTIN_MINER_H
#define BITCOIN_BUILTIN_MINER_H

#include <string>
#include "cpulimiter.h"


class ArgsManager;

namespace BuiltinMiner
{
    /** Limit must be greater than 0, but less than 1. */
    void setCpuLimit(double limit);
    double getCpuLimit();

    /** The flag allows/prohibits mining when the chain is not synchronized. */
    void setCheckSynckChainFlag(bool flag);
    bool getCheckSynckChainFlag();

    void start();
    void stop();
    bool isRunning();

    bool appInit(ArgsManager& args);
    std::string getHelpString();
};

#endif // BITCOIN_BUILTIN_MINER_H
