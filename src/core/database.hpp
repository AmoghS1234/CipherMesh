#pragma once

#include "vault_entry.hpp"
#include <string>
#include <vector>
#include <stdexcept>
#include <map> 

struct sqlite3;

namespace CipherMesh {
namespace Core {

class Database {
public:
    Database();
    ~Database();

    void open(const std::string& path);
    void close();
    bool isOpen() const { return m_db != nullptr; }
    void createTables();

    void storeMetadata(const std::string& key, const std::vector<unsigned char>& value);
    std::vector<unsigned char> getMetadata(const std::string& key);

    void storeEncryptedGroup(const std::string& name, const std::vector<unsigned char>& encryptedKey, const std::string& ownerId);
    std::vector<unsigned char> getEncryptedGroupKey(const std::string& name, int& groupId);
    std::vector<unsigned char> getEncryptedGroupKeyById(int groupId); 
    std::vector<std::string> getAllGroupNames();
    int getGroupId(const std::string& name);
    int getGroupIdForEntry(int entryId); 
    bool deleteGroup(const std::string& name);
    
    std::map<int, std::vector<unsigned char>> getAllEncryptedGroupKeys();
    void updateEncryptedGroupKey(int groupId, const std::vector<unsigned char>& newKey);

    void storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword);
    std::vector<VaultEntry> getEntriesForGroup(int groupId);
    bool deleteEntry(int entryId);
    void updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword);
    
    std::vector<Location> getLocationsForEntry(int entryId);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue); 
    std::vector<VaultEntry> searchEntries(const std::string& searchTerm); 
    std::vector<unsigned char> getEncryptedPassword(int entryId);
    bool entryExists(const std::string& username, const std::string& locationValue);

    // Password history
    void storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword);
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    void deleteOldPasswordHistory(int entryId, int keepCount); // Keep only last N passwords
    
    // Entry access tracking
    void updateEntryAccessTime(int entryId);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int groupId, int limit);

    // --- UPDATED: Pending Invites ---
    void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
    void updatePendingInviteStatus(int inviteId, const std::string& status); // <-- NEW
    std::vector<PendingInvite> getPendingInvites();
    void deletePendingInvite(int inviteId);

    // Members
    void addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status);
    void removeGroupMember(int groupId, const std::string& userId);
    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus);
    std::vector<GroupMember> getGroupMembers(int groupId);
    
    void setGroupPermissions(int groupId, bool adminsOnly);
    GroupPermissions getGroupPermissions(int groupId);
    std::string getGroupOwner(int groupId);

private:
    sqlite3* m_db;
    void exec(const std::string& sql);
};

class DBException : public std::runtime_error {
public:
    explicit DBException(const std::string& what) : std::runtime_error(what) {}
};

}
}