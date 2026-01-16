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
#include <variant> 

const QString STUN_SERVER_URL = "stun:stun.l.google.com:19302"; 

const int ONLINE_PING_DELAY_MS = 500;
const int PENDING_INVITES_CHECK_DELAY_MS = 1000;
const int OFFLINE_DETECTION_TIMEOUT_MS = 10000;

WebRTCService::WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent)
    : QObject(parent), m_signalingUrl(signalingUrl), m_localUserId(QString::fromStdString(localUserId)), m_isAuthenticated(false), m_reconnectAttempts(0)
{
    rtc::InitLogger(rtc::LogLevel::Warning); 
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    connect(m_webSocket, &QWebSocket::connected, this, &WebRTCService::onWsConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebRTCService::onWsDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &WebRTCService::onWsTextMessageReceived);
}

WebRTCService::~WebRTCService() {
    if (m_webSocket) m_webSocket->close();
    m_dataChannels.clear();
    m_peerConnections.clear();
}

void WebRTCService::startSignaling() {
    if (m_webSocket) m_webSocket->open(QUrl(m_signalingUrl));
}

void WebRTCService::setIdentityPublicKey(const std::string& pubKey) {
    m_identityPublicKey = pubKey;
}

rtc::Configuration WebRTCService::getIceConfiguration() const {
    rtc::Configuration config;
    config.iceServers.emplace_back(STUN_SERVER_URL.toStdString());
    return config;
}

void WebRTCService::onWsConnected() {
    qDebug() << "DEBUG: WebSocket Connected!";
    if (onConnectionStatusChanged) onConnectionStatusChanged(true);
    m_reconnectAttempts = 0;
    
    if (m_isAuthenticated) {
        QJsonObject registration;
        registration["type"] = "register";
        registration["id"] = m_localUserId;
        if (!m_identityPublicKey.empty()) registration["pubKey"] = QString::fromStdString(m_identityPublicKey);

        m_webSocket->sendTextMessage(QJsonDocument(registration).toJson(QJsonDocument::Compact));
        QTimer::singleShot(ONLINE_PING_DELAY_MS, this, &WebRTCService::sendOnlinePing);
        QTimer::singleShot(PENDING_INVITES_CHECK_DELAY_MS, this, &WebRTCService::checkAndSendPendingInvites);
    }
}

void WebRTCService::onWsDisconnected() {
    qDebug() << "DEBUG: WebSocket disconnected";
    if (onConnectionStatusChanged) onConnectionStatusChanged(false);
    m_reconnectAttempts++;
    QTimer::singleShot(qMin(5000 * m_reconnectAttempts, 60000), this, [this]() { 
        if(m_webSocket) m_webSocket->open(QUrl(m_signalingUrl)); 
    });
}

void WebRTCService::sendOnlinePing() {
    if (!m_webSocket || m_webSocket->state() != QAbstractSocket::ConnectedState) return;
    QJsonObject ping; ping["type"] = "online-ping"; ping["sender"] = m_localUserId;
    m_webSocket->sendTextMessage(QJsonDocument(ping).toJson(QJsonDocument::Compact));
}

void WebRTCService::onWsTextMessageReceived(const QString& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull() || !doc.isObject()) return;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "user-status") {
        if (onUserStatusResult) onUserStatusResult(obj["target"].toString().toStdString(), (obj["status"].toString() == "online"));
    } 
    else if (type == "offer") handleOffer(obj);
    else if (type == "answer") handleAnswer(obj);
    else if (type == "ice-candidate") handleCandidate(obj);
    else if (type == "user-online") {
        QString user = obj["user"].toString();
        if (onPeerOnline) onPeerOnline(user.toStdString());
        if (m_pendingInvites.contains(user)) retryPendingInviteFor(user);
    }
}

void WebRTCService::requestData(const std::string& senderId, const std::string& groupName) {
    QJsonObject req; req["type"] = "request-data"; req["group"] = QString::fromStdString(groupName);
    if (!m_identityPublicKey.empty()) req["pubKey"] = QString::fromStdString(m_identityPublicKey);
    sendP2PMessage(QString::fromStdString(senderId), req);
}

