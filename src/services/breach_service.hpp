#pragma once

#include <string>
#include <functional>

namespace CipherMesh {
namespace Services {

/**
 * Platform-independent breach checking service using Have I Been Pwned API.
 * Uses SHA-1 hash prefix matching for privacy (k-anonymity).
 */
class BreachService {
public:
    /**
     * Check if a password has been compromised in known data breaches.
     * @param password The password to check
     * @param callback Called with (isCompromised, count) where count is number of times seen
     *                 count = -1 indicates API error
     */
    static void checkPassword(const std::string& password, 
                              std::function<void(bool isCompromised, int count)> callback);

private:
    static std::string getSha1Prefix(const std::string& password);
    static std::string getSha1Suffix(const std::string& password);
};

} // namespace Services
} // namespace CipherMesh
