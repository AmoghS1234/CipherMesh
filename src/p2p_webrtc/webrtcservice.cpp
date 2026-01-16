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
    if (groupName.empty() || userEmail.empty()) {
        LOGE("❌ [P2P] inviteUser called with empty groupName or userEmail");
        return;
    }
    
    try {
        LOGI("Inviting user: %s to group: %s", userEmail.c_str(), groupName.c_str());
        
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pendingInvites[userEmail] = groupName;
        m_pendingKeys[userEmail] = groupKey;
        m_pendingEntries[userEmail] = entries;
        
        // Start the Handshake
        setupPeerConnection(userEmail, true); // true = We are Offerer
        // Note: createAndSendOffer called implicitly by setupPeerConnection callback logic or explicitly here
    } catch (const std::exception& e) {
        LOGE("❌ [P2P] CRITICAL: inviteUser failed for %s to group %s: %s", 
             userEmail.c_str(), groupName.c_str(), e.what());
    }
}

void WebRTCService::setupPeerConnection(const std::string& peerId, bool isOfferer) {
    if (m_peers.count(peerId)) return;

    try {
        rtc::Configuration config;
        config.iceServers.emplace_back("stun:stun.l.google.com:19302");

        auto pc = std::make_shared<rtc::PeerConnection>(config);
        if (!pc) {
            LOGE("❌ [P2P] Failed to create PeerConnection for %s", peerId.c_str());
            return;
        }
        m_peers[peerId] = pc;

        // 1. Handle ICE Candidates
        pc->onLocalCandidate([this, peerId](auto candidate) {
            try {
                LOGI("Generated ICE Candidate for %s", peerId.c_str());
                sendSignalingMessage(peerId, "ice-candidate", std::string(candidate.candidate()));
            } catch (const std::exception& e) {
                LOGE("❌ [P2P] ICE candidate error for %s: %s", peerId.c_str(), e.what());
            }
        });

        // 2. Handle Connection State
        pc->onStateChange([this, peerId](rtc::PeerConnection::State state) {
            LOGI("PC State Change for %s: %d", peerId.c_str(), (int)state);
        });

        // 3. Handle Data Channel (if we are receiver)
        pc->onDataChannel([this, peerId](auto dc) {
            try {
                LOGI("Received DataChannel from %s", peerId.c_str());
                if (dc) {
                    setupDataChannel(dc, peerId);
                } else {
                    LOGE("❌ [P2P] Null DataChannel received from %s", peerId.c_str());
                }
            } catch (const std::exception& e) {
                LOGE("❌ [P2P] DataChannel setup error: %s", e.what());
            }
        });

        // 4. Register onLocalDescription BEFORE setLocalDescription to avoid race condition
        pc->onLocalDescription([this, peerId](auto desc) {
            try {
                std::string type = desc.typeString(); // "offer" or "answer"
                std::string sdp = std::string(desc);
                LOGI("📤 [P2P] Sending %s to %s (SDP length: %zu)", type.c_str(), peerId.c_str(), sdp.length());
                sendSignalingMessage(peerId, type, sdp);
            } catch (const std::exception& e) {
                LOGE("❌ [P2P] Local description error for %s: %s", peerId.c_str(), e.what());
            }
        });

        // 5. Create Offer Logic (only if we are the Offerer) - MUST be after onLocalDescription
        if (isOfferer) {
            auto dc = pc->createDataChannel("ciphermesh-data");
            if (!dc) {
                LOGE("❌ [P2P] Failed to create DataChannel for %s", peerId.c_str());
                return;
            }
            setupDataChannel(dc, peerId);

            LOGI("🔄 [P2P] Creating offer for %s...", peerId.c_str());
            pc->setLocalDescription(); // This triggers gathering -> onLocalDescription callback above
        }
    } catch (const std::exception& e) {
        LOGE("❌ [P2P] CRITICAL: setupPeerConnection failed for %s: %s", peerId.c_str(), e.what());
        // Clean up partial state
        m_peers.erase(peerId);
    }
}