void WebRTCService::handleP2PMessage(const QString& remoteId, const QString& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    qDebug() << "DEBUG: Received P2P message from" << remoteId << "- Type:" << type;

    if (type == "invite-request") {
        QString groupName = obj["group"].toString();
        qDebug() << "DEBUG: Received invite-request for group:" << groupName << "from" << remoteId;
        if (onIncomingInvite) onIncomingInvite(remoteId.toStdString(), groupName.toStdString());
    } 
    else if (type == "invite-accept") {
        QString groupName = obj["group"].toString();
        qDebug() << "DEBUG: Received invite-accept for group:" << groupName << "from" << remoteId;
        if (m_pendingInvites.contains(remoteId)) {
            qDebug() << "DEBUG: Sending group data to" << remoteId << "for group:" << m_pendingInvites[remoteId];
            sendGroupData(remoteId.toStdString(), m_pendingInvites[remoteId].toStdString(), 
                          m_pendingKeys[remoteId], m_pendingEntries[remoteId]);
            // Clean up pending data after sending
            m_pendingInvites.remove(remoteId);
            m_pendingKeys.erase(remoteId);
            m_pendingEntries.erase(remoteId);
            qDebug() << "DEBUG: Cleaned up pending invite data for" << remoteId;
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), true);
        } else {
            qWarning() << "WARNING: Received invite-accept from" << remoteId << "but no pending invite found";
        }
    }
    else if (type == "invite-reject") {
        QString groupName = obj["group"].toString();
        qDebug() << "DEBUG: Received invite-reject for group:" << groupName << "from" << remoteId;
        if (m_pendingInvites.contains(remoteId)) {
            // Clean up pending data
            m_pendingInvites.remove(remoteId);
            m_pendingKeys.erase(remoteId);
            m_pendingEntries.erase(remoteId);
            qDebug() << "DEBUG: Cleaned up pending invite data after rejection from" << remoteId;
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), false);
        } else {
            qWarning() << "WARNING: Received invite-reject from" << remoteId << "but no pending invite found";
        }
    }
    else if (type == "group-data") {
        QString groupName = obj["group"].toString();
        QString keyBase64 = obj["key"].toString();
        qDebug() << "DEBUG: Received group-data for group:" << groupName << "from" << remoteId;
        qDebug() << "DEBUG: Key Base64 length:" << keyBase64.length();
        
        std::string base64Str = keyBase64.toStdString();
        std::vector<unsigned char> keyContainer(base64Str.begin(), base64Str.end());
        
        QJsonArray entriesArr = obj["entries"].toArray();
        qDebug() << "DEBUG: Number of entries:" << entriesArr.size();
        
        std::vector<CipherMesh::Core::VaultEntry> importedEntries;
        for (const auto& val : entriesArr) {
            QJsonObject eObj = val.toObject();
            CipherMesh::Core::VaultEntry e;
            e.title = eObj["title"].toString().toStdString();
            e.username = eObj["username"].toString().toStdString();
            e.password = eObj["password"].toString().toStdString();
            e.notes = eObj["notes"].toString().toStdString();
            importedEntries.push_back(e);
        }
        qDebug() << "DEBUG: Triggering onGroupDataReceived callback";
        if (onGroupDataReceived) onGroupDataReceived(remoteId.toStdString(), groupName.toStdString(), keyContainer, importedEntries);
    }
    else if (type == "request-data") {
        QString groupName = obj["group"].toString();
        std::string requesterPubKey = obj["pubKey"].toString().toStdString();
        qDebug() << "DEBUG: Received request-data for group:" << groupName << "from" << remoteId;
        if (onDataRequested) onDataRequested(remoteId.toStdString(), groupName.toStdString(), requesterPubKey);
    }
}

