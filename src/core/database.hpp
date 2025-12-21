#pragma once

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <QSqlDatabase> // <--- CRITICAL: Added for Qt SQL

namespace CipherMesh {
namespace Core {

// Exception class for Database errors
class DBException : public std::runtime_error {
public:
    explicit DBException(const std::string& msg) : std::runtime_error(msg) {}
};

// Data Structures (Reverse-engineered from your cpp file)
struct Location {
    int id;
    std::string type;
    std::string value;

    Location(int i, std::string t, std::string v) 
        : id(i), type(std::move(t)), value(std::move(v)) {}
};

struct VaultEntry {
    int id;
    std::string title;
    std::string username;
    std::string notes;
    long long createdAt;
    long long lastModified;
    long long lastAccessed;
    long long passwordExpiry;
    std::string totp_secret;
    std::string entry_type;
    std::vector<Location> locations;

    VaultEntry(int i, std::string t, std::string u, std::string n)
        : id(i), title(std::move(t)), username(std::move(u)), notes(std::move(n)),
          createdAt(0), lastModified(0), lastAccessed(0), passwordExpiry(0) {}
};

struct PasswordHistoryEntry {
    int id;
    int entryId;
    std::string encryptedPassword;
    long long changedAt;

    PasswordHistoryEntry(int i, int eid, std::string ep, long long ca)
        : id(i), entryId(eid), encryptedPassword(std::move(ep)), changedAt(ca) {}
};

struct PendingInvite {
    int id;
    std::string senderId;
    std::string groupName;
    std::string payloadJson;
    long long timestamp;
    std::string status;
};

struct GroupMember {
    std::string userId;
    std::string role;
    std::string status;
};

struct GroupPermissions {
    bool adminsOnlyWrite;
};

class Database {
public:
    Database();
    ~Database();

    // Core Management
    void open(const std::string& path);
    void close();
    void createTables();
    void exec(const std::string& sql); // Kept for raw SQL support if needed

    // Metadata
    void storeMetadata(const std::string& key, const std::vector<unsigned char>& value);
    std::vector<unsigned char> getMetadata(const std::string& key);

    // Groups
    void storeEncryptedGroup(const std::string& name, const std::vector<unsigned char>& encryptedKey, const std::string& ownerId);
    std::vector<unsigned char> getEncryptedGroupKey(const std::string& name, int& groupId);
    std::vector<unsigned char> getEncryptedGroupKeyById(int groupId);
    std::map<int, std::vector<unsigned char>> getAllEncryptedGroupKeys();
    void updateEncryptedGroupKey(int groupId, const std::vector<unsigned char>& newKey);
    std::vector<std::string> getAllGroupNames();
    int getGroupId(const std::string& name);
    int getGroupIdForEntry(int entryId);
    bool deleteGroup(const std::string& name);

    // Entries
    void storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword);
    std::vector<VaultEntry> getEntriesForGroup(int groupId);
    std::vector<Location> getLocationsForEntry(int entryId);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);
    std::vector<VaultEntry> searchEntries(const std::string& searchTerm);
    std::vector<unsigned char> getEncryptedPassword(int entryId);
    bool deleteEntry(int entryId);
    void updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword = nullptr);
    bool entryExists(const std::string& username, const std::string& locationValue);

    // Password History
    void storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword);
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    void deleteOldPasswordHistory(int entryId, int keepCount);

    // Access Tracking
    void updateEntryAccessTime(int entryId);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int groupId, int limit);

    // Pending Invites
    void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
    void updatePendingInviteStatus(int inviteId, const std::string& status);
    std::vector<PendingInvite> getPendingInvites();
    void deletePendingInvite(int inviteId);

    // Group Members & Permissions
    void addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status);
    void removeGroupMember(int groupId, const std::string& userId);
    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus);
    std::vector<GroupMember> getGroupMembers(int groupId);
    
    void setGroupPermissions(int groupId, bool adminsOnly);
    GroupPermissions getGroupPermissions(int groupId);
    std::string getGroupOwner(int groupId);

private:
    // CHANGE: No longer 'sqlite3* m_db', now QSqlDatabase
    QSqlDatabase m_db; 
};

} // namespace Core
} // namespace CipherMesh