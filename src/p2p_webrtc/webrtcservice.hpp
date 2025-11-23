#pragma once

#include <QObject>
#include <QTimer> 
#include <QJsonDocument>
#include <QJsonObject>
#include <map>
#include <memory> 
#include <vector> 

#include <QtWebSockets/QWebSocket> 

#include "ip2pservice.hpp" 
#include "rtc/rtc.hpp"     

class WebRTCService : public QObject, public CipherMesh::P2P::IP2PService {
    Q_OBJECT
public:
    explicit WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent = nullptr);
    ~WebRTCService() override;

    void fetchGroupMembers(const std::string& groupName) override;
    
    void inviteUser(const std::string& groupName, 
                    const std::string& userEmail, 
                    const std::vector<unsigned char>& groupKey,
                    const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
                    
    void removeUser(const std::string& groupName, const std::string& userId) override;
    void checkUserAvailability(const std::string& userId) override;
    void respondToInvite(const std::string& senderId, bool accept) override;
    void cancelInvite(const std::string& userId) override;
    void sendGroupData(const std::string& recipientId, 
                       const std::string& groupName,
                       const std::vector<unsigned char>& groupKey,
                       const std::vector<CipherMesh::Core::VaultEntry>& entries) override;
    void requestData(const std::string& senderId, const std::string& groupName) override;
    // Add to retry loop without sending immediate offer
    void queueInvite(const std::string& groupName, 
                     const std::string& userEmail, 
                     const std::vector<unsigned char>& groupKey,
                     const std::vector<CipherMesh::Core::VaultEntry>& entries);
    void sendOnlinePing();
    void checkAndSendPendingInvites();

public slots: 
    void startSignaling(); 
    void onWsConnected();
    void onWsDisconnected();
    void onWsTextMessageReceived(const QString& message);
    
    // Retry Slot
    void onRetryTimer();
    
    // Authentication slot
    void setAuthenticated(bool authenticated);

private:
    void sendSignalingMessage(const QString& targetId, const QJsonObject& payload);
    void handleRegistration(const QJsonObject& message);
    void handleOffer(const QJsonObject& message);
    void handleAnswer(const QJsonObject& message);
    void handleCandidate(const QJsonObject& message);
    void handleOnlinePing(const QJsonObject& message);
    void sendP2PMessage(const QString& remoteId, const QJsonObject& payload);
    void handleP2PMessage(const QString& remoteId, const QString& message);
    void setupPeerConnection(const QString& remoteId, bool isOfferer);
    void createAndSendOffer(const QString& remoteId);
    void flushEarlyCandidatesFor(const QString& peerId);
    void retryPendingInviteFor(const QString& remoteId);
    bool isDuplicateOnlineNotification(const QString& userId);
    
    QWebSocket* m_webSocket;
    QString m_signalingUrl;
    QString m_localUserId; 
    
    // Retry Timer
    QTimer* m_retryTimer;
    
    QMap<QString, std::shared_ptr<rtc::PeerConnection>> m_peerConnections;
    QMap<QString, std::shared_ptr<rtc::DataChannel>> m_dataChannels;
    
    QMap<QString, QString> m_pendingInvites; 
    std::map<QString, std::vector<unsigned char>> m_pendingKeys; 
    std::map<QString, std::vector<CipherMesh::Core::VaultEntry>> m_pendingEntries; 

    QMap<QString, std::vector<QJsonObject>> m_earlyCandidates;
    
    bool m_isAuthenticated;  // Track if user has authenticated with master password
    QMap<QString, qint64> m_recentOnlineNotifications;  // Track recent online notifications to filter duplicates
    QMap<QString, QTimer*> m_watchdogTimers;  // Track watchdog timers to prevent race conditions
    
    // Reconnection management
    int m_reconnectAttempts;
    int m_reconnectDelay;

    rtc::Configuration getIceConfiguration() const;
};