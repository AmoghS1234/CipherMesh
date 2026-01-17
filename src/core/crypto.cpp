#include "crypto.hpp"
#include <sodium.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <random>

namespace CipherMesh {
namespace Core {

// --- Key Derivation ---

std::vector<unsigned char> Crypto::deriveKey(const std::string& password, const std::vector<unsigned char>& salt) {
    if (salt.size() != SALT_SIZE) throw std::invalid_argument("Invalid salt size");

    std::vector<unsigned char> key(KEY_SIZE);
    if (crypto_pwhash(key.data(), key.size(),
                      password.c_str(), password.length(),
                      salt.data(),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("Key derivation failed");
    }
    return key;
}

// --- Symmetric Encryption ---

std::vector<unsigned char> Crypto::encrypt(const std::vector<unsigned char>& plaintext, const std::vector<unsigned char>& key) {
    if (key.size() != KEY_SIZE) throw std::invalid_argument("Invalid key size");

    std::vector<unsigned char> nonce = randomBytes(NONCE_SIZE);
    std::vector<unsigned char> ciphertext(nonce.size() + plaintext.size() + TAG_SIZE);

    std::copy(nonce.begin(), nonce.end(), ciphertext.begin());

    unsigned long long ciphertext_len;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            ciphertext.data() + nonce.size(), &ciphertext_len,
            plaintext.data(), plaintext.size(),
            NULL, 0,
            NULL, nonce.data(), key.data()) != 0) {
        throw std::runtime_error("Encryption failed");
    }
    return ciphertext;
}

void Crypto::randomBytes(unsigned char* buffer, size_t length) {
        // [FIX] Use libsodium's secure RNG instead of weak mt19937
        randombytes_buf(buffer, length);
    }

std::vector<unsigned char> Crypto::encrypt(const std::string& plaintext, const std::vector<unsigned char>& key) {
    return encrypt(std::vector<unsigned char>(plaintext.begin(), plaintext.end()), key);
}

std::vector<unsigned char> Crypto::decrypt(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key) {
    if (key.size() != KEY_SIZE) throw std::invalid_argument("Invalid key size");
    if (ciphertext.size() < NONCE_SIZE + TAG_SIZE) throw std::runtime_error("Ciphertext too short");

    std::vector<unsigned char> nonce(ciphertext.begin(), ciphertext.begin() + NONCE_SIZE);
    const unsigned char* cipher_ptr = ciphertext.data() + NONCE_SIZE;
    size_t cipher_len = ciphertext.size() - NONCE_SIZE;

    std::vector<unsigned char> plaintext(cipher_len - TAG_SIZE);
    unsigned long long plaintext_len;

    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            plaintext.data(), &plaintext_len,
            NULL,
            cipher_ptr, cipher_len,
            NULL, 0,
            nonce.data(), key.data()) != 0) {
        throw std::runtime_error("Decryption failed");
    }
    return plaintext;
}

std::string Crypto::decryptToString(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key) {
    std::vector<unsigned char> decrypted = decrypt(ciphertext, key);
    return std::string(decrypted.begin(), decrypted.end());
}

// --- Utilities ---

std::vector<unsigned char> Crypto::randomBytes(size_t size) {
    std::vector<unsigned char> buf(size);
    randombytes_buf(buf.data(), size);
    return buf;
}

void Crypto::secureWipe(std::vector<unsigned char>& data) {
    if (!data.empty()) sodium_memzero(data.data(), data.size());
}

void Crypto::secureWipe(std::string& str) {
    if (!str.empty()) sodium_memzero(&str[0], str.size());
}

std::string Crypto::generateUUID() {
    // Generate RFC4122 UUID v4 (random)
    unsigned char bytes[16];
    randombytes_buf(bytes, 16);
    
    // Set version (4) and variant bits
    bytes[6] = (bytes[6] & 0x0F) | 0x40; // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80; // Variant 10
    
    // Format as 8-4-4-4-12 hex string
    char uuid[37];
    snprintf(uuid, sizeof(uuid),
        "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
        bytes[0], bytes[1], bytes[2], bytes[3],
        bytes[4], bytes[5], bytes[6], bytes[7],
        bytes[8], bytes[9], bytes[10], bytes[11],
        bytes[12], bytes[13], bytes[14], bytes[15]);
    
    return std::string(uuid);
}

