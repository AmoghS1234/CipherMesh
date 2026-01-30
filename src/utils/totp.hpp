#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace CipherMesh {
namespace Utils {

class TOTP {
public:
    // Standard generation
    static std::string generateCode(const std::string& secretKey);
    
    // Alias to fix the error in VaultQmlWrapper (calls "generate")
    static inline std::string generate(const std::string& secretKey) {
        return generateCode(secretKey);
    }
    
    // Returns seconds remaining in the current 30s window
    static int getSecondsRemaining();

    // Helper to decode Base32
    static std::vector<uint8_t> decodeBase32(const std::string& input);
};

}
}