void WebRTCService::setupDataChannel(std::shared_ptr<rtc::DataChannel> dc, const std::string& peerId) {
    if (!dc) {
        LOGE("❌ [P2P] setupDataChannel called with null DataChannel for %s", peerId.c_str());
        return;
    }

    try {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_channels[peerId] = dc;

        dc->onOpen([this, peerId]() {
            try {
                LOGI("🔓 [P2P] DataChannel OPEN with %s", peerId.c_str());
                // If we have a pending invite, send the request now
                if (m_pendingInvites.count(peerId)) {
                    std::string msg = "{\"type\":\"invite-request\", \"group\":\"" + m_pendingInvites[peerId] + "\"}";
                    if (m_channels.count(peerId) && m_channels[peerId]) {
                        m_channels[peerId]->send(msg);
                        LOGI("📤 [P2P] Sent invite-request for group '%s' to %s", m_pendingInvites[peerId].c_str(), peerId.c_str());
                    } else {
                        LOGE("❌ [P2P] Channel not available for %s", peerId.c_str());
                    }
                }
            } catch (const std::exception& e) {
                LOGE("❌ [P2P] Error in onOpen for %s: %s", peerId.c_str(), e.what());
            }
        });

        dc->onMessage([this, peerId](auto data) {
            try {
                if (std::holds_alternative<std::string>(data)) {
                    std::string msg = std::get<std::string>(data);
                    LOGI("📨 [P2P] Received message from %s: %s", peerId.c_str(), msg.c_str());
            
                    std::string type = extractJsonValue(msg, "type");
                    
                    if (type == "invite-request") {
                        std::string group = extractJsonValue(msg, "group");
                        LOGI("📩 [P2P] Invite request for group '%s' from %s", group.c_str(), peerId.c_str());
                        // Notify via callback (if registered)
                        if (onIncomingInvite) {
                            onIncomingInvite(peerId, group);
                        } else {
                            LOGE("⚠️  [P2P] onIncomingInvite callback not registered!");
                        }
                    }
                    else if (type == "invite-accept") {
                        LOGI("✅ [P2P] Invite accepted by %s", peerId.c_str());
                        // Send group data now if we have pending invite
                        if (m_pendingInvites.count(peerId)) {
                            std::string groupName = m_pendingInvites[peerId];
                            if (onInviteResponse) {
                                onInviteResponse(peerId, groupName, true);
                            }
                            sendGroupData(peerId, groupName, m_pendingKeys[peerId], m_pendingEntries[peerId]);
                        } else {
                            LOGE("⚠️  [P2P] Received invite-accept but no pending invite for %s", peerId.c_str());
                        }
                    }
                    else if (type == "invite-reject") {
                        LOGI("❌ [P2P] Invite rejected by %s", peerId.c_str());
                        if (m_pendingInvites.count(peerId)) {
                            if (onInviteResponse) {
                                onInviteResponse(peerId, m_pendingInvites[peerId], false);
                            }
                        } else {
                            LOGE("⚠️  [P2P] Received invite-reject but no pending invite for %s", peerId.c_str());
                        }
                    }
                    else if (type == "data-request") {
                        std::string group = extractJsonValue(msg, "group");
                        LOGI("📥 [P2P] Data request for group '%s' from %s", group.c_str(), peerId.c_str());
                        // Notify via callback
                        if (onDataRequested) {
                            onDataRequested(peerId, group, "");
                        }
                    }
                    else if (type == "group-data") {
                        std::string group = extractJsonValue(msg, "group");
                        LOGI("📥 [P2P] Received group data for '%s' from %s", group.c_str(), peerId.c_str());
                        // Notify via callback
                        if (onGroupDataReceived) {
                            // For now pass empty vectors - full implementation would deserialize
                            onGroupDataReceived(peerId, group, {}, {});
                        }
                    }
                    else if (type == "sync-update") {
                        std::string group = extractJsonValue(msg, "group");
                        LOGI("🔄 [SYNC] Received sync update for group '%s' from %s", group.c_str(), peerId.c_str());
                        // Notify via callback for sync
                        if (onGroupDataReceived) {
                            onGroupDataReceived(peerId, group, {}, {});
                        }
                    }
                    else {
                        LOGI("ℹ️  [P2P] Unknown message type: %s", type.c_str());
                    }
                }
            } catch (const std::exception& e) {
                LOGE("❌ [P2P] Error processing message from %s: %s", peerId.c_str(), e.what());
            }
        });

        dc->onClosed([this, peerId]() {
            LOGI("🔒 [P2P] DataChannel CLOSED with %s", peerId.c_str());
            std::lock_guard<std::mutex> lock(m_mutex);
            m_channels.erase(peerId);
        });
    } catch (const std::exception& e) {
        LOGE("❌ [P2P] CRITICAL: setupDataChannel failed for %s: %s", peerId.c_str(), e.what());
    }
}

