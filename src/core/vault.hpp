#ifndef CIPHERMESH_CORE_VAULT_HPP
#define CIPHERMESH_CORE_VAULT_HPP

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <map>
#include "database.hpp"
#include "crypto.hpp"
#include "vault_entry.hpp" 

namespace CipherMesh {
namespace Core {

using P2PSendCallback = std::function<void(const std::string& target, const std::string& message)>;
using SyncCallback = std::function<void(const std::string& type, const std::string& payload)>;

class Vault {
public:
    Vault();
    ~Vault();

    void connect(const std::string& dbPath);
    bool unlock(const std::string& masterPassword);
    bool isLocked() const;
    void lock();
    bool createNewVault(const std::string& path, const std::string& masterPassword, const std::string& username);
    std::string getDBPath() const;
    bool isConnected() const;

    bool addEntry(const VaultEntry& entry, const std::string& password);
    bool updateEntry(const VaultEntry& entry, const std::string& newPassword);
    bool deleteEntry(int entryId);
    std::vector<VaultEntry> getEntries();
    std::string getEntryFullDetails(int entryId);
    std::string getDecryptedPassword(int entryId);
    void addEncryptedEntry(const VaultEntry& entry, const std::string& base64Ciphertext);

    bool addGroup(const std::string& groupName, const std::vector<unsigned char>& key = {}, const std::string& ownerId = "");
    bool deleteGroup(const std::string& groupName); 
    bool setActiveGroup(const std::string& groupName);
    std::vector<std::string> getGroupNames();
    bool groupExists(const std::string& groupName);
    bool isGroupActive() const;
    int getGroupId(const std::string& groupName);
    std::string getGroupOwner(const std::string& groupName);
    std::string getGroupOwner(int groupId);
    bool isGroupOwner(const std::string& groupName);
    bool renameGroup(const std::string& oldName, const std::string& newName);
    void leaveGroup(const std::string& groupName);
    void removeUser(const std::string& groupName, const std::string& userId);
    
    void addGroupMember(const std::string& groupName, const std::string& userId, const std::string& role, const std::string& status);
    std::vector<GroupMember> getGroupMembers(const std::string& groupName);
    void updateGroupMemberStatus(const std::string& groupName, const std::string& userId, const std::string& newStatus);
    void updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole);
    void removeGroupMember(const std::string& groupName, const std::string& userId); 
    
    bool canUserEdit(const std::string& groupName);
    void setGroupPermissions(int groupId, bool adminsOnly);
    GroupPermissions getGroupPermissions(int groupId);

    std::string getUserId();
    void setUserId(const std::string& userId);
    void setUsername(const std::string& name);
    std::string getDisplayUsername();
    std::vector<unsigned char> getGroupKey(const std::string& groupName);
    std::string getIdentityPublicKey();
    std::string decryptIncomingKey(const std::string& encryptedBase64);
    std::vector<unsigned char> encryptForUser(const std::string& recipientPublicKeyBase64, const std::vector<unsigned char>& data);
    bool verifyMasterPassword(const std::string& password);
    bool changeMasterPassword(const std::string& newPassword);
    bool hasUsers() const;

    std::vector<VaultEntry> searchEntries(const std::string& searchTerm);
    bool entryExists(const std::string& username, const std::string& locationValue);
    std::vector<VaultEntry> findEntriesByLocation(const std::string& locationValue);
    
    // [FIX] Overloaded methods for Recents (Global vs Group-Specific)
    std::vector<VaultEntry> getRecentlyAccessedEntries(int limit);
    std::vector<VaultEntry> getRecentlyAccessedEntries(int groupId, int limit);

    void updateEntryAccessTime(int entryId);
    std::vector<PasswordHistoryEntry> getPasswordHistory(int entryId);
    std::string decryptPasswordFromHistory(const std::string& encryptedPassword);

    void setP2PSendCallback(P2PSendCallback cb);
    void setSyncCallback(SyncCallback cb);
    void handleIncomingSync(const std::string& senderId, const std::string& payload);
    void processOutboxForUser(const std::string& userId);
    void onPeerOnline(const std::string& userId);
    void queueSyncForMember(const std::string& groupName, const std::string& memberId, const std::string& op, const std::string& payload);
    void broadcastSync(const std::string& groupName); 

    void sendP2PInvite(const std::string& groupName, const std::string& targetUser);
    void respondToInvite(const std::string& groupName, const std::string& senderId, bool accept);
    std::vector<PendingInvite> getPendingInvites();
    void storePendingInvite(const std::string& s, const std::string& g, const std::string& p);
    void deletePendingInvite(int id);
    void updatePendingInviteStatus(int id, const std::string& s);

    std::vector<VaultEntry> exportGroupEntries(const std::string& groupName);
    std::string exportGroupMembers(const std::string& groupName);
    
    void importGroupEntries(const std::string& groupName, const std::vector<VaultEntry>& entries);
    void importGroupMembers(const std::string& groupName, const std::string& membersJson);

    void setThemeId(const std::string& t);
    std::string getThemeId();
    void setAutoLockTimeout(int m);
    int getAutoLockTimeout();

    void processSyncEvent(const std::string& eventJson);
    void checkLocked() const;
    void checkGroupActive() const;
    void ensureIdentityKeys();
    void loadIdentityKeys();
    void lockActiveGroup();
    void generateAndSetUniqueId(const std::string& username);
    
    bool loadVault(const std::string& path, const std::string& masterPassword);

    void handleSyncAck(int jobId);
    void queueSyncForGroup(const std::string& groupName, const std::string& operation, const std::string& payload);
    void notifySync(const std::string& type, const std::string& payload);
    
    int findGroupIdForSync(const std::string& remoteGroupName, const std::string& senderId);
    
    std::string serializeEntry(const VaultEntry& e, const std::string& password, const std::vector<unsigned char>& key);

private:
    std::unique_ptr<Database> m_db;
    std::unique_ptr<Crypto> m_crypto;
    
    std::string m_dbPath;
    std::string m_userId;
    
    std::vector<unsigned char> m_masterKey_RAM;
    std::vector<unsigned char> m_activeGroupKey_RAM;
    std::vector<unsigned char> m_identityPrivateKey;
    std::vector<unsigned char> m_identityPublicKey;
    
    int m_activeGroupId;
    std::string m_activeGroupName;

    P2PSendCallback m_p2pSender;
    SyncCallback m_syncCb;
};

} // namespace Core
} // namespace CipherMesh

#endif