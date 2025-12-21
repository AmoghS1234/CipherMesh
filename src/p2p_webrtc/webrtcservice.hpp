#ifndef WEBRTCSERVICE_HPP
#define WEBRTCSERVICE_HPP

#include "../p2p/ip2pservice.hpp" 
#include <QObject>
#include <QWebSocket>
#include <QMap>
#include <QTimer>
#include <QString>
#include <memory>
#include "rtc/rtc.hpp"

// WebRTCService inherits from QObject (for slots/signals) and your P2P Interface
class WebRTCService : public QObject, public CipherMesh::P2P::IP2PService {
    Q_OBJECT

public:
    WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent = nullptr);
    ~WebRTCService() override;

    // Setter for the Identity Key (used for handshake security)
    void setIdentityPublicKey(const std::string& pubKey); 

    // --- IP2PService Interface Implementation ---
    void inviteUser(const std::string& groupName, const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey,
                    const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
    
    void cancelInvite(const std::string& userId) override;
    
    void respondToInvite(const std::string& senderId, bool accept) override;
    
    // Request data from a peer (includes sending our Public Key)
    void requestData(const std::string& senderId, const std::string& groupName) override;
    
    // Send encrypted data to a peer
    void sendGroupData(const std::string& recipientId, const std::string& groupName,
                       const std::vector<unsigned char>& groupKey,
                       const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
                       
    void fetchGroupMembers(const std::string& groupName) override;
    void removeUser(const std::string& groupName, const std::string& userId) override;
    
    // Helper to queue invites for offline users
    void queueInvite(const std::string& groupName, const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey,
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);

public slots:
    // Slots required for cross-thread invocation
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
    int m_reconnectDelay;

    // WebRTC Objects
    QMap<QString, std::shared_ptr<rtc::PeerConnection>> m_peerConnections;
    QMap<QString, std::shared_ptr<rtc::DataChannel>> m_dataChannels;
    QMap<QString, QTimer*> m_watchdogTimers; 
    
    // State for Pending Operations
    QMap<QString, QString> m_pendingInvites; // userId -> groupName
    std::map<QString, std::vector<unsigned char>> m_pendingKeys;
    std::map<QString, std::vector<CipherMesh::Core::VaultEntry>> m_pendingEntries;
    
    // ICE Candidate Buffer (for when candidates arrive before Offer/Answer)
    QMap<QString, std::vector<QJsonObject>> m_earlyCandidates;
    QMap<QString, qint64> m_recentOnlineNotifications;

    // Internal Helper Methods
    rtc::Configuration getIceConfiguration() const;
    void setupPeerConnection(const QString& remoteId, bool isOfferer);
    void createAndSendOffer(const QString& remoteId);
    void sendSignalingMessage(const QString& targetId, const QJsonObject& payload);
    void handleOffer(const QJsonObject& obj);
    void handleAnswer(const QJsonObject& obj);
    void handleCandidate(const QJsonObject& obj);
    void handleOnlinePing(const QJsonObject& obj);
    void handleP2PMessage(const QString& remoteId, const QString& message);
    void sendP2PMessage(const QString& remoteId, const QJsonObject& payload);
    void flushEarlyCandidatesFor(const QString& peerId);
    bool isDuplicateOnlineNotification(const QString& userId);
    void retryPendingInviteFor(const QString& remoteId);
};

#endif // WEBRTCSERVICE_HPP