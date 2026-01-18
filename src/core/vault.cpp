#include <sodium.h>
#include "vault.hpp"
#include "crypto.hpp"
#include "database.hpp" 
#include <stdexcept>
#include <iostream>
#include <sstream> 
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>

// Compatibility for desktop if needed
#ifdef __ANDROID__
    #include <sqlite3.h>
#endif

namespace CipherMesh {
namespace Core {

const std::string KEY_CANARY = "CIPHERMESH_OK";

// =========================================================
//  JSON HELPERS (Dependency-Free)
// =========================================================

static std::string escapeJson(const std::string& s) {
    std::ostringstream o;
    for (char c : s) {
        switch (c) {
            case '"': o << "\\\""; break;
            case '\\': o << "\\\\"; break;
            case '\b': o << "\\b"; break;
            case '\f': o << "\\f"; break;
            case '\n': o << "\\n"; break;
            case '\r': o << "\\r"; break;
            case '\t': o << "\\t"; break;
            default:
                if ('\x00' <= c && c <= '\x1f') o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                else o << c;
        }
    }
    return o.str();
}

static std::string getJsonString(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":\"";
    size_t start = json.find(search);
    if (start == std::string::npos) return "";
    start += search.length();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

static long long getJsonLong(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return 0;
    start += search.length();
    size_t end = json.find_first_of(",}", start);
    if (end == std::string::npos) return 0;
    try {
        return std::stoll(json.substr(start, end - start));
    } catch (...) { return 0; }
}

// =========================================================
//  VAULT LIFECYCLE
// =========================================================

Vault::Vault() : m_activeGroupId(-1) {
    if (sodium_init() < 0) throw std::runtime_error("libsodium initialization failed!");
    m_crypto = std::make_unique<Crypto>();
    m_db = std::make_unique<Database>();
}

Vault::~Vault() { lock(); if (m_db) m_db->close(); }

void Vault::connect(const std::string& path) {
    try {
        if (m_db->isOpen()) m_db->close();
        m_dbPath = path;
        m_db->open(path);
        m_db->createTables(); 
    } catch (...) {}
}

std::string Vault::getDBPath() const { return m_dbPath; }

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
        
        try {
            std::vector<unsigned char> uidData = m_db->getMetadata("user_id");
            m_userId = std::string(uidData.begin(), uidData.end());
        } catch(...) { m_userId = ""; }

        return true;
    } catch (...) { lock(); return false; }
}

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

bool Vault::hasUsers() const {
    if (!isConnected()) return false;
    try { return !m_db->getMetadata("argon_salt").empty(); } catch (...) { return false; }
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
void Vault::checkGroupActive() const { checkLocked(); if (!isGroupActive()) throw std::runtime_error("No group is active."); }
bool Vault::isConnected() const { return m_db && m_db->isOpen(); }

// =========================================================
//  SYNC LOGIC (Store-and-Forward)
// =========================================================

void Vault::setP2PSendCallback(P2PSendCallback cb) { m_p2pSender = cb; }
void Vault::setSyncCallback(SyncCallback cb) { m_syncCb = cb; }
void Vault::notifySync(const std::string& type, const std::string& payload) { if(m_syncCb) m_syncCb(type, payload); }
void Vault::processSyncEvent(const std::string&) {} // Stub

std::string Vault::serializeEntry(const VaultEntry& e, const std::string& password) {
    std::ostringstream json;
    json << "{"
         << "\"uuid\":\"" << escapeJson(e.uuid) << "\","
         << "\"title\":\"" << escapeJson(e.title) << "\","
         << "\"username\":\"" << escapeJson(e.username) << "\","
         << "\"password\":\"" << escapeJson(password) << "\","
         << "\"url\":\"" << escapeJson(e.url) << "\","
         << "\"notes\":\"" << escapeJson(e.notes) << "\","
         << "\"totpSecret\":\"" << escapeJson(e.totpSecret) << "\","
         << "\"type\":\"" << escapeJson(e.entryType) << "\","
         << "\"updatedAt\":" << e.updatedAt << ","
         << "\"locations\":[";
         
    for(size_t i=0; i<e.locations.size(); ++i) {
        json << "{\"type\":\"" << escapeJson(e.locations[i].type) << "\","
             << "\"value\":\"" << escapeJson(e.locations[i].value) << "\"}";
        if(i < e.locations.size()-1) json << ",";
    }
    json << "]}";
    return json.str();
}

void Vault::queueSyncForGroup(const std::string& groupName, const std::string& operation, const std::string& payload) {
    int gid = m_db->getGroupId(groupName);
    if(gid == -1) return;
    
    std::vector<GroupMember> members = m_db->getGroupMembers(gid);
    std::string myId = getUserId();
    
    for(const auto& m : members) {
        if (m.userId == myId) continue; 
        m_db->storeSyncJob(m.userId, groupName, operation, payload);
        processOutboxForUser(m.userId);
    }
}

void Vault::processOutboxForUser(const std::string& userId) {
    if (!m_p2pSender) return;
    try {
        std::vector<SyncJob> jobs = m_db->getSyncJobsForUser(userId);
        for(const auto& job : jobs) {
            std::ostringstream wrapper;
            wrapper << "{"
                    << "\"type\":\"sync-payload\","
                    << "\"jobId\":" << job.id << ","
                    << "\"group\":\"" << escapeJson(job.groupName) << "\","
                    << "\"op\":\"" << escapeJson(job.operation) << "\","
                    << "\"data\":" << job.payload 
                    << "}";
            m_p2pSender(userId, wrapper.str());
        }
    } catch(...) {}
}

void Vault::handleSyncAck(int jobId) {
    try { m_db->deleteSyncJob(jobId); } catch(...) {}
}

void Vault::handleIncomingSync(const std::string& senderId, const std::string& payload) {
    try {
        // 1. Check if it's an ACK
        bool isAck = (payload.find("\"type\":\"sync-ack\"") != std::string::npos);
        if (isAck) {
            long long jobId = getJsonLong(payload, "jobId");
            if (jobId > 0) handleSyncAck((int)jobId);
            return;
        }

        // 2. Parse Incoming Data
        std::string op = getJsonString(payload, "op");
        std::string group = getJsonString(payload, "group");
        long long remoteJobId = getJsonLong(payload, "jobId");
        
        int gid = m_db->getGroupId(group);
        if (gid == -1) return; // Unknown group

        std::vector<unsigned char> encGroupKey = m_db->getEncryptedGroupKeyById(gid);
        std::vector<unsigned char> groupKey = m_crypto->decrypt(encGroupKey, m_masterKey_RAM);

        size_t dataPos = payload.find("\"data\":{");
        if (dataPos == std::string::npos) return;
        size_t endPos = payload.rfind("}");
        std::string dataJson = payload.substr(dataPos + 8, endPos - (dataPos + 8)); 

        if (op == "UPSERT") {
             VaultEntry e;
             e.uuid = getJsonString(dataJson, "uuid");
             e.title = getJsonString(dataJson, "title");
             e.username = getJsonString(dataJson, "username");
             std::string pass = getJsonString(dataJson, "password");
             e.url = getJsonString(dataJson, "url");
             e.notes = getJsonString(dataJson, "notes");
             e.updatedAt = getJsonLong(dataJson, "updatedAt");
             e.entryType = getJsonString(dataJson, "type");
             e.passwordExpiry = 0; 
             
             std::vector<unsigned char> encPass = m_crypto->encrypt(pass, groupKey);
             m_db->storeEntry(gid, e, encPass);
        } 
        else if (op == "MEMBER_REMOVE") {
             std::string uid = getJsonString(dataJson, "userId");
             m_db->removeGroupMember(gid, uid);
        }

        m_crypto->secureWipe(groupKey);

        // 3. Send ACK back
        if (m_p2pSender && remoteJobId > 0) {
             std::ostringstream ack;
             ack << "{\"type\":\"sync-ack\",\"jobId\":" << remoteJobId << "}";
             m_p2pSender(senderId, ack.str());
        }

    } catch (const std::exception& e) {
        std::cerr << "Error handling incoming sync from " << senderId << ": " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error handling incoming sync from " << senderId << std::endl;
    }
}

// =========================================================
//  ENTRY MANAGEMENT
// =========================================================

bool Vault::addEntry(const VaultEntry& entry, const std::string& password) {
    checkGroupActive();
    try {
        VaultEntry temp = entry;
        if(temp.uuid.empty()) temp.uuid = m_crypto->generateUUID();
        
        std::vector<unsigned char> encryptedPassword = m_crypto->encrypt(password, m_activeGroupKey_RAM);
        m_db->storeEntry(m_activeGroupId, temp, encryptedPassword);
        
        std::string json = serializeEntry(temp, password);
        queueSyncForGroup(m_activeGroupName, "UPSERT", json);
        
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
        
        std::string passToSync = newPassword;
        if(passToSync.empty()) passToSync = getDecryptedPassword(entry.id);
        
        std::string json = serializeEntry(entry, passToSync);
        queueSyncForGroup(m_activeGroupName, "UPSERT", json);
        return true;
    } catch (...) { return false; }
}

bool Vault::deleteEntry(int entryId) {
    checkGroupActive();
    std::string uuid = "";
    auto entries = m_db->getEntriesForGroup(m_activeGroupId);
    for(const auto& e : entries) { if(e.id == entryId) { uuid = e.uuid; break; } }
    
    bool res = m_db->deleteEntry(entryId);
    
    if (res && !uuid.empty()) {
        std::ostringstream json;
        json << "{\"uuid\":\"" << uuid << "\"}";
        queueSyncForGroup(m_activeGroupName, "DELETE", json.str());
    }
    return res;
}

std::vector<VaultEntry> Vault::getEntries() {
    checkGroupActive();
    return m_db->getEntriesForGroup(m_activeGroupId);
}

std::string Vault::getDecryptedPassword(int entryId) {
    checkLocked();
    int groupId = m_db->getGroupIdForEntry(entryId);
    if (groupId == -1) throw std::runtime_error("Entry not found");
    std::vector<unsigned char> encryptedGroupKey = m_db->getEncryptedGroupKeyById(groupId);
    std::vector<unsigned char> groupKey = m_crypto->decrypt(encryptedGroupKey, m_masterKey_RAM);
    std::vector<unsigned char> encryptedPassword = m_db->getEncryptedPassword(entryId);
    std::string decryptedPassword = m_crypto->decryptToString(encryptedPassword, groupKey);
    m_crypto->secureWipe(groupKey);
    return decryptedPassword;
}

std::string Vault::getEntryFullDetails(int entryId) {
    checkLocked();
    VaultEntry e;
    auto entries = getEntries();
    for(const auto& en : entries) if(en.id == entryId) { e = en; break; }
    std::ostringstream ss;
    ss << e.title << "|" << e.username << "|" << getDecryptedPassword(entryId) << "|" << e.notes 
       << "|" << e.totpSecret << "|" << e.createdAt << "|" << e.updatedAt << "|" << e.lastAccessed;
    return ss.str();
}

std::vector<VaultEntry> Vault::searchEntries(const std::string& searchTerm) {
    checkLocked();
    std::vector<VaultEntry> allEntries;
    std::vector<std::string> groups = m_db->getAllGroupNames();
    std::string lowerTerm = searchTerm;
    std::transform(lowerTerm.begin(), lowerTerm.end(), lowerTerm.begin(), ::tolower);
    for (const auto& g : groups) {
        int gid = m_db->getGroupId(g);
        if (gid == -1) continue; 
        std::vector<VaultEntry> entries = m_db->getEntriesForGroup(gid);
        for (const auto& e : entries) {
            std::string lowerTitle = e.title;
            std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);
            if (lowerTitle.find(lowerTerm) != std::string::npos) allEntries.push_back(e);
        }
    }
    return allEntries;
}

bool Vault::entryExists(const std::string& username, const std::string& locationValue) {
    return m_db->entryExists(username, locationValue);
}

std::vector<VaultEntry> Vault::findEntriesByLocation(const std::string& locationValue) {
    return m_db->findEntriesByLocation(locationValue);
}

// =========================================================
//  GROUP & MEMBER MANAGEMENT
// =========================================================

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
        CipherMesh::Core::Crypto::randomBytes(finalKey.data(), 32);
    }
    try {
        std::vector<unsigned char> encryptedKey = CipherMesh::Core::Crypto::encrypt(finalKey, m_masterKey_RAM);
        std::string myId = getUserId();
        std::string finalOwner = (ownerId.empty() || ownerId == myId) ? myId : ownerId;
        m_db->storeEncryptedGroup(groupName, encryptedKey, finalOwner);
        std::string role = (finalOwner == myId) ? "owner" : "member";
        m_db->addGroupMember(m_db->getGroupId(groupName), myId, role, "accepted");
        return true;
    } catch (const DBException&) { return false; }
}

