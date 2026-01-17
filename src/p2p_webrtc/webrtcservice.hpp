#ifndef WEBRTCSERVICE_HPP
#define WEBRTCSERVICE_HPP

#include "ip2pservice.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

// Use angle brackets for library includes
#include <rtc/rtc.hpp> 

// =========================================================
//  ANDROID HEADER (Standard C++)
// =========================================================
#if defined(__ANDROID__) || defined(ANDROID)

#include <unordered_map>
#include <map> 
#include <mutex>

class WebRTCService : public CipherMesh::P2P::IP2PService {
public:
    // [FIX] Added optional parent argument to match native-lib.cpp call
    WebRTCService(const std::string& signalingUrl, const std::string& localUserId, void* parent = nullptr);
    ~WebRTCService() override;

    // --- Core P2P Interface ---
    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey,
                    const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
    
    // [FIX] Added queueInvite (same signature as inviteUser) for native-lib compatibility
    void queueInvite(const std::string& groupName, const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey,
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);

    void sendInvite(const std::string& recipientId, const std::string& groupName) override;
    void cancelInvite(const std::string& userId) override;
    void respondToInvite(const std::string& senderId, bool accept) override;
    void requestData(const std::string& senderId, const std::string& groupName) override;
    void sendGroupData(const std::string& recipientId, const std::string& groupName,
                       const std::vector<unsigned char>& groupKey,
                       const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
                       
    void fetchGroupMembers(const std::string& groupName) override;
    void removeUser(const std::string& groupName, const std::string& userId) override;

    // --- Connection Management ---
    void connect();
    void disconnect();
    
    void setAuthenticated(bool auth);
    void onRetryTimer();
    void broadcastSync(const std::string& payload);
    
    // [FIX] Renamed from handleSignalingMessage to match native-lib.cpp
    void receiveSignalingMessage(const std::string& message);

    // Callbacks
    std::function<void(bool)> onConnectionStateChange;
    std::function<void(const std::string&)> onSyncMessage;
    std::function<void(const std::string&)> onPeerOnline;
    std::function<void(const std::string& senderId, const std::string& groupName)> onIncomingInvite;
    std::function<void(const std::string& senderId, const std::string& groupData)> onGroupDataReceived;
    
    // [FIX] Renamed from onSendSignaling to match native-lib.cpp usage
    // Signature: (target, type, payload)
    std::function<void(std::string, std::string, std::string)> onSignalingMessage;

private:
    std::string m_signalingUrl;
    std::string m_localUserId;
    std::shared_ptr<rtc::WebSocket> m_ws;
    bool m_isConnected;
    bool m_isAuthenticated; 

    // P2P Objects
    std::unordered_map<std::string, std::shared_ptr<rtc::PeerConnection>> m_peers;
    std::unordered_map<std::string, std::shared_ptr<rtc::DataChannel>> m_channels;
    
    // Storage for Pending Operations
    std::map<std::string, std::string> m_pendingInvites; 
    std::map<std::string, std::vector<unsigned char>> m_pendingKeys;
    std::map<std::string, std::vector<CipherMesh::Core::VaultEntry>> m_pendingEntries;
    
    std::map<std::string, std::vector<std::string>> m_earlyCandidates;

    std::mutex m_mutex;

    // Internal Helpers
    void setupPeerConnection(const std::string& peerId, bool isOfferer);
    
    void setupDataChannel(std::shared_ptr<rtc::DataChannel> dc, const std::string& peerId);
    void sendSignalingMessage(const std::string& targetId, const std::string& type, const std::string& payload);
    void retryPendingInviteFor(const std::string& remoteId);
    void flushEarlyCandidatesFor(const std::string& peerId);
    void sendGroupData_unsafe(const std::string& recipientId, const std::string& groupName, 
                              const std::vector<unsigned char>& groupKey, 
                              const std::vector<CipherMesh::Core::VaultEntry>& entries);
};

// =========================================================
//  DESKTOP HEADER (Qt)
// =========================================================
#else

#include <QObject>
#include <QWebSocket>
#include <QMap>
#include <QTimer>
#include <QString>
#include <QJsonObject>

class WebRTCService : public QObject, public CipherMesh::P2P::IP2PService {
    Q_OBJECT

public:
    WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent = nullptr);
    ~WebRTCService() override;

    void setIdentityPublicKey(const std::string& pubKey); 

    // --- IP2PService Interface Implementation ---
    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey,
                    const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
    
    void sendInvite(const std::string& recipientId, const std::string& groupName) override;
    void cancelInvite(const std::string& userId) override;
    void respondToInvite(const std::string& senderId, bool accept) override;
    void requestData(const std::string& senderId, const std::string& groupName) override;
    void sendGroupData(const std::string& recipientId, const std::string& groupName,
                       const std::vector<unsigned char>& groupKey,
                       const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
                       
    void fetchGroupMembers(const std::string& groupName) override;
    void removeUser(const std::string& groupName, const std::string& userId) override;
    
    void queueInvite(const std::string& groupName, const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey,
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);

public slots:
    void startSignaling();
    void setAuthenticated(bool authenticated); 

private slots:
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessageReceived(const QString& message);
    void onRetryTimer();
    void sendOnlinePing();
    void checkAndSendPendingInvites();

private:
    QString m_signalingUrl;
    QString m_localUserId;
    std::string m_identityPublicKey; 
    QWebSocket *m_webSocket;
    bool m_isAuthenticated;
    int m_reconnectAttempts;

    QMap<QString, std::shared_ptr<rtc::PeerConnection>> m_peerConnections;
    QMap<QString, std::shared_ptr<rtc::DataChannel>> m_dataChannels;
    QMap<QString, QTimer*> m_watchdogTimers; 
    
    QMap<QString, QString> m_pendingInvites; 
    std::map<QString, std::vector<unsigned char>> m_pendingKeys;
    std::map<QString, std::vector<CipherMesh::Core::VaultEntry>> m_pendingEntries;
    
    QMap<QString, std::vector<QJsonObject>> m_earlyCandidates;

    rtc::Configuration getIceConfiguration() const;
    void setupPeerConnection(const QString& remoteId, bool isOfferer);
    void createAndSendOffer(const QString& remoteId);
    void sendSignalingMessage(const QString& targetId, const QJsonObject& payload);
    void handleOffer(const QJsonObject& obj);
    void handleAnswer(const QJsonObject& obj);
    void handleCandidate(const QJsonObject& obj);
    void handleP2PMessage(const QString& remoteId, const QString& message);
    void sendP2PMessage(const QString& remoteId, const QJsonObject& payload);
    void flushEarlyCandidatesFor(const QString& peerId);
    void retryPendingInviteFor(const QString& remoteId);
};

#endif // DESKTOP check
#endif // WEBRTCSERVICE_HPP