std::string Crypto::generatePassword(const PasswordOptions& options) {
    if (options.length <= 0) return "";

    std::string chars;
    if (options.useLowercase) chars += "abcdefghijklmnopqrstuvwxyz";
    if (options.useUppercase) chars += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (options.useNumbers) chars += "0123456789";
    chars += options.customSymbols;

    if (chars.empty()) return "";

    std::string password;
    password.reserve(options.length);

    for (int i = 0; i < options.length; ++i) {
        uint32_t index = randombytes_uniform(static_cast<uint32_t>(chars.size()));
        password += chars[index];
    }
    return password;
}

std::string Crypto::sha1(const std::string& input) {
    unsigned char hash[crypto_hash_sha256_BYTES]; // Use SHA256 instead of SHA1 (more secure)
    crypto_hash_sha256(hash, (const unsigned char*)input.c_str(), input.length());
    return base64Encode(std::vector<unsigned char>(hash, hash + crypto_hash_sha256_BYTES));
}

// --- Asymmetric / Sealed Box ---

// [FIX] Implemented decryptAsymmetric to support Vault.cpp calls
std::string Crypto::decryptAsymmetric(const std::string& ciphertextBase64, const std::vector<unsigned char>& privateKey) {
    std::vector<unsigned char> ciphertext = base64Decode(ciphertextBase64);
    
    // Libsodium's seal_open requires the recipient's Public Key as well.
    // We derive it from the Private Key.
    std::vector<unsigned char> pubKey(crypto_box_PUBLICKEYBYTES);
    crypto_scalarmult_base(pubKey.data(), privateKey.data());
    
    std::vector<unsigned char> decrypted = decryptSealed(ciphertext, pubKey, privateKey);
    return std::string(decrypted.begin(), decrypted.end());
}

std::vector<unsigned char> Crypto::decryptSealed(const std::vector<unsigned char>& ciphertext, 
                                                 const std::vector<unsigned char>& recipientPubKey, 
                                                 const std::vector<unsigned char>& recipientPrivKey) {
    if (ciphertext.size() < crypto_box_SEALBYTES) {
        throw std::runtime_error("Sealed ciphertext too short");
    }

    std::vector<unsigned char> decrypted(ciphertext.size() - crypto_box_SEALBYTES);
    
    if (crypto_box_seal_open(decrypted.data(), 
                             ciphertext.data(), 
                             ciphertext.size(), 
                             recipientPubKey.data(), 
                             recipientPrivKey.data()) != 0) {
        throw std::runtime_error("Sealed box decryption failed");
    }
    return decrypted;
}

std::vector<unsigned char> Crypto::encryptSealed(const std::vector<unsigned char>& message, 
                                                 const std::vector<unsigned char>& recipientPubKey) {
    std::vector<unsigned char> ciphertext(message.size() + crypto_box_SEALBYTES);
    if (crypto_box_seal(ciphertext.data(),
                        message.data(),
                        message.size(),
                        recipientPubKey.data()) != 0) {
        throw std::runtime_error("Sealed box encryption failed");
    }
    return ciphertext;
}

// --- Base64 Helpers ---

std::string Crypto::base64Encode(const std::vector<unsigned char>& data) {
    if (data.empty()) return "";
    size_t maxLen = sodium_base64_encoded_len(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::vector<char> encoded(maxLen);
    sodium_bin2base64(encoded.data(), maxLen, data.data(), data.size(), sodium_base64_VARIANT_ORIGINAL);
    return std::string(encoded.data());
}

std::vector<unsigned char> Crypto::base64Decode(const std::string& encoded) {
    if (encoded.empty()) return {};
    std::string clean = encoded;
    clean.erase(std::remove(clean.begin(), clean.end(), '\n'), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), '\r'), clean.end());

    size_t maxLen = clean.length();
    std::vector<unsigned char> decoded(maxLen);
    size_t binLen = 0;
    
    if (sodium_base642bin(decoded.data(), maxLen, clean.c_str(), clean.length(), 
                          NULL, &binLen, NULL, sodium_base64_VARIANT_ORIGINAL) != 0) {
        throw std::runtime_error("Base64 decode failed");
    }
    decoded.resize(binLen);
    return decoded;
}

}
}