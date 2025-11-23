#pragma once
#include <string>
#include <vector>
#include <functional> 
#include "vault_entry.hpp"

namespace CipherMesh {
namespace P2P {

struct GroupMember {
    std::string id;
    std::string displayName;
    std::string status; 
};

class IP2PService {
public:
    virtual ~IP2PService();

    std::function<void(const std::vector<GroupMember>& members)> onGroupMembersUpdated;
    std::function<void(bool success, const std::string& message)> onInviteStatus;
    std::function<void(bool success, const std::string& message)> onRemoveStatus;
    std::function<void(const std::string& userId, bool isOnline)> onUserStatusResult;
    // ...
    std::function<void(const std::string& userId)> onPeerOnline; // NEW
    std::function<void(const std::string& requesterId, const std::string& groupName)> onDataRequested; // NEW
    // ...
    std::function<void(const std::string& senderId, const std::string& groupName)> onIncomingInvite;
    std::function<void(const std::string& senderId)> onInviteCancelled;
    std::function<void(const std::string& userId, const std::string& groupName, bool accepted)> onInviteResponse;
    std::function<void(bool connected)> onConnectionStatusChanged;  // NEW: Connection status callback
    
    // UPDATED: Added 'senderId' as the first argument
    std::function<void(const std::string& senderId,
                       const std::string& groupName, 
                       const std::vector<unsigned char>& key, 
                       const std::vector<CipherMesh::Core::VaultEntry>& entries)> onGroupDataReceived;

    virtual void fetchGroupMembers(const std::string& groupName) = 0;
    virtual void inviteUser(const std::string& groupName, const std::string& userEmail, 
                            const std::vector<unsigned char>& groupKey,
                            const std::vector<CipherMesh::Core::VaultEntry>& entries) = 0;
    virtual void removeUser(const std::string& groupName, const std::string& userId) = 0;
    virtual void checkUserAvailability(const std::string& userId) = 0;
    virtual void respondToInvite(const std::string& senderId, bool accept) = 0;
    virtual void cancelInvite(const std::string& userId) = 0;
    virtual void sendGroupData(const std::string& recipientId, 
                               const std::string& groupName,
                               const std::vector<unsigned char>& groupKey,
                               const std::vector<CipherMesh::Core::VaultEntry>& entries) = 0;
    virtual void requestData(const std::string& senderId, const std::string& groupName) = 0;
};

} 
}