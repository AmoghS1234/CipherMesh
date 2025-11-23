#pragma once

#include <sodium.h> 
#include <vector>
#include <string>

namespace CipherMesh {
namespace Core {

class Crypto {
public:
    static const size_t KEY_SIZE = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
    static const size_t NONCE_SIZE = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    static const size_t SALT_SIZE = crypto_pwhash_SALTBYTES;
    static const size_t TAG_SIZE = crypto_aead_xchacha20poly1305_ietf_ABYTES;

    // --- UPDATED OPTIONS STRUCT ---
    struct PasswordOptions {
        int length = 16;
        bool useUppercase = true;
        bool useLowercase = true;
        bool useNumbers = true;
        std::string customSymbols = "!@#$%^&*()_+-=[]{}|;:,.<>?"; // <-- CHANGED
    };

    static std::vector<unsigned char> deriveKey(const std::string& password, const std::vector<unsigned char>& salt);
    static std::vector<unsigned char> encrypt(const std::vector<unsigned char>& plaintext, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> encrypt(const std::string& plaintext, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> decrypt(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key);
    static std::string decryptToString(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> randomBytes(size_t size);
    static void secureWipe(std::vector<unsigned char>& data);
    static void secureWipe(std::string& str);
    
    static std::string generatePassword(const PasswordOptions& options);
};

}
}