#ifndef CIPHERMESH_WEBRTC_SERVICE_HPP
#define CIPHERMESH_WEBRTC_SERVICE_HPP

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <map>
#include "vault_entry.hpp" 

// Platform detection
#if defined(__ANDROID__) || defined(ANDROID)
    #include "rtc/rtc.hpp"
#else
    #include <QObject>
    #include <QWebSocket>
    #include <QTimer>
    #include <QJsonObject>
    #include "rtc/rtc.hpp"
    #include "ip2pservice.hpp"
#endif

// Shared Callback Types
using SignalingCallback = std::function<void(const std::string& target, const std::string& type, const std::string& payload)>;
using DataCallback = std::function<void(const std::string& sender, const std::string& message)>;
using ConnectionStatusCallback = std::function<void(bool connected)>;
using IncomingInviteCallback = std::function<void(const std::string& sender, const std::string& groupName)>;
using InviteResponseCallback = std::function<void(const std::string& sender, const std::string& groupName, bool accepted)>;
static std::string escapeJsonString(const std::string& input);

#if defined(__ANDROID__) || defined(ANDROID)

// ... (Android Implementation remains unchanged) ...
class WebRTCService {
public:
    WebRTCService(const std::string& signalingUrl, const std::string& localUserId, void* parent = nullptr);
    ~WebRTCService();

    void connect();
    void disconnect();
    void sendP2PMessage(const std::string& targetId, const std::string& jsonPayload);
    void receiveSignalingMessage(const std::string& message);
    
    SignalingCallback onSignalingMessage;
    DataCallback onGroupDataReceived;
    DataCallback onSyncMessage; 
    ConnectionStatusCallback onConnectionStatusChanged;
    IncomingInviteCallback onIncomingInvite;
    InviteResponseCallback onInviteResponse;
    
    std::function<void(const std::string& userId)> onPeerOnline;

    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey, 
                    const std::vector<CipherMesh::Core::VaultEntry>& entries,
                    const std::string& memberListJson);

    void queueInvite(const std::string& groupName, const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey, 
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);

    void respondToInvite(const std::string& senderId, bool accept);
    void setAuthenticated(bool auth);
    void sendInvite(const std::string& recipientId, const std::string& groupName);
    void cancelInvite(const std::string& userId);
    void removeUser(const std::string& groupName, const std::string& userId);
    void fetchGroupMembers(const std::string& groupName);
    void requestData(const std::string& senderId, const std::string& groupName);
    void broadcastSync(const std::string& groupName);
    void onRetryTimer();

    void sendGroupData(const std::string& recipientId, const std::string& groupName, 
                       const std::vector<unsigned char>& groupKey, 
                       const std::vector<CipherMesh::Core::VaultEntry>& entries,
                       const std::string& memberListJson);

private:
    std::string m_signalingUrl;
    std::string m_localUserId;
    bool m_isConnected;
    bool m_isAuthenticated;
    
    std::recursive_mutex m_mutex;
    std::map<std::string, std::shared_ptr<rtc::PeerConnection>> m_peers;
    std::map<std::string, std::shared_ptr<rtc::DataChannel>> m_channels;
    std::map<std::string, std::vector<std::string>> m_earlyCandidates;
    
    std::map<std::string, std::string> m_pendingInvites;
    std::map<std::string, std::vector<unsigned char>> m_pendingKeys;
    std::map<std::string, std::vector<CipherMesh::Core::VaultEntry>> m_pendingEntries;
    std::map<std::string, std::string> m_pendingMembers;

    void setupPeerConnection(const QString& peerId, bool isCaller);
    void setupDataChannel(std::shared_ptr<rtc::DataChannel> dc, const std::string& peerId);
    void sendSignalingMessage(const std::string& targetId, const std::string& type, const std::string& payload);
    void retryPendingInviteFor(const std::string& remoteId);
    void flushEarlyCandidatesFor(const std::string& peerId);
    void sendGroupData_unsafe(const std::string& recipientId, const std::string& groupName, 
                              const std::vector<unsigned char>& groupKey, 
                              const std::vector<CipherMesh::Core::VaultEntry>& entries,
                              const std::string& memberListJson);
};