bool Vault::deleteGroup(const std::string& groupName) {
    checkLocked();
    return m_db->deleteGroup(groupName);
}

void Vault::removeUser(const std::string& groupName, const std::string& userId) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if(gid == -1) return;
    m_db->removeGroupMember(gid, userId);
    
    // Sync KICK
    std::ostringstream kickPayload;
    kickPayload << "{\"reason\":\"removed\"}";
    m_db->storeSyncJob(userId, groupName, "MEMBER_KICK", kickPayload.str());
    processOutboxForUser(userId);
    
    // Sync MEMBER_REMOVE
    std::ostringstream updatePayload;
    updatePayload << "{\"userId\":\"" << userId << "\"}";
    queueSyncForGroup(groupName, "MEMBER_REMOVE", updatePayload.str());
}

// [FIX] Compatibility for Desktop
void Vault::removeGroupMember(const std::string& groupName, const std::string& userId) {
    removeUser(groupName, userId);
}

bool Vault::canUserEdit(const std::string& groupName) {
    if (isLocked()) return false;
    if (groupName == "Personal") return true;
    try {
        int gid = m_db->getGroupId(groupName);
        std::string myId = getUserId();
        std::vector<GroupMember> members = m_db->getGroupMembers(gid);
        for(const auto& m : members) {
            if (m.userId == myId) {
                if(m.role == "owner" || m.role == "admin") return true;
                GroupPermissions perms = m_db->getGroupPermissions(gid);
                if (!perms.adminsOnlyWrite) return true; 
            }
        }
        return false;
    } catch(...) { return false; }
}

