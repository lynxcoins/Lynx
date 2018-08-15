#!/usr/bin/python3

import bitcoin.rpc
from bitcoin.core import COIN
import hashlib
import binascii
unhexlify = binascii.unhexlify
import sys


def print_usage():
    print("Usage: %s <lynx config file> <mainnet/testnet/regtest>" % sys.argv[0])

if len(sys.argv) != 3:
    print_usage()
    sys.exit(0)

if sys.argv[1] in ["help", "--help", "-h"]:
    print_usage()
    sys.exit(0)

config = sys.argv[1]
NetType = sys.argv[2]


class net_params:
    def get_maturity(self, blockn):
        if blockn <= self.HardFork2Height:
            return self.COINBASE_MATURITY
        else:
            return self.COINBASE_MATURITY2

mainnet_params = net_params()
mainnet_params.name = "mainnet"
mainnet_params.HardFork4Height = 99999999
mainnet_params.HardFork5Height = 99999999
mainnet_params.HardFork6Height = 99999999
mainnet_params.HardFork4AddressPrevBlockCount = 60
mainnet_params.HardFork5CoinAgePow = 4
mainnet_params.HardFork5DifficultyPrevBlockCount = 10
mainnet_params.HardFork5LowerLimitMinBalance = 1000 * COIN
mainnet_params.HardFork5UpperLimitMinBalance = 100000000 * COIN
mainnet_params.HardFork6CheckLastCharsCount = 2
mainnet_params.COINBASE_MATURITY = 30
mainnet_params.COINBASE_MATURITY2 = 20160 # Valid after HardFork2
mainnet_params.HardFork2Height = 1711675

testnet_params = net_params()
testnet_params.name = "testnet" 
testnet_params.HardFork4Height = 400
testnet_params.HardFork5Height = 500
testnet_params.HardFork6Height = 600
testnet_params.HardFork4AddressPrevBlockCount = 10
testnet_params.HardFork5CoinAgePow = 2
testnet_params.HardFork5DifficultyPrevBlockCount = 10
testnet_params.HardFork5LowerLimitMinBalance = 1 * COIN
testnet_params.HardFork5UpperLimitMinBalance = 100000000 * COIN
testnet_params.HardFork6CheckLastCharsCount = 1
testnet_params.COINBASE_MATURITY = 30
testnet_params.COINBASE_MATURITY2 = 30 # Valid after HardFork2
testnet_params.HardFork2Height = 200;

regtest_params = net_params() 
regtest_params.name = "regtest"
regtest_params.HardFork4Height = 35
regtest_params.HardFork5Height = 40
regtest_params.HardFork6Height = 45
regtest_params.HardFork4AddressPrevBlockCount = 2
regtest_params.HardFork5CoinAgePow = 1
regtest_params.HardFork5DifficultyPrevBlockCount = 10
regtest_params.HardFork5LowerLimitMinBalance = 1 * COIN
regtest_params.HardFork5UpperLimitMinBalance = 100000000 * COIN
regtest_params.HardFork6CheckLastCharsCount = 1
regtest_params.COINBASE_MATURITY = 2
regtest_params.COINBASE_MATURITY2 = 2 # Valid after HardFork2
regtest_params.HardFork2Height = -1

if NetType == "mainnet":
    params = mainnet_params
elif NetType == "testnet":
    params = testnet_params
elif NetType == "regtest":
    params = regtest_params
else:
    print("Error! incorrect nettype %s. Choose one of mainnet/testnet/regtest" % NetType)
    sys.exit(0)

bitcoin.SelectParams(NetType)
proxy = bitcoin.rpc.Proxy(btc_conf_file = config, timeout = 600)

# internal storages
block_reward_addresses = []
balances = dict()
immature_balances = dict()

def add_immature_coins_to_addr(b, ib, coins, address, blockn):
    if address not in ib:
        ib[address] = dict()
    ib[address][blockn] = coins

def add_coins_to_addr(b, ib, coins, address):
    b[address] = b.get(address, 0) + coins

def move_immature_coins_to_coins(b, ib, address, cur_blockn):
    if address in ib:
        address_im_coins = ib[address]

        for blockn, coins in list(address_im_coins.items()):
            if cur_blockn - blockn >= params.get_maturity(blockn):
                # change
                add_coins_to_addr(b, ib, coins, address)
                address_im_coins.pop(blockn)

        if not ib[address]:
            ib.pop(address)

def get_coins_from_addr(b, ib, coins, address, cur_blockn):
    # maybe some immature coins become mature
    move_immature_coins_to_coins(b, ib, address, cur_blockn)

    if address not in b:
        print("Error! Can't get coins. Address %s is unknown" % address)
        return

    old_balance = b[address]
    if old_balance < coins:
        print("Error! Can't get coins. Not enough coins on address %s. Required %d" % (address, coins))
        return

    b[address] = b.get(address, 0) - coins
    if b[address] == 0:
        b.pop(address) # remove from db