#else

// Desktop (Qt)
class WebRTCService : public QObject, public CipherMesh::P2P::IP2PService {
    Q_OBJECT
public:
    explicit WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent = nullptr);
    ~WebRTCService();

    // Callbacks
    SignalingCallback onSignalingMessage;
    DataCallback onGroupDataReceived;
    DataCallback onSyncMessage;
    ConnectionStatusCallback onConnectionStatusChanged;
    IncomingInviteCallback onIncomingInvite;
    InviteResponseCallback onInviteResponse;
    
    std::function<void(const std::string&)> onInviteCancelled;
    std::function<void(const std::string&, const std::string&, const std::string&)> onDataRequested;
    std::function<void(const std::string& userId)> onPeerOnline;

    void receiveSignalingMessage(const std::string& message); 

    // Actions
    void sendInvite(const std::string& recipientId, const std::string& groupName) override;
    void cancelInvite(const std::string& userId) override;
    void respondToInvite(const std::string& senderId, bool accept) override;
    void removeUser(const std::string& groupName, const std::string& userId) override;
    void fetchGroupMembers(const std::string& groupName) override;
    void requestData(const std::string& senderId, const std::string& groupName) override;
    
    void broadcastSync(const std::string& groupName);
    void onRetryTimer();

    void sendGroupData(const std::string& recipientId, const std::string& groupName, 
                       const std::vector<unsigned char>& groupKey, 
                       const std::vector<CipherMesh::Core::VaultEntry>& entries,
                       const std::string& memberListJson) override;
                       
    void queueInvite(const std::string& groupName, const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey, 
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);

    void sendP2PMessage(const QString& remoteId, const QJsonObject& payload);

public slots:
    void startSignaling();
    void setIdentityPublicKey(const std::string& pubKey);
    void setAuthenticated(bool authenticated);

    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessageReceived(const QString& message);
    void sendOnlinePing();
    void checkAndSendPendingInvites();

private:
    QString m_signalingUrl;
    QString m_localUserId;
    std::string m_identityPublicKey;
    bool m_isAuthenticated;
    int m_reconnectAttempts;

    QWebSocket* m_webSocket;
    
    QMap<QString, std::shared_ptr<rtc::PeerConnection>> m_peerConnections;
    QMap<QString, std::shared_ptr<rtc::DataChannel>> m_dataChannels;
    QMap<QString, std::vector<QJsonObject>> m_earlyCandidates;

    // Pending Invites
    QMap<QString, QString> m_pendingInvites;
    std::map<QString, std::vector<unsigned char>> m_pendingKeys;
    std::map<QString, std::vector<CipherMesh::Core::VaultEntry>> m_pendingEntries;
    QMap<QString, QString> m_pendingMembers;

    struct IncomingGroupData {
        QString groupName;
        std::vector<unsigned char> groupKey;
        std::vector<CipherMesh::Core::VaultEntry> entries;
        QTimer* completionTimer = nullptr;
        bool hasHeader = false;
    };
    QMap<QString, IncomingGroupData> m_incomingGroups;

    rtc::Configuration getIceConfiguration() const;
    void sendSignalingMessage(const QString& targetId, const QJsonObject& payload);
    void retryPendingInviteFor(const QString& remoteId);
    void handleOffer(const QJsonObject& obj);
    void handleAnswer(const QJsonObject& obj);
    void handleCandidate(const QJsonObject& obj);
    void flushEarlyCandidatesFor(const QString& peerId);
    
    void handleP2PMessage(const QString& remoteId, const QString& message);
    void finalizeIncomingGroupData(const QString& remoteId);
    void createAndSendOffer(const QString& remoteId);
    
    // [FIX] Added missing declaration for setupPeerConnection
    void setupPeerConnection(const QString& remoteId, bool isOfferer);
    
    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey, 
                    const std::vector<CipherMesh::Core::VaultEntry>& entries,
                    const std::string& memberListJson);
};
#endif

#endif