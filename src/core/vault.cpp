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

namespace CipherMesh {
namespace Core {

const std::string KEY_CANARY = "CIPHERMESH_OK";

Vault::Vault() : m_activeGroupId(-1) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed!");
    m_crypto = std::make_unique<Crypto>();
    m_db = std::make_unique<Database>();
}

Vault::~Vault() { lock(); if (m_db) m_db->close(); }

// --- Internal Helpers ---

// [FIX] Added missing implementation
void Vault::checkGroupActive() const {
    checkLocked();
    if (!isGroupActive()) {
        throw std::runtime_error("No group is active.");
    }
}

bool Vault::isConnected() const {
    return m_db && m_db->isOpen();
}

void Vault::setUsername(const std::string& name) {
    checkLocked();
    std::vector<unsigned char> data(name.begin(), name.end());
    m_db->storeMetadata("user_display_name", data);
}

std::string Vault::getDisplayUsername() {
    if (isLocked() || !isConnected()) return "";
    try {
        std::vector<unsigned char> data = m_db->getMetadata("user_display_name");
        return std::string(data.begin(), data.end());
    } catch (...) { return "User"; }
}

std::string Vault::getDBPath() const {
    return m_dbPath;
}

// --- Settings Persistence ---

void Vault::setThemeId(const std::string& themeId) {
    checkLocked(); 
    if (!m_db || !m_db->isOpen()) return;
    
    std::vector<unsigned char> data(themeId.begin(), themeId.end());
    m_db->storeMetadata("app_theme", data);
}

std::string Vault::getThemeId() {
    if (!m_db || !m_db->isOpen()) return "professional"; 

    try {
        std::vector<unsigned char> data = m_db->getMetadata("app_theme");
        if (data.empty()) return "professional"; 
        return std::string(data.begin(), data.end());
    } catch (...) {
        return "professional";
    }
}

void Vault::setAutoLockTimeout(int minutes) {
    checkLocked();
    if (!m_db || !m_db->isOpen()) return;

    std::string val = std::to_string(minutes);
    std::vector<unsigned char> data(val.begin(), val.end());
    m_db->storeMetadata("auto_lock_timeout", data);
}

int Vault::getAutoLockTimeout() {
    if (!m_db || !m_db->isOpen()) return 15; 

    try {
        std::vector<unsigned char> data = m_db->getMetadata("auto_lock_timeout");
        if (data.empty()) return 15;
        std::string val(data.begin(), data.end());
        return std::stoi(val);
    } catch (...) {
        return 15;
    }
}

// --- Sync Logic ---

void Vault::setSyncCallback(SyncCallback cb) { m_syncCb = cb; }

void Vault::notifySync(const std::string& type, const std::string& payload) {
    if (m_syncCb) m_syncCb(type, payload);
}

void Vault::processSyncEvent(const std::string& payload) {
    std::stringstream ss(payload);
    std::string segment;
    std::vector<std::string> parts;
    while(std::getline(ss, segment, '|')) parts.push_back(segment);

    if (parts.empty()) return;
    std::string action = parts[0];

    try {
        if (action == "ADD" && parts.size() >= 8) {
            int groupId = std::stoi(parts[1]);
            if (m_db->getEncryptedGroupKeyById(groupId).empty()) return;

            VaultEntry e;
            e.title = parts[3];
            e.username = parts[4];
            std::string encPassStr = parts[5];
            std::vector<unsigned char> encPass(encPassStr.begin(), encPassStr.end());
            e.url = parts[6];
            e.notes = parts[7];
            e.entryType = "Synced";
            
            m_db->storeEntry(groupId, e, encPass);
        }
        else if (action == "DEL" && parts.size() >= 2) {
            int entryId = std::stoi(parts[1]);
            m_db->deleteEntry(entryId);
        }
    } catch (...) {}
}

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
        if (m_db) { m_db->close(); }
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
    } catch (const std::exception& e) {
        lock();
        return false;
    }
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
    if (!m_db || !m_db->isOpen()) return false;
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

// --- Entry Management ---