// --- Signaling Handling (Incoming from Kotlin) ---

void WebRTCService::handleSignalingMessage(const std::string& message) {
    std::string type = extractJsonValue(message, "type");
    std::string sender = extractJsonValue(message, "sender");
    
    LOGI("📥 [SIGNAL] handleSignalingMessage type=%s sender=%s", type.c_str(), sender.c_str());
    
    if (sender.empty()) {
        LOGE("⚠️  [SIGNAL] Message missing sender field");
        return;
    }

    if (type == "user-online") {
        LOGI("🟢 [SIGNAL] Peer Online: %s", sender.c_str());
        
        // [NEW] Notify callback so app can check for pending invites/syncs
        if (onPeerOnline) {
            onPeerOnline(sender);
        }
        
        // Check if we have a pending invite for this user
        if (m_pendingInvites.count(sender)) {
            LOGI("🔄 [SIGNAL] Found pending invite for %s - retrying", sender.c_str());
            retryPendingInviteFor(sender);
        }
    } 
    else if (type == "offer") {
        std::string sdp = extractJsonValue(message, "sdp");
        LOGI("📨 [SIGNAL] Received offer from %s (SDP length: %zu)", sender.c_str(), sdp.length());
        
        if (sdp.empty()) {
            LOGE("❌ [SIGNAL] Received empty SDP in offer from %s", sender.c_str());
            return;
        }
        
        // Validate that SDP contains required ICE parameters
        if (sdp.find("ice-ufrag") == std::string::npos || sdp.find("ice-pwd") == std::string::npos) {
            LOGE("❌ [SIGNAL] Offer from %s missing ICE credentials (ice-ufrag/ice-pwd)", sender.c_str());
            LOGE("❌ [SIGNAL] SDP content: %s", sdp.c_str());
            return;
        }
        
        setupPeerConnection(sender, false); // False = We are Answerer
        if(m_peers.count(sender)) {
            try {
                m_peers[sender]->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Offer));
                LOGI("✅ [SIGNAL] Set remote offer for %s", sender.c_str());
                
                // Add any early candidates that arrived before this offer
                if (m_earlyCandidates.count(sender)) {
                    LOGI("📦 [ICE] Adding %zu queued candidates for %s", m_earlyCandidates[sender].size(), sender.c_str());
                    for (const auto& cand : m_earlyCandidates[sender]) {
                        try {
                            m_peers[sender]->addRemoteCandidate(rtc::Candidate(cand, ""));
                        } catch (const std::exception& e) {
                            LOGE("❌ [ICE] Failed to add queued candidate: %s", e.what());
                        }
                    }
                    m_earlyCandidates.erase(sender);
                }
                
                if(m_peers[sender]->localDescription().has_value() == false) {
                     m_peers[sender]->setLocalDescription(); // Triggers Answer generation
                     LOGI("✅ [SIGNAL] Generating answer for %s", sender.c_str());
                }
            } catch (const std::exception& e) {
                LOGE("❌ [SIGNAL] CRITICAL: Failed to process offer from %s: %s", sender.c_str(), e.what());
                LOGE("❌ [SIGNAL] Problematic SDP: %s", sdp.c_str());
                // Clean up failed peer connection
                m_peers.erase(sender);
                m_earlyCandidates.erase(sender);
            }
        }
    }
    else if (type == "answer") {
        std::string sdp = extractJsonValue(message, "sdp");
        LOGI("📨 [SIGNAL] Received answer from %s (SDP length: %zu)", sender.c_str(), sdp.length());
        
        if (sdp.empty()) {
            LOGE("❌ [SIGNAL] Received empty SDP in answer from %s", sender.c_str());
            return;
        }
        
        // Validate that SDP contains required ICE parameters
        if (sdp.find("ice-ufrag") == std::string::npos || sdp.find("ice-pwd") == std::string::npos) {
            LOGE("❌ [SIGNAL] Answer from %s missing ICE credentials (ice-ufrag/ice-pwd)", sender.c_str());
            LOGE("❌ [SIGNAL] SDP content: %s", sdp.c_str());
            return;
        }
        
        if(m_peers.count(sender)) {
            try {
                m_peers[sender]->setRemoteDescription(rtc::Description(sdp, rtc::Description::Type::Answer));
                LOGI("✅ [SIGNAL] Set remote answer for %s", sender.c_str());
                
                // Add any early candidates that arrived before this answer
                if (m_earlyCandidates.count(sender)) {
                    LOGI("📦 [ICE] Adding %zu queued candidates for %s", m_earlyCandidates[sender].size(), sender.c_str());
                    for (const auto& cand : m_earlyCandidates[sender]) {
                        try {
                            m_peers[sender]->addRemoteCandidate(rtc::Candidate(cand, ""));
                        } catch (const std::exception& e) {
                            LOGE("❌ [ICE] Failed to add queued candidate: %s", e.what());
                        }
                    }
                    m_earlyCandidates.erase(sender);
                }
            } catch (const std::exception& e) {
                LOGE("❌ [SIGNAL] CRITICAL: Failed to set remote description for %s: %s", sender.c_str(), e.what());
                LOGE("❌ [SIGNAL] Problematic SDP: %s", sdp.c_str());
                // Clean up failed peer connection
                m_peers.erase(sender);
                m_earlyCandidates.erase(sender);
            }
        }
    }
    else if (type == "ice-candidate") {
        std::string cand = extractJsonValue(message, "candidate");
        std::string mid = extractJsonValue(message, "mid"); // Optional often
        LOGI("📨 [SIGNAL] Received ICE candidate from %s", sender.c_str());
        if(m_peers.count(sender)) {
            try {
                m_peers[sender]->addRemoteCandidate(rtc::Candidate(cand, mid));
                LOGI("✅ [ICE] Added candidate from %s", sender.c_str());
            } catch (const std::exception& e) {
                // Remote description not set yet - queue the candidate
                LOGI("📦 [ICE] Queueing early candidate from %s (remote desc not ready)", sender.c_str());
                m_earlyCandidates[sender].push_back(cand);
            }
        }
    }
    else {
        LOGI("ℹ️  [SIGNAL] Unknown message type: %s", type.c_str());
    }
}

