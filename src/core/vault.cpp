#include <sodium.h>
#include "vault.hpp"
#include "crypto.hpp"
#include "database.hpp"
#include "vault_entry.hpp" 
#include <stdexcept>
#include <iostream>
#include <sstream> 
#include <vector>
#include <string>
#include <algorithm>
#include <iomanip>
#include <ctime>
#include <chrono>

#ifdef __ANDROID__
    #include <android/log.h>
    #define LOG_DEBUG(msg) __android_log_print(ANDROID_LOG_DEBUG, "CipherMesh_Core", "%s", std::string(msg).c_str())
    #define LOG_INFO(msg) __android_log_print(ANDROID_LOG_INFO, "CipherMesh_Core", "%s", std::string(msg).c_str())
    #define LOGW(msg) __android_log_print(ANDROID_LOG_WARN, "CipherMesh_Core", "%s", std::string(msg).c_str())
#else
    #define LOG_DEBUG(msg) std::cout << "[CORE_DEBUG] " << msg << std::endl
    #define LOG_INFO(msg) std::cout << "[CORE_INFO] " << msg << std::endl
    #define LOGW(msg) std::cerr << "[CORE_WARN] " << msg << std::endl
#endif

#ifdef __ANDROID__
    #include <sqlite3.h>
#endif

namespace CipherMesh {
namespace Core {

const std::string KEY_CANARY = "CIPHERMESH_OK";

// =========================================================
//  STATIC HELPERS
// =========================================================

static std::string base64EncodeInternal(const std::vector<unsigned char>& in) {
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

static std::vector<unsigned char> base64DecodeInternal(const std::string& in) {
    std::vector<unsigned char> out;
    std::vector<int> T(256, -1);
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i=0; i<64; i++) T[b64[i]] = i; 
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

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
    std::string pattern = "\"" + key + "\"";
    size_t keyPos = json.find(pattern);
    if (keyPos == std::string::npos) return "";
    
    size_t colonPos = json.find(":", keyPos + pattern.length());
    if (colonPos == std::string::npos) return "";
    
    size_t valStart = colonPos + 1;
    while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '\t' || json[valStart] == '\n' || json[valStart] == '\r')) {
        valStart++;
    }
    
    if (valStart >= json.length()) return "";

    if (json[valStart] == '"') {
        size_t start = valStart + 1;
        size_t end = start;
        while (end < json.length()) {
            if (json[end] == '"' && json[end-1] != '\\') break;
            end++;
        }
        if (end >= json.length()) return "";
        return json.substr(start, end - start);
    } else {
        size_t end = json.find_first_of(",}", valStart);
        if (end == std::string::npos) end = json.length();
        return json.substr(valStart, end - valStart);
    }
}

static long long getJsonLong(const std::string& json, const std::string& key) {
    std::string val = getJsonString(json, key);
    if (val.empty()) return 0;
    try { return std::stoll(val); } catch(...) { return 0; }
}

// =========================================================
//  VAULT IMPLEMENTATION
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

bool Vault::createNewVault(const std::string& path, const std::string& masterPassword, const std::string& username) {
    try {
        if (m_db) m_db->close();
        lock();
        m_dbPath = path;
        m_db->open(path);
        m_db->createTables();

        std::vector<unsigned char> salt = m_crypto->randomBytes(m_crypto->SALT_SIZE);
        m_masterKey_RAM = m_crypto->deriveKey(masterPassword, salt);
        m_db->storeMetadata("argon_salt", salt);

        std::vector<unsigned char> canary = m_crypto->encrypt(KEY_CANARY, m_masterKey_RAM);
        m_db->storeMetadata("key_canary", canary);

        ensureIdentityKeys();
        generateAndSetUniqueId(username);
        addGroup("Personal");
        setThemeId("professional");
        setAutoLockTimeout(15);
        return true;
    } catch (...) {
        lock();
        return false;
    }
}

static std::string sanitizeUsername(const std::string& input) {
    std::string out;
    for (char c : input) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out += std::tolower(static_cast<unsigned char>(c));
        }
    }
    if (out.empty()) out = "user";
    if (out.length() > 12) out.resize(12);
    return out;
}

void Vault::addEncryptedEntry(const VaultEntry& entry, const std::string& base64Ciphertext) {
    checkGroupActive();
    try {
        VaultEntry temp = entry;
        if(temp.uuid.empty()) temp.uuid = m_crypto->generateUUID();
        
        // UUID-based deduplication: Skip if entry with same UUID exists
        auto existing = m_db->getEntriesForGroup(m_activeGroupId);
        for (const auto& e : existing) {
            if (e.uuid == temp.uuid) {
                LOG_DEBUG("Entry with UUID " + temp.uuid + " already exists, skipping");
                return; // Entry already exists, skip to prevent duplicates
            }
        }
        
        std::vector<unsigned char> encryptedBlob;
        if(!base64Ciphertext.empty()) {
            encryptedBlob = base64DecodeInternal(base64Ciphertext);
        }
        
        LOG_DEBUG("Adding encrypted entry: " + temp.title + " uuid: " + temp.uuid + " password blob size: " + std::to_string(encryptedBlob.size()));
        
        m_db->storeEntry(m_activeGroupId, temp, encryptedBlob);
    } catch (const std::exception& ex) {
        // [FIX] Include entry title and uuid in error message for easier debugging
        LOG_DEBUG("Error adding encrypted entry (title: " + entry.title + ", uuid: " + entry.uuid + "): " + std::string(ex.what()));
    } catch (...) {
        LOG_DEBUG("Unknown error adding encrypted entry (title: " + entry.title + ", uuid: " + entry.uuid + ")");
    }
}

