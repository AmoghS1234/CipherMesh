#include "webrtcservice.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <map>

// Check for Android Environment
#if defined(__ANDROID__) || defined(ANDROID)

#include <android/log.h>
#define LOG_TAG "WebRTCService"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- JSON Helpers ---

std::string extractJsonValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return "";
    start += search.length();
    
    bool isString = false;
    if (json[start] == ' ' || json[start] == '\"') {
        start = json.find("\"", start) + 1;
        isString = true;
    }
    
    size_t end;
    if (isString) end = json.find("\"", start);
    else end = json.find_first_of(",}", start);
    
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

// Simple JSON builder to replace QJsonObject
std::string buildJson(const std::string& type, const std::string& payloadKey, const std::string& payloadVal) {
    return "{\"type\":\"" + type + "\", \"" + payloadKey + "\":\"" + payloadVal + "\"}";
}

// --- Implementation ---

WebRTCService::WebRTCService(const std::string& signalingUrl, const std::string& localUserId)
    : m_signalingUrl(signalingUrl), m_localUserId(localUserId), m_isConnected(false), m_isAuthenticated(false) {
}

WebRTCService::~WebRTCService() {
    disconnect();
}

void WebRTCService::connect() {
    // Android connection is managed by P2PManager.kt (OkHttp)
    // This state tracks logical readiness
    m_isConnected = true;
    LOGI("WebRTCService Logical Connection Active");
}

void WebRTCService::disconnect() {
    m_isConnected = false;
    m_peers.clear();
    m_channels.clear();
}

// --- Core P2P Logic (Mirrors Desktop) ---

void WebRTCService::inviteUser(const std::string& groupName, const std::string& userEmail, 
                               const std::vector<unsigned char>& groupKey, 
                               const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    LOGI("Inviting user: %s to group: %s", userEmail.c_str(), groupName.c_str());
    
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pendingInvites[userEmail] = groupName;
    m_pendingKeys[userEmail] = groupKey;
    m_pendingEntries[userEmail] = entries;
    
    // Start the Handshake
    setupPeerConnection(userEmail, true); // true = We are Offerer
    // Note: createAndSendOffer called implicitly by setupPeerConnection callback logic or explicitly here
}

void WebRTCService::setupPeerConnection(const std::string& peerId, bool isOfferer) {
    if (m_peers.count(peerId)) return;

    rtc::Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    auto pc = std::make_shared<rtc::PeerConnection>(config);
    m_peers[peerId] = pc;

    // 1. Handle ICE Candidates
    pc->onLocalCandidate([this, peerId](auto candidate) {
        LOGI("Generated ICE Candidate for %s", peerId.c_str());
        sendSignalingMessage(peerId, "ice-candidate", std::string(candidate.candidate()));
    });

    // 2. Handle Connection State
    pc->onStateChange([this, peerId](rtc::PeerConnection::State state) {
        LOGI("PC State Change for %s: %d", peerId.c_str(), (int)state);
    });

    // 3. Handle Data Channel (if we are receiver)
    pc->onDataChannel([this, peerId](auto dc) {
        LOGI("Received DataChannel from %s", peerId.c_str());
        setupDataChannel(dc, peerId);
    });

    // 4. Create Offer Logic (only if we are the Offerer)
    if (isOfferer) {
        auto dc = pc->createDataChannel("ciphermesh-data");
        setupDataChannel(dc, peerId);

        pc->setLocalDescription(); // This triggers gathering -> onLocalDescription
    }

    // 5. Send Description (Offer/Answer) when ready
    pc->onLocalDescription([this, peerId](auto desc) {
        std::string type = desc.typeString(); // "offer" or "answer"
        std::string sdp = std::string(desc);
        LOGI("Sending %s to %s", type.c_str(), peerId.c_str());
        sendSignalingMessage(peerId, type, sdp);
    });
}

void WebRTCService::setupDataChannel(std::shared_ptr<rtc::DataChannel> dc, const std::string& peerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channels[peerId] = dc;

    dc->onOpen([this, peerId]() {
        LOGI("DataChannel OPEN with %s", peerId.c_str());
        // If we have a pending invite, send the request now
        if (m_pendingInvites.count(peerId)) {
            std::string msg = "{\"type\":\"invite-request\", \"group\":\"" + m_pendingInvites[peerId] + "\"}";
            m_channels[peerId]->send(msg);
        }
    });

    dc->onMessage([this, peerId](auto data) {
        if (std::holds_alternative<std::string>(data)) {
            std::string msg = std::get<std::string>(data);
            LOGI("Received P2P Msg from %s: %s", peerId.c_str(), msg.c_str());
            
            std::string type = extractJsonValue(msg, "type");
            if (type == "invite-request") {
                // Notify UI
                std::string group = extractJsonValue(msg, "group");
                // TODO: Call Java callback
            }
        }
    });
}

// --- Signaling Handling (Incoming from Kotlin) ---

