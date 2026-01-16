#include "webrtcservice.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <map>
#include <sstream>
#include <iomanip>

// Check for Android Environment
#if defined(__ANDROID__) || defined(ANDROID)

#include <android/log.h>
#define LOG_TAG "WebRTCService"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// --- JSON Helpers ---

std::string unescapeString(const std::string& str) {
    std::string result;
    result.reserve(str.length());
    
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            char next = str[i + 1];
            if (next == 'r') {
                result += '\r';
                ++i;
            } else if (next == 'n') {
                result += '\n';
                ++i;
            } else if (next == 't') {
                result += '\t';
                ++i;
            } else if (next == '\"' || next == '\\') {
                result += next;
                ++i;
            } else {
                result += str[i];
            }
        } else {
            result += str[i];
        }
    }
    
    return result;
}

// [NEW] JSON escape function for encoding strings
std::string escapeJsonString(const std::string& str) {
    std::ostringstream escaped;
    for (unsigned char c : str) {  // Use unsigned char to handle all byte values correctly
        switch (c) {
            case '\"': escaped << "\\\""; break;
            case '\\': escaped << "\\\\"; break;
            case '\b': escaped << "\\b"; break;
            case '\f': escaped << "\\f"; break;
            case '\n': escaped << "\\n"; break;
            case '\r': escaped << "\\r"; break;
            case '\t': escaped << "\\t"; break;
            default:
                if (c < 0x20) {
                    // Control character - use unicode escape
                    escaped << "\\u" << std::hex << std::setw(4) << std::setfill('0') << (int)c;
                } else {
                    escaped << (char)c;  // Cast back to char for printing
                }
        }
    }
    return escaped.str();
}

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
    std::string value = json.substr(start, end - start);
    
    // Unescape string values (important for SDP which contains \r\n)
    if (isString) {
        value = unescapeString(value);
    }
    
    return value;
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

    // [FIX] Add error handlers for peer connection state changes
    pc->onStateChange([this, peerId](rtc::PeerConnection::State state) {
        LOGI("PC State Change for %s: %d", peerId.c_str(), (int)state);
        
        if (state == rtc::PeerConnection::State::Failed) {
            LOGE("Peer connection FAILED for %s", peerId.c_str());
            // Cleanup and potentially retry
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_channels.count(peerId)) {
                m_channels.erase(peerId);
            }
            if (m_peers.count(peerId)) {
                m_peers.erase(peerId);
            }
            
            // Retry if we have a pending invite
            if (m_pendingInvites.count(peerId)) {
                LOGI("Retrying connection for %s", peerId.c_str());
                std::this_thread::sleep_for(std::chrono::seconds(2));
                retryPendingInviteFor(peerId);
            }
        } else if (state == rtc::PeerConnection::State::Closed) {
            LOGI("Peer connection CLOSED for %s", peerId.c_str());
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_channels.count(peerId)) {
                m_channels.erase(peerId);
            }
        } else if (state == rtc::PeerConnection::State::Connected) {
            LOGI("Peer connection CONNECTED to %s", peerId.c_str());
        }
    });

    // 1. Handle ICE Candidates
    pc->onLocalCandidate([this, peerId](auto candidate) {
        LOGI("Generated ICE Candidate for %s", peerId.c_str());
        sendSignalingMessage(peerId, "ice-candidate", std::string(candidate.candidate()));
    });

    // 2. Handle Data Channel (if we are receiver)
    pc->onDataChannel([this, peerId](auto dc) {
        LOGI("Received DataChannel from %s", peerId.c_str());
        setupDataChannel(dc, peerId);
    });

    // 3. Create Data Channel and send offer if we are the offerer
    if (isOfferer) {
        auto dc = pc->createDataChannel("ciphermesh-data");
        setupDataChannel(dc, peerId);
        
        // Set gathering state callback ONCE here for offerer
        pc->onGatheringStateChange([this, peerId, pc](rtc::PeerConnection::GatheringState state) {
            if (state == rtc::PeerConnection::GatheringState::Complete) {
                LOGI("ICE gathering COMPLETE for %s, sending offer", peerId.c_str());
                auto desc = pc->localDescription();
                if (desc.has_value()) {
                    std::string type = desc->typeString();
                    std::string sdp = std::string(*desc);
                    sendSignalingMessage(peerId, type, sdp);
                }
            }
        });
        
        pc->setLocalDescription(); // This triggers gathering
    }
}

