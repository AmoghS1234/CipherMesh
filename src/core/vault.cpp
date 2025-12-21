#include <sodium.h>
#include "vault.hpp"
#include "database.hpp"
#include "crypto.hpp"
#include <stdexcept>
#include <iostream>

namespace CipherMesh {
namespace Core {

const std::string KEY_CANARY = "CIPHERMESH_OK";

Vault::Vault() : m_activeGroupId(-1) {
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium initialization failed!");
    }
    m_db = std::make_unique<Database>();
    m_crypto = std::make_unique<Crypto>();
}

Vault::~Vault() {
    lock();
    if (m_db) {
        m_db->close();
    }
}

bool Vault::createNewVault(const std::string& path, const std::string& masterPassword) {
    try {
        if (m_db) {
            m_db->close();
        }
        lock();
        m_dbPath = path; 
        m_db->open(path);
        m_db->createTables();
        std::vector<unsigned char> salt = m_crypto->randomBytes(m_crypto->SALT_SIZE);
        m_masterKey_RAM = m_crypto->deriveKey(masterPassword, salt);
        m_db->storeMetadata("argon_salt", salt);
        std::vector<unsigned char> canary_blob = m_crypto->encrypt(KEY_CANARY, m_masterKey_RAM);
        m_db->storeMetadata("key_canary", canary_blob);
        
        ensureIdentityKeys(); // P2P Key Gen
        
        addGroup("Personal");
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to create vault: " << e.what() << std::endl;
        lock();
        return false;
    }
}

bool Vault::loadVault(const std::string& path, const std::string& masterPassword) {
    try {
        if (m_db) {
            m_db->close();
        }
        lock();
        m_dbPath = path; 
        m_db->open(path);
        m_db->createTables();
        std::vector<unsigned char> salt = m_db->getMetadata("argon_salt");
        m_masterKey_RAM = m_crypto->deriveKey(masterPassword, salt);
        std::vector<unsigned char> canary_blob = m_db->getMetadata("key_canary");
        std::string decrypted_canary = m_crypto->decryptToString(canary_blob, m_masterKey_RAM);
        if (decrypted_canary != KEY_CANARY) {
            lock();
            return false;
        }
        
        ensureIdentityKeys(); // P2P Key Load
        
        return true;
    } catch (const std::exception& e) {
        lock();
        return false;
    }
}

void Vault::ensureIdentityKeys() {
    try {
        std::vector<unsigned char> encPriv = m_db->getMetadata("identity_priv");
        std::vector<unsigned char> pub = m_db->getMetadata("identity_pub");
        
        m_identityPrivateKey = m_crypto->decrypt(encPriv, m_masterKey_RAM);
        m_identityPublicKey = pub;
    } catch (...) {
        // Generate new keys
        m_identityPublicKey.resize(crypto_box_PUBLICKEYBYTES);
        m_identityPrivateKey.resize(crypto_box_SECRETKEYBYTES);
        crypto_box_keypair(m_identityPublicKey.data(), m_identityPrivateKey.data());
        
        std::vector<unsigned char> encPriv = m_crypto->encrypt(m_identityPrivateKey, m_masterKey_RAM);
        
        m_db->storeMetadata("identity_priv", encPriv);
        m_db->storeMetadata("identity_pub", m_identityPublicKey);
    }
}

void Vault::lock() {
    m_crypto->secureWipe(m_masterKey_RAM);
    m_crypto->secureWipe(m_activeGroupKey_RAM);
    m_crypto->secureWipe(m_identityPrivateKey);
    m_activeGroupId = -1;
    m_activeGroupName = "";
}

bool Vault::isLocked() const {
    return m_masterKey_RAM.empty();
}

void Vault::checkLocked() const {
    if (isLocked()) {
        throw std::runtime_error("Vault is locked.");
    }
}

bool Vault::verifyMasterPassword(const std::string& password) {
    if (!m_db || !m_db->isOpen() || m_dbPath.empty()) {
        return false;
    }
    try {
        std::vector<unsigned char> salt = m_db->getMetadata("argon_salt");
        std::vector<unsigned char> tempKey = m_crypto->deriveKey(password, salt);
        std::vector<unsigned char> canary_blob = m_db->getMetadata("key_canary");
        std::string decrypted_canary = m_crypto->decryptToString(canary_blob, tempKey);
        m_crypto->secureWipe(tempKey); 
        return (decrypted_canary == KEY_CANARY);
    } catch (...) {
        return false; 
    }
}

bool Vault::changeMasterPassword(const std::string& newPassword) {
    checkLocked(); 
    try {
        std::vector<unsigned char> newSalt = m_crypto->randomBytes(m_crypto->SALT_SIZE);
        std::vector<unsigned char> newMasterKey = m_crypto->deriveKey(newPassword, newSalt);
        std::map<int, std::vector<unsigned char>> oldGroupKeys = m_db->getAllEncryptedGroupKeys();
        
        for (auto const& [groupId, oldEncryptedKey] : oldGroupKeys) {
            std::vector<unsigned char> groupKey = m_crypto->decrypt(oldEncryptedKey, m_masterKey_RAM);
            std::vector<unsigned char> newEncryptedKey = m_crypto->encrypt(groupKey, newMasterKey);
            m_db->updateEncryptedGroupKey(groupId, newEncryptedKey);
            m_crypto->secureWipe(groupKey);
        }
        
        if (!m_identityPrivateKey.empty()) {
            std::vector<unsigned char> newEncPriv = m_crypto->encrypt(m_identityPrivateKey, newMasterKey);
            m_db->storeMetadata("identity_priv", newEncPriv);
        }

        std::vector<unsigned char> new_canary_blob = m_crypto->encrypt(KEY_CANARY, newMasterKey);
        m_db->storeMetadata("key_canary", new_canary_blob);
        m_db->storeMetadata("argon_salt", newSalt);
        
        m_crypto->secureWipe(m_masterKey_RAM);
        m_masterKey_RAM = std::move(newMasterKey);
        
        if (isGroupActive()) {
            setActiveGroup(m_activeGroupName);
        }
        return true;
    } catch (...) {
        return false; 
    }
}

// ... Standard methods ...
std::vector<std::string> Vault::getGroupNames() { checkLocked(); return m_db->getAllGroupNames(); }
bool Vault::groupExists(const std::string& groupName) { checkLocked(); try { m_db->getGroupId(groupName); return true; } catch(...) { return false; } }
int Vault::getGroupId(const std::string& groupName) { checkLocked(); return m_db->getGroupId(groupName); }
std::string Vault::getGroupOwner(int groupId) { checkLocked(); return m_db->getGroupOwner(groupId); }
void Vault::setGroupPermissions(int groupId, bool adminsOnly) { checkLocked(); m_db->setGroupPermissions(groupId, adminsOnly); }
GroupPermissions Vault::getGroupPermissions(int groupId) { checkLocked(); return m_db->getGroupPermissions(groupId); }
void Vault::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) { checkLocked(); m_db->updateGroupMemberRole(groupId, userId, newRole); }
void Vault::addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status) { checkLocked(); int groupId = m_db->getGroupId(groupName); m_db->addGroupMember(groupId, userId, role, status); }
void Vault::addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status) { checkLocked(); m_db->addGroupMember(groupId, userId, role, status); }
std::vector<GroupMember> Vault::getGroupMembers(const std::string& groupName) { checkLocked(); int groupId = m_db->getGroupId(groupName); return m_db->getGroupMembers(groupId); }
void Vault::removeGroupMember(const std::string& groupName, const std::string& userId) { checkLocked(); int groupId = m_db->getGroupId(groupName); m_db->removeGroupMember(groupId, userId); }
void Vault::updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus) { checkLocked(); int groupId = m_db->getGroupId(groupName); m_db->updateGroupMemberStatus(groupId, userId, newStatus); }
void Vault::updatePendingInviteStatus(int inviteId, const std::string& status) { checkLocked(); m_db->updatePendingInviteStatus(inviteId, status); }

