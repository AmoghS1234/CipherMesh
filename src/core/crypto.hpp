#pragma once

#include <sodium.h> 
#include <vector>
#include <string>

namespace CipherMesh {
namespace Core {

// [FIX] Struct moved outside class so native-lib.cpp can access it as CipherMesh::Core::PasswordOptions
struct PasswordOptions {
    int length = 16;
    bool useUppercase = true;
    bool useLowercase = true;
    bool useNumbers = true;
    std::string customSymbols = "!@#$%^&*()_+-=[]{}|;:,.<>?";
};

class Crypto {
public:
    // --- Constants ---
    static const size_t KEY_SIZE = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;
    static const size_t NONCE_SIZE = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES;
    static const size_t SALT_SIZE = crypto_pwhash_SALTBYTES;
    static const size_t TAG_SIZE = crypto_aead_xchacha20poly1305_ietf_ABYTES;

    // --- Key Derivation (Argon2) ---
    static std::vector<unsigned char> deriveKey(const std::string& password, const std::vector<unsigned char>& salt);

    // --- Symmetric Encryption (XChaCha20-Poly1305) ---
    static std::vector<unsigned char> encrypt(const std::vector<unsigned char>& plaintext, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> encrypt(const std::string& plaintext, const std::vector<unsigned char>& key);
    static std::vector<unsigned char> decrypt(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key);
    static std::string decryptToString(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key);
    
    // --- Utilities ---
    static std::vector<unsigned char> randomBytes(size_t size);
    static void secureWipe(std::vector<unsigned char>& data);
    static void secureWipe(std::string& str);
    static std::string generatePassword(const PasswordOptions& options);
    
    // --- Hashing ---
    static std::string sha1(const std::string& input);

    // --- Asymmetric / Sealed Box (P2P Security) ---
    
    // [FIX] Added this method which was missing but called by Vault.cpp
    static std::string decryptAsymmetric(const std::string& ciphertextBase64, const std::vector<unsigned char>& privateKey);

    static std::vector<unsigned char> decryptSealed(const std::vector<unsigned char>& ciphertext, 
                                                    const std::vector<unsigned char>& recipientPubKey, 
                                                    const std::vector<unsigned char>& recipientPrivKey);

    static std::vector<unsigned char> encryptSealed(const std::vector<unsigned char>& message, 
                                                    const std::vector<unsigned char>& recipientPubKey);

    // --- Base64 Helpers ---
    static std::string base64Encode(const std::vector<unsigned char>& data);
    static std::vector<unsigned char> base64Decode(const std::string& encoded);
};

}
}