std::vector<std::string> Vault::getGroupNames() { checkLocked(); return m_db->getAllGroupNames(); }
bool Vault::groupExists(const std::string& groupName) { try { return m_db->getGroupId(groupName) != -1; } catch(...) { return false; } }
int Vault::getGroupId(const std::string& groupName) { return m_db->getGroupId(groupName); }
std::string Vault::getGroupOwner(int groupId) { checkLocked(); return m_db->getGroupOwner(groupId); }
std::string Vault::getGroupOwner(const std::string& groupName) { return getGroupOwner(getGroupId(groupName)); }
bool Vault::isGroupOwner(const std::string& groupName) { return getGroupOwner(groupName) == getUserId(); }
void Vault::addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status) { checkLocked(); m_db->addGroupMember(m_db->getGroupId(groupName), userId, role, status); }
std::vector<GroupMember> Vault::getGroupMembers(const std::string& groupName) { checkLocked(); return m_db->getGroupMembers(m_db->getGroupId(groupName)); }

std::vector<unsigned char> Vault::getGroupKey(const std::string& groupName) {
    checkLocked();
    if (isGroupActive() && m_activeGroupName == groupName) return m_activeGroupKey_RAM;
    int groupId = -1;
    std::vector<unsigned char> encryptedKey = m_db->getEncryptedGroupKey(groupName, groupId);
    return m_crypto->decrypt(encryptedKey, m_masterKey_RAM);
}

