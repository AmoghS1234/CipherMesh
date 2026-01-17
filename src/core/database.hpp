#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>

// Shared Structs
#include "vault_entry.hpp"

// Platform Specific Headers
#ifdef __ANDROID__
    // We avoid including sqlite3.h in the header to keep it clean.
    // We use a void* for the handle (PIMPL-lite style).
#else
    #include <QSqlDatabase>
#endif

namespace CipherMesh {
namespace Core {

class DBException : public std::runtime_error {
public:
    explicit DBException(const std::string& message) : std::runtime_error(message) {}
};

class Database {
public:
    Database();
    ~Database();

    void open(const std::string& path);
    void close();
    bool isOpen() const;
    void exec(const std::string& sql); // Helper for raw SQL
    void createTables();
    std::vector<VaultEntry> getRecentEntries(int limit);

    // -- Group Management --
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

    // -- Group Members --
    void addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status);
    std::vector<GroupMember> getGroupMembers(int groupId);
    void removeGroupMember(int groupId, const std::string& userId);
    void setGroupPermissions(int groupId, bool adminsOnly);
    GroupPermissions getGroupPermissions(int groupId);
    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus);

    // -- Entries --
    void storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword);
    std::vector<VaultEntry> getEntriesForGroup(int groupId);
    std::vector<Location> getLocationsForEntry(int entryId);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);
    std::vector<VaultEntry> searchEntries(const std::string& searchTerm);
    std::vector<unsigned char> getEncryptedPassword(int entryId);
    bool deleteEntry(int entryId);
    void updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword);
    bool entryExists(const std::string& username, const std::string& locationValue);
    void updateEntryAccessTime(int entryId);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int groupId, int limit);

    // -- Metadata --
    void storeMetadata(const std::string& key, const std::vector<unsigned char>& value);
    std::vector<unsigned char> getMetadata(const std::string& key);

    // -- History --
    void storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword);
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    void deleteOldPasswordHistory(int entryId, int keepCount);

    // -- Invites --
    void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
    void updatePendingInviteStatus(int inviteId, const std::string& status);
    std::vector<PendingInvite> getPendingInvites();
    void deletePendingInvite(int inviteId);

private:
#ifdef __ANDROID__
    void* m_db_handle = nullptr; // SQLite3 handle (void* to avoid headers)
#else
    std::string m_connectionName;
    QSqlDatabase m_db;
#endif
};

} // namespace Core
} // namespace CipherMesh