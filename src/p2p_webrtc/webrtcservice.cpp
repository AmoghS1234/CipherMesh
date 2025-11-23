#include "webrtcservice.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QTimer>
#include <QJsonArray>
#include <QAbstractSocket>
#include <QThread>
#include <QMetaObject>
#include <QDateTime>
#include <utility>
#include <string> 

// Production STUN Server (Google)
const QString STUN_SERVER_URL = "stun:stun.l.google.com:19302"; 

// Optional: Add TURN credentials here if you have them for strict networks
// const QString TURN_SERVER_URL = "turn:openrelay.metered.ca:80"; 
// const QString TURN_USERNAME = "openrelayproject";
// const QString TURN_CREDENTIAL = "openrelayproject";

// Timing constants for offline handling
const int ONLINE_PING_DELAY_MS = 500;      // Delay before sending online-ping after connection
const int PENDING_INVITES_CHECK_DELAY_MS = 1000;  // Delay before checking pending invites
const int OFFLINE_DETECTION_TIMEOUT_MS = 10000;   // Timeout to detect offline users
const int DATA_CHANNEL_RETRY_DELAY_MS = 2000;     // Delay before retrying data channel message send

WebRTCService::WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent)
    : QObject(parent), m_signalingUrl(signalingUrl), m_localUserId(QString::fromStdString(localUserId)), m_isAuthenticated(false), m_reconnectAttempts(0), m_reconnectDelay(5000)
{
    // Enable internal logging. 'Info' is good for production debugging without spam.
    rtc::InitLogger(rtc::LogLevel::Info); 

    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    // Handle SSL errors for secure wss:// connections
    connect(m_webSocket, &QWebSocket::sslErrors, this, [](const QList<QSslError> &errors){
        for(const auto &e : errors) qDebug() << "SSL Error:" << e.errorString();
    });

    connect(m_webSocket, &QWebSocket::connected, this, &WebRTCService::onWsConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebRTCService::onWsDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &WebRTCService::onWsTextMessageReceived);
}

WebRTCService::~WebRTCService() {
    if (m_webSocket) {
        m_webSocket->close();
    }
    
    // Clean up watchdog timers
    for (auto timer : m_watchdogTimers) {
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }
    }
    m_watchdogTimers.clear();
    
    // Explicitly clear maps to release shared_ptrs and close connections immediately
    m_dataChannels.clear();
    m_peerConnections.clear();
    qDebug() << "DEBUG: WebRTCService destroyed and P2P resources cleaned up.";
}

void WebRTCService::startSignaling() {
    qDebug() << "DEBUG: Connecting to Signaling Server:" << m_signalingUrl;
    if (m_webSocket) m_webSocket->open(QUrl(m_signalingUrl));
}

rtc::Configuration WebRTCService::getIceConfiguration() const {
    rtc::Configuration config;
    
    // --- PRODUCTION SETTINGS ---
    
    // 1. Enable STUN: Required for finding Public IP
    config.iceServers.emplace_back(STUN_SERVER_URL.toStdString());
    
    // 2. Port range for ICE candidates (helps with firewalls)
    config.portRangeBegin = 49152;
    config.portRangeEnd = 65535;

    return config;
}

void WebRTCService::onWsConnected() {
    qDebug() << "DEBUG: WebSocket Connected!";
    
    // Notify UI of connection status
    if (onConnectionStatusChanged) {
        onConnectionStatusChanged(true);
    }
    
    // Reset reconnection tracking on successful connection
    m_reconnectAttempts = 0;
    m_reconnectDelay = 5000;
    
    // Only register and broadcast online status if user is authenticated
    if (m_isAuthenticated) {
        qDebug() << "DEBUG: User is authenticated, sending registration and online ping";
        
        QJsonObject registration;
        registration["type"] = "register";
        registration["id"] = m_localUserId;
        QJsonDocument doc(registration);
        m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        qDebug() << "DEBUG: Sent registration for" << m_localUserId;
        
        QTimer::singleShot(ONLINE_PING_DELAY_MS, this, &WebRTCService::sendOnlinePing);
        QTimer::singleShot(PENDING_INVITES_CHECK_DELAY_MS, this, &WebRTCService::checkAndSendPendingInvites);
    } else {
        qDebug() << "DEBUG: User not authenticated yet, deferring registration and online ping";
    }
}

void WebRTCService::onWsDisconnected() {
    qDebug() << "DEBUG: WebSocket disconnected";
    
    // Notify UI of disconnection
    if (onConnectionStatusChanged) {
        onConnectionStatusChanged(false);
    }
    
    m_reconnectAttempts++;
    
    // Cap maximum reconnection attempts at 10
    if (m_reconnectAttempts > 10) {
        qCritical() << "DEBUG: Max reconnection attempts reached. Stopping reconnection.";
        return;
    }
    
    // Use exponential backoff: 5s, 10s, 20s, 40s, up to 60s max
    int delay = qMin(m_reconnectDelay * m_reconnectAttempts, 60000);
    qDebug() << "DEBUG: Reconnection attempt" << m_reconnectAttempts << "in" << (delay/1000) << "seconds...";
    
    QTimer::singleShot(delay, this, [this]() { 
        if(m_webSocket) m_webSocket->open(QUrl(m_signalingUrl)); 
    });
}