bool Vault::loadVault(const std::string& path, const std::string& masterPassword) {
    try {
        if (!m_db->isOpen() || m_dbPath != path) connect(path);

        std::vector<unsigned char> salt = m_db->getMetadata("argon_salt");
        if (salt.empty()) return false;

        m_masterKey_RAM = m_crypto->deriveKey(masterPassword, salt);

        std::vector<unsigned char> canary = m_db->getMetadata("key_canary");
        if (m_crypto->decryptToString(canary, m_masterKey_RAM) != KEY_CANARY) {
            lock();
            return false;
        }

        std::vector<unsigned char> uid = m_db->getMetadata("user_id");
        if (uid.empty()) throw std::runtime_error("Vault has no user_id");
        m_userId.assign(uid.begin(), uid.end());

        loadIdentityKeys();
        return true;
    } catch (...) {
        lock();
        return false;
    }
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

void Vault::setP2PSendCallback(P2PSendCallback cb) { m_p2pSender = cb; }
void Vault::setSyncCallback(SyncCallback cb) { m_syncCb = cb; }
void Vault::notifySync(const std::string& type, const std::string& payload) { if(m_syncCb) m_syncCb(type, payload); }
void Vault::processSyncEvent(const std::string&) {}

std::string Vault::serializeEntry(const VaultEntry& e, const std::string& password, const std::vector<unsigned char>& key) {
    std::ostringstream json;
    std::vector<unsigned char> encBytes = m_crypto->encrypt(password, key);
    std::string encBase64 = base64EncodeInternal(encBytes);

    json << "{"
         << "\"uuid\":\"" << escapeJson(e.uuid) << "\","
         << "\"title\":\"" << escapeJson(e.title) << "\","
         << "\"username\":\"" << escapeJson(e.username) << "\","
         << "\"password\":\"" << encBase64 << "\"," 
         << "\"url\":\"" << escapeJson(e.url) << "\","
         << "\"notes\":\"" << escapeJson(e.notes) << "\","
         << "\"type\":\"" << escapeJson(e.entryType) << "\","
         << "\"totpSecret\":\"" << escapeJson(e.totpSecret) << "\","
         << "\"updatedAt\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count()
         << ", \"locations\":[";

    for(size_t i = 0; i < e.locations.size(); ++i) {
        json << "{\"locType\":\"" << escapeJson(e.locations[i].type) << "\", \"value\":\"" << escapeJson(e.locations[i].value) << "\"}";
        if(i < e.locations.size() - 1) json << ",";
    }
    json << "]}";
    return json.str();
}

void Vault::queueSyncForGroup(const std::string& groupName, const std::string& operation, const std::string& payload) {
    // [FIX] Removed special case for "Personal" to allow syncing if shared. 
    // Logic handles empty member list (private group) naturally.
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return;
    std::vector<GroupMember> members = m_db->getGroupMembers(gid);
    std::string myId = getUserId();
    LOG_DEBUG("queueSyncForGroup: Queueing '" + operation + "' for group '" + groupName + "' with " + std::to_string(members.size()) + " members");
    
    for (const auto& m : members) {
        if (m.userId == myId) continue;
        
        // [FIX] Log member status to diagnose sync issues
        LOG_DEBUG("queueSyncForGroup: Checking member " + m.userId + " with status " + m.status);
        
        // Ensure we only sync to accepted members (unless it's an invite/kick)
        if (m.status != "accepted" && operation != "MEMBER_KICK" && operation != "GROUP_SPLIT") {
             LOG_DEBUG("queueSyncForGroup: Skipping sync for " + m.userId + " (status not accepted)");
             continue;
        }

        m_db->storeSyncJob(m.userId, groupName, operation, payload);
        processOutboxForUser(m.userId);
    }
}

void Vault::queueSyncForMember(const std::string& groupName, const std::string& memberId, const std::string& operation, const std::string& payload) {
    // [FIX] Removed special case for "Personal"
    m_db->storeSyncJob(memberId, groupName, operation, payload);
    processOutboxForUser(memberId);
}

void Vault::processOutboxForUser(const std::string& userId) {
    if (!m_p2pSender) return;
    std::vector<SyncJob> jobs;
    try { jobs = m_db->getSyncJobsForUser(userId); } catch (...) { return; }
    for (const auto& job : jobs) {
        std::ostringstream msg;
        msg << "{"
            << "\"type\":\"sync-payload\","
            << "\"jobId\":" << job.id << ","
            << "\"group\":\"" << escapeJson(job.groupName) << "\","
            << "\"op\":\"" << escapeJson(job.operation) << "\","
            << "\"data\":" << job.payload
            << "}";
        m_p2pSender(userId, msg.str());
    }
}

void Vault::onPeerOnline(const std::string& userId) {
    processOutboxForUser(userId);
    notifySync("peer-online", userId);
}

void Vault::processAllPendingSync() {
    if (!m_db) return;
    
    LOG_DEBUG("processAllPendingSync: Processing pending sync jobs for all groups");
    
    try {
        auto groups = m_db->getAllGroupNames();
        int totalProcessed = 0;
        
        for (const auto& groupName : groups) {
            int gid = m_db->getGroupId(groupName);
            if (gid == -1) continue;
            
            auto members = m_db->getGroupMembers(gid);
            for (const auto& member : members) {
                if (member.userId != getUserId()) {
                    auto jobs = m_db->getSyncJobsForUser(member.userId);
                    if (!jobs.empty()) {
                        LOG_DEBUG("processAllPendingSync: Processing " + std::to_string(jobs.size()) + " jobs for " + member.userId);
                        processOutboxForUser(member.userId);
                        totalProcessed += jobs.size();
                    }
                }
            }
        }
        
        LOG_INFO("processAllPendingSync: Processed " + std::to_string(totalProcessed) + " pending sync jobs");
    } catch (const std::exception& e) {
        LOGW("processAllPendingSync: Error - " + std::string(e.what()));
    }
}


void Vault::handleSyncAck(int jobId) {
    try { m_db->deleteSyncJob(jobId); } catch(...) {}
}

int Vault::findGroupIdForSync(const std::string& remoteGroupName, const std::string& senderId) {
    LOG_DEBUG("findGroupIdForSync: looking for group matching '" + remoteGroupName + "' from sender '" + senderId + "'");
    
    // Normalize senderId for comparison
    std::string senderLower = senderId;
    std::transform(senderLower.begin(), senderLower.end(), senderLower.begin(), ::tolower);

    // 1. Try exact name match
    int gid = m_db->getGroupId(remoteGroupName);
    if (gid != -1) {
        auto members = m_db->getGroupMembers(gid);
        for (const auto& m : members) { 
            std::string mId = m.userId;
            std::transform(mId.begin(), mId.end(), mId.begin(), ::tolower);
            if (mId == senderLower) {
                LOG_DEBUG("findGroupIdForSync: found exact match group id " + std::to_string(gid));
                return gid; 
            }
        }
    }
    
    // 2. Try explicit derived name match "Name (from Sender)"
    // This is the most likely candidate for split groups
    std::string derivedName = remoteGroupName + " (from " + senderId + ")";
    int derivedId = m_db->getGroupId(derivedName);
    if (derivedId != -1) {
         LOG_DEBUG("findGroupIdForSync: found explicit derived match '" + derivedName + "' id " + std::to_string(derivedId));
         return derivedId;
    }
    
    // 2b. Try derived name with LOWERCASE sender (Case Insensitivity)
    std::string derivedNameLower = remoteGroupName + " (from " + senderLower + ")";
    if (m_db->getGroupId(derivedNameLower) != -1) {
         LOG_DEBUG("findGroupIdForSync: found derived match (lower) id " + std::to_string(m_db->getGroupId(derivedNameLower)));
         return m_db->getGroupId(derivedNameLower);
    }

    // 3. Try generic prefix match (fallback for renamed split groups)
    std::vector<std::string> allGroups = m_db->getAllGroupNames();
    for (const auto& localName : allGroups) {
        // [FIX] Ensure we match the "base" name correctly
        if (localName.find(remoteGroupName) == 0) {
            int candidateId = m_db->getGroupId(localName);
            auto members = m_db->getGroupMembers(candidateId);
            for (const auto& m : members) { 
                std::string mId = m.userId;
                std::transform(mId.begin(), mId.end(), mId.begin(), ::tolower);
                if (mId == senderLower) {
                    LOG_DEBUG("findGroupIdForSync: found prefixed match '" + localName + "' id " + std::to_string(candidateId));
                    return candidateId; 
                }
            }
        }
    }
    
    // [FIX] If we still can't find it, and we just created a group... 
    // Maybe we should check if we possess a group where the sender is Owner?
    
    int fallbackId = m_db->getGroupId(remoteGroupName);
    LOG_DEBUG("findGroupIdForSync: fallback to exact name match, id " + std::to_string(fallbackId));
    return fallbackId;
}

void Vault::handleIncomingSync(const std::string& senderId, const std::string& payload) {
    long long jobId = getJsonLong(payload, "jobId");
    std::string op, group, dataJson;
    int gid = -1;
    std::vector<unsigned char> groupKey;
    bool found = false;

    try {
        // Check message type first for initial group transfer
        std::string msgType = getJsonString(payload, "type");
        
        LOG_DEBUG("handleIncomingSync: type='" + msgType + "' from sender='" + senderId + "'");
        
        // Handle initial group transfer types (from P2P sharing)
            if (msgType == "group-data") {
                std::string groupName = getJsonString(payload, "group");
                std::string keyBase64 = getJsonString(payload, "key");
                
                LOG_DEBUG("handleIncomingSync: Processing group-data for '" + groupName + "'");
                
                if (keyBase64.empty()) {
                    LOGW("handleIncomingSync: Received group-data with EMPTY KEY for '" + groupName + "'. Aborting creation.");
                    return;
                }
                
                std::string localGroup = groupName;
                
                // [FIX] Strict Sender Check: Do NOT use findGroupIdForSync here as it might callback to local groups.
                // explicitly search for a group that this sender is ALREADY a part of.
                int existingId = -1;
                std::vector<std::string> allGroups = m_db->getAllGroupNames();
                
                // Normalize senderId for comparison
                std::string senderLower = senderId;
                std::transform(senderLower.begin(), senderLower.end(), senderLower.begin(), ::tolower);
                
                for (const auto& gName : allGroups) {
                    // Check if name matches or is a "derivative" (e.g. "Personal (from X)")
                    // For now, simpliest is to check if we have a group with this exact internal name 
                    // OR if we decided to rename it previously.
                    // But actually, we just need to find if we have ANY group related to this sender/name combo.
                    
                    int gid = m_db->getGroupId(gName);
                    auto members = m_db->getGroupMembers(gid);
                    bool senderInGroup = false;
                    for (const auto& m : members) { 
                        std::string mId = m.userId;
                        std::transform(mId.begin(), mId.end(), mId.begin(), ::tolower);
                        if (mId == senderLower) { senderInGroup = true; break; } 
                    }
                    
                    if (senderInGroup) {
                         // Check name similarity
                         if (gName == groupName || gName.find(groupName + " (from ") == 0) {
                             existingId = gid;
                             localGroup = gName; // Use the actual local name
                             break;
                         }
                    }
                }

                if (existingId != -1) {
                    LOG_DEBUG("handleIncomingSync: Found existing group '" + localGroup + "' (id=" + std::to_string(existingId) + ") for sender " + senderId);
                    // Update key if needed, or just return. 
                    // For safety, let's treat this as an update. But we need to be careful not to overwrite valid keys with bad ones.
                    return; 
                }
                
                // If we get here, it's a NEW group (or re-share where we lost context).
                // Check for name collision with ANY existing group (regardless of sender)
                if (groupExists(localGroup)) {
                    LOG_DEBUG("handleIncomingSync: Group '" + localGroup + "' name collision. creating unique name.");
                    // Check if the EXISTING group actually belongs to the sender (redundant check but safe)
                    // If it belongs to SELF (Personal), we MUST rename.
                    
                    std::string uniqueName = localGroup + " (from " + senderId + ")";
                    int counter = 1;
                    std::string baseName = uniqueName;
                    while (groupExists(uniqueName)) {
                        uniqueName = baseName + " " + std::to_string(counter++);
                    }
                    localGroup = uniqueName;
                    LOG_DEBUG("handleIncomingSync: Resolved unique name: '" + localGroup + "'");
                }
    
                std::vector<unsigned char> key = base64DecodeInternal(keyBase64);
                LOG_DEBUG("handleIncomingSync: Creating group '" + localGroup + "' with key size " + std::to_string(key.size()));
                
                if (addGroup(localGroup, key, senderId)) {
                    // [FIX] Ensure we add the sender as a member immediately so future lookups work
                    // addGroup adds the owner (senderId) as owner, but let's be explicit about our own internal state
                    int gid = getGroupId(localGroup);
                    if (gid != -1) {
                        // Add SELF as member if not already (addGroup adds owner, but we are the one accepting)
                        std::string myId = getUserId();
                        if (myId != senderId) {
                            addGroupMember(localGroup, myId, "member", "accepted");
                        }
                    }
                    
                    LOG_DEBUG("handleIncomingSync: Group created successfully.");
                    notifySync("groups-updated", localGroup);
                } else {
                    LOGW("handleIncomingSync: Failed to create group '" + localGroup + "' - DB error?");
                }
                return;
            }
        else if (msgType == "entry-data") {
            std::string remoteGroupName = getJsonString(payload, "group");
            
            LOG_DEBUG("Received entry-data for group '" + remoteGroupName + "' from sender '" + senderId + "'");
            
            // [FIX] CRITICAL: Do NOT use findGroupIdForSync for entry-data from initial transfer!
            // Initial transfer entries should ONLY go to groups created by THIS sender.
            // Use strict sender-based lookup to prevent cross-contamination.
            
            int gid = -1;
            std::string groupName;
            
            // 1. Try explicit derived name first (most common for received groups)
            std::string derivedName = remoteGroupName + " (from " + senderId + ")";
            gid = m_db->getGroupId(derivedName);
            if (gid != -1) {
                groupName = derivedName;
                LOG_DEBUG("Found derived group name: " + groupName);
            } else {
                // 2. Search for ANY group where sender is owner
                std::vector<std::string> allGroups = m_db->getAllGroupNames();
                for (const auto& gName : allGroups) {
                    int candidateId = m_db->getGroupId(gName);
                    std::string owner = m_db->getGroupOwner(candidateId);
                    
                    // [FIX] Only accept groups where sender is the OWNER
                    if (owner == senderId) {
                        // Also check if base name matches (or matches derived pattern)
                        if (gName == remoteGroupName || gName.find(remoteGroupName + " (from ") == 0) {
                            gid = candidateId;
                            groupName = gName;
                            LOG_DEBUG("Found group by owner: " + groupName);
                            break;
                        }
                    }
                }
            }
            
            if (gid == -1 || groupName.empty()) {
                LOG_DEBUG("No group found for entry-data from " + senderId + " - group '" + remoteGroupName + "' doesn't exist locally");
                return;
            }
            
            LOG_DEBUG("Resolved to local group '" + groupName + "' with id " + std::to_string(gid));
            
            VaultEntry e;
            e.uuid = getJsonString(payload, "uuid");
            e.title = getJsonString(payload, "title");
            e.username = getJsonString(payload, "username");
            e.notes = getJsonString(payload, "notes");
            e.url = getJsonString(payload, "url");
            e.totpSecret = getJsonString(payload, "totpSecret");
            
            std::string encPass = getJsonString(payload, "password");
            
            LOG_DEBUG("Entry details - uuid:" + e.uuid + " title:" + e.title + " password length:" + std::to_string(encPass.length()));
            
            // Parse locations array from JSON
            LOG_DEBUG("Parsing locations from payload...");
            size_t locPos = payload.find("\"locations\"");
            if (locPos != std::string::npos) {
                size_t arrStart = payload.find('[', locPos);
                if (arrStart != std::string::npos) {
                    size_t objStart = arrStart;
                    while ((objStart = payload.find('{', objStart)) != std::string::npos) {
                        size_t objEnd = payload.find('}', objStart);
                        if (objEnd == std::string::npos || objEnd > payload.find(']', arrStart)) break;
                        std::string locObj = payload.substr(objStart, objEnd - objStart + 1);
                        std::string locType = getJsonString(locObj, "locType");
                        std::string locValue = getJsonString(locObj, "value");
                        if (!locType.empty() && !locValue.empty()) {
                            e.locations.push_back(Location(-1, locType, locValue));
                        }
                        objStart = objEnd + 1;
                    }
                }
            }
            
            LOG_DEBUG("Parsed " + std::to_string(e.locations.size()) + " locations for entry " + e.title);
            if(!e.locations.empty()) {
                LOG_DEBUG("First location: type='" + e.locations[0].type + "' value='" + e.locations[0].value + "'");
            }
            
            // Fallback: add url as location if no locations parsed
            if (e.locations.empty() && !e.url.empty()) {
                e.locations.push_back(Location(-1, "URL", e.url)); // Use "URL" not "url"
            }
            
            // [FIX] Save and restore the current active group context to avoid side effects
            int savedGroupId = m_activeGroupId;
            std::string savedGroupName = m_activeGroupName;
            std::vector<unsigned char> savedGroupKey = m_activeGroupKey_RAM;
            
            if (setActiveGroup(groupName)) {
                addEncryptedEntry(e, encPass);
                notifySync("entry-updated", groupName);
            }
            
            // [FIX] Restore previous active group if one was active
            if (savedGroupId != -1 && !savedGroupName.empty()) {
                m_activeGroupId = savedGroupId;
                m_activeGroupName = savedGroupName;
                m_activeGroupKey_RAM = savedGroupKey;
            }
            return;
        }
        else if (msgType == "member-list") {
            // Handle member list import if needed
            notifySync("members-updated", getJsonString(payload, "group"));
            return;
        }
        
        // 1. Always handle ACKs immediately
        if (msgType == "sync-ack") {
            if (jobId > 0) handleSyncAck((int)jobId);
            return;
        }

        op = getJsonString(payload, "op");
        group = getJsonString(payload, "group");
        
        LOG_DEBUG("handleIncomingSync: Extracted operation='" + op + "' group='" + group + "'");
        
        if (op.empty()) {
            LOGW("handleIncomingSync: Operation is EMPTY for sync-payload from " + senderId);
            // Try to infer operation type from message structure as fallback
            if (payload.find("\"data\"") != std::string::npos) {
                LOGW("handleIncomingSync: Found data object, but operation is missing. Check sender serialization.");
            }
        }

        // 2. Robust JSON Object Extraction
        size_t dpos = payload.find("\"data\"");
        if (dpos != std::string::npos) {
            size_t b = payload.find("{", dpos);
            if (b != std::string::npos) {
                int depth = 1;
                size_t i = b + 1;
                for (; i < payload.size() && depth > 0; ++i) {
                    if (payload[i] == '{') depth++;
                    else if (payload[i] == '}') depth--;
                }
                dataJson = payload.substr(b + 1, i - b - 2);
            }
        }

        if (op == "INVITE") {
            m_db->storePendingInvite(senderId, group, dataJson);
            notifySync("invites-updated", "");
            goto SEND_ACK; // Always ACK invites
        }
        
        if (op == "INVITE_ACCEPT") {
            // [FIX] Desktop sends INVITE_ACCEPT after sharing group with Mobile
            // This is just a notification that the user accepted - we don't need to do anything
            // The group and entries have already been received via group-data/entry-data messages
            LOG_DEBUG("handleIncomingSync: Received INVITE_ACCEPT from " + senderId + " for group " + group);
            goto SEND_ACK; // ACK and skip further processing
        }

        gid = findGroupIdForSync(group, senderId);
        
        if (gid == -1) {
            LOGW("handleIncomingSync: Group '" + group + "' from sender " + senderId + " NOT FOUND locally. Acknowledging but skipping processing.");
            goto SEND_ACK; 
        }

        groupKey = m_crypto->decrypt(m_db->getEncryptedGroupKeyById(gid), m_masterKey_RAM);

        if (op == "UPSERT") {
            try {
                LOG_DEBUG("handleIncomingSync: Processing UPSERT for group '" + group + "'");

                VaultEntry e;
            e.uuid = getJsonString(dataJson, "uuid");
            e.title = getJsonString(dataJson, "title");
            e.username = getJsonString(dataJson, "username");
            e.notes = getJsonString(dataJson, "notes");
            e.url = getJsonString(dataJson, "url");
            e.totpSecret = getJsonString(dataJson, "totpSecret");
            e.updatedAt = getJsonLong(dataJson, "updatedAt");
            e.entryType = getJsonString(dataJson, "type");
            
            // Locations Parsing (Defensive)
            try {
                e.locations.clear();
                if (dataJson.find("\"locations\"") != std::string::npos) {
                     size_t locPos = dataJson.find("\"locations\"");
                     size_t arrStart = dataJson.find('[', locPos);
                     if (arrStart != std::string::npos) {
                        size_t objStart = arrStart;
                        while ((objStart = dataJson.find('{', objStart)) != std::string::npos) {
                            size_t objEnd = dataJson.find('}', objStart);
                             if (objEnd == std::string::npos || objEnd > dataJson.find(']', arrStart)) break;
                            std::string locObj = dataJson.substr(objStart, objEnd - objStart + 1);
                            
                            std::string locType = getJsonString(locObj, "locType");
                            std::string locValue = getJsonString(locObj, "value");
                            if (locType == "url") locType = "URL"; // Normalize
                            
                            if (!locType.empty() && !locValue.empty()) {
                                e.locations.push_back(Location(-1, locType, locValue));
                            }
                            objStart = objEnd + 1;
                        }
                     }
                }
            } catch (...) {
                LOGW("UPSERT: Error parsing locations, proceeding without them.");
            }
            
            // Fallback location
            if (e.locations.empty() && !e.url.empty()) {
               e.locations.push_back(Location(-1, "URL", e.url));
            }

            std::vector<unsigned char> encPass;
            try {
                encPass = base64DecodeInternal(getJsonString(dataJson, "password"));
            } catch (...) {}
            
            // Check if entry exists by UUID in this group
            bool found = false;
            auto locals = m_db->getEntriesForGroup(gid);
            for (auto& l : locals) {
                if (l.uuid == e.uuid) {
                    // IDEMPOTENCY: Only skip if Remote has a VALID timestamp AND Local is NEWER.
                    // If Remote timestamp is 0 (missing/parse fail), we FORCE UPDATE to be safe.
                    if (e.updatedAt > 0 && l.updatedAt > e.updatedAt) {
                        LOG_DEBUG("UPSERT: Skipping update - local is newer for uuid " + e.uuid);
                        goto SEND_ACK; 
                    }
                    found = true;
                    e.id = l.id; // Preserve ID
                    break;
                }
            }
            
            LOG_DEBUG("UPSERT: " + std::string(found ? "Updating " : "Creating ") + "entry '" + e.title + "' in group " + group);
            
            if (found) {
                 m_db->updateEntry(e, &encPass);
            } else {
                 m_db->storeEntry(gid, e, encPass);
            }

                notifySync("entry-updated", group);

            } catch (const std::exception& ex) {
                LOGW("handleIncomingSync: Error processing UPSERT JSON: " + std::string(ex.what()));
            }
        }
        else if (op == "DELETE") {
            std::string uuid = getJsonString(dataJson, "uuid");
            auto locals = m_db->getEntriesForGroup(gid);
            for (auto& l : locals) {
                if (l.uuid == uuid) {
                    m_db->deleteEntry(l.id);
                    notifySync("entry-deleted", group);
                    break;
                }
            }
        }
        else if (op == "MEMBER_KICK") {
            // [FIX] Handle being kicked from a group
            LOG_DEBUG("handleIncomingSync: Received MEMBER_KICK for group '" + group + "'");
            std::string reason = getJsonString(dataJson, "reason");
            
            // Remove the group locally
            int gid = m_db->getGroupId(group);
            if (gid != -1) {
                m_db->deleteGroup(group);
                notifySync("group-deleted", group);
                notifySync("kicked-from-group", group + "|" + reason);
            }
        }
        else if (op == "MEMBER_REMOVE") {
            // [FIX] Handle notification that a member was removed  
            LOG_DEBUG("handleIncomingSync: Received MEMBER_REMOVE for group '" + group + "'");
            std::string userId = getJsonString(dataJson, "userId");
            
            // Update local member list
            int gid = m_db->getGroupId(group);
            if (gid != -1) {
                m_db->removeGroupMember(gid, userId);
                notifySync("members-updated", group);
            }
        }
        else if (op == "GROUP_SPLIT") {
            // [FIX] Handle group deletion by owner
            LOG_DEBUG("handleIncomingSync: Received GROUP_SPLIT for group '" + group + "'");
            std::string reason = getJsonString(dataJson, "reason");
            
            // Remove the group locally
            int gid = m_db->getGroupId(group);
            if (gid != -1) {
                m_db->deleteGroup(group);
                notifySync("group-deleted", group);
                notifySync("group-disbanded", group + "|" + reason);
            }
        }

    SEND_ACK:
        if (jobId > 0 && m_p2pSender) {
            std::ostringstream ack;
            ack << "{\"type\":\"sync-ack\",\"jobId\":" << jobId << "}";
            m_p2pSender(senderId, ack.str());
        }
    } catch (...) {
        // Guarantee ACK on error to clear sender outbox
        if (jobId > 0 && m_p2pSender) {
            std::ostringstream ack;
            ack << "{\"type\":\"sync-ack\",\"jobId\":" << jobId << "}";
            m_p2pSender(senderId, ack.str());
        }
    }
}

bool Vault::addEntry(const VaultEntry& entry, const std::string& password) {
    if (isLocked()) return false;
    if (!isGroupActive()) return false;

    VaultEntry e = entry;
    if (e.uuid.empty()) e.uuid = m_crypto->generateUUID();
    e.createdAt = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    e.updatedAt = e.createdAt;

    std::vector<unsigned char> encPass = m_crypto->encrypt(password, m_activeGroupKey_RAM);
    m_db->storeEntry(m_activeGroupId, e, encPass);

    // [FIX] Always queue sync, regardless of group name (if it has other members, it will sync)
    std::string payload = serializeEntry(e, password, m_activeGroupKey_RAM);
    queueSyncForGroup(m_activeGroupName, "UPSERT", payload);
    return true;
}

bool Vault::updateEntry(const VaultEntry& entry, const std::string& newPassword) {
    checkLocked();
    int groupId = m_db->getGroupIdForEntry(entry.id);
    if (groupId == -1) return false;

    VaultEntry original;
    auto existingEntries = m_db->getEntriesForGroup(groupId);
    bool found = false;
    for(const auto& ex : existingEntries) {
        if (ex.id == entry.id) { original = ex; found = true; break; }
    }
    if (!found) return false;

    VaultEntry updated = entry;
    updated.uuid = original.uuid; 
    updated.entryType = original.entryType;
    updated.totpSecret = original.totpSecret;
    updated.updatedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::vector<unsigned char> encryptionKey;
    std::string groupName;
    auto names = m_db->getAllGroupNames();
    for(const auto& n : names) if(m_db->getGroupId(n) == groupId) { groupName = n; break; }

    if (groupId == m_activeGroupId && !m_activeGroupKey_RAM.empty()) {
        encryptionKey = m_activeGroupKey_RAM;
    } else {
        auto encKey = m_db->getEncryptedGroupKeyById(groupId);
        try { encryptionKey = m_crypto->decrypt(encKey, m_masterKey_RAM); } catch (...) { return false; }
    }

    std::vector<unsigned char> encPass = m_crypto->encrypt(newPassword, encryptionKey);
    m_db->updateEntry(updated, &encPass);

    // [FIX] Always queue sync
    if (!groupName.empty()) {
        std::string payload = serializeEntry(updated, newPassword, encryptionKey);
        queueSyncForGroup(groupName, "UPSERT", payload);
    }
    return true;
}

bool Vault::deleteEntry(int entryId) {
    if (isLocked()) return false;
    int groupId = m_db->getGroupIdForEntry(entryId);
    if (groupId == -1) return false;

    std::string uuid;
    auto existingEntries = m_db->getEntriesForGroup(groupId);
    for(const auto& ex : existingEntries) {
        if (ex.id == entryId) { uuid = ex.uuid; break; }
    }

    if (!m_db->deleteEntry(entryId)) return false;

    if (!uuid.empty()) {
        std::string groupName;
        auto names = m_db->getAllGroupNames();
        for(const auto& n : names) if(m_db->getGroupId(n) == groupId) { groupName = n; break; }

        // [FIX] Always queue sync
        if (!groupName.empty()) { 
            std::string payload = "{\"uuid\":\"" + escapeJson(uuid) + "\"}";
            queueSyncForGroup(groupName, "DELETE", payload);
        }
    }
    return true;
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
       << "|" << e.totpSecret << "|" << e.createdAt << "|" << e.updatedAt << "|" << e.lastAccessed << "|" << e.url;

    // Append locations
    ss << "|[";
    for(size_t i = 0; i < e.locations.size(); ++i) {
        ss << "{\"type\":\"" << escapeJson(e.locations[i].type) << "\",\"value\":\"" << escapeJson(e.locations[i].value) << "\"}";
        if(i < e.locations.size() - 1) ss << ",";
    }
    ss << "]";
    return ss.str();
}

bool Vault::renameGroup(const std::string& oldName, const std::string& newName) {
    checkLocked();
    if (!isGroupOwner(oldName)) return false;
    int gid = m_db->getGroupId(oldName);
    if (gid == -1) return false;

    m_db->exec("UPDATE groups SET name = '" + escapeJson(newName) + "' WHERE id = " + std::to_string(gid));

    std::ostringstream payload;
    payload << "{\"old\":\"" << escapeJson(oldName) << "\",\"new\":\"" << escapeJson(newName) << "\"}";

    queueSyncForGroup(newName, "GROUP_RENAME", payload.str());
    notifySync("groups-updated", "");
    return true;
}

void Vault::leaveGroup(const std::string& groupName) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return;

    std::string myId = getUserId();
    m_db->removeGroupMember(gid, myId);

    std::string owner = m_db->getGroupOwner(gid);
    if (!owner.empty() && owner != myId) {
        std::ostringstream payload;
        payload << "{\"userId\":\"" << escapeJson(myId) << "\"}";
        m_db->storeSyncJob(owner, groupName, "MEMBER_REMOVE", payload.str());
        processOutboxForUser(owner);
    }
    
    // Delete group locally to avoid ghost groups
    m_db->deleteGroup(groupName);
    notifySync("groups-updated", "");
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
    checkLocked();
    std::string myId = getUserId();
    if (myId.empty()) return false;

    try {
        std::vector<unsigned char> finalKey = key;
        if (finalKey.empty()) {
            finalKey.resize(32);
            Crypto::randomBytes(finalKey.data(), 32);
        }
        std::vector<unsigned char> encryptedKey = Crypto::encrypt(finalKey, m_masterKey_RAM);
        std::string finalOwner = ownerId.empty() || ownerId == myId ? myId : ownerId;

        m_db->storeEncryptedGroup(groupName, encryptedKey, finalOwner);
        int gid = m_db->getGroupId(groupName);
        if (gid == -1) return false;

        m_db->addGroupMember(gid, finalOwner, "owner", "accepted");
        m_db->updateGroupOwner(gid, finalOwner);
        m_db->updateGroupMemberRole(gid, finalOwner, "owner");
        return true;
    } catch (...) { return false; }
}