void WebRTCService::inviteUser(const std::string& groupName, const std::string& userEmail, const std::vector<unsigned char>& groupKey, const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    QString remoteId = QString::fromStdString(userEmail);
    qDebug() << "DEBUG: inviteUser called - Group:" << QString::fromStdString(groupName) << "User:" << remoteId << "Entries:" << entries.size();
    
    if (m_peerConnections.contains(remoteId)) { m_peerConnections[remoteId]->close(); m_peerConnections.remove(remoteId); }
    m_dataChannels.remove(remoteId);
    
    m_pendingInvites[remoteId] = QString::fromStdString(groupName);
    m_pendingKeys[remoteId] = groupKey;
    m_pendingEntries[remoteId] = entries;
    
    qDebug() << "DEBUG: Stored pending invite for" << remoteId << "- Starting peer connection setup";
    QMetaObject::invokeMethod(this, [this, remoteId]() { setupPeerConnection(remoteId, true); createAndSendOffer(remoteId); }, Qt::QueuedConnection);
    if (onInviteStatus) onInviteStatus(true, "Connecting...");
}

void WebRTCService::createAndSendOffer(const QString& remoteId) {
    if (!m_peerConnections.contains(remoteId)) return;
    auto pc = m_peerConnections[remoteId];
    
    pc->onGatheringStateChange([this, remoteId, pc](rtc::PeerConnection::GatheringState state) {
        if (state == rtc::PeerConnection::GatheringState::Complete) {
            QMetaObject::invokeMethod(this, [this, remoteId, pc]() {
                auto desc = pc->localDescription();
                if (desc.has_value()) {
                    QJsonObject payload; payload["type"] = "offer";
                    payload["sdp"] = QString::fromStdString(std::string(*desc));
                    payload["sdpType"] = QString::fromStdString(desc->typeString());
                    sendSignalingMessage(remoteId, payload);
                }
            }, Qt::QueuedConnection);
        }
    });

    // --- FIX IS HERE: Use setLocalDescription() instead of createOffer() ---
    pc->setLocalDescription();
    
    // Watchdog Timer
    if (m_watchdogTimers.contains(remoteId)) {
        m_watchdogTimers[remoteId]->stop(); m_watchdogTimers[remoteId]->deleteLater();
    }
    QTimer* t = new QTimer(this);
    m_watchdogTimers[remoteId] = t;
    t->setSingleShot(true);
    connect(t, &QTimer::timeout, this, [this, remoteId, t]() {
        m_watchdogTimers.remove(remoteId); t->deleteLater();
        if (m_peerConnections.contains(remoteId)) {
            auto pc = m_peerConnections[remoteId];
            if (m_pendingInvites.contains(remoteId) && pc->state() != rtc::PeerConnection::State::Connected) {
                qDebug() << "DEBUG: Connection timed out for" << remoteId << "- Retrying later.";
                pc->close();
                m_peerConnections.remove(remoteId);
            }
        }
    });
    t->start(OFFLINE_DETECTION_TIMEOUT_MS);
}