void WebRTCService::checkUserAvailability(const std::string& userId) {
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) return;
    QJsonObject req;
    req["type"] = "check-user";
    req["target"] = QString::fromStdString(userId);
    QJsonDocument doc(req);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void WebRTCService::onWsTextMessageReceived(const QString& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "WARNING: Received invalid or non-JSON message:" << message;
        return;
    }

    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "user-status") {
        std::string target = obj["target"].toString().toStdString();
        bool isOnline = (obj["status"].toString() == "online");
        if (onUserStatusResult) onUserStatusResult(target, isOnline);
    } 
    else if (type == "offer") {
        handleOffer(obj);
    }
    else if (type == "answer") {
        handleAnswer(obj);
    }
    else if (type == "ice-candidate") {
        handleCandidate(obj);
    }
    else if (type == "online-ping") {
        handleOnlinePing(obj);
    }
    else if (type == "error") {
        qCritical() << "DEBUG: Server Error:" << obj["message"].toString();
    }
    else if (type == "user-online") {
        QString user = obj["user"].toString();
        
        // Simple debounce to avoid spamming logs/logic if server sends multiple notifications
        static QMap<QString, qint64> lastOnlineTime;
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (lastOnlineTime.contains(user) && (now - lastOnlineTime[user] < 2000)) {
            // Ignore duplicate within 2 seconds
            return;
        }
        lastOnlineTime[user] = now;
        
        qDebug() << "DEBUG: Peer came online:" << user;
        
        // Notify UI (so bob can auto-reply if needed)
        if (onPeerOnline) onPeerOnline(user.toStdString());
        
        // SENDER LOGIC: Check if we have pending invites for this user and resend
        if (m_pendingInvites.contains(user)) {
            qDebug() << "DEBUG: Found pending invite for" << user << "- Retrying now!";
            
            // 1. Clean up stale connection
            if (m_peerConnections.contains(user)) {
                // Only reset if not already connected/connecting
                auto pc = m_peerConnections[user];
                if (pc->state() != rtc::PeerConnection::State::Connected) {
                    m_peerConnections.remove(user);
                    m_dataChannels.remove(user);
                    m_earlyCandidates.remove(user);
                } else {
                    qDebug() << "DEBUG: Already connected to" << user << "- skipping retry.";
                    return;
                }
            }

            // 2. Resend Invite
            QString groupName = m_pendingInvites[user];
            // Access maps using QString key directly
            auto key = m_pendingKeys[user];
            auto entries = m_pendingEntries[user];
            
            inviteUser(groupName.toStdString(), user.toStdString(), key, entries);
        }
    }
}

// --- P2P Messaging Logic ---

void WebRTCService::inviteUser(const std::string& groupName, const std::string& userEmail, 
                               const std::vector<unsigned char>& groupKey,
                               const std::vector<CipherMesh::Core::VaultEntry>& entries) 
{
    QString remoteId = QString::fromStdString(userEmail);
    qDebug() << "DEBUG: inviteUser called. Target:" << remoteId;

    // Clean up any existing connection/invite for this user to allow re-invitation
    if (m_peerConnections.contains(remoteId)) {
        qDebug() << "DEBUG: Cleaning up existing connection for re-invitation to" << remoteId;
        auto pc = m_peerConnections[remoteId];
        if (pc) {
            pc->close();
        }
        m_peerConnections.remove(remoteId);
    }
    
    if (m_dataChannels.contains(remoteId)) {
        auto dc = m_dataChannels[remoteId];
        if (dc && dc->isOpen()) {
            try {
                dc->close();
            } catch (const std::exception& e) {
                qWarning() << "DEBUG: Exception closing data channel:" << e.what();
            }
        }
        m_dataChannels.remove(remoteId);
    }
    
    // Cancel any existing watchdog timer
    if (m_watchdogTimers.contains(remoteId)) {
        m_watchdogTimers[remoteId]->stop();
        m_watchdogTimers[remoteId]->deleteLater();
        m_watchdogTimers.remove(remoteId);
    }
    
    // Clear any early candidates
    m_earlyCandidates.remove(remoteId);

    // Store pending invite data
    m_pendingInvites[remoteId] = QString::fromStdString(groupName);
    m_pendingKeys[remoteId] = groupKey;
    m_pendingEntries[remoteId] = entries; 
    qDebug() << "DEBUG: Stored pending invite. Total pending invites:" << m_pendingInvites.size();
    
    // Try to establish connection immediately
    QMetaObject::invokeMethod(this, [this, remoteId]() {
        setupPeerConnection(remoteId, true);
        createAndSendOffer(remoteId);
    }, Qt::QueuedConnection);
    
    if (onInviteStatus) onInviteStatus(true, "Connecting...");
}