void WebRTCService::handleSignalingMessage(const std::string& message) {
    std::string type = extractJsonValue(message, "type");
    std::string sender = extractJsonValue(message, "sender");
    
    if (sender.empty()) return;

    if (type == "user-online") {
        LOGI("Peer Online: %s", sender.c_str());
        if (m_pendingInvites.count(sender)) retryPendingInviteFor(sender);
    } 
    else if (type == "offer") {
        std::string sdp = extractJsonValue(message, "sdp");
        setupPeerConnection(sender, false); // False = We are Answerer
        if(m_peers.count(sender)) {
            m_peers[sender]->setRemoteDescription(rtc::Description(sdp, type));
            if(m_peers[sender]->localDescription().has_value() == false) {
                 m_peers[sender]->setLocalDescription(); // Triggers Answer generation
            }
        }
    }
    else if (type == "answer") {
        std::string sdp = extractJsonValue(message, "sdp");
        if(m_peers.count(sender)) {
            m_peers[sender]->setRemoteDescription(rtc::Description(sdp, type));
        }
    }
    else if (type == "ice-candidate") {
        std::string cand = extractJsonValue(message, "candidate");
        std::string mid = extractJsonValue(message, "mid"); // Optional often
        if(m_peers.count(sender)) {
            m_peers[sender]->addRemoteCandidate(rtc::Candidate(cand, mid));
        }
    }
}

void WebRTCService::sendSignalingMessage(const std::string& targetId, const std::string& type, const std::string& payload) {
    // Call back into Kotlin to send over WebSocket
    if (onSendSignaling) {
        onSendSignaling(targetId, type, payload);
    } else {
        LOGE("Cannot send signaling: Callback not registered!");
    }
}

void WebRTCService::retryPendingInviteFor(const std::string& remoteId) {
    if (m_peers.count(remoteId) == 0) {
        setupPeerConnection(remoteId, true);
    }
}

// --- Required Stubs (Filled) ---
void WebRTCService::setAuthenticated(bool auth) { m_isAuthenticated = auth; }
void WebRTCService::sendInvite(const std::string& recipientId, const std::string& groupName) { inviteUser(groupName, recipientId, {}, {}); }
void WebRTCService::cancelInvite(const std::string& userId) { 
    m_pendingInvites.erase(userId); 
    if(m_peers.count(userId)) m_peers.erase(userId);
}
void WebRTCService::respondToInvite(const std::string& senderId, bool accept) {
    if(m_channels.count(senderId)) {
        std::string msg = accept ? "{\"type\":\"invite-accept\"}" : "{\"type\":\"invite-reject\"}";
        m_channels[senderId]->send(msg);
    }
}
void WebRTCService::requestData(const std::string&, const std::string&) {}
void WebRTCService::sendGroupData(const std::string&, const std::string&, const std::vector<unsigned char>&, const std::vector<CipherMesh::Core::VaultEntry>&) {}
void WebRTCService::fetchGroupMembers(const std::string&) {}
void WebRTCService::removeUser(const std::string&, const std::string&) {}
void WebRTCService::onRetryTimer() {}
void WebRTCService::broadcastSync(const std::string&) {}

// =========================================================
//  DESKTOP IMPLEMENTATION (Qt / QWebSocket / QJson)
//  (KEEPING YOUR ORIGINAL DESKTOP CODE BELOW EXACTLY AS IS)
// =========================================================
#else

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QDebug>
#include <QJsonArray>
#include <QAbstractSocket>
#include <QThread>
#include <QMetaObject>
#include <QDateTime>
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

    if (type == "offer") handleOffer(obj);
    else if (type == "answer") handleAnswer(obj);
    else if (type == "ice-candidate") handleCandidate(obj);
    else if (type == "user-online") {
        QString user = obj["user"].toString();
        if (onPeerOnline) onPeerOnline(user.toStdString());
        if (m_pendingInvites.contains(user)) retryPendingInviteFor(user);
    }
}

void WebRTCService::handleP2PMessage(const QString& remoteId, const QString& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "invite-request") {
        if (onIncomingInvite) onIncomingInvite(remoteId.toStdString(), obj["group"].toString().toStdString());
    } 
    else if (type == "invite-accept") {
        if (m_pendingInvites.contains(remoteId)) {
            sendGroupData(remoteId.toStdString(), m_pendingInvites[remoteId].toStdString(), m_pendingKeys[remoteId], m_pendingEntries[remoteId]);
            m_pendingInvites.remove(remoteId);
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), obj["group"].toString().toStdString(), true);
        }
    }
}

void WebRTCService::inviteUser(const std::string& groupName, const std::string& userEmail, const std::vector<unsigned char>& groupKey, const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    QString remoteId = QString::fromStdString(userEmail);
    if (m_peerConnections.contains(remoteId)) { m_peerConnections[remoteId]->close(); m_peerConnections.remove(remoteId); }
    m_dataChannels.remove(remoteId);
    
    m_pendingInvites[remoteId] = QString::fromStdString(groupName);
    m_pendingKeys[remoteId] = groupKey;
    m_pendingEntries[remoteId] = entries;
    
    QMetaObject::invokeMethod(this, [this, remoteId]() { setupPeerConnection(remoteId, true); createAndSendOffer(remoteId); }, Qt::QueuedConnection);
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
                    sendSignalingMessage(remoteId, payload);
                }
            }, Qt::QueuedConnection);
        }
    });

    pc->setLocalDescription();
}

