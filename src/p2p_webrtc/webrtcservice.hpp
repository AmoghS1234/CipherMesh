#ifndef CIPHERMESH_WEBRTC_SERVICE_HPP
#define CIPHERMESH_WEBRTC_SERVICE_HPP

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <map>
#include "vault_entry.hpp" 
#include "ip2pservice.hpp" // Ensure this is included for both platforms

// Platform detection
#if defined(__ANDROID__) || defined(ANDROID)
    #include "rtc/rtc.hpp"
#else
    #include <QObject>
    #include <QWebSocket>
    #include <QTimer>
    #include <QJsonObject>
    #include "rtc/rtc.hpp"
#endif

// Shared Callback Types
using SignalingCallback = std::function<void(const std::string& target, const std::string& type, const std::string& payload)>;
using DataCallback = std::function<void(const std::string& sender, const std::string& message)>;
using ConnectionStatusCallback = std::function<void(bool connected)>;
using IncomingInviteCallback = std::function<void(const std::string& sender, const std::string& groupName)>;
using InviteResponseCallback = std::function<void(const std::string& sender, const std::string& groupName, bool accepted)>;

#if defined(__ANDROID__) || defined(ANDROID)

// =========================================================
//  ANDROID HEADER (std::string ONLY)
// =========================================================
class WebRTCService : public CipherMesh::P2P::IP2PService {
public:
    WebRTCService(const std::string& signalingUrl, const std::string& localUserId, void* parent = nullptr);
    ~WebRTCService() override;

    void connect();
    void disconnect();
    void sendP2PMessage(const std::string& targetId, const std::string& jsonPayload);
    void receiveSignalingMessage(const std::string& message);
    
    // Public Callbacks
    SignalingCallback onSignalingMessage;
    DataCallback onGroupDataReceived;
    DataCallback onSyncMessage; 
    ConnectionStatusCallback onConnectionStatusChanged;
    IncomingInviteCallback onIncomingInvite;
    InviteResponseCallback onInviteResponse;
    std::function<void(const std::string& userId)> onPeerOnline;

    // IP2PService Interface Implementation
    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey, 
                    const std::vector<CipherMesh::Core::VaultEntry>& entries,
                    const std::string& memberListJson) override;

    void sendInvite(const std::string& recipientId, const std::string& groupName) override;
    void cancelInvite(const std::string& userId) override;
    void respondToInvite(const std::string& senderId, bool accept) override;
    void removeUser(const std::string& groupName, const std::string& userId) override;
    void fetchGroupMembers(const std::string& groupName) override;
    void requestData(const std::string& senderId, const std::string& groupName) override;
    void sendGroupData(const std::string& recipientId, const std::string& groupName, 
                       const std::vector<unsigned char>& groupKey, 
                       const std::vector<CipherMesh::Core::VaultEntry>& entries,
                       const std::string& memberListJson) override;

    // Helper Actions
    void queueInvite(const std::string& groupName, const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey, 
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);
    void setAuthenticated(bool auth);
    void broadcastSync(const std::string& payload);
    void onRetryTimer();

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

    // [FIX] Correct type: std::string
    void setupPeerConnection(const std::string& peerId, bool isCaller);
    
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

// =========================================================
//  DESKTOP HEADER (Qt / QString)
// =========================================================
class WebRTCService : public QObject, public CipherMesh::P2P::IP2PService {
    Q_OBJECT
public:
    explicit WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent = nullptr);
    ~WebRTCService() override;

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

    // IP2PService Interface
    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey, 
                    const std::vector<CipherMesh::Core::VaultEntry>& entries,
                    const std::string& memberListJson) override;

    void sendInvite(const std::string& recipientId, const std::string& groupName) override;
    void cancelInvite(const std::string& userId) override;
    void respondToInvite(const std::string& senderId, bool accept) override;
    void removeUser(const std::string& groupName, const std::string& userId) override;
    void fetchGroupMembers(const std::string& groupName) override;
    void requestData(const std::string& senderId, const std::string& groupName) override;
    void sendGroupData(const std::string& recipientId, const std::string& groupName, 
                       const std::vector<unsigned char>& groupKey, 
                       const std::vector<CipherMesh::Core::VaultEntry>& entries,
                       const std::string& memberListJson) override;

    // Helper Actions
    void broadcastSync(const std::string& groupName);
    void onRetryTimer();
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
    
    // [FIX] Correct type: QString for Desktop
    void setupPeerConnection(const QString& remoteId, bool isOfferer);
};
#endif

#endif