void WebRTCService::createAndSendOffer(const QString& remoteId) {
    if (!m_peerConnections.contains(remoteId)) {
        qWarning() << "ERROR: Cannot create offer - no peer connection for" << remoteId;
        return;
    }
    
    std::shared_ptr<rtc::PeerConnection> pc = m_peerConnections[remoteId];
    
    qDebug() << "DEBUG: Setting up offer for" << remoteId;
    
    // With auto-negotiation, onLocalDescription may not fire, so use onGatheringStateChange instead
    pc->onGatheringStateChange([this, remoteId, pc](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            QMetaObject::invokeMethod(this, [this, remoteId, pc]() {
                auto desc = pc->localDescription();
                if (desc.has_value()) {
                    qDebug() << "DEBUG: Gathering complete - Sending offer for" << remoteId;
                    QJsonObject payload;
                    payload["type"] = "offer";
                    payload["sdp"] = QString::fromStdString(std::string(*desc));
                    payload["sdpType"] = QString::fromStdString(desc->typeString());
                    sendSignalingMessage(remoteId, payload);
                } else {
                    qWarning() << "ERROR: Gathering complete but no local description for" << remoteId;
                }
            }, Qt::QueuedConnection);
        }
    });

    qDebug() << "DEBUG: Calling createOffer() for" << remoteId;
    pc->createOffer();

    // Cancel any existing watchdog timer for this peer
    if (m_watchdogTimers.contains(remoteId)) {
        qDebug() << "DEBUG: Cancelling old watchdog timer for" << remoteId;
        m_watchdogTimers[remoteId]->stop();
        m_watchdogTimers[remoteId]->deleteLater();
        m_watchdogTimers.remove(remoteId);
    }

    // Create new watchdog timer
    QTimer* watchdogTimer = new QTimer(this);
    m_watchdogTimers[remoteId] = watchdogTimer;
    watchdogTimer->setSingleShot(true);
    
    connect(watchdogTimer, &QTimer::timeout, this, [this, remoteId, watchdogTimer]() {
        // Remove timer from map
        m_watchdogTimers.remove(remoteId);
        watchdogTimer->deleteLater();
        
        // Check if peer connection still exists
        if (!m_peerConnections.contains(remoteId)) {
            qDebug() << "DEBUG: Peer connection for" << remoteId << "was already cleaned up";
            return;
        }
        
        auto pc = m_peerConnections[remoteId];
        
        // Check if we still have pending invite (not completed) and no established connection
        if (m_pendingInvites.contains(remoteId) && 
            (!pc->remoteDescription().has_value() || 
             pc->state() != rtc::PeerConnection::State::Connected)) {
            qWarning() << "DEBUG: User" << remoteId << "appears offline. Invite will retry when they come online.";
            // Don't show user-facing message here - the QMessageBox in MainWindow already handles this
            
            // Clean up the failed peer connection to allow retry when peer comes online
            qDebug() << "DEBUG: Cleaning up stale peer connection for" << remoteId;
            
            // Close the data channel if it exists
            if (m_dataChannels.contains(remoteId)) {
                auto dc = m_dataChannels[remoteId];
                if (dc && dc->isOpen()) {
                    dc->close();
                }
                m_dataChannels.remove(remoteId);
            }
            
            // Close the peer connection to stop ICE gathering and release resources
            pc->close();
            m_peerConnections.remove(remoteId);
            m_earlyCandidates.remove(remoteId);
        } else if (!pc->localDescription()) {
            qWarning() << "WATCHDOG: No Offer generated for" << remoteId << "after 10s. Check Network/STUN.";
        } else {
            qDebug() << "DEBUG: Connection attempt in progress for" << remoteId;
        }
    });
    
    // Start the watchdog timer
    watchdogTimer->start(OFFLINE_DETECTION_TIMEOUT_MS);
}

void WebRTCService::cancelInvite(const std::string& userId) {
    QString remoteId = QString::fromStdString(userId);
    m_pendingInvites.remove(remoteId);
    m_pendingKeys.erase(remoteId);
    m_pendingEntries.erase(remoteId);
    qDebug() << "DEBUG: Cancelled pending invite for" << remoteId << ". Remaining:" << m_pendingInvites.size();
    
    // Cancel watchdog timer if it exists
    if (m_watchdogTimers.contains(remoteId)) {
        m_watchdogTimers[remoteId]->stop();
        m_watchdogTimers[remoteId]->deleteLater();
        m_watchdogTimers.remove(remoteId);
    }
    
    QJsonObject cancelMsg;
    cancelMsg["type"] = "invite-cancel";
    sendP2PMessage(remoteId, cancelMsg);
    
    if (onInviteStatus) onInviteStatus(false, "Invite cancelled.");
}

void WebRTCService::respondToInvite(const std::string& senderId, bool accept) {
    QString remoteId = QString::fromStdString(senderId);
    QJsonObject response;
    response["type"] = accept ? "invite-accept" : "invite-reject";
    sendP2PMessage(remoteId, response);
}