bool Vault::addGroup(const std::string& groupName) {
    checkLocked();
    try {
        std::vector<unsigned char> newGroupKey = m_crypto->randomBytes(m_crypto->KEY_SIZE);
        std::vector<unsigned char> encryptedGroupKey = m_crypto->encrypt(newGroupKey, m_masterKey_RAM);
        std::string ownerId = getUserId(); if(ownerId.empty()) ownerId = "me";
        m_db->storeEncryptedGroup(groupName, encryptedGroupKey, ownerId);
        m_crypto->secureWipe(newGroupKey);
        int gid = m_db->getGroupId(groupName);
        m_db->addGroupMember(gid, ownerId, "owner", "accepted");
        return true;
    } catch (...) { return false; }
}

bool Vault::addGroup(const std::string& groupName, const std::vector<unsigned char>& key) {
    checkLocked();
    try {
        std::vector<unsigned char> encryptedGroupKey = m_crypto->encrypt(key, m_masterKey_RAM);
        std::string ownerId = getUserId(); if(ownerId.empty()) ownerId = "me";
        m_db->storeEncryptedGroup(groupName, encryptedGroupKey, ownerId);
        int gid = m_db->getGroupId(groupName);
        m_db->addGroupMember(gid, ownerId, "admin", "accepted");
        return true;
    } catch (...) { return false; }
}

