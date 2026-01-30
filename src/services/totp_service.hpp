#pragma once

#include <string>

namespace CipherMesh {
namespace Services {

/**
 * Platform-independent TOTP (Time-based One-Time Password) service.
 * Wraps TOTP generation with consistent business logic.
 */
class TotpService {
public:
    /**
     * Generate a 6-digit TOTP code from a Base32-encoded secret.
     * @param secret Base32-encoded secret key
     * @return 6-digit TOTP code (e.g., "123456")
     */
    static std::string generateCode(const std::string& secret);
    
    /**
     * Get seconds remaining in the current 30-second TOTP window.
     * @return Seconds until the code changes (0-29)
     */
    static int getSecondsRemaining();
    
    /**
     * Get progress percentage for UI visualization (0-100).
     * @return Progress through current 30-second window (0-100)
     */
    static int getProgressPercentage();
    
    /**
     * Validate if a secret is properly Base32 encoded.
     * @param secret The secret to validate
     * @return true if valid Base32 format, false otherwise
     */
    static bool isValidSecret(const std::string& secret);
};

} // namespace Services
} // namespace CipherMesh