bool Vault::deleteGroup(const std::string& groupName) {
    checkLocked();
    if (!isGroupOwner(groupName)) return false;
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return false;

    auto members = m_db->getGroupMembers(gid);
    std::string myId = getUserId();
    for (const auto& m : members) {
        if (m.userId == myId) continue;
        std::ostringstream payload;
        payload << "{ \"reason\":\"group_deleted\" }";
        m_db->storeSyncJob(m.userId, groupName, "GROUP_SPLIT", payload.str());
        processOutboxForUser(m.userId);
    }
    m_db->deleteGroup(groupName);
    notifySync("group-deleted", groupName);
    return true;
}

void Vault::removeUser(const std::string& groupName, const std::string& userId) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if(gid == -1) return;
    m_db->removeGroupMember(gid, userId);
    
    std::ostringstream kickPayload;
    kickPayload << "{\"reason\":\"removed\"}";
    m_db->storeSyncJob(userId, groupName, "MEMBER_KICK", kickPayload.str());
    processOutboxForUser(userId);
    
    std::ostringstream updatePayload;
    updatePayload << "{\"userId\":\"" << userId << "\"}";
    queueSyncForGroup(groupName, "MEMBER_REMOVE", updatePayload.str());
}

void Vault::removeGroupMember(const std::string& groupName, const std::string& userId) {
    removeUser(groupName, userId);
}