bool Vault::setActiveGroup(const std::string& groupName) {
    checkLocked();
    lockActiveGroup();
    try {
        int groupId = -1;
        std::vector<unsigned char> encryptedKey = m_db->getEncryptedGroupKey(groupName, groupId);
        m_activeGroupKey_RAM = m_crypto->decrypt(encryptedKey, m_masterKey_RAM);
        m_activeGroupId = groupId;
        m_activeGroupName = groupName;
        return true;
    } catch (...) { lockActiveGroup(); return false; }
}

void Vault::lockActiveGroup() {
    m_crypto->secureWipe(m_activeGroupKey_RAM);
    m_activeGroupId = -1;
    m_activeGroupName = "";
}

bool Vault::isGroupActive() const { return !m_activeGroupKey_RAM.empty() && m_activeGroupId != -1; }

bool Vault::deleteGroup(const std::string& groupName) {
    checkLocked();
    try {
        if (groupName == m_activeGroupName) lockActiveGroup();
        return m_db->deleteGroup(groupName);
    } catch (...) { return false; }
}

void Vault::checkGroupActive() const { checkLocked(); if (!isGroupActive()) throw std::runtime_error("No group is active."); }

std::vector<VaultEntry> Vault::getEntries() { checkGroupActive(); return m_db->getEntriesForGroup(m_activeGroupId); }

bool Vault::addEntry(const VaultEntry& entry, const std::string& password) {
    checkGroupActive();
    try {
        VaultEntry tempEntry = entry; 
        std::vector<unsigned char> encryptedPassword = m_crypto->encrypt(password, m_activeGroupKey_RAM);
        m_db->storeEntry(m_activeGroupId, tempEntry, encryptedPassword);
        return true;
    } catch (...) { return false; }
}

bool Vault::deleteEntry(int entryId) { checkGroupActive(); try { return m_db->deleteEntry(entryId); } catch (...) { return false; } }

bool Vault::updateEntry(const VaultEntry& entry, const std::string& newPassword) {
    checkGroupActive();
    try {
        if (!newPassword.empty()) {
            std::vector<unsigned char> encryptedPassword = m_crypto->encrypt(newPassword, m_activeGroupKey_RAM);
            m_db->updateEntry(entry, &encryptedPassword);
        } else {
            m_db->updateEntry(entry, nullptr); 
        }
        return true;
    } catch (...) { return false; }
}

std::string Vault::getDecryptedPassword(int entryId) {
    checkLocked(); 
    int groupId = m_db->getGroupIdForEntry(entryId);
    std::vector<unsigned char> encryptedGroupKey = m_db->getEncryptedGroupKeyById(groupId);
    std::vector<unsigned char> groupKey = m_crypto->decrypt(encryptedGroupKey, m_masterKey_RAM);
    std::vector<unsigned char> encryptedPassword = m_db->getEncryptedPassword(entryId);
    std::string decryptedPassword = m_crypto->decryptToString(encryptedPassword, groupKey);
    m_crypto->secureWipe(groupKey);
    return decryptedPassword;
}

