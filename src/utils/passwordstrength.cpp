#include "passwordstrength.hpp"
#include <cmath>
#include <cctype>

namespace CipherMesh {
namespace Utils {

PasswordStrengthInfo PasswordStrengthCalculator::calculate(const std::string& password)
{
    PasswordStrengthInfo info;
    
    if (password.empty()) {
        info.strength = PasswordStrength::VeryWeak;
        info.text = "No password";
        info.color = {204, 204, 204}; // Grey
        info.score = 0;
        return info;
    }
    
    int score = 0;
    
    // Length bonus
    int length = password.length();
    if (length >= 8) score += 20;
    if (length >= 12) score += 15;
    if (length >= 16) score += 15;
    if (length >= 20) score += 10;
    
    // Character variety
    if (hasUppercase(password)) score += 10;
    if (hasLowercase(password)) score += 10;
    if (hasNumbers(password)) score += 10;
    if (hasSymbols(password)) score += 15;
    
    // Entropy bonus (considering character set diversity)
    int entropy = calculateEntropy(password);
    if (entropy > 50) score += 10;
    if (entropy > 70) score += 10;
    
    // Cap at 100
    if (score > 100) score = 100;
    
    info.score = score;
    
    // Determine strength level
    if (score < 30) {
        info.strength = PasswordStrength::VeryWeak;
        info.text = "Very Weak";
        info.color = {211, 47, 47}; // Red
    } else if (score < 50) {
        info.strength = PasswordStrength::Weak;
        info.text = "Weak";
        info.color = {245, 124, 0}; // Orange
    } else if (score < 70) {
        info.strength = PasswordStrength::Fair;
        info.text = "Fair";
        info.color = {251, 192, 45}; // Yellow
    } else if (score < 85) {
        info.strength = PasswordStrength::Strong;
        info.text = "Strong";
        info.color = {124, 179, 66}; // Light Green
    } else {
        info.strength = PasswordStrength::VeryStrong;
        info.text = "Very Strong";
        info.color = {56, 142, 60}; // Dark Green
    }
    
    return info;
}

int PasswordStrengthCalculator::calculateEntropy(const std::string& password)
{
    // Typical symbol set size for common password symbols
    const int SYMBOL_CHARSET_SIZE = 32;
    
    int charsetSize = 0;
    
    if (hasUppercase(password)) charsetSize += 26;
    if (hasLowercase(password)) charsetSize += 26;
    if (hasNumbers(password)) charsetSize += 10;
    if (hasSymbols(password)) charsetSize += SYMBOL_CHARSET_SIZE;
    
    if (charsetSize == 0) return 0;
    
    // Entropy = length * log2(charsetSize)
    double entropy = password.length() * std::log2(charsetSize);
    return static_cast<int>(entropy);
}

bool PasswordStrengthCalculator::hasUppercase(const std::string& password)
{
    for (char ch : password) {
        if (std::isupper(static_cast<unsigned char>(ch))) return true;
    }
    return false;
}

bool PasswordStrengthCalculator::hasLowercase(const std::string& password)
{
    for (char ch : password) {
        if (std::islower(static_cast<unsigned char>(ch))) return true;
    }
    return false;
}

bool PasswordStrengthCalculator::hasNumbers(const std::string& password)
{
    for (char ch : password) {
        if (std::isdigit(static_cast<unsigned char>(ch))) return true;
    }
    return false;
}

bool PasswordStrengthCalculator::hasSymbols(const std::string& password)
{
    for (char ch : password) {
        if (!std::isalnum(static_cast<unsigned char>(ch))) return true;
    }
    return false;
}

} // namespace Utils
} // namespace CipherMesh
