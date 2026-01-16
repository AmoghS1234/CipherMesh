#include "password_service.hpp"
#include "../core/crypto.hpp"
#include "../utils/passwordstrength.hpp"

namespace CipherMesh {
namespace Services {

std::string PasswordService::generatePassword(const GeneratorOptions& options) {
    if (!options.isValid() || options.length < 4 || options.length > 128) {
        return ""; // Invalid options
    }
    
    return CipherMesh::Core::Crypto::generatePassword(
        options.length,
        options.useUppercase,
        options.useLowercase,
        options.useNumbers,
        options.symbols
    );
}

std::string PasswordService::generatePassword(int length, bool upper, bool lower, 
                                             bool numbers, const std::string& symbols) {
    GeneratorOptions options;
    options.length = length;
    options.useUppercase = upper;
    options.useLowercase = lower;
    options.useNumbers = numbers;
    options.symbols = symbols;
    
    return generatePassword(options);
}

int PasswordService::calculateStrength(const std::string& password) {
    auto info = CipherMesh::Utils::PasswordStrengthCalculator::calculate(password);
    return info.score;
}

std::string PasswordService::getStrengthText(int score) {
    if (score < 20) return "Very Weak";
    if (score < 40) return "Weak";
    if (score < 60) return "Fair";
    if (score < 80) return "Strong";
    return "Very Strong";
}

std::vector<int> PasswordService::getStrengthColor(int score) {
    // Map score to color gradient: red -> orange -> yellow -> light green -> green
    if (score < 20) return {220, 53, 69};    // Red (danger)
    if (score < 40) return {253, 126, 20};   // Orange (warning)
    if (score < 60) return {255, 193, 7};    // Yellow (caution)
    if (score < 80) return {40, 167, 69};    // Light green (good)
    return {25, 135, 84};                     // Dark green (excellent)
}

} // namespace Services
} // namespace CipherMesh