void Vault::setGroupPermissions(int groupId, bool adminsOnly) { m_db->setGroupPermissions(groupId, adminsOnly); }
GroupPermissions Vault::getGroupPermissions(int groupId) { return m_db->getGroupPermissions(groupId); }
void Vault::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) { m_db->updateGroupMemberRole(groupId, userId, newRole); }
void Vault::updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus) { 
    int gid = m_db->getGroupId(groupName); 
    m_db->updateGroupMemberStatus(gid, userId, newStatus); 
}

// --- Identity & Invites ---

void Vault::setUserId(const std::string& userId) {
    checkLocked();
    std::vector<unsigned char> idData(userId.begin(), userId.end());
    m_db->storeMetadata("user_id", idData);
    m_userId = userId; 
}

std::string Vault::getUserId() {
    checkLocked();
    if (!m_userId.empty()) return m_userId;
    try { 
        std::vector<unsigned char> data = m_db->getMetadata("user_id"); 
        m_userId = std::string(data.begin(), data.end()); 
        return m_userId; 
    } catch (...) { return ""; }
}

void Vault::generateAndSetUniqueId(const std::string& username) {
    checkLocked();
    if (m_identityPublicKey.empty()) ensureIdentityKeys();
    unsigned char hash[4]; 
    crypto_generichash(hash, sizeof(hash), m_identityPublicKey.data(), m_identityPublicKey.size(), NULL, 0);
    char suffix[16];
    snprintf(suffix, sizeof(suffix), "#%02X%02X%02X%02X", hash[0], hash[1], hash[2], hash[3]);
    std::string uniqueId = username + std::string(suffix);
    setUserId(uniqueId);
    setUsername(username);
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

std::string Vault::decryptIncomingKey(const std::string& encryptedBase64) {
    checkLocked();
    if (m_identityPrivateKey.empty()) ensureIdentityKeys();
    return m_crypto->decryptAsymmetric(encryptedBase64, m_identityPrivateKey);
}

std::vector<unsigned char> Vault::encryptForUser(const std::string& recipientPublicKeyBase64, const std::vector<unsigned char>& data) {
    checkLocked();
    std::vector<unsigned char> recipientPublicKey = m_crypto->base64Decode(recipientPublicKeyBase64);
    std::string encrypted = m_crypto->encryptAsymmetric(std::string(data.begin(), data.end()), recipientPublicKey);
    return std::vector<unsigned char>(encrypted.begin(), encrypted.end());
}

// --- History & Misc ---

std::vector<PasswordHistoryEntry> Vault::getPasswordHistory(int entryId) {
    return m_db->getPasswordHistory(entryId);
}

std::string Vault::decryptPasswordFromHistory(const std::string& encryptedPassword) {
    checkLocked();
    try {
        std::vector<unsigned char> encryptedData = m_crypto->base64Decode(encryptedPassword);
        std::string decrypted = m_crypto->decryptToString(encryptedData, m_masterKey_RAM);
        return decrypted;
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed: ") + e.what());
    }
}

void Vault::updateEntryAccessTime(int entryId) { m_db->updateEntryAccessTime(entryId); }
std::vector<VaultEntry> Vault::getRecentlyAccessedEntries(int limit) { return m_db->getRecentEntries(limit); }

// Invites & Stubs
void Vault::storePendingInvite(const std::string& s, const std::string& g, const std::string& p) { checkLocked(); m_db->storePendingInvite(s, g, p); }
std::vector<PendingInvite> Vault::getPendingInvites() { checkLocked(); return m_db->getPendingInvites(); }
void Vault::deletePendingInvite(int id) { checkLocked(); m_db->deletePendingInvite(id); }
void Vault::updatePendingInviteStatus(int id, const std::string& s) { checkLocked(); m_db->updatePendingInviteStatus(id, s); }
void Vault::respondToInvite(const std::string&, const std::string&, bool) {}
void Vault::sendP2PInvite(const std::string&, const std::string&) {}
std::vector<VaultEntry> Vault::exportGroupEntries(const std::string& groupName) {
    checkLocked();
    
    // Get group ID
    int groupId = m_db->getGroupId(groupName);
    if (groupId == -1) {
        return {}; // Group not found
    }
    
    // Get encrypted group key
    std::vector<unsigned char> encryptedGroupKey = m_db->getEncryptedGroupKeyById(groupId);
    if (encryptedGroupKey.empty()) {
        return {}; // No group key
    }
    
    // Decrypt group key
    std::vector<unsigned char> groupKey = m_crypto->decrypt(encryptedGroupKey, m_masterKey_RAM);
    
    // Get all entries for the group
    std::vector<VaultEntry> entries = m_db->getEntriesForGroup(groupId);
    
    // Decrypt passwords for each entry
    for (auto& entry : entries) {
        try {
            std::vector<unsigned char> encryptedPassword = m_db->getEncryptedPassword(entry.id);
            if (!encryptedPassword.empty()) {
                entry.password = m_crypto->decryptToString(encryptedPassword, groupKey);
            }
        } catch (const std::exception& e) {
            // Log decryption failure for debugging
            std::cerr << "Warning: Failed to decrypt password for entry " << entry.id 
                     << " in group " << groupName << ": " << e.what() << std::endl;
            // Leave password empty but continue with other entries
            entry.password = "";
        }
    }
    
    // Securely wipe the group key from memory
    m_crypto->secureWipe(groupKey);
    
    return entries;
}
std::string Vault::getIdentityPublicKey() { if(m_identityPublicKey.empty()) ensureIdentityKeys(); return m_crypto->base64Encode(m_identityPublicKey); }
void Vault::setUsername(const std::string& name) { checkLocked(); std::vector<unsigned char> d(name.begin(), name.end()); m_db->storeMetadata("user_display_name", d); }
std::string Vault::getDisplayUsername() { try { std::vector<unsigned char> d = m_db->getMetadata("user_display_name"); return std::string(d.begin(), d.end()); } catch(...) { return "User"; } }
void Vault::setThemeId(const std::string& t) { checkLocked(); std::vector<unsigned char> d(t.begin(), t.end()); m_db->storeMetadata("app_theme", d); }
std::string Vault::getThemeId() { try { std::vector<unsigned char> d = m_db->getMetadata("app_theme"); return d.empty()?"professional":std::string(d.begin(), d.end()); } catch(...) { return "professional"; } }
void Vault::setAutoLockTimeout(int m) { checkLocked(); std::string s=std::to_string(m); std::vector<unsigned char> d(s.begin(), s.end()); m_db->storeMetadata("auto_lock_timeout", d); }
int Vault::getAutoLockTimeout() { try { std::vector<unsigned char> d = m_db->getMetadata("auto_lock_timeout"); return d.empty()?15:std::stoi(std::string(d.begin(), d.end())); } catch(...) { return 15; } }

void Vault::importGroupMembers(const std::string& groupName, const std::string& membersJson) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    if (groupId == -1) return;
    
    // Simple parser for legacy import format (User1|role|status,User2|role|status)
    // This is a stub for desktop restore functionality
    // Real implementation would parse JSON or CSV string
    (void)membersJson; 
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

} // namespace Core
} // namespace CipherMesh