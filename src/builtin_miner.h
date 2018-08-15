#ifndef BITCOIN_BUILTIN_MINER_H
#define BITCOIN_BUILTIN_MINER_H

#include "cpulimiter.h"

namespace BuiltinMiner
{
    /* limit must be greater than 0, but less than 1 */
    void setCpuLimit(double limit);
    double getCpuLimit();

    void start();
    void stop();
    bool isRunning();
};

#endif // BITCOIN_BUILTIN_MINER_H