bool Vault::canUserEdit(const std::string& groupName) {
    if (isLocked()) return false;
    // [FIX] Remove hardcoded Personal permission, rely on proper owner role check below

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
std::string Vault::getGroupOwner(int groupId) {
    checkLocked();
    if (groupId == -1) return "";
    auto members = m_db->getGroupMembers(groupId);
    for (const auto& m : members) { if (m.role == "owner") return m.userId; }
    return "";
}

std::string Vault::getGroupOwner(const std::string& groupName) { return getGroupOwner(getGroupId(groupName)); }
bool Vault::isGroupOwner(const std::string& groupName) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return false;
    std::string myId = getUserId();
    auto members = m_db->getGroupMembers(gid);
    for (const auto& m : members) { if (m.userId == myId && m.role == "owner") return true; }
    return false;
}

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
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return;

    m_db->updateGroupMemberStatus(gid, userId, newStatus);
    if (newStatus != "accepted") return;
    if (!isGroupOwner(groupName)) return;

    auto members = m_db->getGroupMembers(gid);
    std::ostringstream snapshot;
    snapshot << "{ \"members\":[";
    for (size_t i = 0; i < members.size(); ++i) {
        snapshot << "{\"userId\":\"" << escapeJson(members[i].userId) << "\","
                 << "\"role\":\"" << escapeJson(members[i].role) << "\","
                 << "\"status\":\"" << escapeJson(members[i].status) << "\"}";
        if (i + 1 < members.size()) snapshot << ",";
    }
    snapshot << "]}";

    queueSyncForGroup(groupName, "MEMBER_LIST", snapshot.str());
    notifySync("members-updated", groupName);
}