void WebRTCService::setupDataChannel(std::shared_ptr<rtc::DataChannel> dc, const std::string& peerId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_channels[peerId] = dc;

    // [FIX] Add data channel error handlers
    dc->onError([this, peerId](std::string error) {
        LOGE("DataChannel error for %s: %s", peerId.c_str(), error.c_str());
    });
    
    dc->onClosed([this, peerId]() {
        LOGI("DataChannel CLOSED for %s", peerId.c_str());
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_channels.count(peerId)) {
            m_channels.erase(peerId);
        }
        
        // Cleanup peer connection
        if (m_peers.count(peerId)) {
            m_peers[peerId]->close();
            m_peers.erase(peerId);
        }
    });

    dc->onOpen([this, peerId]() {
        LOGI("DataChannel OPEN with %s", peerId.c_str());
        // If we have a pending invite, send the request now
        if (m_pendingInvites.count(peerId)) {
            std::string msg = "{\"type\":\"invite-request\", \"group\":\"" + m_pendingInvites[peerId] + "\"}";
            LOGI("Sending invite-request to %s for group %s", peerId.c_str(), m_pendingInvites[peerId].c_str());
            if (m_channels.count(peerId) && m_channels[peerId]) {
                m_channels[peerId]->send(msg);
                LOGI("Invite-request sent successfully");
            } else {
                LOGE("Channel not available for %s", peerId.c_str());
            }
        } else {
            LOGI("No pending invite for %s", peerId.c_str());
        }
    });

    dc->onMessage([this, peerId](auto data) {
        if (std::holds_alternative<std::string>(data)) {
            std::string msg = std::get<std::string>(data);
            LOGI("Received P2P Msg from %s: %s", peerId.c_str(), msg.c_str());
            
            std::string type = extractJsonValue(msg, "type");
            if (type == "invite-request") {
                // Notify UI via callback
                std::string group = extractJsonValue(msg, "group");
                if (onIncomingInvite) {
                    onIncomingInvite(peerId, group);
                } else {
                    LOGE("onIncomingInvite callback not set!");
                }
            }
            else if (type == "invite-accept") {
                // Handle invite acceptance
                if (m_pendingInvites.count(peerId)) {
                    LOGI("Invite accepted by %s", peerId.c_str());
                    // Send group data if we have it stored
                    if (m_pendingKeys.count(peerId) && m_pendingEntries.count(peerId)) {
                        sendGroupData(peerId, m_pendingInvites[peerId], m_pendingKeys[peerId], m_pendingEntries[peerId]);
                    }
                    m_pendingInvites.erase(peerId);
                    m_pendingKeys.erase(peerId);
                    m_pendingEntries.erase(peerId);
                }
            }
            else if (type == "invite-reject") {
                // Handle invite rejection
                if (m_pendingInvites.count(peerId)) {
                    LOGI("Invite rejected by %s", peerId.c_str());
                    m_pendingInvites.erase(peerId);
                    m_pendingKeys.erase(peerId);
                    m_pendingEntries.erase(peerId);
                }
            }
            else if (type == "group-data") {
                // Handle received group data
                LOGI("Received group-data from %s", peerId.c_str());
                
                std::string groupName = extractJsonValue(msg, "group");
                std::string keyBase64 = extractJsonValue(msg, "key");
                
                if (groupName.empty() || keyBase64.empty()) {
                    LOGE("Invalid group-data: missing group name or key");
                    return;
                }
                
                LOGI("Group: %s, Key length: %zu", groupName.c_str(), keyBase64.length());
                
                // Pass the entire JSON message to JNI layer for processing
                // The JNI layer will parse it and save to the vault
                if (onGroupDataReceived) {
                    onGroupDataReceived(peerId, msg);
                    LOGI("Group data passed to JNI callback");
                } else {
                    LOGE("onGroupDataReceived callback not set!");
                }
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
        
        // [FIX] If we already have a peer connection, close it first
        // This handles simultaneous connection attempts
        if (m_peers.count(sender)) {
            LOGI("Closing existing peer connection for %s before processing offer", sender.c_str());
            m_peers[sender]->close();
            m_peers.erase(sender);
            if (m_channels.count(sender)) {
                m_channels.erase(sender);
            }
        }
        
        setupPeerConnection(sender, false); // False = We are Answerer
        if(m_peers.count(sender)) {
            auto pc = m_peers[sender];
            
            // Set up handler to send answer after ICE gathering completes
            // IMPORTANT: Must be set BEFORE setLocalDescription to avoid race
            pc->onGatheringStateChange([this, sender, pc](rtc::PeerConnection::GatheringState state) {
                if (state == rtc::PeerConnection::GatheringState::Complete) {
                    LOGI("ICE gathering COMPLETE for %s (answer)", sender.c_str());
                    auto desc = pc->localDescription();
                    if (desc.has_value()) {
                        LOGI("Sending answer to %s with ICE candidates", sender.c_str());
                        sendSignalingMessage(sender, "answer", std::string(*desc));
                    }
                }
            });
            
            pc->setRemoteDescription(rtc::Description(sdp, type));
            
            if(pc->localDescription().has_value() == false) {
                 // Create answer - libdatachannel will auto-generate it
                 pc->setLocalDescription();
            }
            // [FIX] Flush any early ICE candidates that arrived before the offer
            flushEarlyCandidatesFor(sender);
        }
    }
    else if (type == "answer") {
        std::string sdp = extractJsonValue(message, "sdp");
        if(m_peers.count(sender)) {
            // [FIX] libdatachannel may send actpass in answers which violates spec
            // Fix the SDP before applying it to avoid crash
            size_t pos = sdp.find("a=setup:actpass");
            if (pos != std::string::npos) {
                sdp.replace(pos, 15, "a=setup:active ");
                LOGI("Fixed received answer SDP - replaced actpass with active");
            }
            
            m_peers[sender]->setRemoteDescription(rtc::Description(sdp, type));
            // [FIX] Flush any early ICE candidates that arrived before the answer
            flushEarlyCandidatesFor(sender);
        }
    }
    else if (type == "ice-candidate") {
        std::string cand = extractJsonValue(message, "candidate");
        std::string mid = extractJsonValue(message, "mid"); // Optional often
        if(m_peers.count(sender)) {
            // [FIX] Check if remote description is set before adding candidate
            // If not, queue it for later to avoid "Got a remote candidate without remote description" exception
            if (m_peers[sender]->remoteDescription().has_value()) {
                try {
                    m_peers[sender]->addRemoteCandidate(rtc::Candidate(cand, mid));
                } catch (const std::exception& e) {
                    LOGE("Failed to add ICE candidate from %s: %s", sender.c_str(), e.what());
                }
            } else {
                // Queue this candidate for later processing
                LOGI("Queueing early ICE candidate from %s", sender.c_str());
                m_earlyCandidates[sender].push_back(message);
            }
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

void WebRTCService::flushEarlyCandidatesFor(const std::string& peerId) {
    if (m_earlyCandidates.count(peerId) == 0) return;
    
    LOGI("Flushing %zu early candidates for %s", m_earlyCandidates[peerId].size(), peerId.c_str());
    
    for (const auto& candidateMsg : m_earlyCandidates[peerId]) {
        std::string cand = extractJsonValue(candidateMsg, "candidate");
        std::string mid = extractJsonValue(candidateMsg, "mid");
        
        if (m_peers.count(peerId)) {
            try {
                m_peers[peerId]->addRemoteCandidate(rtc::Candidate(cand, mid));
            } catch (const std::exception& e) {
                LOGE("Failed to add queued ICE candidate: %s", e.what());
            }
        }
    }
    
    m_earlyCandidates.erase(peerId);
}

// --- Required Stubs (Filled) ---
void WebRTCService::setAuthenticated(bool auth) { m_isAuthenticated = auth; }
void WebRTCService::sendInvite(const std::string& recipientId, const std::string& groupName) { inviteUser(groupName, recipientId, {}, {}); }
void WebRTCService::cancelInvite(const std::string& userId) { 
    m_pendingInvites.erase(userId); 
    m_pendingKeys.erase(userId);
    m_pendingEntries.erase(userId);
    if(m_peers.count(userId)) m_peers.erase(userId);
}
void WebRTCService::respondToInvite(const std::string& senderId, bool accept) {
    if(m_channels.count(senderId)) {
        std::string msg = accept ? "{\"type\":\"invite-accept\"}" : "{\"type\":\"invite-reject\"}";
        m_channels[senderId]->send(msg);
        LOGI("Sent %s to %s", accept ? "invite-accept" : "invite-reject", senderId.c_str());
    } else {
        LOGE("Cannot respond to invite: no channel to %s", senderId.c_str());
    }
}
void WebRTCService::requestData(const std::string&, const std::string&) {}

void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName,
                                   const std::vector<unsigned char>& groupKey,
                                   const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    // Check if we have an active data channel
    if (m_channels.count(recipientId) == 0 || !m_channels[recipientId] || !m_channels[recipientId]->isOpen()) {
        LOGE("Cannot send group data: no active channel to %s", recipientId.c_str());
        return;
    }
    
    LOGI("Sending group data for '%s' to %s", groupName.c_str(), recipientId.c_str());
    LOGI("Group key size: %zu bytes, Entries: %zu", groupKey.size(), entries.size());
    
    // Build JSON message manually (since we don't have Qt on Android)
    std::ostringstream json;
    json << "{\"type\":\"group-data\",\"group\":\"" << escapeJsonString(groupName) << "\",";
    
    // Encode group key as base64
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string keyBase64;
    int i = 0;
    int j = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    for (unsigned char c : groupKey) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; i < 4; i++) keyBase64 += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) keyBase64 += base64_chars[char_array_4[j]];
        while(i++ < 3) keyBase64 += '=';
    }
    
    json << "\"key\":\"" << keyBase64 << "\",\"entries\":[";
    
    // Serialize entries with proper JSON escaping
    for (size_t idx = 0; idx < entries.size(); idx++) {
        const auto& entry = entries[idx];
        if (idx > 0) json << ",";
        json << "{";
        json << "\"id\":" << entry.id << ",";
        json << "\"groupId\":" << entry.groupId << ",";
        json << "\"title\":\"" << escapeJsonString(entry.title) << "\",";
        json << "\"username\":\"" << escapeJsonString(entry.username) << "\",";
        json << "\"password\":\"" << escapeJsonString(entry.password) << "\",";
        json << "\"url\":\"" << escapeJsonString(entry.url) << "\",";
        json << "\"notes\":\"" << escapeJsonString(entry.notes) << "\",";
        json << "\"totpSecret\":\"" << escapeJsonString(entry.totpSecret) << "\",";
        json << "\"entryType\":\"" << escapeJsonString(entry.entryType) << "\",";
        json << "\"createdAt\":" << entry.createdAt << ",";
        json << "\"updatedAt\":" << entry.updatedAt << ",";
        json << "\"lastAccessed\":" << entry.lastAccessed << ",";
        json << "\"passwordExpiry\":" << entry.passwordExpiry;
        json << "}";
    }
    
    json << "]}";
    
    std::string jsonStr = json.str();
    LOGI("Sending group data message, size: %zu bytes", jsonStr.length());
    
    // Send via data channel
    m_channels[recipientId]->send(jsonStr);
    
    LOGI("Group data sent successfully to %s", recipientId.c_str());
    
    // Clean up pending data
    m_pendingKeys.erase(recipientId);
    m_pendingEntries.erase(recipientId);
}

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
    
    // [FIX] Ensure m_isAuthenticated is checked BEFORE sending registration
    // This prevents race condition where registration is sent before vault is unlocked
    if (m_isAuthenticated) {
        QJsonObject registration;
        registration["type"] = "register";
        // [FIX] Send both "id" and "userId" for backwards compatibility with signal server
        registration["id"] = m_localUserId;
        registration["userId"] = m_localUserId;
        if (!m_identityPublicKey.empty()) registration["pubKey"] = QString::fromStdString(m_identityPublicKey);

        m_webSocket->sendTextMessage(QJsonDocument(registration).toJson(QJsonDocument::Compact));
        qDebug() << "WebRTC: Registered user" << m_localUserId;
        
        QTimer::singleShot(ONLINE_PING_DELAY_MS, this, &WebRTCService::sendOnlinePing);
        QTimer::singleShot(PENDING_INVITES_CHECK_DELAY_MS, this, &WebRTCService::checkAndSendPendingInvites);
    } else {
        qWarning() << "WebRTC: Connected but not authenticated - waiting for authentication";
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

    qDebug() << "WebRTC: Received P2P message from" << remoteId << "type:" << type;

    if (type == "invite-request") {
        QString groupName = obj["group"].toString();
        qDebug() << "WebRTC: Invite request for group:" << groupName;
        if (onIncomingInvite) onIncomingInvite(remoteId.toStdString(), groupName.toStdString());
    } 
    else if (type == "invite-accept") {
        qDebug() << "WebRTC: Invite accepted by" << remoteId;
        if (m_pendingInvites.contains(remoteId)) {
            QString groupName = m_pendingInvites[remoteId];
            sendGroupData(remoteId.toStdString(), groupName.toStdString(), m_pendingKeys[remoteId], m_pendingEntries[remoteId]);
            m_pendingInvites.remove(remoteId);
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), true);
        } else {
            qWarning() << "WebRTC: Received accept but no pending invite for" << remoteId;
        }
    }
    else if (type == "invite-reject") {
        qDebug() << "WebRTC: Invite rejected by" << remoteId;
        if (m_pendingInvites.contains(remoteId)) {
            QString groupName = m_pendingInvites[remoteId];
            m_pendingInvites.remove(remoteId);
            m_pendingKeys.erase(remoteId);
            m_pendingEntries.erase(remoteId);
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), false);
        }
    }
    else if (type == "group-data") {
        qDebug() << "WebRTC: Received group data from" << remoteId;
        // Desktop receiving group data (e.g., when mobile invites desktop)
        // This would need implementation if desktop can also be invited by mobile
        qWarning() << "WebRTC: Desktop group-data reception not yet implemented";
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
    
    // Just trigger local description - the gathering callback is already set in setupPeerConnection
    pc->setLocalDescription();
}

