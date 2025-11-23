#pragma once

#include <string>

namespace CipherMesh {
namespace Utils {

enum class PasswordStrength {
    VeryWeak,
    Weak,
    Fair,
    Strong,
    VeryStrong
};

struct PasswordStrengthInfo {
    PasswordStrength strength;
    std::string text;
    int score; // 0-100
    
    // RGB color values (0-255) for UI representation
    struct {
        int r;
        int g;
        int b;
    } color;
};

class PasswordStrengthCalculator {
public:
    static PasswordStrengthInfo calculate(const std::string& password);
    
private:
    static int calculateEntropy(const std::string& password);
    static bool hasUppercase(const std::string& password);
    static bool hasLowercase(const std::string& password);
    static bool hasNumbers(const std::string& password);
    static bool hasSymbols(const std::string& password);
};

} // namespace Utils
} // namespace CipherMesh
