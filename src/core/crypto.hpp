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
        std::string customSymbols = "!@#$%^&*()_+-=[]{}|;:,.<>?";
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
    
    // SHA-1 hash for breach detection (Have I Been Pwned API)
    static std::string sha1(const std::string& input);

    // --- REQUIRED FOR P2P SECURITY (Added to support Vault) ---
    // Encrypts for a specific recipient using their Public Key (Sealed Box)
    static std::string encryptAsymmetric(const std::string& message, const std::vector<unsigned char>& recipientPublicKey);
    
    // Decrypts a message meant for us using our Private Key
    static std::string decryptAsymmetric(const std::string& encryptedBase64, const std::vector<unsigned char>& privateKey);

    // Base64 Helpers
    static std::string base64Encode(const std::vector<unsigned char>& data);
    static std::vector<unsigned char> base64Decode(const std::string& encoded);
};

}
}