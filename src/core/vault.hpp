#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "database.hpp"
#include "crypto.hpp"
#include "vault_entry.hpp"

namespace CipherMesh {
namespace Core {

typedef std::function<void(const std::string& targetUser, const std::string& message)> P2PSendCallback;
typedef std::function<void(const std::string& type, const std::string& payload)> SyncCallback;

class Vault {
public:
    Vault();
    ~Vault();

    // --- Core Lifecycle ---
    void connect(const std::string& path);
    bool unlock(const std::string& masterPassword);
    bool createNewVault(const std::string& path, const std::string& masterPassword);
    bool changeMasterPassword(const std::string& newPassword);
    bool isLocked() const;
    bool isConnected() const;
    bool hasUsers() const;
    bool verifyMasterPassword(const std::string& password);
    void setActivityContext(void* context) {}
    std::string getDBPath() const; 

    // [FIX] These must be PUBLIC for Desktop UI
    void lock();
    bool loadVault(const std::string& path, const std::string& masterPassword);

    // --- User Identity ---
    void setUserId(const std::string& userId);
    std::string getUserId();
    void setUsername(const std::string& name);
    std::string getDisplayUsername();
    void generateAndSetUniqueId(const std::string& username);
    std::string getIdentityPublicKey();

    // --- Entry Management ---
    bool addEntry(const VaultEntry& entry, const std::string& password);
    bool updateEntry(const VaultEntry& entry, const std::string& newPassword);
    bool deleteEntry(int entryId);
    std::vector<VaultEntry> getEntries();
    std::string getDecryptedPassword(int entryId);
    std::string getEntryFullDetails(int entryId);
    std::vector<VaultEntry> searchEntries(const std::string& searchTerm);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int limit);
    void updateEntryAccessTime(int entryId);
    bool entryExists(const std::string& username, const std::string& locationValue);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);

    // --- Group Management ---
    std::vector<std::string> getGroupNames();
    bool groupExists(const std::string& groupName);
    bool addGroup(const std::string& groupName, const std::vector<unsigned char>& key = {}, const std::string& ownerId = "");
    bool setActiveGroup(const std::string& groupName);
    bool isGroupActive() const;
    bool deleteGroup(const std::string& groupName);
    std::string getGroupOwner(int groupId);
    std::string getGroupOwner(const std::string& groupName);
    bool isGroupOwner(const std::string& groupName);
    int getGroupId(const std::string& groupName);
    void setGroupPermissions(int groupId, bool adminsOnly);
    GroupPermissions getGroupPermissions(int groupId);
    bool canUserEdit(const std::string& groupName);

    // --- Member Management ---
    std::vector<GroupMember> getGroupMembers(const std::string& groupName);
    void addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status);
    
    // [FIX] The main logic uses removeUser, but we keep this signature for Desktop compatibility
    void removeUser(const std::string& groupName, const std::string& userId);
    void removeGroupMember(const std::string& groupName, const std::string& userId);

    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus);
    void importGroupMembers(const std::string& groupName, const std::string& membersJson);

    // --- P2P & Syncing ---
    void setP2PSendCallback(P2PSendCallback cb);
    void processOutboxForUser(const std::string& userId);
    void handleSyncAck(int jobId);
    void handleIncomingSync(const std::string& senderId, const std::string& payload);
    void setSyncCallback(SyncCallback cb);
    void notifySync(const std::string& type, const std::string& payload);
    void processSyncEvent(const std::string& payload);
    
    // [FIX] Add this declaration
    void addEncryptedEntry(const VaultEntry& entry, const std::string& base64Ciphertext);

    // --- Invite System ---
    void sendP2PInvite(const std::string& groupName, const std::string& targetUser);
    void respondToInvite(const std::string& groupName, const std::string& senderId, bool accept);
    std::vector<PendingInvite> getPendingInvites();
    void deletePendingInvite(int inviteId);
    void updatePendingInviteStatus(int inviteId, const std::string& status);
    std::vector<unsigned char> getGroupKey(const std::string& groupName);
    std::vector<VaultEntry> exportGroupEntries(const std::string& groupName);
    void storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson);
    void importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries);
    std::string decryptIncomingKey(const std::string& encryptedBase64);
    std::vector<unsigned char> encryptForUser(const std::string& recipientPublicKeyBase64, const std::vector<unsigned char>& data);

    // --- History ---
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    std::string decryptPasswordFromHistory(const std::string& encryptedPassword);

    // --- Settings ---
    void setThemeId(const std::string& themeId);
    std::string getThemeId();
    void setAutoLockTimeout(int minutes);
    int getAutoLockTimeout();

private:
    void checkLocked() const;
    void checkGroupActive() const;
    void ensureIdentityKeys();
    void lockActiveGroup();

    // Sync Helpers
    void queueSyncForGroup(const std::string& groupName, const std::string& operation, const std::string& payload);
    void queueSyncForMember(const std::string& groupName, const std::string& memberId, const std::string& operation, const std::string& payload); // [NEW] For targeted sync
    std::string serializeEntry(const VaultEntry& e, const std::string& password);

    std::unique_ptr<Database> m_db;
    std::unique_ptr<Crypto> m_crypto;
    std::string m_dbPath;
    
    // RAM Keys (Wiped on lock)
    std::vector<unsigned char> m_masterKey_RAM;
    std::vector<unsigned char> m_activeGroupKey_RAM;
    std::vector<unsigned char> m_identityPrivateKey;
    std::vector<unsigned char> m_identityPublicKey;
    
    // State
    int m_activeGroupId;
    std::string m_activeGroupName;
    std::string m_userId; 
    
    P2PSendCallback m_p2pSender;
    SyncCallback m_syncCb;
};

} // namespace Core
} // namespace CipherMesh