bool Vault::entryExists(const std::string& username, const std::string& locationValue) { checkLocked(); return m_db->entryExists(username, locationValue); }
std::vector<VaultEntry> Vault::findEntriesByLocation(const std::string& locationValue) { checkLocked(); return m_db->findEntriesByLocation(locationValue); }
std::vector<VaultEntry> Vault::searchEntries(const std::string& searchTerm) { checkLocked(); return m_db->searchEntries(searchTerm); }

std::vector<unsigned char> Vault::getGroupKey(const std::string& groupName) {
    checkLocked();
    if (isGroupActive() && m_activeGroupName == groupName) return m_activeGroupKey_RAM;
    int groupId = -1;
    std::vector<unsigned char> encryptedKey = m_db->getEncryptedGroupKey(groupName, groupId);
    return m_crypto->decrypt(encryptedKey, m_masterKey_RAM);
}

std::vector<VaultEntry> Vault::exportGroupEntries(const std::string& groupName) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    std::vector<unsigned char> groupKey;
    if (isGroupActive() && m_activeGroupName == groupName) groupKey = m_activeGroupKey_RAM;
    else {
        std::vector<unsigned char> encKey = m_db->getEncryptedGroupKeyById(groupId);
        groupKey = m_crypto->decrypt(encKey, m_masterKey_RAM);
    }
    std::vector<VaultEntry> entries = m_db->getEntriesForGroup(groupId);
    for (auto& entry : entries) {
        std::vector<unsigned char> encPass = m_db->getEncryptedPassword(entry.id);
        entry.password = m_crypto->decryptToString(encPass, groupKey);
    }
    if (!isGroupActive() || m_activeGroupName != groupName) m_crypto->secureWipe(groupKey);
    return entries;
}

// --- UPDATED IMPORT WITH VALIDATION ---
void Vault::importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    std::vector<unsigned char> groupKey;
    if (isGroupActive() && m_activeGroupName == groupName) groupKey = m_activeGroupKey_RAM;
    else {
        std::vector<unsigned char> encKey = m_db->getEncryptedGroupKeyById(groupId);
        groupKey = m_crypto->decrypt(encKey, m_masterKey_RAM);
    }
    
    for (auto entry : entries) {
        // --- SECURITY VALIDATION: Prevent buffer overflows / malicious huge data ---
        if (entry.title.length() > 256) entry.title = entry.title.substr(0, 256);
        if (entry.username.length() > 256) entry.username = entry.username.substr(0, 256);
        if (entry.notes.length() > 10000) entry.notes = entry.notes.substr(0, 10000); 
        // --------------------------------------------------------------------------

        std::vector<unsigned char> encPass = m_crypto->encrypt(entry.password, groupKey);
        m_db->storeEntry(groupId, entry, encPass);
        m_crypto->secureWipe(entry.password); 
    }
    if (!isGroupActive() || m_activeGroupName != groupName) m_crypto->secureWipe(groupKey);
}

void Vault::setUserId(const std::string& userId) { checkLocked(); std::vector<unsigned char> idData(userId.begin(), userId.end()); m_db->storeMetadata("user_id", idData); }
std::string Vault::getUserId() { checkLocked(); try { std::vector<unsigned char> data = m_db->getMetadata("user_id"); return std::string(data.begin(), data.end()); } catch (...) { return ""; } }
void Vault::storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson) { checkLocked(); m_db->storePendingInvite(senderId, groupName, payloadJson); }
std::vector<PendingInvite> Vault::getPendingInvites() { checkLocked(); return m_db->getPendingInvites(); }
void Vault::deletePendingInvite(int inviteId) { checkLocked(); m_db->deletePendingInvite(inviteId); }