void WebRTCService::setupPeerConnection(const QString& remoteId, bool isOfferer) {
    if (m_peerConnections.contains(remoteId)) return;

    rtc::Configuration config = getIceConfiguration();
    auto pc = std::make_shared<rtc::PeerConnection>(config);
    m_peerConnections[remoteId] = pc;

    // Handle Local ICE Candidates
    pc->onLocalCandidate([this, remoteId](const rtc::Candidate& candidate) {
        QMetaObject::invokeMethod(this, [this, remoteId, candidate]() {
            QJsonObject payload;
            payload["type"] = "ice-candidate";
            payload["candidate"] = QString::fromStdString(candidate.candidate());
            payload["sdpMid"] = QString::fromStdString(candidate.mid());
            sendSignalingMessage(remoteId, payload);
        }, Qt::QueuedConnection);
    });

    pc->onStateChange([this, remoteId](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) {
            qDebug() << "DEBUG: WebRTC Connected to" << remoteId;
        }
        else if (state == rtc::PeerConnection::State::Failed) {
            qWarning() << "DEBUG: WebRTC Connection FAILED to" << remoteId;
        }
    });

    // Shared Data Channel Logic
    auto setupDataChannel = [this, remoteId](std::shared_ptr<rtc::DataChannel> dc) {
        QMetaObject::invokeMethod(this, [this, remoteId, dc]() {
            qDebug() << "DEBUG: DataChannel Attached for" << remoteId;
            m_dataChannels[remoteId] = dc;
            
            dc->onOpen([this, remoteId]() {
                 QMetaObject::invokeMethod(this, [this, remoteId]() {
                    qDebug() << "DEBUG: DataChannel OPEN!";
                    if (m_pendingInvites.contains(remoteId)) {
                        qDebug() << "DEBUG: Sending invite-request...";
                        QJsonObject req;
                        req["type"] = "invite-request";
                        req["group"] = m_pendingInvites[remoteId];
                        sendP2PMessage(remoteId, req);
                        if(onInviteStatus) onInviteStatus(true, "Waiting for user consent...");
                    }
                }, Qt::QueuedConnection);
            });
            
            dc->onClosed([this, remoteId]() {
                QMetaObject::invokeMethod(this, [this, remoteId]() {
                    qDebug() << "DEBUG: DataChannel closed for" << remoteId;
                    m_dataChannels.remove(remoteId);
                }, Qt::QueuedConnection);
            });
            
            dc->onError([this, remoteId](std::string error) {
                QMetaObject::invokeMethod(this, [this, remoteId, error]() {
                    qWarning() << "DEBUG: DataChannel error for" << remoteId << ":" << QString::fromStdString(error);
                    m_dataChannels.remove(remoteId);
                }, Qt::QueuedConnection);
            });
            
            dc->onMessage([this, remoteId](std::variant<rtc::binary, rtc::string> message) {
                if (std::holds_alternative<rtc::string>(message)) {
                    QString msg = QString::fromStdString(std::get<rtc::string>(message));
                    QMetaObject::invokeMethod(this, [this, remoteId, msg]() {
                        try {
                            handleP2PMessage(remoteId, msg);
                        } catch (const std::exception& e) {
                            qWarning() << "DEBUG: Exception handling P2P message:" << e.what();
                        }
                    }, Qt::QueuedConnection);
                }
            });
        }, Qt::QueuedConnection);
    };

    if (isOfferer) {
        qDebug() << "DEBUG: Creating DataChannel (Sender side)";
        auto dc = pc->createDataChannel("ciphermesh-data");
        setupDataChannel(dc);
    } else {
        // Receiver waits for the channel
        pc->onDataChannel([setupDataChannel](std::shared_ptr<rtc::DataChannel> dc) {
             setupDataChannel(dc);
        });
    }
}

// Helper to flush candidates that arrived before PC was ready
void WebRTCService::flushEarlyCandidatesFor(const QString& peerId) {
    if (!m_earlyCandidates.contains(peerId)) return;
    
    qDebug() << "Flushing" << m_earlyCandidates[peerId].size() << "early candidates for" << peerId;
    
    if (!m_peerConnections.contains(peerId) || !m_peerConnections[peerId]->remoteDescription().has_value()) {
        qWarning() << "Cannot flush candidates: PeerConnection not ready for" << peerId;
        return;
    }

    auto pc = m_peerConnections[peerId];
    for (const QJsonObject &candObj : m_earlyCandidates[peerId]) {
        QString candidate = candObj["candidate"].toString();
        QString sdpMid = candObj["sdpMid"].toString();
        try {
            pc->addRemoteCandidate(rtc::Candidate(candidate.toStdString(), sdpMid.toStdString()));
        } catch (const std::exception& e) {
            qWarning() << "Failed to add remote candidate for" << peerId << ":" << e.what();
        }
    }
    m_earlyCandidates.remove(peerId);
}