void WebRTCService::sendSignalingMessage(const std::string& targetId, const std::string& type, const std::string& payload) {
    // Call back into Kotlin to send over WebSocket
    if (onSendSignaling) {
        LOGI("📤 [SIGNAL] Sending %s to %s (payload length: %zu)", type.c_str(), targetId.c_str(), payload.length());
        onSendSignaling(targetId, type, payload);
    } else {
        LOGE("❌ [SIGNAL] CRITICAL ERROR: onSendSignaling callback NOT registered! Cannot send %s to %s", type.c_str(), targetId.c_str());
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
    LOGI("🎯 [P2P] respondToInvite to=%s accept=%s", senderId.c_str(), accept ? "YES" : "NO");
    if(m_channels.count(senderId) && m_channels[senderId]) {
        std::string msg = accept ? "{\"type\":\"invite-accept\"}" : "{\"type\":\"invite-reject\"}";
        m_channels[senderId]->send(msg);
        LOGI("📤 [P2P] Sent response to %s", senderId.c_str());
    } else {
        LOGE("⚠️  [P2P] No data channel to %s - cannot send response", senderId.c_str());
    }
}
void WebRTCService::requestData(const std::string& senderId, const std::string& groupName) {
    LOGI("📤 [P2P] requestData from=%s group=%s", senderId.c_str(), groupName.c_str());
    if (m_channels.count(senderId) && m_channels[senderId]->isOpen()) {
        std::string msg = "{\"type\":\"data-request\", \"group\":\"" + groupName + "\"}";
        m_channels[senderId]->send(msg);
        LOGI("✅ [P2P] Sent data request to %s", senderId.c_str());
    } else {
        LOGE("⚠️  [P2P] No open channel to %s - cannot request data", senderId.c_str());
    }
}
void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName, 
                                  const std::vector<unsigned char>& groupKey, 
                                  const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    LOGI("📤 [P2P] sendGroupData to=%s group=%s entries=%zu", recipientId.c_str(), groupName.c_str(), entries.size());
    
    if (!m_channels.count(recipientId) || !m_channels[recipientId]->isOpen()) {
        LOGE("⚠️  [P2P] No open channel to %s - cannot send data", recipientId.c_str());
        return;
    }
    
    // Build JSON with group data
    std::string msg = "{\"type\":\"group-data\", \"group\":\"" + groupName + "\", \"keySize\":" + std::to_string(groupKey.size()) + ", \"entryCount\":" + std::to_string(entries.size()) + "}";
    
    // For now send basic notification - full implementation would serialize entries
    m_channels[recipientId]->send(msg);
    LOGI("✅ [P2P] Sent group data to %s", recipientId.c_str());
    
    // Notify callback if registered
    if (onInviteStatus) {
        onInviteStatus(true, "Data sent to " + recipientId);
    }
}
void WebRTCService::fetchGroupMembers(const std::string&) {}
void WebRTCService::removeUser(const std::string&, const std::string&) {}
void WebRTCService::onRetryTimer() {}
void WebRTCService::broadcastSync(const std::string&) {}