void Vault::setUserId(const std::string& userId) {
    checkLocked();
    std::vector<unsigned char> idData(userId.begin(), userId.end());
    m_db->storeMetadata("user_id", idData);
    m_userId = userId; 
}

std::string Vault::getUserId() {
    checkLocked();
    if (!m_userId.empty()) return m_userId;
    std::vector<unsigned char> data = m_db->getMetadata("user_id");
    if (data.empty()) throw std::runtime_error("Vault has no user_id. Vault is corrupted.");
    m_userId.assign(data.begin(), data.end());
    return m_userId;
}

void Vault::generateAndSetUniqueId(const std::string& username) {
    checkLocked();
    std::string uuid = m_crypto->generateUUID();
    std::string finalId = sanitizeUsername(username) + "#" + uuid.substr(0, 8);
    setUserId(finalId);
    setUsername(username);
}

void Vault::ensureIdentityKeys() {
    checkLocked();
    m_identityPublicKey.resize(crypto_box_PUBLICKEYBYTES);
    m_identityPrivateKey.resize(crypto_box_SECRETKEYBYTES);
    crypto_box_keypair(m_identityPublicKey.data(), m_identityPrivateKey.data());
    std::vector<unsigned char> encPriv = m_crypto->encrypt(m_identityPrivateKey, m_masterKey_RAM);
    m_db->storeMetadata("identity_priv", encPriv);
    m_db->storeMetadata("identity_pub", m_identityPublicKey);
}