void WebRTCService::handleP2PMessage(const QString& remoteId, const QString& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) {
         qWarning() << "WARNING: P2P message from" << remoteId << "is not valid JSON:" << message;
         return;
    }
    
    qDebug() << "DEBUG: P2P Message Received:" << message;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "invite-request") {
        QString group = obj["group"].toString();
        
        // Always deliver the invite - MainWindow will handle queueing if vault not ready
        if (onIncomingInvite) onIncomingInvite(remoteId.toStdString(), group.toStdString());
    } 
    else if (type == "invite-cancel") {
        if (onInviteCancelled) onInviteCancelled(remoteId.toStdString());
    }
    else if (type == "invite-reject") {
        if (onInviteStatus) onInviteStatus(false, "User declined the invitation.");
        
        // Notify about the rejection with group name
        QString groupName = m_pendingInvites.value(remoteId, "");
        if (!groupName.isEmpty() && onInviteResponse) {
            onInviteResponse(remoteId.toStdString(), groupName.toStdString(), false);
        }
        
        // Cancel watchdog timer if it exists
        if (m_watchdogTimers.contains(remoteId)) {
            m_watchdogTimers[remoteId]->stop();
            m_watchdogTimers[remoteId]->deleteLater();
            m_watchdogTimers.remove(remoteId);
        }
        
        m_pendingInvites.remove(remoteId);
        m_pendingKeys.erase(remoteId);
        m_pendingEntries.erase(remoteId);
        qDebug() << "DEBUG: Removed pending invite for" << remoteId << "due to rejection. Remaining:" << m_pendingInvites.size();
    }
    else if (type == "invite-accept") {
        if (m_pendingInvites.contains(remoteId)) {
            qDebug() << "DEBUG: Invite accepted! Sending group data...";
            QJsonObject keyMsg;
            keyMsg["type"] = "group-data";
            keyMsg["group"] = m_pendingInvites[remoteId];
            
            // 1. Encode Key
            std::vector<unsigned char>& key = m_pendingKeys[remoteId];
            QByteArray keyBytes(reinterpret_cast<const char*>(key.data()), key.size());
            keyMsg["key"] = QString(keyBytes.toBase64());
            
            // 2. Encode Entries
            QJsonArray entriesArr;
            std::vector<CipherMesh::Core::VaultEntry>& entries = m_pendingEntries[remoteId];
            for (const auto& entry : entries) {
                QJsonObject eObj;
                eObj["title"] = QString::fromStdString(entry.title);
                eObj["username"] = QString::fromStdString(entry.username);
                eObj["password"] = QString::fromStdString(entry.password); 
                eObj["notes"] = QString::fromStdString(entry.notes);
                
                QJsonArray locArr;
                for (const auto& l : entry.locations) {
                    QJsonObject lObj;
                    lObj["type"] = QString::fromStdString(l.type);
                    lObj["value"] = QString::fromStdString(l.value);
                    locArr.append(lObj); 
                }
                eObj["locations"] = locArr;
                entriesArr.append(eObj);
            }
            keyMsg["entries"] = entriesArr;

            sendP2PMessage(remoteId, keyMsg);
            
            if (onInviteStatus) onInviteStatus(true, "Transfer complete!");
            
            // Notify about the acceptance with group name
            QString groupName = m_pendingInvites[remoteId];
            if (onInviteResponse) {
                onInviteResponse(remoteId.toStdString(), groupName.toStdString(), true);
            }
            
            // Cancel watchdog timer if it exists
            if (m_watchdogTimers.contains(remoteId)) {
                m_watchdogTimers[remoteId]->stop();
                m_watchdogTimers[remoteId]->deleteLater();
                m_watchdogTimers.remove(remoteId);
            }
            
            m_pendingInvites.remove(remoteId);
            m_pendingKeys.erase(remoteId);
            m_pendingEntries.erase(remoteId);
            qDebug() << "DEBUG: Removed pending invite for" << remoteId << "after successful transfer. Remaining:" << m_pendingInvites.size();
        } else {
            // This can happen if:
            // 1. The sender already sent the data and cleaned up
            // 2. The application was restarted and lost pending invites (memory-only storage)
            // 3. The acceptance is a duplicate/resend
            // This is expected behavior and not an error - just log for debugging
            qDebug() << "DEBUG: Received invite-accept but no pending invite found (already completed or app restarted).";
        }
    }
    else if (type == "group-data") {
        qDebug() << "DEBUG: Received GROUP DATA!";
        QString groupName = obj["group"].toString();
        QString keyBase64 = obj["key"].toString();
        QByteArray keyBytes = QByteArray::fromBase64(keyBase64.toUtf8());
        std::vector<unsigned char> rawKey(keyBytes.begin(), keyBytes.end());
        
        QJsonArray entriesArr = obj["entries"].toArray();
        std::vector<CipherMesh::Core::VaultEntry> importedEntries;
        
        for (const auto& val : entriesArr) {
            QJsonObject eObj = val.toObject();
            CipherMesh::Core::VaultEntry e;
            e.title = eObj["title"].toString().toStdString();
            e.username = eObj["username"].toString().toStdString();
            e.password = eObj["password"].toString().toStdString();
            e.notes = eObj["notes"].toString().toStdString();
            
            QJsonArray locArr = eObj["locations"].toArray();
            for (const auto& lVal : locArr) {
                QJsonObject lObj = lVal.toObject();
                CipherMesh::Core::Location l;
                l.type = lObj["type"].toString().toStdString();
                l.value = lObj["value"].toString().toStdString();
                e.locations.push_back(l);
            }
            importedEntries.push_back(e);
        }
        // Pass SENDER ID correctly here
        if (onGroupDataReceived) {
            onGroupDataReceived(remoteId.toStdString(), groupName.toStdString(), rawKey, importedEntries);
        }
    }
    else if (type == "request-data") {
        QString group = obj["group"].toString();
        qDebug() << "DEBUG: Data requested by" << remoteId << "for" << group;
        if (onDataRequested) onDataRequested(remoteId.toStdString(), group.toStdString());
    }
}

