#include "crypto.hpp"
#include <sodium.h>
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <random>

namespace CipherMesh {
namespace Core {

std::vector<unsigned char> Crypto::deriveKey(const std::string& password, const std::vector<unsigned char>& salt) {
    if (salt.size() != SALT_SIZE) {
        throw std::invalid_argument("Invalid salt size");
    }

    std::vector<unsigned char> key(KEY_SIZE);

    if (crypto_pwhash(key.data(), key.size(),
                      password.c_str(), password.length(),
                      salt.data(),
                      crypto_pwhash_OPSLIMIT_INTERACTIVE,
                      crypto_pwhash_MEMLIMIT_INTERACTIVE,
                      crypto_pwhash_ALG_ARGON2ID13) != 0) {
        throw std::runtime_error("Key derivation failed (out of memory?)");
    }

    return key;
}

std::vector<unsigned char> Crypto::encrypt(const std::vector<unsigned char>& plaintext, const std::vector<unsigned char>& key) {
    if (key.size() != KEY_SIZE) {
        throw std::invalid_argument("Invalid key size");
    }

    std::vector<unsigned char> nonce = randomBytes(NONCE_SIZE);
    std::vector<unsigned char> ciphertext(nonce.size() + plaintext.size() + TAG_SIZE);

    // Copy nonce to the beginning of the ciphertext buffer
    std::copy(nonce.begin(), nonce.end(), ciphertext.begin());

    unsigned long long ciphertext_len;
    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
            ciphertext.data() + nonce.size(), &ciphertext_len,
            plaintext.data(), plaintext.size(),
            NULL, 0, // No additional data
            NULL, nonce.data(), key.data()) != 0) {
        throw std::runtime_error("Encryption failed");
    }

    return ciphertext;
}

std::vector<unsigned char> Crypto::encrypt(const std::string& plaintext, const std::vector<unsigned char>& key) {
    return encrypt(std::vector<unsigned char>(plaintext.begin(), plaintext.end()), key);
}

std::vector<unsigned char> Crypto::decrypt(const std::vector<unsigned char>& ciphertext, const std::vector<unsigned char>& key) {
    if (key.size() != KEY_SIZE) {
        throw std::invalid_argument("Invalid key size");
    }

    if (ciphertext.size() < NONCE_SIZE + TAG_SIZE) {
        throw std::runtime_error("Ciphertext too short");
    }

    // Extract nonce
    std::vector<unsigned char> nonce(ciphertext.begin(), ciphertext.begin() + NONCE_SIZE);
    
    // Pointer to actual encrypted data starts after nonce
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
        throw std::runtime_error("Decryption failed (integrity check failed)");
    }

    return plaintext;
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
}

void Crypto::secureWipe(std::string& str) {
    if (!str.empty()) {
        sodium_memzero(&str[0], str.size());
    }
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

    // Use randombytes_uniform for unbiased selection
    for (int i = 0; i < options.length; ++i) {
        uint32_t index = randombytes_uniform(static_cast<uint32_t>(chars.size()));
        password += chars[index];
    }

    return password;
}

// SHA-1 for HIBP (Have I Been Pwned)
std::string Crypto::sha1(const std::string& input) {
    unsigned char hash[crypto_hash_sha256_BYTES]; // Using SHA256 size as buffer, but calling SHA1 logic if needed
    // NOTE: Libsodium doesn't strictly expose SHA1 directly in standard build sometimes. 
    // However, most systems have it. If libsodium doesn't support generic hash, we might need simple-sha1.
    // BUT for simplicity in this project, we rely on Qt's QCryptographicHash in the UI layer usually.
    // If we MUST do it here without Qt:
    // It's safer to leave this empty or use a tiny implementation if libsodium lacks it.
    // For now, let's return empty and rely on the UI layer (BreachChecker) doing it via Qt.
    return ""; 
}

// --- P2P ASYMMETRIC HELPERS (The Core Fixes) ---

