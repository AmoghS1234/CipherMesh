#ifndef CIPHERMESH_P2P_IP2PSERVICE_HPP
#define CIPHERMESH_P2P_IP2PSERVICE_HPP

#include <string>
#include <vector>
#include <functional>
#include <utility>
#include "../core/vault_entry.hpp" 

namespace CipherMesh {
namespace P2P {

class IP2PService {
public:
    virtual ~IP2PService() = default;

    // Core Methods
    virtual void inviteUser(const std::string& groupName, const std::string& userEmail, 
                            const std::vector<unsigned char>& groupKey,
                            const std::vector<CipherMesh::Core::VaultEntry>& entries,
                            const std::string& memberListJson) = 0;
                            
    virtual void sendInvite(const std::string& recipientId, const std::string& groupName) = 0;
    virtual void cancelInvite(const std::string& userId) = 0;
    virtual void respondToInvite(const std::string& senderId, bool accept) = 0;
    virtual void requestData(const std::string& senderId, const std::string& groupName) = 0;
    
    virtual void sendGroupData(const std::string& recipientId, const std::string& groupName,
                               const std::vector<unsigned char>& groupKey,
                               const std::vector<CipherMesh::Core::VaultEntry>& entries,
                               const std::string& memberListJson) = 0;
                               
    virtual void fetchGroupMembers(const std::string& groupName) = 0;
    virtual void removeUser(const std::string& groupName, const std::string& userId) = 0;

    // Callbacks
    std::function<void(bool)> onConnectionStatusChanged;
    std::function<void(std::string, std::string)> onIncomingInvite; 
    std::function<void(std::string)> onInviteCancelled; 
    std::function<void(std::string, std::string, bool)> onInviteResponse; 
    
    // [FIX] Changed to 2 arguments (Sender ID, JSON Payload) for flexibility
    std::function<void(std::string, std::string)> onGroupDataReceived;
    
    std::function<void(std::string)> onPeerOnline;
    std::function<void(std::string)> onSyncMessage;
    std::function<void(std::string, std::string, std::string)> onDataRequested;
    std::function<void(std::string, bool)> onUserStatusResult;
    std::function<void(bool, std::string)> onInviteStatus;
    std::function<void(bool, std::string)> onRemoveStatus;
    std::function<void(std::vector<std::pair<std::string, std::string>>)> onGroupMembersUpdated;
};

} // namespace P2P
} // namespace CipherMesh

#endif // IP2PSERVICE_HPP