bool Vault::canUserEdit(const std::string& groupName) {
    checkLocked();
    try {
        int groupId = m_db->getGroupId(groupName);
        std::string myId = getUserId();
        if (myId.empty()) return true;
        if (m_db->getGroupOwner(groupId) == myId || m_db->getGroupOwner(groupId) == "me") return true;

        std::vector<GroupMember> members = m_db->getGroupMembers(groupId);
        std::string myRole = "member";
        bool found = false;
        for(const auto& m : members) { if(m.userId == myId) { myRole = m.role; found = true; break; } }
        if (!found) return true; 
        if (myRole == "owner" || myRole == "admin") return true;

        GroupPermissions perms = m_db->getGroupPermissions(groupId);
        if (perms.adminsOnlyWrite) return false;
        return true; 
    } catch (...) { return true; }
}

void Vault::setThemeId(const std::string& themeId) { checkLocked(); std::vector<unsigned char> data(themeId.begin(), themeId.end()); m_db->storeMetadata("app_theme", data); }
std::string Vault::getThemeId() { checkLocked(); try { std::vector<unsigned char> data = m_db->getMetadata("app_theme"); return std::string(data.begin(), data.end()); } catch (...) { return "professional"; } }
void Vault::setAutoLockTimeout(int minutes) { checkLocked(); std::string value = std::to_string(minutes); std::vector<unsigned char> data(value.begin(), value.end()); m_db->storeMetadata("auto_lock_timeout", data); }
int Vault::getAutoLockTimeout() { checkLocked(); try { std::vector<unsigned char> data = m_db->getMetadata("auto_lock_timeout"); std::string value(data.begin(), data.end()); return std::stoi(value); } catch (...) { return 15; } }
std::vector<PasswordHistoryEntry> Vault::getPasswordHistory(int entryId) { checkLocked(); checkGroupActive(); return m_db->getPasswordHistory(entryId); }
std::string Vault::decryptPasswordFromHistory(const std::string& encryptedPassword) { checkLocked(); checkGroupActive(); std::vector<unsigned char> encrypted(encryptedPassword.begin(), encryptedPassword.end()); std::vector<unsigned char> decrypted = m_crypto->decrypt(encrypted, m_activeGroupKey_RAM); return std::string(decrypted.begin(), decrypted.end()); }
void Vault::updateEntryAccessTime(int entryId) { checkLocked(); checkGroupActive(); m_db->updateEntryAccessTime(entryId); }
std::vector<VaultEntry> Vault::getRecentlyAccessedEntries(int limit) { checkLocked(); checkGroupActive(); return m_db->getRecentlyAccessedEntries(m_activeGroupId, limit); }

// --- P2P ASYMMETRIC LOGIC ---

std::string Vault::getIdentityPublicKey() {
    checkLocked();
    if (m_identityPublicKey.empty()) ensureIdentityKeys();
    return m_crypto->base64Encode(m_identityPublicKey);
}

std::string Vault::decryptIncomingKey(const std::string& encryptedBase64) {
    checkLocked();
    if (m_identityPrivateKey.empty()) ensureIdentityKeys();
    return m_crypto->decryptAsymmetric(encryptedBase64, m_identityPrivateKey);
}

// --- NEW SECURE SENDER METHOD ---
std::vector<unsigned char> Vault::encryptForUser(const std::string& recipientPubKeyBase64, const std::vector<unsigned char>& data) {
    checkLocked();
    
    // 1. Decode the Recipient's Public Key
    std::vector<unsigned char> recipientKeyBin = m_crypto->base64Decode(recipientPubKeyBase64);
    
    if (recipientKeyBin.size() != crypto_box_PUBLICKEYBYTES) {
        throw std::runtime_error("Invalid recipient public key size.");
    }
    
    // 2. Prepare ciphertext buffer
    // sealed box size = message size + crypto_box_SEALBYTES
    std::vector<unsigned char> ciphertext(crypto_box_SEALBYTES + data.size());
    
    // 3. Encrypt (Sealed Box)
    // crypto_box_seal automatically generates an ephemeral keypair, encrypts the message,
    // and prepends the ephemeral public key to the ciphertext.
    if (crypto_box_seal(ciphertext.data(), data.data(), data.size(), recipientKeyBin.data()) != 0) {
        throw std::runtime_error("Asymmetric encryption failed.");
    }
    
    return ciphertext;
}

}
}