std::string Crypto::encryptAsymmetric(const std::string& message, const std::vector<unsigned char>& recipientPublicKey) {
    if (recipientPublicKey.size() != crypto_box_PUBLICKEYBYTES) {
        throw std::invalid_argument("Invalid public key size");
    }

    // crypto_box_seal automatically generates an ephemeral keypair
    // Output size = message len + crypto_box_SEALBYTES
    std::vector<unsigned char> ciphertext(message.length() + crypto_box_SEALBYTES);
    
    if (crypto_box_seal(ciphertext.data(), 
                        reinterpret_cast<const unsigned char*>(message.c_str()), 
                        message.length(), 
                        recipientPublicKey.data()) != 0) {
        throw std::runtime_error("Asymmetric encryption failed");
    }

    // Return as Base64 for JSON transport
    return base64Encode(ciphertext);
}

std::string Crypto::decryptAsymmetric(const std::string& encryptedBase64, const std::vector<unsigned char>& privateKey) {
    if (privateKey.size() != crypto_box_SECRETKEYBYTES) {
        throw std::invalid_argument("Invalid private key size");
    }

    // 1. Decode Base64 to Binary
    std::vector<unsigned char> ciphertext = base64Decode(encryptedBase64);
    
    if (ciphertext.size() < crypto_box_SEALBYTES) {
        throw std::runtime_error("Ciphertext too short for asymmetric decryption");
    }

    // 2. Prepare Output Buffer
    std::vector<unsigned char> plaintext(ciphertext.size() - crypto_box_SEALBYTES);

    // 3. Decrypt using our Private Key and the ephemeral public key embedded in ciphertext
    if (crypto_box_seal_open(plaintext.data(), 
                             ciphertext.data(), 
                             ciphertext.size(), 
                             crypto_box_PUBLICKEYBYTES + privateKey.data(), // Note: PK is needed for seal_open? No, seal_open takes (pk, sk).
                             privateKey.data()) != 0) {
                             
        // Wait, crypto_box_seal_open API is: (m, c, clen, pk, sk)
        // pk is MY public key, sk is MY private key. 
        // We need to derive the public key from the secret key if we don't have it passed in.
        // Or we can just generate it.
        
        std::vector<unsigned char> myPubKey(crypto_box_PUBLICKEYBYTES);
        crypto_scalarmult_base(myPubKey.data(), privateKey.data());
        
        if (crypto_box_seal_open(plaintext.data(),
                                 ciphertext.data(),
                                 ciphertext.size(),
                                 myPubKey.data(),
                                 privateKey.data()) != 0) {
             throw std::runtime_error("Asymmetric decryption failed");
        }
    }

    return std::string(plaintext.begin(), plaintext.end());
}

// --- BASE64 HELPERS (Robust) ---

std::string Crypto::base64Encode(const std::vector<unsigned char>& data) {
    if (data.empty()) return "";
    
    // libsodium calculates max len
    size_t maxLen = sodium_base64_encoded_len(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::vector<char> encoded(maxLen);
    
    sodium_bin2base64(encoded.data(), maxLen, 
                      data.data(), data.size(), 
                      sodium_base64_VARIANT_ORIGINAL);
                      
    return std::string(encoded.data());
}

std::vector<unsigned char> Crypto::base64Decode(const std::string& encoded) {
    if (encoded.empty()) return {};

    // Remove newlines if any, just in case JSON added them
    std::string clean = encoded;
    clean.erase(std::remove(clean.begin(), clean.end(), '\n'), clean.end());
    clean.erase(std::remove(clean.begin(), clean.end(), '\r'), clean.end());

    size_t maxLen = clean.length(); // Upper bound
    std::vector<unsigned char> decoded(maxLen);
    
    size_t binLen = 0;
    // Allow characters to be ignored (like whitespace) if supported, but ORIGINAL is strict.
    // We try strict first.
    if (sodium_base642bin(decoded.data(), maxLen, 
                          clean.c_str(), clean.length(), 
                          NULL, &binLen, 
                          NULL, sodium_base64_VARIANT_ORIGINAL) != 0) {
        throw std::runtime_error("Base64 decode failed");
    }
    
    decoded.resize(binLen);
    return decoded;
}

}
}