void Vault::loadIdentityKeys() {
    checkLocked();
    std::vector<unsigned char> encPriv = m_db->getMetadata("identity_priv");
    std::vector<unsigned char> pub = m_db->getMetadata("identity_pub");
    if (encPriv.empty() || pub.empty()) throw std::runtime_error("Identity keys missing");
    m_identityPrivateKey = m_crypto->decrypt(encPriv, m_masterKey_RAM);
    m_identityPublicKey = pub;
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

std::vector<PasswordHistoryEntry> Vault::getPasswordHistory(int entryId) { return m_db->getPasswordHistory(entryId); }
std::string Vault::decryptPasswordFromHistory(const std::string& encryptedPassword) {
    checkLocked();
    try {
        std::vector<unsigned char> encryptedData = m_crypto->base64Decode(encryptedPassword);
        return m_crypto->decryptToString(encryptedData, m_masterKey_RAM);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed: ") + e.what());
    }
}
void Vault::updateEntryAccessTime(int entryId) { m_db->updateEntryAccessTime(entryId); }
std::vector<VaultEntry> Vault::getRecentlyAccessedEntries(int limit) { return m_db->getRecentEntries(limit); }

// [FIX] Implemented 2-arg Overload for Desktop
std::vector<VaultEntry> Vault::getRecentlyAccessedEntries(int groupId, int limit) {
    return m_db->getRecentlyAccessedEntries(groupId, limit);
}

void Vault::storePendingInvite(const std::string& s, const std::string& g, const std::string& p) { checkLocked(); m_db->storePendingInvite(s, g, p); }
std::vector<PendingInvite> Vault::getPendingInvites() { checkLocked(); return m_db->getPendingInvites(); }
void Vault::deletePendingInvite(int id) { checkLocked(); m_db->deletePendingInvite(id); }
void Vault::updatePendingInviteStatus(int id, const std::string& s) { checkLocked(); m_db->updatePendingInviteStatus(id, s); }

void Vault::respondToInvite(const std::string& groupName, const std::string& senderId, bool accept) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return;
    std::string myId = getUserId();

    // [FIX] Delete pending invite FIRST, BEFORE sending response
    // This prevents the invite from persisting if Desktop sends INVITE_ACCEPT afterwards
    auto invites = m_db->getPendingInvites();
    
    // Normalize senderId for comparison
    std::string senderLower = senderId;
    // Trim
    senderLower.erase(0, senderLower.find_first_not_of(" \t\n\r\f\v"));
    senderLower.erase(senderLower.find_last_not_of(" \t\n\r\f\v") + 1);
    std::transform(senderLower.begin(), senderLower.end(), senderLower.begin(), ::tolower);
    
    LOG_DEBUG("respondToInvite: Cleaning up for group '" + groupName + "' sender '" + senderId + "' (norm: " + senderLower + ")");

    int deletedCount = 0;
    for (const auto& i : invites) {
        std::string iSender = i.senderId;
        std::transform(iSender.begin(), iSender.end(), iSender.begin(), ::tolower);
        
        // Check group name and sender
        if (i.groupName == groupName && iSender == senderLower) {
            LOG_DEBUG("respondToInvite: Deleting pending invite ID " + std::to_string(i.id));
            m_db->deletePendingInvite(i.id);
            deletedCount++;
        } else {
             LOG_DEBUG("respondToInvite: Skipping ID " + std::to_string(i.id) + " - Group: " + i.groupName + " Sender: " + i.senderId);
        }
    }
    LOG_INFO("respondToInvite: Deleted " + std::to_string(deletedCount) + " pending invites.");
    
    // NOW send acceptance response (if accepting)
    if (accept) {
        m_db->addGroupMember(gid, myId, "member", "accepted");
        std::ostringstream payload;
        payload << "{\"userId\":\"" << escapeJson(myId) << "\"}" ;
        m_db->storeSyncJob(senderId, groupName, "INVITE_ACCEPT", payload.str());
        processOutboxForUser(senderId);
    }

    // Always notify UI that invites changed
    notifySync("invites-updated", "");
}