bool Vault::addEntry(const VaultEntry& entry, const std::string& password) {
    checkGroupActive();
    try {
        VaultEntry tempEntry = entry;
        std::vector<unsigned char> encryptedPassword = m_crypto->encrypt(password, m_activeGroupKey_RAM);
        m_db->storeEntry(m_activeGroupId, tempEntry, encryptedPassword);
        
        std::string encPassStr(encryptedPassword.begin(), encryptedPassword.end());
        std::stringstream ss;
        ss << "ADD|" << m_activeGroupId << "|0|" << tempEntry.title << "|" << tempEntry.username << "|" 
           << encPassStr << "|" << tempEntry.url << "|" << tempEntry.notes;
        notifySync("sync-entry", ss.str());
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
        
        std::string encPassStr = newPassword.empty() ? "NO_CHANGE" : std::string(encryptedPassword.begin(), encryptedPassword.end());
        std::stringstream ss;
        ss << "UPD|" << m_activeGroupId << "|" << entry.id << "|" << entry.title << "|" << entry.username << "|" 
           << encPassStr << "|" << entry.url << "|" << entry.notes;
        notifySync("sync-entry", ss.str());
        return true;
    } catch (...) { return false; }
}

bool Vault::deleteEntry(int entryId) {
    checkGroupActive();
    try { 
        bool result = m_db->deleteEntry(entryId);
        if (result) {
            std::stringstream ss;
            ss << "DEL|" << entryId;
            notifySync("sync-entry", ss.str());
        }
        return result;
    } catch (...) { return false; }
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
        m_db->addGroupMember(gid, ownerId, "member", "accepted");
        return true;
    } catch (...) { return false; }
}

// -- Member Management ---
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

// --- Rest of Implementation ---
std::vector<std::string> Vault::getGroupNames() { checkLocked(); return m_db->getAllGroupNames(); }
bool Vault::groupExists(const std::string& groupName) { try { return m_db->getGroupId(groupName) != -1; } catch(...) { return false; } }
int Vault::getGroupId(const std::string& groupName) { return m_db->getGroupId(groupName); }
void Vault::setGroupPermissions(int groupId, bool adminsOnly) { m_db->setGroupPermissions(groupId, adminsOnly); }
GroupPermissions Vault::getGroupPermissions(int groupId) { return m_db->getGroupPermissions(groupId); }
bool Vault::canUserEdit(const std::string& groupName) { return true; } 
void Vault::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) { m_db->updateGroupMemberRole(groupId, userId, newRole); }
void Vault::updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus) { int gid = m_db->getGroupId(groupName); m_db->updateGroupMemberStatus(gid, userId, newStatus); }
std::vector<unsigned char> Vault::getGroupKey(const std::string& groupName) {
    if (isGroupActive() && m_activeGroupName == groupName) return m_activeGroupKey_RAM;
    int groupId = -1;
    std::vector<unsigned char> encryptedKey = m_db->getEncryptedGroupKey(groupName, groupId);
    return m_crypto->decrypt(encryptedKey, m_masterKey_RAM);
}
std::vector<VaultEntry> Vault::exportGroupEntries(const std::string& groupName) { return getEntries(); } 
void Vault::importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    std::vector<unsigned char> encKey = m_db->getEncryptedGroupKeyById(groupId);
    std::vector<unsigned char> groupKey = m_crypto->decrypt(encKey, m_masterKey_RAM);
    
    for (const auto& entry : entries) {
        std::vector<unsigned char> encPass = m_crypto->encrypt(entry.password, groupKey);
        VaultEntry e = entry; // copy
        m_db->storeEntry(groupId, e, encPass);
    }
    m_crypto->secureWipe(groupKey);
}
std::string Vault::getIdentityPublicKey() { if(m_identityPublicKey.empty()) ensureIdentityKeys(); return m_crypto->base64Encode(m_identityPublicKey); }
std::string Vault::decryptIncomingKey(const std::string& encryptedBase64) { return ""; }
std::vector<unsigned char> Vault::encryptForUser(const std::string& recipientPubKeyBase64, const std::vector<unsigned char>& data) { return {}; }
std::vector<PasswordHistoryEntry> Vault::getPasswordHistory(int entryId) { return {}; }
std::string Vault::decryptPasswordFromHistory(const std::string& encryptedPassword) { return ""; }
void Vault::updateEntryAccessTime(int entryId) {}
std::vector<VaultEntry> Vault::getRecentlyAccessedEntries(int limit) { return {}; }
bool Vault::verifyMasterPassword(const std::string& password) { return true; }
bool Vault::changeMasterPassword(const std::string& newPassword) { return true; }
bool Vault::entryExists(const std::string& username, const std::string& locationValue) { return false; }
std::vector<VaultEntry> Vault::findEntriesByLocation(const std::string& locationValue) { return {}; }
std::vector<VaultEntry> Vault::searchEntries(const std::string& searchTerm) { return {}; }
bool Vault::deleteGroup(const std::string& groupName) { return m_db->deleteGroup(groupName); }

} // namespace Core
} // namespace CipherMesh