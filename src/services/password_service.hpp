#pragma once

#include <string>
#include <vector>

namespace CipherMesh {
namespace Services {

/**
 * Platform-independent password generation service.
 * Wraps core crypto password generation with business logic.
 */
class PasswordService {
public:
    struct GeneratorOptions {
        int length = 16;
        bool useUppercase = true;
        bool useLowercase = true;
        bool useNumbers = true;
        std::string symbols = "!@#$%^&*()_+-=[]{}|;:,.<>?";
        
        // Validate that at least one character type is selected
        bool isValid() const {
            return (useUppercase || useLowercase || useNumbers || !symbols.empty());
        }
    };
    
    /**
     * Generate a random password with the given options.
     * @param options Generation options (length, character types)
     * @return Generated password, or empty string if options are invalid
     */
    static std::string generatePassword(const GeneratorOptions& options);
    
    /**
     * Generate a password with individual parameters (convenience method).
     */
    static std::string generatePassword(int length, bool upper, bool lower, 
                                       bool numbers, const std::string& symbols);
    
    /**
     * Calculate password strength (0-100 score).
     * @param password The password to evaluate
     * @return Score from 0 (very weak) to 100 (very strong)
     */
    static int calculateStrength(const std::string& password);
    
    /**
     * Get password strength category as text.
     * @param score Strength score (0-100)
     * @return Text like "Very Weak", "Weak", "Fair", "Strong", "Very Strong"
     */
    static std::string getStrengthText(int score);
    
    /**
     * Get RGB color for strength visualization.
     * @param score Strength score (0-100)
     * @return RGB values (0-255) as {r, g, b}
     */
    static std::vector<int> getStrengthColor(int score);
};

} // namespace Services
} // namespace CipherMesh
