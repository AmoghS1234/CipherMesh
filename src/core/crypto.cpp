#include <sodium.h>
#include "crypto.hpp"
#include <stdexcept>
#include <string> // for std::string

namespace CipherMesh {
namespace Core {

// ... (all other functions: deriveKey, encrypt, decrypt, etc. remain unchanged) ...
std::vector<unsigned char> Crypto::deriveKey(const std::string& password, const std::vector<unsigned char>& salt) {
    if (salt.size() != SALT_SIZE) {
        throw std::runtime_error("Invalid salt size.");
    }
    std::vector<unsigned char> key(KEY_SIZE);
    if (crypto_pwhash(
            key.data(), key.size(),
            password.c_str(), password.length(),
            salt.data(),
            crypto_pwhash_OPSLIMIT_INTERACTIVE,
            crypto_pwhash_MEMLIMIT_INTERACTIVE,
            crypto_pwhash_ALG_DEFAULT
        ) != 0) {
        throw std::runtime_error("Key derivation (crypto_pwhash) failed.");
    }
    return key;
}

std::vector<unsigned char> Crypto::encrypt(const std::vector<unsigned char>& plaintext, const std::vector<unsigned char>& key) {
    if (key.size() != KEY_SIZE) {
        throw std::runtime_error("Invalid key size for encryption.");
    }
    std::vector<unsigned char> ciphertext(NONCE_SIZE + plaintext.size() + TAG_SIZE);
    std::vector<unsigned char> nonce = randomBytes(NONCE_SIZE);
    unsigned long long ciphertext_len;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            ciphertext.data() + NONCE_SIZE, &ciphertext_len,
            plaintext.data(), plaintext.size(),
            nullptr, 0,
            nullptr,
            nonce.data(),
            key.data()
        ) != 0) {
        throw std::runtime_error("Encryption (crypto_aead_xchacha20poly1305_ietf_encrypt) failed.");
    }
    std::copy(nonce.begin(), nonce.end(), ciphertext.begin());
    ciphertext.resize(NONCE_SIZE + ciphertext_len);
    return ciphertext;
}

std::vector<unsigned char> Crypto::encrypt(const std::string& plaintext, const std::vector<unsigned char>& key) {
    std::vector<unsigned char> pt(plaintext.begin(), plaintext.end());
    return encrypt(pt, key);
}

std::vector<unsigned char> Crypto::decrypt(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key) {
    if (key.size() != KEY_SIZE) {
        throw std::runtime_error("Invalid key size for decryption.");
    }
    if (ciphertext.size() < NONCE_SIZE + TAG_SIZE) {
        throw std::runtime_error("Invalid ciphertext size (too small).");
    }
    std::vector<unsigned char> nonce(ciphertext.begin(), ciphertext.begin() + NONCE_SIZE);
    const unsigned char* encrypted_data = ciphertext.data() + NONCE_SIZE;
    size_t encrypted_data_len = ciphertext.size() - NONCE_SIZE;
    std::vector<unsigned char> decrypted(encrypted_data_len - TAG_SIZE);
    unsigned long long decrypted_len;
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            decrypted.data(), &decrypted_len,
            nullptr,
            encrypted_data, encrypted_data_len,
            nullptr, 0,
            nonce.data(),
            key.data()
        ) != 0) {
        throw std::runtime_error("Decryption failed. Invalid key or tampered data.");
    }
    decrypted.resize(decrypted_len);
    return decrypted;
}

std::string Crypto::decryptToString(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key) {
    std::vector<unsigned char> decrypted = decrypt(ciphertext, key);
    return std::string(decrypted.begin(), decrypted.end());
}

std::vector<unsigned char> Crypto::randomBytes(size_t size) {
    std::vector<unsigned char> buf(size);
    randombytes_buf(buf.data(), size);
    return buf;
}

void Crypto::secureWipe(std::vector<unsigned char>& data) {
    if (!data.empty()) {
        sodium_memzero(data.data(), data.size());
    }
    data.clear();
}

void Crypto::secureWipe(std::string& str) {
    if (!str.empty()) {
        sodium_memzero(&str[0], str.size());
    }
    str.clear();
}

// --- UPDATED PASSWORD GENERATOR IMPLEMENTATION ---

std::string Crypto::generatePassword(const PasswordOptions& options) {
    const std::string upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const std::string lower = "abcdefghijklmnopqrstuvwxyz";
    const std::string numbers = "0123456789";

    std::string charSet;
    if (options.useUppercase) charSet += upper;
    if (options.useLowercase) charSet += lower;
    if (options.useNumbers) charSet += numbers;
    
    // --- USE THE CUSTOM SYMBOLS STRING ---
    if (!options.customSymbols.empty()) {
        charSet += options.customSymbols;
    }

    if (charSet.empty()) {
        throw std::runtime_error("No character sets selected.");
    }

    std::string password;
    password.reserve(options.length);

    for (int i = 0; i < options.length; ++i) {
        uint32_t index = randombytes_uniform(charSet.length());
        password += charSet[index];
    }

    return password;
}

} 
}