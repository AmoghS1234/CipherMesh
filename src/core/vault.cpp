#include <sodium.h>
#include "vault.hpp"
#include "crypto.hpp"
#include "database.hpp" 
#include <stdexcept>
#include <iostream>
#include <sqlite3.h>
#include <sstream> 
#include <vector>
#include <string>
#include <algorithm>

namespace CipherMesh {
namespace Core {

const std::string KEY_CANARY = "CIPHERMESH_OK";

Vault::Vault() : m_activeGroupId(-1) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed!");
    m_crypto = std::make_unique<Crypto>();
    m_db = std::make_unique<Database>();
}

Vault::~Vault() { lock(); if (m_db) m_db->close(); }

// --- Helpers ---

void Vault::checkGroupActive() const {
    checkLocked();
    if (!isGroupActive()) throw std::runtime_error("No group is active.");
}

bool Vault::isConnected() const { return m_db && m_db->isOpen(); }

void Vault::setUsername(const std::string& name) {
    checkLocked();
    std::vector<unsigned char> data(name.begin(), name.end());
    m_db->storeMetadata("user_display_name", data);
}

std::string Vault::getDisplayUsername() {
    if (isLocked() || !isConnected()) return "User";
    try {
        std::vector<unsigned char> data = m_db->getMetadata("user_display_name");
        return std::string(data.begin(), data.end());
    } catch (...) { return "User"; }
}

std::string Vault::getDBPath() const { return m_dbPath; }

// --- Settings ---

void Vault::setThemeId(const std::string& themeId) {
    checkLocked(); if (!isConnected()) return;
    std::vector<unsigned char> data(themeId.begin(), themeId.end());
    m_db->storeMetadata("app_theme", data);
}

std::string Vault::getThemeId() {
    if (!isConnected()) return "professional"; 
    try {
        std::vector<unsigned char> data = m_db->getMetadata("app_theme");
        if (data.empty()) return "professional"; 
        return std::string(data.begin(), data.end());
    } catch (...) { return "professional"; }
}

void Vault::setAutoLockTimeout(int minutes) {
    checkLocked(); if (!isConnected()) return;
    std::string val = std::to_string(minutes);
    std::vector<unsigned char> data(val.begin(), val.end());
    m_db->storeMetadata("auto_lock_timeout", data);
}

int Vault::getAutoLockTimeout() {
    if (!isConnected()) return 15; 
    try {
        std::vector<unsigned char> data = m_db->getMetadata("auto_lock_timeout");
        if (data.empty()) return 15;
        return std::stoi(std::string(data.begin(), data.end()));
    } catch (...) { return 15; }
}

// --- Sync ---

void Vault::setSyncCallback(SyncCallback cb) { m_syncCb = cb; }
void Vault::notifySync(const std::string& type, const std::string& payload) { if (m_syncCb) m_syncCb(type, payload); }
void Vault::processSyncEvent(const std::string&) {}

// --- Core Lifecycle ---

void Vault::connect(const std::string& path) {
    try {
        if (m_db->isOpen()) m_db->close();
        m_dbPath = path;
        m_db->open(path);
        m_db->createTables(); 
    } catch (...) {}
}

bool Vault::unlock(const std::string& masterPassword) {
    if (m_dbPath.empty()) return false;
    return loadVault(m_dbPath, masterPassword);
}

bool Vault::createNewVault(const std::string& path, const std::string& masterPassword) {
    try {
        if (m_db) m_db->close();
        lock();
        m_dbPath = path;
        m_db->open(path);
        m_db->createTables();
        std::vector<unsigned char> salt = m_crypto->randomBytes(m_crypto->SALT_SIZE);
        m_masterKey_RAM = m_crypto->deriveKey(masterPassword, salt);
        m_db->storeMetadata("argon_salt", salt);
        std::vector<unsigned char> canary_blob = m_crypto->encrypt(KEY_CANARY, m_masterKey_RAM);
        m_db->storeMetadata("key_canary", canary_blob);
        ensureIdentityKeys();
        addGroup("Personal"); 
        setThemeId("professional");
        setAutoLockTimeout(15);
        return true;
    } catch (...) { lock(); return false; }
}

bool Vault::loadVault(const std::string& path, const std::string& masterPassword) {
    try {
        if (!m_db->isOpen() || m_dbPath != path) connect(path);
        std::vector<unsigned char> salt = m_db->getMetadata("argon_salt");
        if (salt.empty()) return false;
        m_masterKey_RAM = m_crypto->deriveKey(masterPassword, salt);
        std::vector<unsigned char> canary_blob = m_db->getMetadata("key_canary");
        std::string decrypted_canary = m_crypto->decryptToString(canary_blob, m_masterKey_RAM);
        if (decrypted_canary != KEY_CANARY) { lock(); return false; }
        ensureIdentityKeys();
        return true;
    } catch (...) { lock(); return false; }
}

bool Vault::hasUsers() const {
    if (!isConnected()) return false;
    try { return !m_db->getMetadata("argon_salt").empty(); } catch (...) { return false; }
}

void Vault::ensureIdentityKeys() {
    try {
        std::vector<unsigned char> encPriv = m_db->getMetadata("identity_priv");
        std::vector<unsigned char> pub = m_db->getMetadata("identity_pub");
        if (encPriv.empty() || pub.empty()) throw std::runtime_error("Keys missing");
        m_identityPrivateKey = m_crypto->decrypt(encPriv, m_masterKey_RAM);
        m_identityPublicKey = pub;
    } catch (...) {
        m_identityPublicKey.resize(crypto_box_PUBLICKEYBYTES);
        m_identityPrivateKey.resize(crypto_box_SECRETKEYBYTES);
        crypto_box_keypair(m_identityPublicKey.data(), m_identityPrivateKey.data());
        std::vector<unsigned char> encPriv = m_crypto->encrypt(m_identityPrivateKey, m_masterKey_RAM);
        m_db->storeMetadata("identity_priv", encPriv);
        m_db->storeMetadata("identity_pub", m_identityPublicKey);
    }
}

void Vault::lock() {
    if (m_crypto) {
        m_crypto->secureWipe(m_masterKey_RAM);
        m_crypto->secureWipe(m_activeGroupKey_RAM);
        m_crypto->secureWipe(m_identityPrivateKey);
    }
    m_activeGroupId = -1;
    m_activeGroupName = "";
}

bool Vault::isLocked() const { return m_masterKey_RAM.empty(); }
void Vault::checkLocked() const { if (isLocked()) throw std::runtime_error("Vault is locked."); }

bool Vault::verifyMasterPassword(const std::string& password) {
    if (!isConnected()) return false;
    try {
        std::vector<unsigned char> salt = m_db->getMetadata("argon_salt");
        if (salt.empty()) return false;
        std::vector<unsigned char> testKey = m_crypto->deriveKey(password, salt);
        std::vector<unsigned char> canary_blob = m_db->getMetadata("key_canary");
        std::string decrypted = m_crypto->decryptToString(canary_blob, testKey);
        m_crypto->secureWipe(testKey);
        return (decrypted == KEY_CANARY);
    } catch (...) { return false; }
}

bool Vault::changeMasterPassword(const std::string& newPassword) {
    checkLocked();
    try {
        std::vector<unsigned char> newSalt = m_crypto->randomBytes(m_crypto->SALT_SIZE);
        std::vector<unsigned char> newMasterKey = m_crypto->deriveKey(newPassword, newSalt);
        std::vector<unsigned char> newCanary = m_crypto->encrypt(KEY_CANARY, newMasterKey);
        std::vector<unsigned char> newIdPriv = m_crypto->encrypt(m_identityPrivateKey, newMasterKey);
        
        std::vector<std::string> groups = m_db->getAllGroupNames();
        std::map<std::string, std::vector<unsigned char>> decryptedKeys;
        for (const auto& g : groups) {
            int gid = -1;
            std::vector<unsigned char> encKey = m_db->getEncryptedGroupKey(g, gid);
            if (!encKey.empty()) decryptedKeys[g] = m_crypto->decrypt(encKey, m_masterKey_RAM);
        }
        
        m_db->storeMetadata("argon_salt", newSalt);
        m_db->storeMetadata("key_canary", newCanary);
        m_db->storeMetadata("identity_priv", newIdPriv);
        
        for (auto const& [name, key] : decryptedKeys) {
            std::vector<unsigned char> reEncKey = m_crypto->encrypt(key, newMasterKey);
            int gid = m_db->getGroupId(name);
            std::string owner = m_db->getGroupOwner(gid);
            m_db->storeEncryptedGroup(name, reEncKey, owner);
        }
        
        m_crypto->secureWipe(m_masterKey_RAM);
        m_masterKey_RAM = newMasterKey;
        return true;
    } catch (...) { return false; }
}

// --- Entry Management ---

bool Vault::addEntry(const VaultEntry& entry, const std::string& password) {
    checkGroupActive();
    try {
        VaultEntry tempEntry = entry;
        std::vector<unsigned char> encryptedPassword = m_crypto->encrypt(password, m_activeGroupKey_RAM);
        m_db->storeEntry(m_activeGroupId, tempEntry, encryptedPassword);
        return true;
    } catch (...) { return false; }
}

bool Vault::updateEntry(const VaultEntry& entry, const std::string& newPassword) {
    checkGroupActive();
    try {
        std::vector<unsigned char> encryptedPassword;
        if (!newPassword.empty()) {
            encryptedPassword = m_crypto->encrypt(newPassword, m_activeGroupKey_RAM);
            m_db->updateEntry(entry, &encryptedPassword);
        } else {
            m_db->updateEntry(entry, nullptr);
        }
        return true;
    } catch (...) { return false; }
}

bool Vault::deleteEntry(int entryId) {
    checkGroupActive();
    return m_db->deleteEntry(entryId);
}

std::vector<VaultEntry> Vault::getEntries() {
    checkGroupActive();
    return m_db->getEntriesForGroup(m_activeGroupId);
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

std::vector<VaultEntry> Vault::searchEntries(const std::string& searchTerm) {
    checkLocked();
    std::vector<VaultEntry> allEntries;
    std::vector<std::string> groups = m_db->getAllGroupNames();
    
    std::string lowerTerm = searchTerm;
    std::transform(lowerTerm.begin(), lowerTerm.end(), lowerTerm.begin(), ::tolower);
    
    for (const auto& g : groups) {
        int gid = m_db->getGroupId(g);
        std::vector<VaultEntry> entries = m_db->getEntriesForGroup(gid);
        for (const auto& e : entries) {
            std::string lowerTitle = e.title;
            std::string lowerUser = e.username;
            std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
            std::transform(lowerUser.begin(), lowerUser.end(), lowerUser.begin(), ::tolower);
            
            if (lowerTitle.find(lowerTerm) != std::string::npos || lowerUser.find(lowerTerm) != std::string::npos) {
                allEntries.push_back(e);
            }
        }
    }
    return allEntries;
}

// --- Group Management ---

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
    if (m_crypto) m_crypto->secureWipe(m_activeGroupKey_RAM);
    m_activeGroupId = -1;
    m_activeGroupName = "";
}

bool Vault::isGroupActive() const { return !m_activeGroupKey_RAM.empty() && m_activeGroupId != -1; }

bool Vault::addGroup(const std::string& groupName, const std::vector<unsigned char>& key, const std::string& ownerId) {
    if (isLocked()) return false;
    
    std::vector<unsigned char> finalKey = key;
    if (finalKey.empty()) {
        finalKey.resize(32);
        // [FIX] Call the function from Crypto class
        CipherMesh::Core::Crypto::randomBytes(finalKey.data(), 32);
    }

    try {
        // [FIX] Use m_masterKey_RAM
        std::vector<unsigned char> encryptedKey = CipherMesh::Core::Crypto::encrypt(finalKey, m_masterKey_RAM);
        
        // Use provided ownerId if available, else self
        std::string finalOwner = ownerId.empty() ? m_userId : ownerId;
        
        // [FIX] Use -> for pointers
        m_db->storeEncryptedGroup(groupName, encryptedKey, finalOwner);
        
        std::string role = (finalOwner == m_userId) ? "owner" : "member";
        
        // [FIX] Use -> for pointers
        m_db->addGroupMember(m_db->getGroupId(groupName), m_userId, role, "accepted");
        
        return true;
    } catch (const DBException& e) {
        return false;
    }
}

bool Vault::deleteGroup(const std::string& groupName) {
    checkLocked();
    return m_db->deleteGroup(groupName);
}

// --- [FIX] Missing Desktop Linking Methods ---

std::vector<std::string> Vault::getGroupNames() { checkLocked(); return m_db->getAllGroupNames(); }

bool Vault::groupExists(const std::string& groupName) { 
    try { return m_db->getGroupId(groupName) != -1; } catch(...) { return false; } 
}

int Vault::getGroupId(const std::string& groupName) { return m_db->getGroupId(groupName); }

void Vault::setGroupPermissions(int groupId, bool adminsOnly) { m_db->setGroupPermissions(groupId, adminsOnly); }

GroupPermissions Vault::getGroupPermissions(int groupId) { return m_db->getGroupPermissions(groupId); }

// [FIX] Checks if current user is owner/admin
bool Vault::canUserEdit(const std::string& groupName) {
    if (isLocked()) return false;
    if (groupName == "Personal") return true;
    
    try {
        int gid = m_db->getGroupId(groupName);
        std::string myId = getUserId();
        // Simple check: Owner or Admin role
        // Ideally DB should have a checkMemberRole method
        std::vector<GroupMember> members = m_db->getGroupMembers(gid);
        for(const auto& m : members) {
            if (m.userId == myId && (m.role == "owner" || m.role == "admin")) return true;
        }
        return false;
    } catch(...) { return false; }
}

void Vault::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) { 
    m_db->updateGroupMemberRole(groupId, userId, newRole); 
}

