#include <chrono>
#include <iostream>
#include <unordered_map>

// with c++14 enabled, std::experimental::optional is always available
#define PQXX_HAVE_EXP_OPTIONAL 1
#include <pqxx/pqxx>
#include <sodium.h>

#include "assets.h"
#include "blockchain_state.h"
#include "blockchain_state_validator.h"
#include "block.h"
#include "block_validator.h"
#include "lisk.h"
#include "payload.h"
#include "settings.h"
#include "scopedbenchmark.h"
#include "summaries.h"
#include "transaction.h"
#include "transaction_validator.h"
#include "types.h"
#include "utils.h"
#include "log.h"

address_t TRASH = 12125591683379294247ul; // random address that is hopefully not used

void printHelp()
{
    std::cout << "usage: validate-snapshot-database mainnet|testnet database_name" << std::endl;
}

int run(std::vector<std::string> args)
{
    if (args.size() >= 2 && args[1] == "--help")
    {
        printHelp();
        return 0;
    }

    if (args.size() != 3)
    {
        printHelp();
        return 1;
    }

    ScopedBenchmark benchmarkFull("Overall runtime"); static_cast<void>(benchmarkFull);

    if (sodium_init() == -1) {
        return 1;
    }

    const Network network = networkFromName(args[1]);
    const std::string dbname = args[2];

    try
    {
        pqxx::connection dbConnection("dbname=" + dbname);
        std::cout << "Connected to database " << dbConnection.dbname() << std::endl;
        pqxx::read_transaction db(dbConnection);

        {
            auto row = db.exec1("SELECT COUNT(*) AS number FROM trs");
            std::cout << "Transaction count " << row[0].c_str() << std::endl;
        }

        {
            auto row = db.exec1("SELECT COUNT(*) AS number FROM blocks");
            std::cout << "Blocks count " << row[0].c_str() << std::endl;
        }

        {
            auto row = db.exec1("SELECT MAX(height) AS height FROM blocks");
            std::cout << "Height: " << row[0].c_str() << std::endl;
        }

        Settings settings(network);

        if (network == Network::Mainnet) {
            // Why is this exception required? Old broken data?
            AddressSummary::defaultLastBlockId = settings.genesisBlock;
        }

        Assets::peersEmpty(db);
        Assets::validateType0AssetData(db);
        Assets::validateType1AssetData(db);
        Assets::validateType2AssetData(db);
        Assets::validateType3AssetData(db);
        Assets::validateType4AssetData(db);
        Assets::validateType5AssetData(db);
        Assets::validateType6AssetData(db);
        Assets::validateType7AssetData(db);
        Assets::checkUnconfirmedInMemAccounts(db);

        std::unordered_map<std::uint64_t, std::vector<TransactionRow>> blockToTransactions;
        BlockchainState blockchainState;

        {
            std::cout << "Reading transactions ..." << std::endl;
            ScopedBenchmark benchmarkTransactions("Reading transactions"); static_cast<void>(benchmarkTransactions);

            pqxx::result result = db.exec(R"SQL(
                SELECT
                    id, "blockId", trs.type, timestamp, "senderPublicKey", coalesce(left("recipientId", -1), '0') AS recipient_address,
                    amount, fee, signature, "signSignature",
                    transfer.data AS type0_asset,
                    signatures."publicKey" AS type1_asset,
                    delegates.username AS type2_asset,
                    replace(votes.votes, ',', '') AS type3_asset,
                    coalesce(multisignatures.min, 0) AS type4_asset_min, coalesce(multisignatures.lifetime, 0) AS type4_asset_lifetime,
                    replace(multisignatures.keysgroup, ',', '') AS type4_asset_keys,
                    (coalesce(dapps.name, '') || coalesce(dapps.description, '') || coalesce(dapps.tags, '') || coalesce(dapps.link, '') || coalesce(dapps.icon, '')) AS type5_asset_texts,
                    coalesce(dapps.type, 0) AS type5_asset_type, coalesce(dapps.category, 0) AS type5_asset_category,
                    coalesce(intransfer."dappId", '0') AS type6_asset,
                    coalesce(outtransfer."dappId", '0') AS type7_asset_dappid,
                    coalesce(outtransfer."outTransactionId", '0') AS type7_asset_outtransactionId
                FROM trs
                LEFT JOIN transfer ON trs.id = transfer."transactionId"
                LEFT JOIN signatures ON trs.id = signatures."transactionId"
                LEFT JOIN delegates ON trs.id = delegates."transactionId"
                LEFT JOIN votes ON trs.id = votes."transactionId"
                LEFT JOIN multisignatures ON trs.id = multisignatures."transactionId"
                LEFT JOIN dapps ON trs.id = dapps."transactionId"
                LEFT JOIN intransfer ON trs.id = intransfer."transactionId"
                LEFT JOIN outtransfer ON trs.id = outtransfer."transactionId"
                ORDER BY "rowId"
            )SQL");
            for (auto row : result) {
                try {
                    // Read fields in row
                    int index = 0;
                    const auto dbId = row[index++].as<std::uint64_t>();
                    const auto dbBockId = row[index++].as<std::uint64_t>();
                    const auto dbType = row[index++].as<int>();
                    const auto dbTimestamp = row[index++].as<std::int32_t>();
                    const auto dbSenderPublicKey = pqxx::binarystring(row[index++]);
                    std::uint64_t dbRecipientId;
                    if (settings.exceptions.transactionsContainingInvalidRecipientAddress.count(dbId)) {
                        index++;
                        dbRecipientId = TRASH;
                    } else {
                        dbRecipientId = row[index++].as<std::uint64_t>();
                    }
                    const auto dbAmount = row[index++].as<std::uint64_t>();
                    const auto dbFee = row[index++].as<std::uint64_t>();
                    const auto dbSignature = pqxx::binarystring(row[index++]);
                    const auto dbSecondSignature = pqxx::binarystring(row[index++]);
                    const auto dbType0Asset = pqxx::binarystring(row[index++]);
                    const auto dbType1Asset = pqxx::binarystring(row[index++]);
                    const auto dbType2Asset = row[index++].get<std::string>();
                    const auto dbType3Asset = row[index++].get<std::string>();
                    const auto dbType4AssetMin = row[index++].as<int>();
                    const auto dbType4AssetLifetime = row[index++].as<int>();
                    const auto dbType4AssetKeys = row[index++].get<std::string>();
                    const auto dbType5AssetText = row[index++].get<std::string>();
                    const auto dbType5AssetType = row[index++].as<std::uint32_t>();
                    const auto dbType5AssetCategory = row[index++].as<std::uint32_t>();
                    const auto dbType6AssetDappId = row[index++].as<std::uint64_t>();
                    const auto dbType7AssetDappId = row[index++].as<std::uint64_t>();
                    const auto dbType7AssetDappOutTransferId = row[index++].as<std::uint64_t>();

                    // Parse fields in row
                    const auto senderPublicKey = asVector(dbSenderPublicKey);
                    const auto signature = asVector(dbSignature);
                    const auto secondSignature = asVector(dbSecondSignature);

                    std::uint64_t dappId = 0;
                    std::vector<unsigned char> assetData = {};
                    switch (dbType) {
                    case 0:
                        assetData = asVector(dbType0Asset);
                        break;
                    case 1:
                        assetData = asVector(dbType1Asset);
                        break;
                    case 2:
                        assetData = asVector(*dbType2Asset);
                        break;
                    case 3:
                        assetData = asVector(*dbType3Asset);
                        break;
                    case 4: {
                        assetData.push_back(static_cast<std::uint8_t>(dbType4AssetMin));
                        assetData.push_back(static_cast<std::uint8_t>(dbType4AssetLifetime));
                        auto keys = asVector(*dbType4AssetKeys);
                        assetData.insert(assetData.end(), keys.cbegin(), keys.cend());
                        break;
                    }
                    case 5:
                        assetData = asVector(*dbType5AssetText);
                        assetData.push_back((dbType5AssetType >> 0*8) & 0xff);
                        assetData.push_back((dbType5AssetType >> 1*8) & 0xff);
                        assetData.push_back((dbType5AssetType >> 2*8) & 0xff);
                        assetData.push_back((dbType5AssetType >> 3*8) & 0xff);
                        assetData.push_back((dbType5AssetCategory >> 0*8) & 0xff);
                        assetData.push_back((dbType5AssetCategory >> 1*8) & 0xff);
                        assetData.push_back((dbType5AssetCategory >> 2*8) & 0xff);
                        assetData.push_back((dbType5AssetCategory >> 3*8) & 0xff);
                        break;
                    case 6:
                        assetData = asVector(std::to_string(dbType6AssetDappId));
                        dappId = dbType6AssetDappId;
                        break;
                    case 7:
                        assetData = asVector(std::to_string(dbType7AssetDappId) + std::to_string(dbType7AssetDappOutTransferId));
                        dappId = dbType7AssetDappId;
                        break;
                    }

                    auto t = Transaction(
                        dbType,
                        dbTimestamp,
                        senderPublicKey,
                        dbRecipientId,
                        dbAmount,
                        dbFee,
                        assetData,
                        dappId
                    );
                    blockToTransactions[dbBockId].emplace_back(t, signature, secondSignature, dbId, dbBockId);
                }
                catch (const std::exception &e)
                {
                    std::cout << "Exception '" << e.what() << "' when reading row:\n";
                    for (int i = 0; i < row.size(); ++i)
                    {
                        if (i > 0) std::cout << "|";
                        std::cout << std::string(row[i].c_str());
                    }
                    std::cout << std::endl;
                    throw;
                }
            }
        }


        {
            std::cout << "Reading blocks ..." << std::endl;
            ScopedBenchmark benchmarkBlocks("Reading blocks"); static_cast<void>(benchmarkBlocks);
            std::unordered_map<std::uint64_t, std::chrono::steady_clock::time_point> times;

            pqxx::result R = db.exec(R"SQL(
                SELECT
                    id, version, timestamp, height, "previousBlock", "numberOfTransactions", "totalAmount", "totalFee", reward,
                    "payloadLength", "payloadHash", "generatorPublicKey", "blockSignature"
                FROM blocks
                ORDER BY height
            )SQL");

            std::uint64_t lastHeight = 0;
            std::uint64_t lastBlockId = 0;
            std::uint64_t roundFees = 0;
            std::vector<std::uint64_t> roundDelegates = std::vector<std::uint64_t>(101);
            std::vector<std::uint64_t> roundRewards = std::vector<std::uint64_t>(101);
            for (auto row : R) {
                int index = 0;
                const auto dbId = row[index++].as<std::uint64_t>();
                const auto dbVersion = row[index++].as<std::uint32_t>();
                const auto dbTimestamp = row[index++].as<std::uint32_t>();
                const auto dbHeight = row[index++].as<std::uint64_t>();
                const auto dbPreviousBlock = row[index++].get<std::uint64_t>();
                const auto dbNumberOfTransactions = row[index++].as<std::uint32_t>();
                const auto dbTotalAmount = row[index++].as<std::uint64_t>();
                const auto dbTotalFee = row[index++].as<std::uint64_t>();
                const auto dbReward = row[index++].as<std::uint64_t>();
                const auto dbPayloadLength = row[index++].as<std::uint32_t>();
                const auto dbPayloadHash = pqxx::binarystring(row[index++]);
                const auto dbGeneratorPublicKey = pqxx::binarystring(row[index++]);
                const auto dbSignature = pqxx::binarystring(row[index++]);

                const auto generatorPublicKey = asVector(dbGeneratorPublicKey);
                const auto payloadHash = asVector(dbPayloadHash);
                const auto signature = asVector(dbSignature);

                if (dbHeight != lastHeight + 1) {
                    throw std::runtime_error("Height mismatch");
                }
                lastHeight = dbHeight;

                if (dbHeight != 1) {
                    if (*dbPreviousBlock != lastBlockId) {
                        throw std::runtime_error("previous block mismatch");
                    }
                }
                lastBlockId = dbId;


                BlockHeader bh(
                    dbVersion,
                    dbTimestamp,
                    dbPreviousBlock ? *dbPreviousBlock : 0,
                    dbNumberOfTransactions,
                    dbTotalAmount,
                    dbTotalFee,
                    dbReward,
                    dbPayloadLength,
                    payloadHash,
                    generatorPublicKey
                );

                BlockValidator::validate(BlockRow(bh, dbHeight, dbId, signature), settings);

                Payload payload(blockToTransactions[dbId]);
                if (payload.transactionCount() != bh.numberOfTransactions) {
                    throw std::runtime_error(
                                "transactions count mismatch in block at height " +
                                std::to_string(dbHeight) + ". " +
                                "Expected by block header: " + std::to_string(bh.numberOfTransactions) +
                                " found: " + std::to_string(payload.transactionCount())
                                );
                }

                if (settings.exceptions.payloadHashMismatch.count(dbId) == 0) {
                    auto calculatedPayloadHash = payload.hash();
                    if (payloadHash != calculatedPayloadHash) {
                        // payload hash
                        std::cout << "Payload hash expected " << bytes2Hex(payloadHash) << ", "
                                  << "calculated " << bytes2Hex(calculatedPayloadHash)
                                  << std::endl;

                        // debug payload
                        auto payloadSerialized = payload.serialize();
                        std::cout << "Payload length calculated: " << payloadSerialized.size()
                                  << " expected: " << bh.payloadLength << std::endl;
                        // std::cout << "payload: " << bytes2Hex(payloadSerialized) << std::endl;

                        for (auto &tws : blockToTransactions[dbId]) {
                            auto transactionId = tws.transaction.id(tws.signature, tws.secondSignature);
                            std::cout << "Payload transaction: " << tws.transaction << " " << transactionId << std::endl;
                        }

                        if (dbHeight == 1) {
                            // warn only (https://github.com/LiskHQ/lisk/issues/2047)
                            std::cout << "payload hash mismatch for block " << dbId << std::endl;
                        } else {
                            throw std::runtime_error("Payload hash mismatch in block id " + std::to_string(dbId) +
                                                     " height " + std::to_string(dbHeight));
                        }
                    }
                }

                for (auto &transactionRow : blockToTransactions[dbId]) {
                    auto &t = transactionRow.transaction;

                    // Validate transaction

                    if (settings.exceptions.invalidTransactionSignature.count(transactionRow.id)) {
                        // skip
                    } else {
                        std::vector<unsigned char> secondSignatureRequiredBy;
                        try {
                            secondSignatureRequiredBy = blockchainState.addressSummaries.at(t.senderAddress).secondPubkey;
                        } catch (std::out_of_range) {
                        }
                        TransactionValidator::validate(transactionRow, secondSignatureRequiredBy, settings.exceptions);
                    }
                }

                // Update state from block transactions
                // This is done outside of the first transactions loop because second signatures are
                // only required for later blocks (see e.g. https://explorer.lisk.io/block/3087130330171409946)
                for (auto &transactionRow : blockToTransactions[dbId]) {
                    if (settings.exceptions.inertTransactions.count(transactionRow.id) == 0) {
                        blockchainState.applyTransaction(transactionRow);
                    }

                    if (settings.exceptions.balanceAdjustments.count(transactionRow.id)) {
                        blockchainState.addressSummaries[transactionRow.transaction.senderAddress].balance += settings.exceptions.balanceAdjustments[transactionRow.id];
                    }
                }
                BlockchainStateValidator::validate(blockchainState, settings);

                blockchainState.applyBlock(bh, dbId);

                roundFees += bh.totalFee;
                roundDelegates[(dbHeight-1)%101] = addressFromPubkey(bh.generatorPublicKey);
                roundRewards[(dbHeight-1)%101] = bh.reward;

                bool isLast = (dbHeight%101 == 0);
                //std::cout << "Block: " << id << " in round " << roundFromHeight(dbHeight) << " last: " << isLast << " reward: " << bh.reward << std::endl;


                if (isLast) {
                    auto roundNumber = roundFromHeight(dbHeight);
                    if (settings.exceptions.rewardsFactor.count(roundNumber)) {
                        int rewardsFactor = settings.exceptions.rewardsFactor.at(roundNumber);
                        for (int i = 0; i < 101; ++i)
                        {
                            roundRewards[i] *= rewardsFactor;
                        }
                    }
                    if (settings.exceptions.feesFactor.count(roundNumber)) {
                        roundFees *= settings.exceptions.feesFactor.at(roundNumber);
                    }
                    if (settings.exceptions.feesBonus.count(roundNumber)) {
                        roundFees += settings.exceptions.feesBonus.at(roundNumber);
                    }

                    auto feePerDelegate = roundFees/101;
                    auto feeRemaining = roundFees - (101*feePerDelegate);

                    for (int i = 0; i < 101; ++i)
                    {
                        blockchainState.addressSummaries[roundDelegates[i]].balance += roundRewards[i];
                        blockchainState.addressSummaries[roundDelegates[i]].balance += feePerDelegate;
                    }

                    if (feeRemaining > 0) {
                        // rest goes to the last delegate
                        blockchainState.addressSummaries[roundDelegates[100]].balance += feeRemaining;
                    }

                    for (int i = 0; i < 101; ++i) {
                        blockchainState.addressSummaries[roundDelegates[i]].lastBlockId = dbId;
                    }

                    roundFees = 0;
                }

                if (dbHeight%1000 == 0) {
                    auto now = std::chrono::steady_clock::now();
                    times[dbHeight] = now;
                    NumberLog().out() << "Done processing block at height " << dbHeight;
                    const int benchmarkSpan = 10000;
                    if (times.count(dbHeight-benchmarkSpan)) {
                        auto diff = std::chrono::duration<float>(now - times[dbHeight-benchmarkSpan]).count();
                        auto bps = benchmarkSpan / diff;
                        std::cout << " (current speed " << std::fixed << std::setprecision(1) << bps  << " blocks/s)";
                    }
                    std::cout << std::endl;
                }
            }
        }

        // validate after all blocks
        BlockchainStateValidator::validate(blockchainState, settings);

        blockchainState.addressSummaries.erase(TRASH);
        Summaries::checkMemAccounts(db, blockchainState, settings);

        db.commit();
    }
    catch (const std::exception &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    return 0;
}

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv, argv + argc);
    return run(args);
}
