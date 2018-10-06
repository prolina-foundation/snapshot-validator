#pragma once

#include <string>
#include <vector>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <unordered_map>
#include <pqxx/pqxx>

inline std::vector<unsigned char> hex2Bytes(const std::string hex) {
    std::vector<unsigned char> out(hex.size() / 2);
    for (int i = 0; i < hex.size(); i += 2)
    {
        std::istringstream iss(hex.substr(i, 2));
        int temp;
        iss >> std::hex >> temp;
        out[i/2] = static_cast<unsigned char>(temp);
    }
    return out;
}

inline std::string bytes2Hex(const std::vector<unsigned char> data) {
    std::stringstream out;
    out << std::setfill('0') << std::hex;
    for(size_t i = 0; i < data.size(); ++i) {
        out << std::setw(2) << static_cast<int>(data[i]);
    }
    return out.str();
}

inline std::vector<unsigned char> asVector(const pqxx::binarystring &binstr) {
    return std::vector<unsigned char>(
                reinterpret_cast<const unsigned char*>(binstr.get()),
                reinterpret_cast<const unsigned char*>(binstr.get() + binstr.length())
    );
}

inline std::vector<unsigned char> asVector(const std::string &str) {
    return std::vector<unsigned char>(
                reinterpret_cast<const unsigned char*>(str.data()),
                reinterpret_cast<const unsigned char*>(str.data() + str.size())
    );
}

template<typename T>
bool compareKeys(
        const std::unordered_map<std::uint64_t, T> a,
        const std::unordered_map<std::uint64_t, T> b,
        const bool logging = false,
        const std::string aName = "a",
        const std::string bName = "b")
{
    bool ok = true;
    for (auto iter = a.begin(); iter != a.end(); ++iter) {
        auto key = iter->first;
        if (b.count(key) != 1) {
            if (logging) std::cout << "key " << key << " not in map " << bName << std::endl;
            ok = false;
        }
    }

    for (auto iter = b.begin(); iter != b.end(); ++iter) {
        auto key = iter->first;
        if (a.count(key) != 1) {
            if (logging) std::cout << "key " << key << " not in map " << aName << std::endl;
            ok = false;
        }
    }

    return ok;
}

template<typename T>
bool compareValues(
        const std::unordered_map<std::uint64_t, T> a,
        const std::unordered_map<std::uint64_t, T> b,
        const bool logging = false,
        const std::string aName = "a",
        const std::string bName = "b")
{
    bool ok = true;
    for (auto iter = a.cbegin(); iter != a.cend(); ++iter) {
        const auto key = iter->first;
        const auto aValue = iter->second;
        const auto bValue = b.at(key);

        if (bValue != aValue) {
            if (logging) {
                std::cout << aName << "[" << key << "] = " << aValue << "; "
                          << bName << "[" << key << "] = " << bValue << std::endl;
            }
            ok = false;
        }
    }

    return ok;
}