void Vault::updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus) { 
    int gid = m_db->getGroupId(groupName); 
    m_db->updateGroupMemberStatus(gid, userId, newStatus); 
}

std::vector<unsigned char> Vault::getGroupKey(const std::string& groupName) {
    if (isGroupActive() && m_activeGroupName == groupName) return m_activeGroupKey_RAM;
    int groupId = -1;
    std::vector<unsigned char> encryptedKey = m_db->getEncryptedGroupKey(groupName, groupId);
    return m_crypto->decrypt(encryptedKey, m_masterKey_RAM);
}

// --- Member Management ---

void Vault::addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    m_db->addGroupMember(groupId, userId, role, status);
}

void Vault::removeGroupMember(const std::string& groupName, const std::string& userId) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    m_db->removeGroupMember(groupId, userId);
}

std::vector<GroupMember> Vault::getGroupMembers(const std::string& groupName) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    return m_db->getGroupMembers(groupId);
}

std::string Vault::getGroupOwner(int groupId) {
    checkLocked();
    return m_db->getGroupOwner(groupId);
}

void Vault::setUserId(const std::string& userId) {
    checkLocked();
    std::vector<unsigned char> idData(userId.begin(), userId.end());
    m_db->storeMetadata("user_id", idData);
}

std::string Vault::getUserId() {
    checkLocked();
    try { 
        std::vector<unsigned char> data = m_db->getMetadata("user_id"); 
        return std::string(data.begin(), data.end()); 
    } catch (...) { return ""; }
}

