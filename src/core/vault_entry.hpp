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
    int groupId;
    std::string uuid; 
    std::string title;
    std::string username;
    std::string notes;
    std::string password; // Plaintext (only in RAM)
    std::string url;      
    std::vector<Location> locations; 
    
    long long createdAt;
    long long updatedAt;      
    long long lastAccessed;
    long long passwordExpiry;
    
    std::string totpSecret;   
    std::string entryType;    
    bool isDeleted;           

    VaultEntry() 
        : id(-1), groupId(-1), createdAt(0), updatedAt(0), lastAccessed(0), passwordExpiry(0), 
          totpSecret(""), entryType("password"), isDeleted(false) {}

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
    std::string payloadJson; 
    long long timestamp;
    std::string status; 
};

// --- 5. History ---
struct PasswordHistoryEntry {
    int id;
    int entryId;
    std::string encryptedPassword;
    long long changedAt; 
    
    PasswordHistoryEntry() : id(-1), entryId(-1), changedAt(0) {}
    PasswordHistoryEntry(int id, int eId, std::string pwd, long long ts)
        : id(id), entryId(eId), encryptedPassword(std::move(pwd)), changedAt(ts) {}
};

// --- 6. Syncing (The Outbox) ---
struct SyncJob {
    int id;
    std::string targetUser;
    std::string groupName;
    std::string operation; // "UPSERT", "DELETE", "MEMBER_ADD", "MEMBER_KICK"
    std::string payload;   // Encrypted JSON content
    long long createdAt;
};

} // namespace Core
} // namespace CipherMesh