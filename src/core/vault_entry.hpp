#pragma once

#include <string>
#include <vector>

namespace CipherMesh {
namespace Core {

// --- 1. Location (For future geo-fencing features) ---
struct Location {
    int id;
    std::string type;  
    std::string value; 
    Location() : id(-1) {}
    Location(int id, std::string t, std::string v) : id(id), type(std::move(t)), value(std::move(v)) {}
};

// --- 2. Vault Entry (The main data item) ---
struct VaultEntry {
    int id;
    int groupId; // [ADDED] Missing in previous version
    std::string uuid; // [ADDED] UUID for sync conflict resolution
    std::string title;
    std::string username;
    std::string notes;
    std::string password; // Plaintext (only in RAM)
    std::string url;      // [ADDED]
    std::vector<Location> locations; 
    
    long long createdAt;
    long long updatedAt;      // [RENAMED] from lastModified to match Android code
    long long lastAccessed;
    long long passwordExpiry;
    
    std::string totpSecret;   // [RENAMED] from totp_secret (camelCase standard)
    std::string entryType;    // [RENAMED] from entry_type (camelCase standard)
    bool isDeleted;           // [ADDED] For tombstoning (sync deletions)

    // Default Constructor
    VaultEntry() 
        : id(-1), groupId(-1), createdAt(0), updatedAt(0), lastAccessed(0), passwordExpiry(0), 
          totpSecret(""), entryType("password"), isDeleted(false) {}

    // Convenience Constructor
    VaultEntry(int id, std::string t, std::string u, std::string n) 
        : id(id), groupId(-1), title(std::move(t)), username(std::move(u)), notes(std::move(n)), 
          createdAt(0), updatedAt(0), lastAccessed(0), passwordExpiry(0), 
          totpSecret(""), entryType("password"), isDeleted(false) {}
};

// --- 3. Group Structures ---
struct GroupMember {
    std::string userId;
    std::string role;   // "owner", "admin", "member"
    std::string status; // "accepted", "pending"
};

struct GroupPermissions {
    bool adminsOnlyWrite = false;
    bool adminsOnlyInvite = false;
};

// --- 4. Invites ---
struct PendingInvite {
    int id;
    std::string senderId;
    std::string groupName;
    std::string payloadJson; // Encrypted key/data
    long long timestamp;
    std::string status; 
};

// --- 5. History ---
struct PasswordHistoryEntry {
    int id;
    int entryId;
    std::string encryptedPassword;
    long long changedAt; // Unix timestamp
    
    PasswordHistoryEntry() : id(-1), entryId(-1), changedAt(0) {}
    PasswordHistoryEntry(int id, int eId, std::string pwd, long long ts)
        : id(id), entryId(eId), encryptedPassword(std::move(pwd)), changedAt(ts) {}
};

} // namespace Core
} // namespace CipherMesh