void Vault::storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson) {
    checkLocked();
    m_db->storePendingInvite(senderId, groupName, payloadJson);
}

std::vector<PendingInvite> Vault::getPendingInvites() {
    checkLocked();
    return m_db->getPendingInvites();
}

void Vault::deletePendingInvite(int inviteId) {
    checkLocked();
    m_db->deletePendingInvite(inviteId);
}

void Vault::updatePendingInviteStatus(int inviteId, const std::string& status) {
    checkLocked();
    m_db->updatePendingInviteStatus(inviteId, status);
}

// --- Data Export/Import ---

std::vector<VaultEntry> Vault::exportGroupEntries(const std::string& groupName) {
    std::string oldGroup = m_activeGroupName;
    if(setActiveGroup(groupName)) {
        std::vector<VaultEntry> entries = getEntries();
        for(auto& e : entries) {
            e.password = getDecryptedPassword(e.id);
        }
        if (!oldGroup.empty()) setActiveGroup(oldGroup);
        return entries;
    }
    return {};
}

void Vault::importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    if (groupId == -1) return;
    
    std::vector<unsigned char> encKey = m_db->getEncryptedGroupKeyById(groupId);
    std::vector<unsigned char> groupKey = m_crypto->decrypt(encKey, m_masterKey_RAM);
    
    for (const auto& entry : entries) {
        std::vector<unsigned char> encPass = m_crypto->encrypt(entry.password, groupKey);
        VaultEntry e = entry;
        m_db->storeEntry(groupId, e, encPass);
    }
    m_crypto->secureWipe(groupKey);
}

std::string Vault::getIdentityPublicKey() { 
    if(m_identityPublicKey.empty()) ensureIdentityKeys(); 
    return m_crypto->base64Encode(m_identityPublicKey); 
}

std::string Vault::decryptIncomingKey(const std::string& encryptedBase64) { return ""; }
std::vector<unsigned char> Vault::encryptForUser(const std::string&, const std::vector<unsigned char>&) { return {}; }

// --- History & Extras ---

std::vector<PasswordHistoryEntry> Vault::getPasswordHistory(int entryId) {
    return m_db->getPasswordHistory(entryId);
}

std::string Vault::decryptPasswordFromHistory(const std::string& encryptedPassword) { return ""; }

void Vault::updateEntryAccessTime(int entryId) {
    m_db->updateEntryAccessTime(entryId);
}

std::vector<VaultEntry> Vault::getRecentlyAccessedEntries(int limit) {
    return m_db->getRecentEntries(limit);
}

bool Vault::entryExists(const std::string&, const std::string&) { return false; }
std::vector<VaultEntry> Vault::findEntriesByLocation(const std::string&) { return {}; }

} // namespace Core
} // namespace CipherMesh