void WebRTCService::sendP2PMessage(const QString& remoteId, const QJsonObject& payload) {
    if (m_dataChannels.contains(remoteId) && m_dataChannels[remoteId]) {
        try {
            if (m_dataChannels[remoteId]->isOpen()) {
                QJsonDocument doc(payload);
                m_dataChannels[remoteId]->send(doc.toJson(QJsonDocument::Compact).toStdString());
            } else {
                qWarning() << "WARNING: Cannot send P2P message to" << remoteId << "- Channel not open.";
            }
        } catch (const std::exception& e) {
            qWarning() << "WARNING: Exception sending P2P message to" << remoteId << ":" << e.what();
            // Clean up the problematic channel
            m_dataChannels.remove(remoteId);
        }
    } else {
        qWarning() << "WARNING: Cannot send P2P message to" << remoteId << "- Channel not available.";
    }
}

void WebRTCService::sendSignalingMessage(const QString& targetId, const QJsonObject& payload) {
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) {
        qWarning() << "DEBUG: WebSocket NOT connected. Failed to send signaling.";
        return;
    }
    QJsonObject message = payload;
    message["target"] = targetId;
    message["sender"] = m_localUserId; 
    QJsonDocument doc(message);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
}

void WebRTCService::fetchGroupMembers(const std::string& groupName) { if (onGroupMembersUpdated) onGroupMembersUpdated({}); }
void WebRTCService::removeUser(const std::string& groupName, const std::string& userId) { if (onRemoveStatus) onRemoveStatus(false, "Not implemented."); }

void WebRTCService::handleOffer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpOffer = obj["sdp"].toString();
    
    QMetaObject::invokeMethod(this, [this, senderId, sdpOffer]() {
        setupPeerConnection(senderId, false);
        std::shared_ptr<rtc::PeerConnection> pc = m_peerConnections[senderId];
        
        // With auto-negotiation, use onGatheringStateChange to send answer and flush candidates
        pc->onGatheringStateChange([this, senderId, pc](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete) {
                QMetaObject::invokeMethod(this, [this, senderId, pc]() {
                    auto desc = pc->localDescription();
                    if (desc.has_value()) {
                        qDebug() << "DEBUG: Gathering complete - Sending answer for" << senderId;
                        QJsonObject payload;
                        payload["type"] = "answer";
                        payload["sdp"] = QString::fromStdString(std::string(*desc));
                        payload["sdpType"] = QString::fromStdString(desc->typeString());
                        sendSignalingMessage(senderId, payload);
                        
                        // Flush early candidates after the answer is fully generated
                        flushEarlyCandidatesFor(senderId);
                    } else {
                        qWarning() << "ERROR: Gathering complete but no local description for" << senderId;
                    }
                }, Qt::QueuedConnection);
            }
        });
        
        pc->setRemoteDescription(rtc::Description(sdpOffer.toStdString(), "offer"));
        pc->createAnswer();
    }, Qt::QueuedConnection);
}

void WebRTCService::handleAnswer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpAnswer = obj["sdp"].toString();
    
    if (!m_peerConnections.contains(senderId)) {
        qWarning() << "ERROR: Received answer from unknown peer" << senderId;
        return;
    }
    
    QMetaObject::invokeMethod(this, [this, senderId, sdpAnswer]() {
        if (!m_peerConnections.contains(senderId)) {
            qWarning() << "ERROR: Peer connection no longer exists for" << senderId;
            return;
        }
        
        auto pc = m_peerConnections[senderId];
        try {
            pc->setRemoteDescription(rtc::Description(sdpAnswer.toStdString(), "answer"));
            qDebug() << "DEBUG: Remote description (answer) set for" << senderId;
            flushEarlyCandidatesFor(senderId);
        } catch (const std::exception& e) {
            qWarning() << "CRITICAL: Failed to set remote description:" << e.what();
            // Don't crash, just abort this connection attempt
        }
    }, Qt::QueuedConnection);
}

void WebRTCService::handleCandidate(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    bool ready = false;
    if (m_peerConnections.contains(senderId)) {
        auto pc = m_peerConnections[senderId];
        if (pc->remoteDescription().has_value()) {
             ready = true;
        }
    }

    if (ready) {
         QString candidate = obj["candidate"].toString();
         QString sdpMid = obj["sdpMid"].toString();
         try {
             m_peerConnections[senderId]->addRemoteCandidate(rtc::Candidate(candidate.toStdString(), sdpMid.toStdString()));
         } catch (const std::exception& e) {
             qWarning() << "Failed to add remote candidate from" << senderId << ":" << e.what();
             // If adding candidate fails, the connection might be in a bad state
             // Store as early candidate for potential retry
             m_earlyCandidates[senderId].push_back(obj);
         }
    } else {
        m_earlyCandidates[senderId].push_back(obj);
    }
}

