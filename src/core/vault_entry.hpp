#pragma once

#include <string>
#include <vector> 

namespace CipherMesh {
namespace Core {

struct Location {
    int id;
    std::string type;  
    std::string value; 
    Location() : id(-1) {}
    Location(int id, std::string t, std::string v) : id(id), type(std::move(t)), value(std::move(v)) {}
};

struct VaultEntry {
    int id;
    std::string title;
    std::string username;
    std::string notes;
    std::string password; 
    std::vector<Location> locations; 
    long long createdAt;        // Unix timestamp when entry was created
    long long lastModified;     // Unix timestamp when entry was last modified
    long long lastAccessed;     // Unix timestamp when entry was last accessed
    long long passwordExpiry;   // Unix timestamp when password expires (0 = no expiry)

    VaultEntry() : id(-1), createdAt(0), lastModified(0), lastAccessed(0), passwordExpiry(0) {}
    VaultEntry(int id, std::string t, std::string u, std::string n) 
        : id(id), title(std::move(t)), username(std::move(u)), notes(std::move(n)), 
          createdAt(0), lastModified(0), lastAccessed(0), passwordExpiry(0) {}
};

struct PendingInvite {
    int id;
    std::string senderId;
    std::string groupName;
    std::string payloadJson;
    long long timestamp;
    std::string status; // "pending" or "accepted" (waiting for data)
};

struct GroupMember {
    std::string userId;
    std::string role; 
    std::string status; 
};

struct GroupPermissions {
    bool adminsOnlyWrite;
};

struct PasswordHistoryEntry {
    int id;
    int entryId;
    std::string encryptedPassword;  // Encrypted old password
    long long changedAt;            // Unix timestamp when password was changed
    
    PasswordHistoryEntry() : id(-1), entryId(-1), changedAt(0) {}
    PasswordHistoryEntry(int id, int eId, std::string pwd, long long ts)
        : id(id), entryId(eId), encryptedPassword(std::move(pwd)), changedAt(ts) {}
};

}
}