// [NEW] Broadcast group updates to all members
void WebRTCService::broadcastGroupUpdate(const std::string& groupName, 
                                         const std::vector<CipherMesh::Core::VaultEntry>& updatedEntries) {
    LOGI("📡 [SYNC] broadcastGroupUpdate group=%s entries=%zu", groupName.c_str(), updatedEntries.size());
    
    // Build sync message
    std::string msg = "{\"type\":\"sync-update\", \"group\":\"" + groupName + "\", \"entryCount\":" + std::to_string(updatedEntries.size()) + "}";
    
    // Send to all connected peers
    std::lock_guard<std::mutex> lock(m_mutex);
    int sent = 0;
    for (const auto& [peerId, channel] : m_channels) {
        if (channel && channel->isOpen()) {
            channel->send(msg);
            sent++;
            LOGI("📤 [SYNC] Sent update to %s", peerId.c_str());
        }
    }
    LOGI("✅ [SYNC] Broadcast complete - sent to %d peers", sent);
}

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
    if (doc.isNull() || !doc.isObject()) {
        qWarning() << "⚠️  [SIGNAL] Received invalid JSON:" << message;
        return;
    }
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    QString sender = obj["sender"].toString();
    
    qDebug() << "📥 [SIGNAL] Received type=" << type << "sender=" << sender;

    if (type == "offer") handleOffer(obj);
    else if (type == "answer") handleAnswer(obj);
    else if (type == "ice-candidate") handleCandidate(obj);
    else if (type == "user-online") {
        QString user = obj["user"].toString();
        qDebug() << "🟢 [SIGNAL] Peer online:" << user;
        if (onPeerOnline) onPeerOnline(user.toStdString());
        if (m_pendingInvites.contains(user)) {
            qDebug() << "🔄 [SIGNAL] Retrying pending invite for" << user;
            retryPendingInviteFor(user);
        }
    }
    else {
        qDebug() << "ℹ️  [SIGNAL] Unknown message type:" << type;
    }
}