void WebRTCService::onRetryTimer() {
    // Check and send pending invites
    checkAndSendPendingInvites();
}

void WebRTCService::sendOnlinePing() {
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) return;
    
    QJsonObject ping;
    ping["type"] = "online-ping";
    ping["sender"] = m_localUserId;
    
    QJsonDocument doc(ping);
    m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
    qDebug() << "DEBUG: Sent online ping from" << m_localUserId;
}

void WebRTCService::setAuthenticated(bool authenticated) {
    m_isAuthenticated = authenticated;
    qDebug() << "DEBUG: Authentication state changed to" << authenticated;
    
    // If newly authenticated and connected, register and send online ping
    if (authenticated && m_webSocket && m_webSocket->state() == QAbstractSocket::ConnectedState) {
        qDebug() << "DEBUG: User authenticated, registering with server now";
        
        // Send registration
        QJsonObject registration;
        registration["type"] = "register";
        registration["id"] = m_localUserId;
        QJsonDocument doc(registration);
        m_webSocket->sendTextMessage(doc.toJson(QJsonDocument::Compact));
        qDebug() << "DEBUG: Sent registration for" << m_localUserId;
        
        // Send online ping and check pending invites
        QTimer::singleShot(ONLINE_PING_DELAY_MS, this, &WebRTCService::sendOnlinePing);
        QTimer::singleShot(PENDING_INVITES_CHECK_DELAY_MS, this, &WebRTCService::checkAndSendPendingInvites);
    }
    // If deauthenticated (vault locked), cleanup all active connections
    else if (!authenticated) {
        qDebug() << "DEBUG: User deauthenticated, cleaning up P2P connections";
        
        // Close all peer connections
        for (auto it = m_peerConnections.begin(); it != m_peerConnections.end(); ++it) {
            if (it.value()) {
                try {
                    it.value()->close();
                } catch (const std::exception& e) {
                    qWarning() << "DEBUG: Exception closing peer connection:" << e.what();
                }
            }
        }
        m_peerConnections.clear();
        
        // Close all data channels
        for (auto it = m_dataChannels.begin(); it != m_dataChannels.end(); ++it) {
            if (it.value()) {
                try {
                    if (it.value()->isOpen()) {
                        it.value()->close();
                    }
                } catch (const std::exception& e) {
                    qWarning() << "DEBUG: Exception closing data channel:" << e.what();
                }
            }
        }
        m_dataChannels.clear();
        
        // Clear early candidates
        m_earlyCandidates.clear();
        
        // Cancel all watchdog timers
        for (auto timer : m_watchdogTimers) {
            if (timer) {
                timer->stop();
                timer->deleteLater();
            }
        }
        m_watchdogTimers.clear();
        
        qDebug() << "DEBUG: All P2P resources cleaned up after deauthentication";
    }
}

void WebRTCService::handleOnlinePing(const QJsonObject& message) {
    QString senderId = message["sender"].toString();
    
    // Filter duplicate notifications
    if (isDuplicateOnlineNotification(senderId)) {
        qDebug() << "DEBUG: Ignoring duplicate online-ping from" << senderId;
        return;
    }
    
    qDebug() << "DEBUG: Received online-ping from" << senderId;
    qDebug() << "DEBUG: Current pending invites count:" << m_pendingInvites.size();
    
    // Notify listeners that this peer is online
    if (onPeerOnline) {
        onPeerOnline(senderId.toStdString());
    }
    
    // Check if we have pending invites for this user and resend
    retryPendingInviteFor(senderId);
}

bool WebRTCService::isDuplicateOnlineNotification(const QString& userId) {
    qint64 currentTime = QDateTime::currentMSecsSinceEpoch();
    const qint64 DUPLICATE_WINDOW_MS = 5000;  // 5 second window
    
    if (m_recentOnlineNotifications.contains(userId)) {
        qint64 lastNotificationTime = m_recentOnlineNotifications[userId];
        if (currentTime - lastNotificationTime < DUPLICATE_WINDOW_MS) {
            return true;  // Duplicate within window
        }
    }
    
    // Update or add the timestamp
    m_recentOnlineNotifications[userId] = currentTime;
    
    // Clean up old entries (older than 10 seconds)
    QMutableMapIterator<QString, qint64> it(m_recentOnlineNotifications);
    while (it.hasNext()) {
        it.next();
        if (currentTime - it.value() > 10000) {
            it.remove();
        }
    }
    
    return false;  // Not a duplicate
}

void WebRTCService::retryPendingInviteFor(const QString& remoteId) {
    if (m_pendingInvites.contains(remoteId)) {
        qDebug() << "DEBUG: Found pending invite for" << remoteId << ", resending offer...";
        
        // Skip if already have a peer connection (invite already in progress)
        if (m_peerConnections.contains(remoteId)) {
            qDebug() << "DEBUG: Peer connection already exists for" << remoteId << ", skipping retry";
            return;
        }
        
        // Resend the offer
        QMetaObject::invokeMethod(this, [this, remoteId]() {
            setupPeerConnection(remoteId, true);
            createAndSendOffer(remoteId);
        }, Qt::QueuedConnection);
    } else {
        qDebug() << "DEBUG: No pending invite found for" << remoteId;
    }
}

