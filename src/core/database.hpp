#ifndef CIPHERMESH_CORE_DATABASE_HPP
#define CIPHERMESH_CORE_DATABASE_HPP

#include <string>
#include <vector>
#include <map>
#include "vault_entry.hpp"

#if defined(__ANDROID__) || defined(ANDROID)
    typedef struct sqlite3 sqlite3;
#else
    #include <QSqlDatabase>
#endif

namespace CipherMesh {
namespace Core {

class DBException : public std::exception {
    std::string msg;
public:
    DBException(const std::string& m) : msg(m) {}
    const char* what() const noexcept override { return msg.c_str(); }
};

class Database {
public:
    Database();
    ~Database();

    void open(const std::string& path);
    void close();
    bool isOpen() const;
    void createTables();
    void exec(const std::string& sql);

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
    std::string getGroupOwner(int groupId);
    void updateGroupOwner(int groupId, const std::string& ownerId);

    // Members
    void addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status);
    std::vector<GroupMember> getGroupMembers(int groupId);
    void removeGroupMember(int groupId, const std::string& userId);
    void setGroupPermissions(int groupId, bool adminsOnly);
    GroupPermissions getGroupPermissions(int groupId);
    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus);

    // Entries
    void storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword);
    std::vector<VaultEntry> getEntriesForGroup(int groupId);
    std::vector<Location> getLocationsForEntry(int entryId);
    std::vector<unsigned char> getEncryptedPassword(int entryId);
    void updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword);
    bool deleteEntry(int entryId);
    
    // [NEW] Phase 3: Cleanup
    void cleanupTombstones(long long olderThanTimestamp);

    bool entryExists(const std::string& username, const std::string& locationValue);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);
    std::vector<VaultEntry> searchEntries(const std::string& searchTerm);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int groupId, int limit);
    void updateEntryAccessTime(int entryId);

    // Metadata & History
    void storeMetadata(const std::string& key, const std::vector<unsigned char>& value);
    std::vector<unsigned char> getMetadata(const std::string& key);
    void storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword);
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    void deleteOldPasswordHistory(int entryId, int keepCount);

    // Invites
    void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
    std::vector<PendingInvite> getPendingInvites();
    void deletePendingInvite(int inviteId);
    void updatePendingInviteStatus(int inviteId, const std::string& status);

    std::vector<VaultEntry> getRecentEntries(int limit);

    // Sync Queue
    void storeSyncJob(const std::string& targetUser, const std::string& groupName, const std::string& operation, const std::string& payload);
    std::vector<SyncJob> getSyncJobsForUser(const std::string& userId);
    void deleteSyncJob(int jobId);

private:
#if defined(__ANDROID__) || defined(ANDROID)
    sqlite3* m_db_handle;
#else
    std::string m_connectionName;
    QSqlDatabase m_db;
#endif
};

} // namespace Core
} // namespace CipherMesh

#endif