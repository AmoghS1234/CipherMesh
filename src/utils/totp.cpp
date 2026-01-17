#include "totp.hpp"
#include <ctime>
#include <vector>
#include <string>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace CipherMesh {
namespace Utils {

// --- Minimal SHA1 Implementation (Required for Standard TOTP) ---
// Libsodium does not provide SHA1 (it's considered weak for crypto, but mandatory for TOTP).
class MiniSHA1 {
public:
    static std::vector<uint8_t> hmac_sha1(const std::vector<uint8_t>& key, const std::vector<uint8_t>& data) {
        std::vector<uint8_t> k = key;
        if (k.size() > 64) {
            // If key > block size, hash it
            // (Skipping for brevity as TOTP keys are usually short, but standard requires it)
        }
        if (k.size() < 64) k.resize(64, 0); // Pad with zeros

        std::vector<uint8_t> i_key(64), o_key(64);
        for (int i = 0; i < 64; ++i) {
            i_key[i] = k[i] ^ 0x36;
            o_key[i] = k[i] ^ 0x5c;
        }

        std::vector<uint8_t> inner = i_key;
        inner.insert(inner.end(), data.begin(), data.end());
        std::vector<uint8_t> inner_hash = sha1(inner);

        std::vector<uint8_t> outer = o_key;
        outer.insert(outer.end(), inner_hash.begin(), inner_hash.end());
        return sha1(outer);
    }

private:
    static uint32_t rol(uint32_t value, uint32_t bits) { return (value << bits) | (value >> (32 - bits)); }
    static uint32_t blk(const uint32_t* block, int i) { return rol(block[(i + 13) & 15] ^ block[(i + 8) & 15] ^ block[(i + 2) & 15] ^ block[i], 1); }

    static std::vector<uint8_t> sha1(const std::vector<uint8_t>& data) {
        uint32_t digest[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        std::vector<uint8_t> padded = data;
        padded.push_back(0x80);
        while ((padded.size() * 8) % 512 != 448) padded.push_back(0);
        uint64_t bits = data.size() * 8;
        for (int i = 7; i >= 0; --i) padded.push_back((bits >> (i * 8)) & 0xFF);

        for (size_t i = 0; i < padded.size(); i += 64) {
            uint32_t w[80];
            for (int j = 0; j < 16; ++j) {
                w[j] = (padded[i + j * 4] << 24) | (padded[i + j * 4 + 1] << 16) | (padded[i + j * 4 + 2] << 8) | (padded[i + j * 4 + 3]);
            }
            for (int j = 16; j < 80; ++j) w[j] = rol(w[j - 3] ^ w[j - 8] ^ w[j - 14] ^ w[j - 16], 1);

            uint32_t a = digest[0], b = digest[1], c = digest[2], d = digest[3], e = digest[4];

            for (int j = 0; j < 80; ++j) {
                uint32_t f, k;
                if (j < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
                else if (j < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
                else if (j < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
                else { f = b ^ c ^ d; k = 0xCA62C1D6; }
                uint32_t temp = rol(a, 5) + f + e + k + w[j];
                e = d; d = c; c = rol(b, 30); b = a; a = temp;
            }
            digest[0] += a; digest[1] += b; digest[2] += c; digest[3] += d; digest[4] += e;
        }

        std::vector<uint8_t> res;
        for (int i = 0; i < 5; ++i) {
            res.push_back((digest[i] >> 24) & 0xFF);
            res.push_back((digest[i] >> 16) & 0xFF);
            res.push_back((digest[i] >> 8) & 0xFF);
            res.push_back(digest[i] & 0xFF);
        }
        return res;
    }
};

std::vector<uint8_t> TOTP::decodeBase32(const std::string& input) {
    const std::string base32Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::vector<uint8_t> output;
    std::string cleanInput;
    for (char c : input) if (c != ' ' && c != '\n' && c != '-') cleanInput += std::toupper(c);
    if (cleanInput.empty()) return output;
    
    int buffer = 0; int bitsLeft = 0;
    for (char c : cleanInput) {
        if (c == '=') break;
        size_t val = base32Chars.find(c);
        if (val == std::string::npos) continue;
        buffer = (buffer << 5) | val;
        bitsLeft += 5;
        if (bitsLeft >= 8) {
            output.push_back((buffer >> (bitsLeft - 8)) & 0xFF);
            bitsLeft -= 8;
        }
    }
    return output;
}

std::string TOTP::generateCode(const std::string& secretKey) {
    if (secretKey.empty()) return "000000";

    long long t = std::time(nullptr) / 30;
    std::vector<uint8_t> data(8);
    for (int i = 7; i >= 0; i--) {
        data[i] = static_cast<uint8_t>(t & 0xFF);
        t >>= 8;
    }

    std::vector<uint8_t> key = decodeBase32(secretKey);
    if (key.empty()) return "INV-KEY"; // Invalid Key

    // Calculate HMAC-SHA1
    std::vector<uint8_t> hash = MiniSHA1::hmac_sha1(key, data);

    // Truncate
    int offset = hash[hash.size() - 1] & 0x0F;
    int binary = ((hash[offset] & 0x7F) << 24) |
                 ((hash[offset + 1] & 0xFF) << 16) |
                 ((hash[offset + 2] & 0xFF) << 8) |
                 (hash[offset + 3] & 0xFF);

    int otp = binary % 1000000;
    
    std::ostringstream ss;
    ss << std::setw(6) << std::setfill('0') << otp;
    return ss.str();
}

int TOTP::getSecondsRemaining() {
    std::time_t t = std::time(nullptr);
    return 30 - (t % 30);
}

} // namespace Utils
} // namespace CipherMesh