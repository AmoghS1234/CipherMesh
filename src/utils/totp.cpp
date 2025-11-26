#include "totp.hpp"
#include <ctime>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <openssl/hmac.h>
#include <openssl/evp.h>

namespace CipherMesh {
namespace Utils {

std::vector<uint8_t> TOTP::decodeBase32(const std::string& input) {
    // Base32 alphabet (RFC 4648)
    const std::string base32Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::vector<uint8_t> output;
    
    // Remove spaces and convert to uppercase
    std::string cleanInput;
    for (char c : input) {
        if (c != ' ' && c != '\n' && c != '\r' && c != '\t') {
            cleanInput += std::toupper(c);
        }
    }
    
    if (cleanInput.empty()) {
        return output;
    }
    
    int buffer = 0;
    int bitsLeft = 0;
    
    for (char c : cleanInput) {
        // Skip padding
        if (c == '=') {
            break;
        }
        
        // Find character in base32 alphabet
        size_t val = base32Chars.find(c);
        if (val == std::string::npos) {
            // Invalid character, skip it
            continue;
        }
        
        buffer <<= 5;
        buffer |= val & 0x1F;
        bitsLeft += 5;
        
        if (bitsLeft >= 8) {
            output.push_back((buffer >> (bitsLeft - 8)) & 0xFF);
            bitsLeft -= 8;
        }
    }
    
    return output;
}

std::string TOTP::generateCode(const std::string& secretKey) {
    // 1. Get Time Steps (30s intervals)
    long long t = std::time(nullptr) / 30;
    
    // Convert time to big-endian 8-byte array
    unsigned char data[8];
    for (int i = 7; i >= 0; i--) {
        data[i] = static_cast<unsigned char>(t & 0xFF);
        t >>= 8;
    }

    // 2. Decode Secret
    std::vector<uint8_t> key = decodeBase32(secretKey);
    
    if (key.empty()) {
        // Invalid key - return empty string to indicate error
        return "";
    }

    // 3. HMAC-SHA1
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    
    HMAC(EVP_sha1(), key.data(), key.size(), data, 8, digest, &digest_len);

    // 4. Dynamic Truncation
    int offset = digest[digest_len - 1] & 0x0F;
    int binary = ((digest[offset] & 0x7F) << 24) |
                 ((digest[offset + 1] & 0xFF) << 16) |
                 ((digest[offset + 2] & 0xFF) << 8) |
                 (digest[offset + 3] & 0xFF);

    int otp = binary % 1000000;
    
    // 5. Format as 6-digit string with leading zeros
    std::stringstream ss;
    ss << std::setw(6) << std::setfill('0') << otp;
    return ss.str();
}

}
}
