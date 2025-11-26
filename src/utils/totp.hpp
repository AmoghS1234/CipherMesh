#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace CipherMesh {
namespace Utils {

class TOTP {
public:
    // Generates the current 6-digit code for the given secret key (Base32)
    static std::string generateCode(const std::string& secretKey);
    
    // Helper to decode Base32 (standard for TOTP secrets)
    static std::vector<uint8_t> decodeBase32(const std::string& input);
};

}
}