void Vault::sendP2PInvite(const std::string& groupName, const std::string& targetUser) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return;
    if (!isGroupOwner(groupName)) return;
    
    // [FIX] Delete any existing pending invites from this sender for this group to the target user
    // This prevents duplicate invites when re-inviting removed members
    auto existingInvites = m_db->getPendingInvites();
    for (const auto& inv : existingInvites) {
        std::string invSenderLower = inv.senderId;
        std::string myIdLower = getUserId();
        std::transform(invSenderLower.begin(), invSenderLower.end(), invSenderLower.begin(), ::tolower);
        std::transform(myIdLower.begin(), myIdLower.end(), myIdLower.begin(), ::tolower);
        
        if (inv.groupName == groupName && invSenderLower == myIdLower) {
            LOG_DEBUG("sendP2PInvite: Deleting old local invite record ID " + std::to_string(inv.id));
            m_db->deletePendingInvite(inv.id);
        }
    }
    
    // [FIX] If user was previously a member (e.g., removed and now being re-invited),
    // remove their old membership record so they start fresh with "pending" status
    auto members = m_db->getGroupMembers(gid);
    for (const auto& m : members) {
        std::string mIdLower = m.userId;
        std::string targetLower = targetUser;
        std::transform(mIdLower.begin(), mIdLower.end(), mIdLower.begin(), ::tolower);
        std::transform(targetLower.begin(), targetLower.end(), targetLower.begin(), ::tolower);
        
        if (mIdLower == targetLower) {
            LOG_DEBUG("sendP2PInvite: Removing old membership for " + targetUser + " before re-invite");
            m_db->removeGroupMember(gid, targetUser);
            break;
        }
    }
    
    // Add as pending member
    m_db->addGroupMember(gid, targetUser, "member", "pending");

    std::ostringstream payload;
    payload << "{\"group\":\"" << escapeJson(groupName) << "\","
            << "\"owner\":\"" << escapeJson(getUserId()) << "\","
            << "\"ownerName\":\"" << escapeJson(getDisplayUsername()) << "\"}";

    m_db->storePendingInvite(getUserId(), groupName, payload.str());
    m_db->storeSyncJob(targetUser, groupName, "INVITE", payload.str());
    processOutboxForUser(targetUser);
    notifySync("invite-sent", groupName);
}