void WebRTCService::setupPeerConnection(const QString& remoteId, bool isOfferer) {
    if (m_peerConnections.contains(remoteId)) return;
    auto pc = std::make_shared<rtc::PeerConnection>(getIceConfiguration());
    m_peerConnections[remoteId] = pc;

    // [FIX] Add error handlers for peer connection state changes
    pc->onStateChange([this, remoteId](rtc::PeerConnection::State state) {
        QMetaObject::invokeMethod(this, [this, remoteId, state]() {
            qDebug() << "WebRTC: PC State Change for" << remoteId << ":" << (int)state;
            
            if (state == rtc::PeerConnection::State::Failed) {
                qCritical() << "WebRTC: Peer connection FAILED for" << remoteId;
                // Cleanup and potentially retry
                if (m_dataChannels.contains(remoteId)) {
                    m_dataChannels.remove(remoteId);
                }
                if (m_peerConnections.contains(remoteId)) {
                    m_peerConnections.remove(remoteId);
                }
                
                // Retry if we have a pending invite
                if (m_pendingInvites.contains(remoteId)) {
                    qDebug() << "WebRTC: Retrying connection for" << remoteId;
                    QTimer::singleShot(2000, this, [this, remoteId]() {
                        retryPendingInviteFor(remoteId);
                    });
                }
            } else if (state == rtc::PeerConnection::State::Closed) {
                qDebug() << "WebRTC: Peer connection CLOSED for" << remoteId;
                if (m_dataChannels.contains(remoteId)) {
                    m_dataChannels.remove(remoteId);
                }
            } else if (state == rtc::PeerConnection::State::Connected) {
                qDebug() << "WebRTC: Peer connection CONNECTED to" << remoteId;
            }
        }, Qt::QueuedConnection);
    });

    // [FIX] Add gathering state change handler to detect ICE failures and send offer/answer
    pc->onGatheringStateChange([this, remoteId, isOfferer](rtc::PeerConnection::GatheringState state) {
        QMetaObject::invokeMethod(this, [this, remoteId, state, isOfferer]() {
            qDebug() << "WebRTC: ICE Gathering State for" << remoteId << ":" << (int)state;
            
            if (state == rtc::PeerConnection::GatheringState::Complete) {
                qDebug() << "WebRTC: ICE gathering COMPLETE for" << remoteId;
                
                // Send offer or answer when gathering completes
                if (m_peerConnections.contains(remoteId)) {
                    auto pc = m_peerConnections[remoteId];
                    auto desc = pc->localDescription();
                    if (desc.has_value()) {
                        QJsonObject payload;
                        payload["type"] = isOfferer ? "offer" : "answer";
                        payload["sdp"] = QString::fromStdString(std::string(*desc));
                        sendSignalingMessage(remoteId, payload);
                        
                        // Flush early candidates for answers
                        if (!isOfferer) {
                            flushEarlyCandidatesFor(remoteId);
                        }
                    }
                }
            }
        }, Qt::QueuedConnection);
    });

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
            
            // [FIX] Add data channel error handlers
            dc->onError([this, remoteId](std::string error) {
                QMetaObject::invokeMethod(this, [this, remoteId, error]() {
                    qCritical() << "WebRTC: DataChannel error for" << remoteId << ":" << QString::fromStdString(error);
                }, Qt::QueuedConnection);
            });
            
            dc->onClosed([this, remoteId]() {
                QMetaObject::invokeMethod(this, [this, remoteId]() {
                    qDebug() << "WebRTC: DataChannel CLOSED for" << remoteId;
                    if (m_dataChannels.contains(remoteId)) {
                        m_dataChannels.remove(remoteId);
                    }
                    
                    // Cleanup peer connection
                    if (m_peerConnections.contains(remoteId)) {
                        m_peerConnections[remoteId]->close();
                        m_peerConnections.remove(remoteId);
                    }
                }, Qt::QueuedConnection);
            });
            
            dc->onOpen([this, remoteId]() {
                 QMetaObject::invokeMethod(this, [this, remoteId]() {
                    qDebug() << "WebRTC: DataChannel OPEN for" << remoteId;
                    if (m_pendingInvites.contains(remoteId)) {
                        QJsonObject req; req["type"] = "invite-request"; req["group"] = m_pendingInvites[remoteId];
                        qDebug() << "WebRTC: Sending invite-request for group:" << m_pendingInvites[remoteId];
                        sendP2PMessage(remoteId, req);
                    } else {
                        qDebug() << "WebRTC: No pending invite for" << remoteId;
                    }
                }, Qt::QueuedConnection);
            });
            
            dc->onMessage([this, remoteId](std::variant<rtc::binary, rtc::string> message) {
                QString msg;
                if (std::holds_alternative<rtc::string>(message)) {
                    msg = QString::fromStdString(std::get<rtc::string>(message));
                    qDebug() << "WebRTC: Received raw message from" << remoteId << ":" << msg;
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
        // [FIX] If we already have a peer connection for this user, close it first
        // This handles the case where both sides try to connect simultaneously
        if (m_peerConnections.contains(senderId)) {
            qDebug() << "WebRTC: Closing existing peer connection for" << senderId << "before processing offer";
            m_peerConnections[senderId]->close();
            m_peerConnections.remove(senderId);
            m_dataChannels.remove(senderId);
        }
        
        setupPeerConnection(senderId, false); // false = answerer, callback already set in setupPeerConnection
        std::shared_ptr<rtc::PeerConnection> pc = m_peerConnections[senderId];
        
        try {
            pc->setRemoteDescription(rtc::Description(sdpOffer.toStdString(), "offer"));
            // Create answer - libdatachannel will auto-generate it based on the offer
            pc->setLocalDescription();
        } catch (const std::exception& e) {
            qCritical() << "WebRTC: Error setting descriptions:" << e.what();
        }
    }, Qt::QueuedConnection);
}

void WebRTCService::handleAnswer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpAnswer = obj["sdp"].toString();
    QMetaObject::invokeMethod(this, [this, senderId, sdpAnswer]() {
        if (!m_peerConnections.contains(senderId)) return;
        
        // [FIX] libdatachannel may send actpass in answers which violates spec
        // Fix the SDP before applying it to avoid crash
        std::string sdpStr = sdpAnswer.toStdString();
        size_t pos = sdpStr.find("a=setup:actpass");
        if (pos != std::string::npos) {
            sdpStr.replace(pos, 15, "a=setup:active ");
            qDebug() << "WebRTC: Fixed received answer SDP - replaced actpass with active";
        }
        
        m_peerConnections[senderId]->setRemoteDescription(rtc::Description(sdpStr, "answer"));
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

void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName, 
                                   const std::vector<unsigned char>& groupKey, 
                                   const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    QString recipient = QString::fromStdString(recipientId);
    
    // Check if we have an active data channel
    if (!m_dataChannels.contains(recipient) || !m_dataChannels[recipient] || !m_dataChannels[recipient]->isOpen()) {
        qCritical() << "WebRTC: Cannot send group data - no active channel to" << recipient;
        return;
    }
    
    qDebug() << "WebRTC: Sending group data for" << QString::fromStdString(groupName) << "to" << recipient;
    qDebug() << "WebRTC: Group key size:" << groupKey.size() << "bytes";
    qDebug() << "WebRTC: Number of entries:" << entries.size();
    
    // Build the group-data message
    QJsonObject msg;
    msg["type"] = "group-data";
    msg["group"] = QString::fromStdString(groupName);
    
    // Encode group key as base64
    QByteArray keyBytes(reinterpret_cast<const char*>(groupKey.data()), groupKey.size());
    msg["key"] = QString::fromUtf8(keyBytes.toBase64());
    
    // Serialize entries
    QJsonArray entriesArray;
    for (const auto& entry : entries) {
        QJsonObject entryObj;
        entryObj["id"] = entry.id;
        entryObj["groupId"] = entry.groupId;
        entryObj["title"] = QString::fromStdString(entry.title);
        entryObj["username"] = QString::fromStdString(entry.username);
        entryObj["password"] = QString::fromStdString(entry.password); // Already encrypted
        entryObj["url"] = QString::fromStdString(entry.url);
        entryObj["notes"] = QString::fromStdString(entry.notes);
        entryObj["totpSecret"] = QString::fromStdString(entry.totpSecret);
        entryObj["entryType"] = QString::fromStdString(entry.entryType);
        entryObj["createdAt"] = static_cast<qint64>(entry.createdAt);
        entryObj["updatedAt"] = static_cast<qint64>(entry.updatedAt);
        entryObj["lastAccessed"] = static_cast<qint64>(entry.lastAccessed);
        entryObj["passwordExpiry"] = static_cast<qint64>(entry.passwordExpiry);
        
        entriesArray.append(entryObj);
    }
    msg["entries"] = entriesArray;
    
    // Send the message
    QString jsonStr = QJsonDocument(msg).toJson(QJsonDocument::Compact);
    qDebug() << "WebRTC: Sending group data message, size:" << jsonStr.length() << "bytes";
    
    sendP2PMessage(recipient, msg);
    
    qDebug() << "WebRTC: Group data sent successfully to" << recipient;
    
    // Clean up pending data
    m_pendingInvites.remove(recipient);
    m_pendingKeys.erase(recipient);
    m_pendingEntries.erase(recipient);
}

void WebRTCService::onRetryTimer() {}

#endif