def get_address_balance(b, ib, address, cur_blockn):
    move_immature_coins_to_coins(b, ib, address, cur_blockn)
    return b.get(address, 0)

def print_all_balances(cur_blockn):
    for address in list(immature_balances.keys()):
        move_immature_coins_to_coins(balances, immature_balances, address, cur_blockn)

    print("===========================================")
    print("=============== mature coins ==============")
    print("===========================================")
    for address in balances:
        print(address, balances[address] / COIN)

    print("===========================================")
    print("=============== immature coins ============")
    print("===========================================")

    for address in immature_balances:
        total = 0
        for blockn, coins in immature_balances[address].items():
            total = total + coins
        print(address, total / COIN)


print("checking blocks...")
block_count = proxy.getblockcount()
for blockn in range(0, block_count + 1):
#     print("checking block %d" % blockn)
    block_reward_addresses.append([])

    bhash = proxy.getblockhash(blockn)
    block = proxy.getblock(bhash)

    # get reward addresses
    for vout in block.vtx[0].vout:
        if vout.nValue > 0: # only significant outputs
            address = bitcoin.wallet.CBitcoinAddress.from_scriptPubKey(vout.scriptPubKey)
            block_reward_addresses[blockn].append(address)


    # check rule1
    if blockn > params.HardFork4Height:
        cur_addrs = set(block_reward_addresses[-1])
        for i in range(max(blockn - params.HardFork4AddressPrevBlockCount, 0), blockn):
            prev_addrs = set(block_reward_addresses[i])
            if not cur_addrs.isdisjoint(prev_addrs):
                print("Error! Block %d RULE 1! address %s were met in block %d" % (blockn, cur_addrs.intersection(prev_addrs).pop(), i))

    # check rule2
    if blockn > params.HardFork5Height:
        balance = get_address_balance(balances, immature_balances, block_reward_addresses[-1][0], blockn)

        difficulty_blockn = blockn - params.HardFork5DifficultyPrevBlockCount - 1
        if difficulty_blockn < 0:
            difficulty = 1.0
        else:
            difficulty = proxy.getblock(proxy.getblockhash(difficulty_blockn)).difficulty

        minBalanceForMining = pow(difficulty, params.HardFork5CoinAgePow) * COIN
        minBalanceForMining = min(minBalanceForMining, params.HardFork5UpperLimitMinBalance)
        minBalanceForMining = max(minBalanceForMining, params.HardFork5LowerLimitMinBalance)

        if balance < minBalanceForMining:
            print("Error! Block %d RULE 2! Not enough coins for mining on adrress %s. Got %d, required %d" % (blockn, block_reward_addresses[-1][0], (balance+0.0) / COIN, (minBalanceForMining+0.0 )/ COIN))

    # check rule3
    if blockn > params.HardFork6Height:
        addr_hash = hashlib.sha256(str(block_reward_addresses[-1][0]).encode('utf-8')).hexdigest()
        block_hash = bhash[::-1].hex()
        if addr_hash[-params.HardFork6CheckLastCharsCount:] != block_hash[-params.HardFork6CheckLastCharsCount:]:
            print("Error! Block %d RULE 3! Address hash %s and block hash %s don't end on the same %d chars" % (blockn, addr_hash, block_hash, params.HardFork6CheckLastCharsCount))

    # parse block transactions
    for t in block.vtx:
        if t.is_coinbase():
            for vout in t.vout:
                if vout.nValue > 0: # only significant outputs
                    address = bitcoin.wallet.CBitcoinAddress.from_scriptPubKey(vout.scriptPubKey)
                    # print("***TO", address, vout.nValue / COIN)
                    add_immature_coins_to_addr(balances, immature_balances, vout.nValue, address, blockn)
        else:
            for vin in t.vin:
                prev_tx_hash = vin.prevout.hash
                prev_tx_n = vin.prevout.n
                prev_raw_tx = proxy.gettransaction(prev_tx_hash)["hex"]
                prev_tx = bitcoin.core.CTransaction.deserialize(unhexlify(prev_raw_tx))
                prev_out = prev_tx.vout[prev_tx_n]
                nValue = prev_out.nValue
                address = bitcoin.wallet.CBitcoinAddress.from_scriptPubKey(prev_out.scriptPubKey)
                # print("***FROM: ", address, nValue / COIN)
                get_coins_from_addr(balances, immature_balances, nValue, address, blockn)

            for vout in t.vout:
                if vout.nValue > 0: # only significant outputs
                    address = bitcoin.wallet.CBitcoinAddress.from_scriptPubKey(vout.scriptPubKey)
                    add_coins_to_addr(balances, immature_balances, vout.nValue, address)
                    # print("***TO: ", address, vout.nValue / COIN)

print("Done!")
# print_all_balances(blockn)