std::vector<VaultEntry> Vault::exportGroupEntries(const std::string& groupName) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    if (groupId == -1) return {}; 
    std::vector<VaultEntry> entries = m_db->getEntriesForGroup(groupId);
    for (auto& entry : entries) {
        try {
            std::vector<unsigned char> encryptedPassword = m_db->getEncryptedPassword(entry.id);
            if (!encryptedPassword.empty()) {
                entry.password = m_crypto->base64Encode(encryptedPassword);
            } else { entry.password = ""; }
        } catch (...) { entry.password = ""; }
    }
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
    // Stub
    (void)membersJson; 
}

void Vault::importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries) {
    checkLocked();
    int groupId = m_db->getGroupId(groupName);
    if (groupId == -1) return;
    for (const auto& entry : entries) {
        std::vector<unsigned char> encPass;
        if(!entry.password.empty()) {
            encPass = m_crypto->base64Decode(entry.password);
        }
        VaultEntry e = entry;
        e.id = -1; 
        m_db->storeEntry(groupId, e, encPass);
    }
}

void Vault::broadcastSync(const std::string& groupName) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return;

    auto members = m_db->getGroupMembers(gid);
    std::string myId = getUserId();

    for (const auto& m : members) {
        if (m.userId != myId && m.status == "accepted") {
            processOutboxForUser(m.userId);
        }
    }
    notifySync("groups-updated", groupName);
}

std::string Vault::exportGroupMembers(const std::string& groupName) {
    checkLocked();
    int gid = m_db->getGroupId(groupName);
    if (gid == -1) return "[]";
    auto members = m_db->getGroupMembers(gid);
    std::ostringstream json;
    json << "[";
    for (size_t i = 0; i < members.size(); ++i) {
        json << "{\"userId\":\"" << escapeJson(members[i].userId) << "\","
             << "\"role\":\"" << escapeJson(members[i].role) << "\","
             << "\"status\":\"" << escapeJson(members[i].status) << "\"}";
        if (i + 1 < members.size()) json << ",";
    }
    json << "]";
    return json.str();
}

} // namespace Core
} // namespace CipherMesh