void WebRTCService::checkAndSendPendingInvites() {
    if (m_pendingInvites.isEmpty()) {
        qDebug() << "DEBUG: No pending invites to send";
        return;
    }
    
    qDebug() << "DEBUG: Checking" << m_pendingInvites.size() << "pending invite(s)";
    
    // Try to send offers for all pending invites
    for (auto it = m_pendingInvites.begin(); it != m_pendingInvites.end(); ++it) {
        QString remoteId = it.key();
        
        // Skip if already have a peer connection (invite already in progress)
        if (m_peerConnections.contains(remoteId)) {
            qDebug() << "DEBUG: Peer connection already exists for" << remoteId;
            continue;
        }
        
        qDebug() << "DEBUG: Attempting to send pending invite to" << remoteId;
        
        QMetaObject::invokeMethod(this, [this, remoteId]() {
            setupPeerConnection(remoteId, true);
            createAndSendOffer(remoteId);
        }, Qt::QueuedConnection);
    }
}

void WebRTCService::requestData(const std::string& senderId, const std::string& groupName) {
     QString remoteId = QString::fromStdString(senderId);
     
     qDebug() << "DEBUG: Requesting data from" << remoteId << "for group" << QString::fromStdString(groupName);
     
     // Store the request to send when connection is established
     QJsonObject req;
     req["type"] = "request-data";
     req["group"] = QString::fromStdString(groupName);
     
     // Check if we already have an open data channel
     if (m_dataChannels.contains(remoteId) && m_dataChannels[remoteId] && m_dataChannels[remoteId]->isOpen()) {
         qDebug() << "DEBUG: Data channel already open, sending request immediately";
         sendP2PMessage(remoteId, req);
     } else {
         qDebug() << "DEBUG: No open data channel, establishing connection first";
         // Ensure connection exists and create offer if needed
         if (!m_peerConnections.contains(remoteId)) {
             setupPeerConnection(remoteId, true);
             createAndSendOffer(remoteId);
         }
         // Use delayed retry to allow data channel to establish
         // Note: This is a simple approach. In production, consider implementing
         // a proper message queue that waits for the data channel's onOpen event
         QTimer::singleShot(DATA_CHANNEL_RETRY_DELAY_MS, this, [this, remoteId, req]() {
             sendP2PMessage(remoteId, req);
         });
     }
}

void WebRTCService::sendGroupData(const std::string& recipientId, 
                                   const std::string& groupName,
                                   const std::vector<unsigned char>& groupKey,
                                   const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    QString remoteId = QString::fromStdString(recipientId);
    QString groupNameQt = QString::fromStdString(groupName);
    
    qDebug() << "DEBUG: Sending group data to" << remoteId << "for group" << groupNameQt;
    
    // Build the group-data message
    QJsonObject keyMsg;
    keyMsg["type"] = "group-data";
    keyMsg["group"] = groupNameQt;
    
    // 1. Encode Key
    QByteArray keyBytes(reinterpret_cast<const char*>(groupKey.data()), groupKey.size());
    keyMsg["key"] = QString(keyBytes.toBase64());
    
    // 2. Encode Entries
    QJsonArray entriesArr;
    for (const auto& entry : entries) {
        QJsonObject eObj;
        eObj["title"] = QString::fromStdString(entry.title);
        eObj["username"] = QString::fromStdString(entry.username);
        eObj["password"] = QString::fromStdString(entry.password);
        eObj["notes"] = QString::fromStdString(entry.notes);
        
        QJsonArray locArr;
        for (const auto& l : entry.locations) {
            QJsonObject lObj;
            lObj["type"] = QString::fromStdString(l.type);
            lObj["value"] = QString::fromStdString(l.value);
            locArr.append(lObj);
        }
        eObj["locations"] = locArr;
        entriesArr.append(eObj);
    }
    keyMsg["entries"] = entriesArr;
    
    // Send the message
    sendP2PMessage(remoteId, keyMsg);
    
    qDebug() << "DEBUG: Group data sent successfully to" << remoteId;
}

// ...
void WebRTCService::queueInvite(const std::string& groupName, const std::string& userEmail, 
                               const std::vector<unsigned char>& groupKey,
                               const std::vector<CipherMesh::Core::VaultEntry>& entries) 
{
    QString remoteId = QString::fromStdString(userEmail);
    qDebug() << "DEBUG: Queuing pending invite for" << remoteId << "(Will send when online)";

    m_pendingInvites[remoteId] = QString::fromStdString(groupName);
    m_pendingKeys[remoteId] = groupKey;
    m_pendingEntries[remoteId] = entries;
    
    // Do NOT call setupPeerConnection or createOffer here.
    // The onRetryTimer() will handle it when they come online.
}
// ...