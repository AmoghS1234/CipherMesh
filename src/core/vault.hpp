#pragma once

#include "vault_entry.hpp"
#include <string>
#include <vector>
#include <memory>

namespace CipherMesh {
namespace Core {
class Database;
class Crypto;
}
}

namespace CipherMesh {
namespace Core {

// REMOVED: Structs GroupMember and GroupPermissions
// They are already defined in "vault_entry.hpp"

class Vault {
public:
    Vault();
    ~Vault();

    bool createNewVault(const std::string& path, const std::string& masterPassword);
    bool loadVault(const std::string& path, const std::string& masterPassword);
    void lock();
    bool isLocked() const;

    bool verifyMasterPassword(const std::string& password);
    bool changeMasterPassword(const std::string& newPassword);

    std::vector<std::string> getGroupNames();
    
    // -- Group Management --
    int getGroupId(const std::string& groupName); 
    bool addGroup(const std::string& groupName);
    bool addGroup(const std::string& groupName, const std::vector<unsigned char>& key);
    bool groupExists(const std::string& groupName);
    
    // -- Permissions & Roles --
    std::string getGroupOwner(int groupId); 
    void setGroupPermissions(int groupId, bool adminsOnly); 
    GroupPermissions getGroupPermissions(int groupId); 
    
    // -- Member Management --
    void addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status);
    void addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status); 
    void removeGroupMember(const std::string& groupName, const std::string& userId);
    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus);
    void updatePendingInviteStatus(int inviteId, const std::string& status);
    std::vector<GroupMember> getGroupMembers(const std::string& groupName);

    // -- Active Group Operations --
    bool setActiveGroup(const std::string& groupName);
    int getActiveGroupId() const { return m_activeGroupId; } 
    void lockActiveGroup();
    bool isGroupActive() const;
    bool deleteGroup(const std::string& groupName);

    std::vector<VaultEntry> getEntries();
    bool addEntry(const VaultEntry& entry, const std::string& password);
    std::string getDecryptedPassword(int entryId);
    bool deleteEntry(int entryId);
    bool updateEntry(const VaultEntry& entry, const std::string& newPassword);
    
    bool entryExists(const std::string& username, const std::string& locationValue);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);
    std::vector<VaultEntry> searchEntries(const std::string& searchTerm); 

    // -- P2P / Sync Helpers --
    std::vector<unsigned char> getGroupKey(const std::string& groupName);
    std::vector<VaultEntry> exportGroupEntries(const std::string& groupName);
    void importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries);

    void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
    std::vector<PendingInvite> getPendingInvites();
    void deletePendingInvite(int inviteId);

    void setUserId(const std::string& userId);
    std::string getUserId();
    
    bool canUserEdit(const std::string& groupName);
    // ... (existing public methods) ...
    
    // --- Theme Persistence ---
    void setThemeId(const std::string& themeId);
    std::string getThemeId();
    
    // --- Auto-lock Settings ---
    void setAutoLockTimeout(int minutes); // 0 = never auto-lock
    int getAutoLockTimeout(); // Returns timeout in minutes, 0 = disabled
    
    // --- Password History ---
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    std::string decryptPasswordFromHistory(const std::string& encryptedPassword);
    
    // --- Recently Accessed Entries ---
    void updateEntryAccessTime(int entryId);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int limit = 5);

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<Crypto> m_crypto;
    std::vector<unsigned char> m_masterKey_RAM;
    std::vector<unsigned char> m_activeGroupKey_RAM;
    int m_activeGroupId;
    std::string m_activeGroupName;
    std::string m_dbPath; 

    void checkLocked() const;
    void checkGroupActive() const;
};

}
}