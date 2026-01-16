#include "totp_service.hpp"
#include "../utils/totp.hpp"
#include <stdexcept>
#include <algorithm>

namespace CipherMesh {
namespace Services {

std::string TotpService::generateCode(const std::string& secret) {
    if (secret.empty()) {
        return "000000"; // Return default for empty secret
    }
    
    try {
        return CipherMesh::Utils::TOTP::generateCode(secret);
    } catch (const std::exception&) {
        return "000000"; // Return default on error
    }
}

int TotpService::getSecondsRemaining() {
    return CipherMesh::Utils::TOTP::getSecondsRemaining();
}

int TotpService::getProgressPercentage() {
    int remaining = getSecondsRemaining();
    // Convert to percentage (30 seconds = 100%, 0 seconds = 0%)
    return static_cast<int>((remaining * 100) / 30);
}

bool TotpService::isValidSecret(const std::string& secret) {
    if (secret.empty()) {
        return false;
    }
    
    // Base32 alphabet: A-Z and 2-7
    const std::string base32Chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    
    for (char c : secret) {
        // Allow spaces (often used in formatted secrets)
        if (c == ' ') continue;
        
        // Check if character is in Base32 alphabet
        if (base32Chars.find(c) == std::string::npos) {
            return false;
        }
    }
    
    return true;
}

} // namespace Services
} // namespace CipherMesh