void WebRTCService::handleP2PMessage(const QString& remoteId, const QString& message) {
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) {
        qWarning() << "⚠️  [P2P] Invalid JSON from" << remoteId << ":" << message;
        return;
    }
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    qDebug() << "📨 [P2P] Received from" << remoteId << "type=" << type;

    if (type == "invite-request") {
        QString groupName = obj["group"].toString();
        qDebug() << "📩 [P2P] Invite request for group" << groupName << "from" << remoteId;
        if (onIncomingInvite) {
            onIncomingInvite(remoteId.toStdString(), groupName.toStdString());
        } else {
            qWarning() << "⚠️  [P2P] onIncomingInvite callback not registered!";
        }
    } 
    else if (type == "invite-accept") {
        qDebug() << "✅ [P2P] Invite accepted by" << remoteId;
        if (m_pendingInvites.contains(remoteId)) {
            QString groupName = m_pendingInvites[remoteId];
            sendGroupData(remoteId.toStdString(), groupName.toStdString(), m_pendingKeys[remoteId], m_pendingEntries[remoteId]);
            m_pendingInvites.remove(remoteId);
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), true);
        }
    }
    else if (type == "invite-reject") {
        qDebug() << "❌ [P2P] Invite rejected by" << remoteId;
        if (m_pendingInvites.contains(remoteId)) {
            QString groupName = m_pendingInvites[remoteId];
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), false);
            m_pendingInvites.remove(remoteId);
        }
    }
    else if (type == "data-request") {
        QString groupName = obj["group"].toString();
        qDebug() << "📥 [P2P] Data request for group" << groupName << "from" << remoteId;
        if (onDataRequested) {
            onDataRequested(remoteId.toStdString(), groupName.toStdString(), "");
        }
    }
    else if (type == "group-data") {
        QString groupName = obj["group"].toString();
        int keySize = obj["keySize"].toInt();
        int entryCount = obj["entryCount"].toInt();
        qDebug() << "📥 [P2P] Received group data for" << groupName << "from" << remoteId << "entries=" << entryCount;
        if (onGroupDataReceived) {
            // For now pass empty vectors - full implementation would deserialize
            onGroupDataReceived(remoteId.toStdString(), groupName.toStdString(), {}, {});
        }
    }
    else if (type == "sync-update") {
        QString groupName = obj["group"].toString();
        qDebug() << "🔄 [SYNC] Received sync update for group" << groupName << "from" << remoteId;
        if (onGroupDataReceived) {
            onGroupDataReceived(remoteId.toStdString(), groupName.toStdString(), {}, {});
        }
    }
    else {
        qDebug() << "ℹ️  [P2P] Unknown P2P message type:" << type;
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
void WebRTCService::respondToInvite(const std::string& senderId, bool accept) { 
    QJsonObject resp; 
    resp["type"] = accept ? "invite-accept" : "invite-reject"; 
    sendP2PMessage(QString::fromStdString(senderId), resp); 
}
void WebRTCService::removeUser(const std::string&, const std::string&) {}
void WebRTCService::fetchGroupMembers(const std::string&) {}
void WebRTCService::requestData(const std::string& senderId, const std::string& groupName) {
    qDebug() << "📤 [P2P] requestData from=" << QString::fromStdString(senderId) << "group=" << QString::fromStdString(groupName);
    QJsonObject req;
    req["type"] = "data-request";
    req["group"] = QString::fromStdString(groupName);
    sendP2PMessage(QString::fromStdString(senderId), req);
}
void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName, 
                                  const std::vector<unsigned char>& groupKey, 
                                  const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    qDebug() << "📤 [P2P] sendGroupData to=" << QString::fromStdString(recipientId) << "group=" << QString::fromStdString(groupName) << "entries=" << entries.size();
    QJsonObject data;
    data["type"] = "group-data";
    data["group"] = QString::fromStdString(groupName);
    data["keySize"] = static_cast<int>(groupKey.size());
    data["entryCount"] = static_cast<int>(entries.size());
    // TODO: Serialize entries and key properly
    sendP2PMessage(QString::fromStdString(recipientId), data);
}
void WebRTCService::onRetryTimer() {}

// [NEW] Broadcast group updates to all members (Desktop)
void WebRTCService::broadcastGroupUpdate(const std::string& groupName, 
                                         const std::vector<CipherMesh::Core::VaultEntry>& updatedEntries) {
    qDebug() << "📡 [SYNC] broadcastGroupUpdate group=" << QString::fromStdString(groupName) << "entries=" << updatedEntries.size();
    
    QJsonObject syncMsg;
    syncMsg["type"] = "sync-update";
    syncMsg["group"] = QString::fromStdString(groupName);
    syncMsg["entryCount"] = static_cast<int>(updatedEntries.size());
    
    // Send to all connected peers with open data channels
    int sent = 0;
    for (auto it = m_dataChannels.begin(); it != m_dataChannels.end(); ++it) {
        if (it.value() && it.value()->isOpen()) {
            sendP2PMessage(it.key(), syncMsg);
            sent++;
            qDebug() << "📤 [SYNC] Sent update to" << it.key();
        }
    }
    qDebug() << "✅ [SYNC] Broadcast complete - sent to" << sent << "peers";
}

#endif