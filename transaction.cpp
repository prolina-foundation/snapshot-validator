#include "transaction.h"

#include <sodium.h>

#include "lisk.h"

Transaction::Transaction(
        std::uint8_t _type,
        std::uint32_t _timestamp,
        std::vector<unsigned char> _senderPublicKey,
        std::uint64_t _recipientId,
        std::uint64_t _amount,
        const unsigned char* _assetDataBegin,
        std::size_t _assetDataLength
        )
    : type(_type)
    , timestamp_(_timestamp)
    , senderAddress(addressFromPubkey(_senderPublicKey))
    , senderPublicKey_(_senderPublicKey)
    , recipientAddress(_recipientId)
    , amount(_amount)
    , assetDataBegin_(_assetDataBegin)
    , assetDataLength_(_assetDataLength)
{
}

std::vector<unsigned char> Transaction::serialize() const
{
    std::size_t size = 1 // type
            + 4 // timestamp
            + 32 // sender pubkey
            + 8 // recipient
            + 8 // amount
            + assetDataLength_
            ;
    auto out = std::vector<unsigned char>(size);
    out[0] = type;
    out[1] = (timestamp_ >> 0) & 0xFF;
    out[2] = (timestamp_ >> 8) & 0xFF;
    out[3] = (timestamp_ >> 16) & 0xFF;
    out[4] = (timestamp_ >> 24) & 0xFF;
    for (int i = 0; i < senderPublicKey_.size(); ++i) {
        out[5+i] = senderPublicKey_[i];
    }

    for (int i = 0; i < 8; ++i) {
        out[37+i] = (recipientAddress >> (7-i)*8) & 0xFF;
    }

    for (int i = 0; i < 8; ++i) {
        out[45+i] = (amount >> i*8) & 0xFF;
    }

    for (int i = 0; i < assetDataLength_; ++i) {
        out[53+i] = *(assetDataBegin_ + i);
    }

    return out;
}

std::vector<unsigned char> Transaction::hash(std::vector<unsigned char> signature) const
{
    auto out = std::vector<unsigned char>(crypto_hash_sha256_BYTES);

    auto message = serialize();
    message.insert(message.end(), signature.begin(), signature.end());
    crypto_hash_sha256(out.data(), message.data(), message.size());

    return out;
}

std::uint64_t Transaction::id(std::vector<unsigned char> signature) const
{
    return idFromEightBytes(firstEightBytesReversed(hash(signature)));
}

std::ostream& operator<<(std::ostream& os, const Transaction& trx)
{
    os << std::to_string(trx.type)
       << "/" << trx.timestamp_
       << "/" << trx.senderPublicKey_.size()
       << "/" << trx.recipientAddress << "L"
       << "/" << trx.amount
    ;
    return os;
}
