#pragma once

#include <cstdint>
#include <string>
#include <ostream>
#include <vector>

#include "types.h"

struct Transaction {
    Transaction(
        std::uint8_t type,
        std::int32_t timestamp,
        std::vector<unsigned char> senderPublicKey,
        std::uint64_t recipientId,
        std::uint64_t amount,
        std::uint64_t fee,
        std::vector<unsigned char> assetData,
        std::uint64_t dappId
    );

    const std::uint8_t type;
    const std::int32_t timestamp;
    const std::vector<unsigned char> senderPublicKey;
    const std::uint64_t senderAddress;
    const std::uint64_t recipientAddress;
    const std::uint64_t amount;
    const std::uint64_t fee; // not signed
    const std::vector<unsigned char> assetData;
    const std::uint64_t dappId; // not signed

    // derived data
    const VotesUpdate type3Votes;
    const std::vector<std::vector<unsigned char>> type4Pubkeys;

    std::vector<unsigned char> serialize() const;
    std::vector<unsigned char> hash(std::vector<unsigned char> signature = {}, std::vector<unsigned char> secondSignature = {}) const;
    std::uint64_t id(std::vector<unsigned char> signature, std::vector<unsigned char> secondSignature) const;

private:
    static VotesUpdate parseType3Votes(const std::string transactionAsset);
    static std::vector<std::vector<unsigned char>> parseType4Pubkeys(const std::string transactionAsset);

    friend std::ostream& operator<<(std::ostream& os, const Transaction& transaction);
};

struct TransactionRow {
    TransactionRow(
            Transaction _transaction,
            std::vector<unsigned char> _signature,
            std::vector<unsigned char> _secondSignature,
            std::uint64_t _id,
            std::uint64_t _blockId)
        : transaction(_transaction)
        , signature(_signature)
        , secondSignature(_secondSignature)
        , id(_id)
        , blockId(_blockId)
    {
    }

    const Transaction transaction;
    const std::vector<unsigned char> signature;
    const std::vector<unsigned char> secondSignature;
    const std::uint64_t id;
    const std::uint64_t blockId;
};