void WebRTCService::setupPeerConnection(const QString& remoteId, bool isOfferer) {
    if (m_peerConnections.contains(remoteId)) return;
    auto pc = std::make_shared<rtc::PeerConnection>(getIceConfiguration());
    m_peerConnections[remoteId] = pc;

    pc->onLocalCandidate([this, remoteId](const rtc::Candidate& candidate) {
        QMetaObject::invokeMethod(this, [this, remoteId, candidate]() {
            QJsonObject payload; payload["type"] = "ice-candidate";
            payload["candidate"] = QString::fromStdString(candidate.candidate());
            payload["sdpMid"] = QString::fromStdString(candidate.mid());
            sendSignalingMessage(remoteId, payload);
        }, Qt::QueuedConnection);
    });

    pc->onStateChange([this, remoteId](rtc::PeerConnection::State state) {
        if (state == rtc::PeerConnection::State::Connected) qDebug() << "DEBUG: WebRTC Connected to" << remoteId;
    });

    auto setupDataChannel = [this, remoteId](std::shared_ptr<rtc::DataChannel> dc) {
        QMetaObject::invokeMethod(this, [this, remoteId, dc]() {
            m_dataChannels[remoteId] = dc;
            dc->onOpen([this, remoteId]() {
                 QMetaObject::invokeMethod(this, [this, remoteId]() {
                    qDebug() << "DEBUG: Data channel opened for" << remoteId;
                    if (m_pendingInvites.contains(remoteId)) {
                        qDebug() << "DEBUG: Sending invite-request to" << remoteId << "for group:" << m_pendingInvites[remoteId];
                        QJsonObject req; req["type"] = "invite-request"; req["group"] = m_pendingInvites[remoteId];
                        sendP2PMessage(remoteId, req);
                    } else {
                        qDebug() << "DEBUG: No pending invite for" << remoteId << "- this is likely an incoming connection";
                    }
                }, Qt::QueuedConnection);
            });
            dc->onClosed([this, remoteId]() { QMetaObject::invokeMethod(this, [this, remoteId]() { m_dataChannels.remove(remoteId); }, Qt::QueuedConnection); });
            dc->onMessage([this, remoteId](std::variant<rtc::binary, rtc::string> message) {
                QString msg;
                if (std::holds_alternative<rtc::string>(message)) msg = QString::fromStdString(std::get<rtc::string>(message));
                else if (std::holds_alternative<rtc::binary>(message)) {
                    auto bin = std::get<rtc::binary>(message);
                    std::string s(reinterpret_cast<const char*>(bin.data()), bin.size());
                    msg = QString::fromStdString(s);
                }
                if (!msg.isEmpty()) QMetaObject::invokeMethod(this, [this, remoteId, msg]() { try { handleP2PMessage(remoteId, msg); } catch (...) {} }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    };

    if (isOfferer) { auto dc = pc->createDataChannel("ciphermesh-data"); setupDataChannel(dc); } 
    else { pc->onDataChannel(setupDataChannel); }
}

void WebRTCService::handleOffer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpOffer = obj["sdp"].toString();
    
    QMetaObject::invokeMethod(this, [this, senderId, sdpOffer]() {
        setupPeerConnection(senderId, false);
        std::shared_ptr<rtc::PeerConnection> pc = m_peerConnections[senderId];
        
        pc->onGatheringStateChange([this, senderId, pc](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete) {
                QMetaObject::invokeMethod(this, [this, senderId, pc]() {
                    auto desc = pc->localDescription();
                    if (desc.has_value()) {
                        QJsonObject payload; payload["type"] = "answer";
                        payload["sdp"] = QString::fromStdString(std::string(*desc));
                        payload["sdpType"] = QString::fromStdString(desc->typeString());
                        sendSignalingMessage(senderId, payload);
                        flushEarlyCandidatesFor(senderId);
                    }
                }, Qt::QueuedConnection);
            }
        });
        
        pc->setRemoteDescription(rtc::Description(sdpOffer.toStdString(), "offer"));
        
        // --- FIX: UPDATED API ---
        pc->setLocalDescription();
        
    }, Qt::QueuedConnection);
}

void WebRTCService::handleAnswer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpAnswer = obj["sdp"].toString();
    QMetaObject::invokeMethod(this, [this, senderId, sdpAnswer]() {
        if (!m_peerConnections.contains(senderId)) return;
        try {
            m_peerConnections[senderId]->setRemoteDescription(rtc::Description(sdpAnswer.toStdString(), "answer"));
            flushEarlyCandidatesFor(senderId);
        } catch (const std::exception& e) { qWarning() << "CRITICAL: Failed to set remote description:" << e.what(); }
    }, Qt::QueuedConnection);
}

void WebRTCService::handleCandidate(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    if (!m_peerConnections.contains(senderId)) return;
    auto pc = m_peerConnections[senderId];
    if (pc->state() == rtc::PeerConnection::State::Closed) return;
    
    if (pc->remoteDescription().has_value()) {
        try { pc->addRemoteCandidate(rtc::Candidate(obj["candidate"].toString().toStdString(), obj["sdpMid"].toString().toStdString())); } catch(...) {}
    } else { m_earlyCandidates[senderId].push_back(obj); }
}

void WebRTCService::flushEarlyCandidatesFor(const QString& peerId) {
    if (!m_earlyCandidates.contains(peerId)) return;
    auto pc = m_peerConnections[peerId];
    if (pc->remoteDescription().has_value()) { for (const auto& obj : m_earlyCandidates[peerId]) handleCandidate(obj); }
    m_earlyCandidates.remove(peerId);
}

void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName, const std::vector<unsigned char>& groupKey, const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    QString remoteId = QString::fromStdString(recipientId);
    qDebug() << "DEBUG: sendGroupData called - Recipient:" << remoteId << "Group:" << QString::fromStdString(groupName) << "Entries:" << entries.size();
    
    QJsonObject keyMsg; keyMsg["type"] = "group-data"; keyMsg["group"] = QString::fromStdString(groupName);
    QByteArray keyBytes(reinterpret_cast<const char*>(groupKey.data()), groupKey.size());
    keyMsg["key"] = QString(keyBytes.toBase64());
    qDebug() << "DEBUG: Group key size:" << groupKey.size() << "Base64 length:" << QString(keyBytes.toBase64()).length();
    
    QJsonArray entriesArr;
    for (const auto& entry : entries) {
        QJsonObject eObj; eObj["title"] = QString::fromStdString(entry.title); eObj["username"] = QString::fromStdString(entry.username); 
        eObj["password"] = QString::fromStdString(entry.password); eObj["notes"] = QString::fromStdString(entry.notes); 
        entriesArr.append(eObj);
    }
    keyMsg["entries"] = entriesArr;
    qDebug() << "DEBUG: Sending group-data message to" << remoteId << "- Message size:" << QJsonDocument(keyMsg).toJson(QJsonDocument::Compact).size() << "bytes";
    sendP2PMessage(remoteId, keyMsg);
}

void WebRTCService::sendP2PMessage(const QString& remoteId, const QJsonObject& payload) {
    if (m_dataChannels.contains(remoteId) && m_dataChannels[remoteId] && m_dataChannels[remoteId]->isOpen()) {
        qDebug() << "DEBUG: sendP2PMessage to" << remoteId << "- Type:" << payload["type"].toString();
        m_dataChannels[remoteId]->send(QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString());
    } else {
        qWarning() << "WARNING: Cannot send message to" << remoteId << "- Data channel not available or not open";
    }
}

void WebRTCService::sendSignalingMessage(const QString& targetId, const QJsonObject& payload) {
    if (m_webSocket) {
        QJsonObject msg = payload; msg["target"] = targetId; msg["sender"] = m_localUserId;
        m_webSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WebRTCService::retryPendingInviteFor(const QString& remoteId) {
    if (m_pendingInvites.contains(remoteId) && !m_peerConnections.contains(remoteId)) {
        QMetaObject::invokeMethod(this, [this, remoteId]() { setupPeerConnection(remoteId, true); createAndSendOffer(remoteId); }, Qt::QueuedConnection);
    }
}

void WebRTCService::checkAndSendPendingInvites() {
    for(auto it = m_pendingInvites.begin(); it != m_pendingInvites.end(); ++it) { if (!m_peerConnections.contains(it.key())) retryPendingInviteFor(it.key()); }
}

void WebRTCService::queueInvite(const std::string& groupName, const std::string& userEmail, const std::vector<unsigned char>& groupKey, const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    QString remoteId = QString::fromStdString(userEmail);
    m_pendingInvites[remoteId] = QString::fromStdString(groupName);
    m_pendingKeys[remoteId] = groupKey;
    m_pendingEntries[remoteId] = entries;
}

void WebRTCService::setAuthenticated(bool authenticated) {
    m_isAuthenticated = authenticated;
    if (authenticated && m_webSocket->state() == QAbstractSocket::ConnectedState) onWsConnected();
}

void WebRTCService::cancelInvite(const std::string& userId) { m_pendingInvites.remove(QString::fromStdString(userId)); }
void WebRTCService::respondToInvite(const std::string& senderId, const std::string& groupName, bool accept) { 
    qDebug() << "DEBUG: Responding to invite from" << QString::fromStdString(senderId) << "for group:" << QString::fromStdString(groupName) << "- Accept:" << accept;
    QJsonObject resp; 
    resp["type"] = accept ? "invite-accept" : "invite-reject"; 
    resp["group"] = QString::fromStdString(groupName);
    sendP2PMessage(QString::fromStdString(senderId), resp); 
}
void WebRTCService::removeUser(const std::string&, const std::string&) {}
void WebRTCService::fetchGroupMembers(const std::string&) {}
void WebRTCService::handleOnlinePing(const QJsonObject&) {}
void WebRTCService::onRetryTimer() {}
bool WebRTCService::isDuplicateOnlineNotification(const QString&) { return false; }