void WebRTCService::setupPeerConnection(const QString& remoteId, bool isOfferer) {
    if (m_peerConnections.contains(remoteId)) return;
    auto pc = std::make_shared<rtc::PeerConnection>(getIceConfiguration());
    m_peerConnections[remoteId] = pc;

    pc->onLocalCandidate([this, remoteId](const rtc::Candidate& candidate) {
        QMetaObject::invokeMethod(this, [this, remoteId, candidate]() {
            QJsonObject payload; payload["type"] = "ice-candidate";
            payload["candidate"] = QString::fromStdString(candidate.candidate());
            sendSignalingMessage(remoteId, payload);
        }, Qt::QueuedConnection);
    });

    auto setupDataChannel = [this, remoteId](std::shared_ptr<rtc::DataChannel> dc) {
        QMetaObject::invokeMethod(this, [this, remoteId, dc]() {
            m_dataChannels[remoteId] = dc;
            dc->onOpen([this, remoteId]() {
                 QMetaObject::invokeMethod(this, [this, remoteId]() {
                    if (m_pendingInvites.contains(remoteId)) {
                        QJsonObject req; req["type"] = "invite-request"; req["group"] = m_pendingInvites[remoteId];
                        sendP2PMessage(remoteId, req);
                    }
                }, Qt::QueuedConnection);
            });
            dc->onMessage([this, remoteId](std::variant<rtc::binary, rtc::string> message) {
                QString msg;
                if (std::holds_alternative<rtc::string>(message)) msg = QString::fromStdString(std::get<rtc::string>(message));
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
                        sendSignalingMessage(senderId, payload);
                        flushEarlyCandidatesFor(senderId);
                    }
                }, Qt::QueuedConnection);
            }
        });
        
        pc->setRemoteDescription(rtc::Description(sdpOffer.toStdString(), "offer"));
        pc->setLocalDescription();
    }, Qt::QueuedConnection);
}

void WebRTCService::handleAnswer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpAnswer = obj["sdp"].toString();
    QMetaObject::invokeMethod(this, [this, senderId, sdpAnswer]() {
        if (!m_peerConnections.contains(senderId)) return;
        m_peerConnections[senderId]->setRemoteDescription(rtc::Description(sdpAnswer.toStdString(), "answer"));
        flushEarlyCandidatesFor(senderId);
    }, Qt::QueuedConnection);
}

void WebRTCService::handleCandidate(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    if (!m_peerConnections.contains(senderId)) return;
    auto pc = m_peerConnections[senderId];
    
    if (pc->remoteDescription().has_value()) {
        try { pc->addRemoteCandidate(rtc::Candidate(obj["candidate"].toString().toStdString(), "0")); } catch(...) {}
    } else { m_earlyCandidates[senderId].push_back(obj); }
}

void WebRTCService::flushEarlyCandidatesFor(const QString& peerId) {
    if (!m_earlyCandidates.contains(peerId)) return;
    for (const auto& obj : m_earlyCandidates[peerId]) handleCandidate(obj);
    m_earlyCandidates.remove(peerId);
}

void WebRTCService::sendP2PMessage(const QString& remoteId, const QJsonObject& payload) {
    if (m_dataChannels.contains(remoteId) && m_dataChannels[remoteId] && m_dataChannels[remoteId]->isOpen()) {
        m_dataChannels[remoteId]->send(QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString());
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
    inviteUser(groupName, userEmail, groupKey, entries);
}

void WebRTCService::setAuthenticated(bool authenticated) {
    m_isAuthenticated = authenticated;
    if (authenticated && m_webSocket->state() == QAbstractSocket::ConnectedState) onWsConnected();
}

void WebRTCService::sendInvite(const std::string& recipientId, const std::string& groupName) {
    inviteUser(groupName, recipientId, {}, {});
}

void WebRTCService::cancelInvite(const std::string& userId) { m_pendingInvites.remove(QString::fromStdString(userId)); }
void WebRTCService::respondToInvite(const std::string& senderId, bool accept) { QJsonObject resp; resp["type"] = accept ? "invite-accept" : "invite-reject"; sendP2PMessage(QString::fromStdString(senderId), resp); }
void WebRTCService::removeUser(const std::string&, const std::string&) {}
void WebRTCService::fetchGroupMembers(const std::string&) {}
void WebRTCService::requestData(const std::string&, const std::string&) {}
void WebRTCService::sendGroupData(const std::string&, const std::string&, const std::vector<unsigned char>&, const std::vector<CipherMesh::Core::VaultEntry>&) {}
void WebRTCService::onRetryTimer() {}

#endif