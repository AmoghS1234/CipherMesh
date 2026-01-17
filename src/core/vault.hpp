#pragma once

#include "vault_entry.hpp"
#include "crypto.hpp" 
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace CipherMesh {
namespace Core {

    class Database;

    class Vault {
    public:
        Vault();
        ~Vault();

        void connect(const std::string& path); 
        bool isConnected() const;
        std::string getDBPath() const;

        bool createNewVault(const std::string& path, const std::string& masterPassword);
        bool loadVault(const std::string& path, const std::string& masterPassword);
        bool unlock(const std::string& masterPassword); 
        void lock();
        bool isLocked() const;
        bool verifyMasterPassword(const std::string& password);
        bool changeMasterPassword(const std::string& newPassword);
        bool hasUsers() const;

        using SyncCallback = std::function<void(const std::string& type, const std::string& payload)>;
        void setSyncCallback(SyncCallback cb);
        void processSyncEvent(const std::string& payload);

        // -- Identity --
        void setUserId(const std::string& userId);
        std::string getUserId();
        void setUsername(const std::string& name); 
        std::string getDisplayUsername();          
        
        std::string getIdentityPublicKey(); 
        std::string decryptIncomingKey(const std::string& encryptedBase64);
        std::vector<unsigned char> encryptForUser(const std::string& recipientPubKeyBase64, const std::vector<unsigned char>& data);

        // -- Groups & Entries --
        std::vector<std::string> getGroupNames();
        int getGroupId(const std::string& groupName);
        
        bool addGroup(const std::string& groupName, const std::vector<unsigned char>& key = {}, const std::string& ownerId = "");
        
        bool deleteGroup(const std::string& groupName);
        bool groupExists(const std::string& groupName);
        bool setActiveGroup(const std::string& groupName);
        int getActiveGroupId() const { return m_activeGroupId; }
        std::string getActiveGroupName() const { return m_activeGroupName; }
        bool isGroupActive() const;

        void addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status);
        void removeGroupMember(const std::string& groupName, const std::string& userId);
        std::vector<GroupMember> getGroupMembers(const std::string& groupName);
        std::string getGroupOwner(int groupId);
        bool canUserEdit(const std::string& groupName);
        void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
        void updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus);
        void setGroupPermissions(int groupId, bool adminsOnly);
        GroupPermissions getGroupPermissions(int groupId);

        std::vector<VaultEntry> getEntries();
        bool addEntry(const VaultEntry& entry, const std::string& password);
        std::string getDecryptedPassword(int entryId);
        bool deleteEntry(int entryId);
        bool updateEntry(const VaultEntry& entry, const std::string& newPassword);
        
        std::vector<unsigned char> getGroupKey(const std::string& groupName);
        std::vector<VaultEntry> exportGroupEntries(const std::string& groupName);
        void importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries);
        void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
        std::vector<PendingInvite> getPendingInvites();
        void deletePendingInvite(int inviteId);
        void updatePendingInviteStatus(int inviteId, const std::string& status);

        // -- Settings --
        void setThemeId(const std::string& themeId);
        std::string getThemeId();
        void setAutoLockTimeout(int minutes);
        int getAutoLockTimeout();
        std::string getUsername();
        
        std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
        std::string decryptPasswordFromHistory(const std::string& encryptedPassword);
        void updateEntryAccessTime(int entryId);
        std::vector<VaultEntry> getRecentlyAccessedEntries(int limit = 5);
        bool entryExists(const std::string& username, const std::string& locationValue);
        std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);
        std::vector<VaultEntry> searchEntries(const std::string& searchTerm);

    private:
        void checkLocked() const;
        void checkGroupActive() const;
        void ensureIdentityKeys();
        void notifySync(const std::string& type, const std::string& payload);
        void lockActiveGroup();

        std::unique_ptr<Crypto> m_crypto;
        std::unique_ptr<Database> m_db;
        SyncCallback m_syncCb;

        std::vector<unsigned char> m_masterKey_RAM;
        std::vector<unsigned char> m_activeGroupKey_RAM;
        std::vector<unsigned char> m_identityPrivateKey;
        std::vector<unsigned char> m_identityPublicKey;

        int m_activeGroupId;
        std::string m_activeGroupName;
        std::string m_dbPath;

        // [FIX] These are essential
        std::string m_userId;
        bool m_isLocked;
    };

} 
}