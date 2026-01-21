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

// =========================================================
//  SHARED UTILITY (Available for both platforms)
// =========================================================
static std::string escapeJsonString(const std::string& input)
{
    std::string out;
    out.reserve(input.size());

    for (char c : input) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;
        }
    }
    return out;
}

// =========================================================
//  ANDROID IMPLEMENTATION
// =========================================================
#if defined(__ANDROID__) || defined(ANDROID)

#include <android/log.h>
#define LOG_TAG "WebRTCService"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

std::string unescapeString(const std::string& str) {
    std::string result; result.reserve(str.length());
    for (size_t i = 0; i < str.length(); ++i) {
        if (str[i] == '\\' && i + 1 < str.length()) {
            char next = str[i + 1];
            if (next == 'r') { result += '\r'; ++i; } 
            else if (next == 'n') { result += '\n'; ++i; } 
            else if (next == 't') { result += '\t'; ++i; } 
            else if (next == '\"' || next == '\\') { result += next; ++i; } 
            else if (next == '/') { result += '/'; ++i; } 
            else { result += str[i]; }
        } else { result += str[i]; }
    }
    return result;
}

std::string extractJsonValue(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t start = json.find(search);
    if (start == std::string::npos) return "";
    start += search.length();
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n')) { start++; }
    if (start >= json.length()) return "";
    
    if (json[start] == '\"') {
        start++; 
        size_t end = start;
        while (end < json.length()) {
            if (json[end] == '\"' && json[end-1] != '\\') break;
            end++;
        }
        if (end >= json.length()) return "";
        return unescapeString(json.substr(start, end - start));
    } 
    else if (json[start] == '[' || json[start] == '{') {
        int depth = 1;
        size_t end = start + 1;
        char open = json[start];
        char close = (open == '[') ? ']' : '}';
        while (end < json.length() && depth > 0) {
            if (json[end] == open) depth++;
            else if (json[end] == close) depth--;
            end++;
        }
        return json.substr(start, end - start);
    }
    else {
        size_t end = json.find_first_of(",}", start);
        if (end == std::string::npos) end = json.length();
        return json.substr(start, end - start);
    }
}

WebRTCService::WebRTCService(const std::string& signalingUrl, const std::string& localUserId, void* parent)
    : m_signalingUrl(signalingUrl), m_localUserId(localUserId), m_isConnected(false), m_isAuthenticated(false) {}

WebRTCService::~WebRTCService() { disconnect(); }

void WebRTCService::connect() { m_isConnected = true; LOGI("WebRTCService Logical Connection Active"); }

void WebRTCService::disconnect() {
    m_isConnected = false;
    m_peers.clear();
    m_channels.clear();
}

void WebRTCService::inviteUser(const std::string& groupName, const std::string& userEmail, 
                               const std::vector<unsigned char>& groupKey, 
                               const std::vector<CipherMesh::Core::VaultEntry>& entries,
                               const std::string& memberListJson) {
    LOGI("Inviting user: %s to group: %s", userEmail.c_str(), groupName.c_str());
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (m_peers.count(userEmail)) { m_peers[userEmail]->close(); m_peers.erase(userEmail); }
        m_channels.erase(userEmail);
        m_pendingInvites[userEmail] = groupName;
        m_pendingKeys[userEmail] = groupKey;
        m_pendingEntries[userEmail] = entries;
        m_pendingMembers[userEmail] = memberListJson;
    }
    this->setupPeerConnection(userEmail, true);
}

void WebRTCService::queueInvite(const std::string& groupName, const std::string& userEmail, 
                                const std::vector<unsigned char>& groupKey, 
                                const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    inviteUser(groupName, userEmail, groupKey, entries, "[]");
}

void WebRTCService::setupPeerConnection(const QString& peerId, bool isCaller)
{
    using namespace rtc;

    Configuration config;
    config.iceServers.emplace_back("stun:stun.l.google.com:19302");

    auto pc = std::make_shared<PeerConnection>(config);
    m_peers[peerId] = pc;

    /* ================= ICE CANDIDATES ================= */
    pc->onLocalCandidate([this, peerId](Candidate candidate) {
        std::string json =
            "{\"candidate\":\"" + escapeJsonString(candidate.candidate()) +
            "\",\"mid\":\"" + escapeJsonString(candidate.mid()) + "\"}";

        sendSignalingMessage(peerId, "ice-candidate", json);
    });

    /* ================= CONNECTION STATE ================= */
    pc->onStateChange([peerId](PeerConnection::State state) {
        LOGI("[WebRTC] Peer %s state = %d", peerId.c_str(), static_cast<int>(state));
    });

    /* ================= DATA CHANNEL ================= */
    pc->onDataChannel([this, peerId](std::shared_ptr<DataChannel> dc) {
        LOGI("[WebRTC] DataChannel open from %s", peerId.c_str());
        setupDataChannel(dc, peerId);
    });

    /* ================= CALLER LOGIC ================= */
    if (isCaller) {
        auto dc = pc->createDataChannel("data");
        setupDataChannel(dc, peerId);
        
        pc->setLocalDescription();
        auto offer = pc->localDescription();

        if (offer) {
            std::string json =
                "{\"sdp\":\"" + escapeJsonString(offer->sdp()) +
                "\",\"type\":\"offer\"}";

            sendSignalingMessage(peerId, "offer", json);
        }
    }
}

void WebRTCService::setupDataChannel(std::shared_ptr<rtc::DataChannel> dc, const std::string& peerId) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_channels[peerId] = dc;

    dc->onClosed([this, peerId]() {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_channels.erase(peerId);
        if (m_peers.count(peerId)) { m_peers[peerId]->close(); m_peers.erase(peerId); }
    });

    dc->onOpen([this, peerId]() {
        if (onPeerOnline) onPeerOnline(peerId);

        std::thread([this, peerId]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            if (m_pendingInvites.count(peerId)) {
                std::string msg = "{\"type\":\"invite-request\", \"group\":\"" + m_pendingInvites[peerId] + "\"}";
                if (m_channels.count(peerId)) m_channels[peerId]->send(msg);
            }
        }).detach();
    });

    dc->onMessage([this, peerId](auto data) {
        std::string msg;
        if (std::holds_alternative<std::string>(data)) {
            msg = std::get<std::string>(data);
        } else if (std::holds_alternative<rtc::binary>(data)) {
            auto bin = std::get<rtc::binary>(data);
            msg = std::string(reinterpret_cast<const char*>(bin.data()), bin.size());
        }

        if (msg.empty()) return;
        
        std::string type = extractJsonValue(msg, "type");
        
        if (type == "invite-request") {
            if (onIncomingInvite) onIncomingInvite(peerId, extractJsonValue(msg, "group"));
        }
        else if (type == "invite-accept") {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            
            bool hasPendingInvite = m_pendingInvites.count(peerId) > 0;
            bool hasPendingKeys = m_pendingKeys.count(peerId) > 0;
            bool hasPendingEntries = m_pendingEntries.count(peerId) > 0;
            
            if (hasPendingInvite && hasPendingKeys && hasPendingEntries) {
                std::string mList = m_pendingMembers.count(peerId) ? m_pendingMembers[peerId] : "[]";
                sendGroupData_unsafe(peerId, m_pendingInvites[peerId], m_pendingKeys[peerId], m_pendingEntries[peerId], mList);
            }
            
            m_pendingInvites.erase(peerId); 
            m_pendingKeys.erase(peerId); 
            m_pendingEntries.erase(peerId);
            m_pendingMembers.erase(peerId);
        }
        else if (type == "invite-reject") {
            std::lock_guard<std::recursive_mutex> lock(m_mutex);
            m_pendingInvites.erase(peerId); m_pendingKeys.erase(peerId); m_pendingEntries.erase(peerId); m_pendingMembers.erase(peerId);
        }
        else if (type == "group-data" || type == "entry-data" || type == "member-list" || type == "member-leave" || type == "member-kick") {
            if (onGroupDataReceived) onGroupDataReceived(peerId, msg);
        }
        else if (type == "sync-payload" || type == "sync-ack") {
            if (onSyncMessage) onSyncMessage(peerId, msg);
        }
    });
}

void WebRTCService::receiveSignalingMessage(const std::string& message) {
    std::string type = extractJsonValue(message, "type");
    std::string sender = extractJsonValue(message, "sender");
    
    if (sender.empty()) {
        LOGE("Dropped signaling message (missing sender): %s", message.c_str());
        return;
    }

    std::lock_guard<std::recursive_mutex> lock(m_mutex);

    if (type == "user-online") {
        if (m_pendingInvites.count(sender)) retryPendingInviteFor(sender);
        if (m_peers.count(sender) == 0) this->setupPeerConnection(sender, true);
    } 
    else if (type == "offer") {
        std::string sdp = extractJsonValue(message, "sdp");
        if (sdp.empty()) {
            std::string payload = extractJsonValue(message, "payload");
            if (!payload.empty()) sdp = extractJsonValue(payload, "sdp");
        }

        LOGI("Received OFFER from %s", sender.c_str());

        if(m_peers.count(sender)) { 
            m_peers[sender]->close(); 
            m_peers.erase(sender); 
            m_channels.erase(sender); 
        }
        
        this->setupPeerConnection(sender, false); 
        
        if(m_peers.count(sender)) {
            auto pc = m_peers[sender];
            try {
                pc->setRemoteDescription(rtc::Description(sdp, type));
                if(!pc->localDescription().has_value()) pc->setLocalDescription();
                flushEarlyCandidatesFor(sender);
            } catch (const std::exception& e) {
                LOGE("Error applying remote offer from %s: %s", sender.c_str(), e.what());
            }
        }
    }
    else if (type == "answer") {
        std::string sdp = extractJsonValue(message, "sdp");
        if (sdp.empty()) {
            std::string payload = extractJsonValue(message, "payload");
            if (!payload.empty()) sdp = extractJsonValue(payload, "sdp");
        }

        LOGI("Received ANSWER from %s", sender.c_str());

        if(m_peers.count(sender)) {
            try {
                auto pc = m_peers[sender];
                size_t pos = sdp.find("a=setup:actpass");
                if (pos != std::string::npos) sdp.replace(pos, 15, "a=setup:active ");
                
                pc->setRemoteDescription(rtc::Description(sdp, type));
                flushEarlyCandidatesFor(sender);
            } catch (const std::exception& e) {
                LOGE("Error applying answer from %s: %s", sender.c_str(), e.what());
            }
        }
    }
    else if (type == "ice-candidate") {
        std::string cand = extractJsonValue(message, "candidate");
        std::string mid = extractJsonValue(message, "mid");
        
        if (cand.empty()) {
            std::string payload = extractJsonValue(message, "payload");
            if (!payload.empty()) {
                cand = extractJsonValue(payload, "candidate");
                mid = extractJsonValue(payload, "mid");
            }
        }
        
        if (mid.empty()) mid = extractJsonValue(message, "sdpMid");
        if (mid.empty()) mid = "0";

        if(m_peers.count(sender)) {
            try {
                if (m_peers[sender]->remoteDescription().has_value()) {
                    m_peers[sender]->addRemoteCandidate(rtc::Candidate(cand, mid)); 
                } else { 
                    m_earlyCandidates[sender].push_back(message); 
                }
            } catch(...) {}
        }
    }
}

void WebRTCService::sendSignalingMessage(const std::string& targetId, const std::string& type, const std::string& payload) {
    if (onSignalingMessage) onSignalingMessage(targetId, type, payload);
}

void WebRTCService::retryPendingInviteFor(const std::string& remoteId) {
    if (m_peers.count(remoteId) == 0) this->setupPeerConnection(remoteId, true);
}

void WebRTCService::flushEarlyCandidatesFor(const std::string& peerId) {
    if (m_earlyCandidates.count(peerId) == 0) return;
    for (const auto& msg : m_earlyCandidates[peerId]) {
        std::string cand = extractJsonValue(msg, "candidate");
        std::string mid = extractJsonValue(msg, "mid");
        
        if (cand.empty()) {
            std::string payload = extractJsonValue(msg, "payload");
            if (!payload.empty()) {
                cand = extractJsonValue(payload, "candidate");
                mid = extractJsonValue(payload, "mid");
            }
        }
        
        if (mid.empty()) mid = extractJsonValue(msg, "sdpMid");
        if (mid.empty()) mid = "0";

        if (m_peers.count(peerId)) { 
            try { 
                m_peers[peerId]->addRemoteCandidate(rtc::Candidate(cand, mid)); 
            } catch(...) {}
        }
    }
    m_earlyCandidates.erase(peerId);
}

void WebRTCService::sendP2PMessage(const std::string& targetId, const std::string& jsonPayload) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_channels.count(targetId) && m_channels[targetId]->isOpen()) {
        m_channels[targetId]->send(jsonPayload);
        return;
    }
}

void WebRTCService::sendGroupData_unsafe(const std::string& recipientId, const std::string& groupName, 
                                         const std::vector<unsigned char>& groupKey, 
                                         const std::vector<CipherMesh::Core::VaultEntry>& entries,
                                         const std::string& memberListJson) {
    if (!m_channels.count(recipientId) || !m_channels[recipientId]->isOpen()) return;
    
    std::ostringstream jsonHeader;
    jsonHeader << "{\"type\":\"group-data\",\"group\":\"" << escapeJsonString(groupName) << "\",";
    
    static const char b64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string keyBase64;
    int i = 0, j = 0;
    unsigned char char_array_3[3], char_array_4[4];
    for (unsigned char c : groupKey) {
        char_array_3[i++] = c;
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            for(i = 0; i < 4; i++) keyBase64 += b64_chars[char_array_4[i]];
            i = 0;
        }
    }
    if (i) {
        for(j = i; j < 3; j++) char_array_3[j] = '\0';
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        for (j = 0; j < i + 1; j++) keyBase64 += b64_chars[char_array_4[j]];
        while(i++ < 3) keyBase64 += '=';
    }
    jsonHeader << "\"key\":\"" << keyBase64 << "\"}";
    
    m_channels[recipientId]->send(jsonHeader.str());
    
    for (const auto& e : entries) {
        std::ostringstream entryJson;
        entryJson << "{\"type\":\"entry-data\","
                  << "\"group\":\"" << escapeJsonString(groupName) << "\","
                  << "\"title\":\"" << escapeJsonString(e.title) << "\","
                  << "\"username\":\"" << escapeJsonString(e.username) << "\","
                  << "\"password\":\"" << escapeJsonString(e.password) << "\"," 
                  << "\"url\":\"" << escapeJsonString(e.url) << "\","
                  << "\"notes\":\"" << escapeJsonString(e.notes) << "\","
                  << "\"totpSecret\":\"" << escapeJsonString(e.totpSecret) << "\","
                  << "\"locations\":[";
                  
        for(size_t k = 0; k < e.locations.size(); k++) {
            entryJson << "{\"type\":\"" << escapeJsonString(e.locations[k].type) << "\",";
            entryJson << "\"value\":\"" << escapeJsonString(e.locations[k].value) << "\"}";
            if(k < e.locations.size() - 1) entryJson << ",";
        }
        entryJson << "]}";
        
        m_channels[recipientId]->send(entryJson.str());
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    std::ostringstream memberListMsg;
    std::string safeMembers = memberListJson.empty() ? "[]" : memberListJson;
    memberListMsg << "{\"type\":\"member-list\",\"group\":\"" << escapeJsonString(groupName) << "\",\"members\":" << safeMembers << "}";
    m_channels[recipientId]->send(memberListMsg.str());
    
    m_pendingKeys.erase(recipientId);
    m_pendingEntries.erase(recipientId);
}

void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName,
                                  const std::vector<unsigned char>& groupKey,
                                  const std::vector<CipherMesh::Core::VaultEntry>& entries,
                                  const std::string& memberListJson) {
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    sendGroupData_unsafe(recipientId, groupName, groupKey, entries, memberListJson);
}

void WebRTCService::setAuthenticated(bool auth) { m_isAuthenticated = auth; }
void WebRTCService::sendInvite(const std::string& recipientId, const std::string& groupName) { inviteUser(groupName, recipientId, {}, {}, "[]"); }
void WebRTCService::cancelInvite(const std::string& userId) { m_pendingInvites.erase(userId); m_pendingKeys.erase(userId); m_pendingEntries.erase(userId); if(m_peers.count(userId)) m_peers.erase(userId); }
void WebRTCService::respondToInvite(const std::string& senderId, bool accept) { if(m_channels.count(senderId)) { std::string msg = accept ? "{\"type\":\"invite-accept\"}" : "{\"type\":\"invite-reject\"}"; m_channels[senderId]->send(msg); } }

void WebRTCService::removeUser(const std::string& groupName, const std::string& userId) { 
    std::string msg = "{\"type\":\"member-kick\", \"group\":\"" + escapeJsonString(groupName) + "\"}";
    sendP2PMessage(userId, msg);
}

void WebRTCService::fetchGroupMembers(const std::string&) {}
void WebRTCService::requestData(const std::string& senderId, const std::string& groupName) {
    std::string msg = "{\"type\":\"invite-accept\", \"group\":\"" + escapeJsonString(groupName) + "\"}";
    sendP2PMessage(senderId, msg);
}

void WebRTCService::onRetryTimer() {}
void WebRTCService::broadcastSync(const std::string&) {}

// =========================================================
//  DESKTOP IMPLEMENTATION (Qt / QWebSocket / QJson)
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
#include <QTimer>
#include <QMutex>
#include <QMutexLocker>
#include <variant> 

const std::vector<std::string> STUN_SERVERS = {
    "stun:stun.l.google.com:19302",
    "stun:stun1.l.google.com:19302"
};

const int ONLINE_PING_DELAY_MS = 500;
const int PENDING_INVITES_CHECK_DELAY_MS = 1000;
static QMutex g_peerMutex;

WebRTCService::WebRTCService(const QString& signalingUrl, const std::string& localUserId, QObject *parent)
    : QObject(parent), m_signalingUrl(signalingUrl), m_localUserId(QString::fromStdString(localUserId)), m_isAuthenticated(false), m_reconnectAttempts(0)
{
    rtc::InitLogger(rtc::LogLevel::Error); 
    m_webSocket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    
    connect(m_webSocket, &QWebSocket::connected, this, &WebRTCService::onWsConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &WebRTCService::onWsDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &WebRTCService::onWsTextMessageReceived);
}

WebRTCService::~WebRTCService() {
    if (m_webSocket) m_webSocket->close();
    QMutexLocker locker(&g_peerMutex);
    m_dataChannels.clear();
    m_peerConnections.clear();
}

void WebRTCService::startSignaling() {
    if (m_webSocket) m_webSocket->open(QUrl(m_signalingUrl));
}

void WebRTCService::setIdentityPublicKey(const std::string& pubKey) { m_identityPublicKey = pubKey; }

rtc::Configuration WebRTCService::getIceConfiguration() const {
    rtc::Configuration config;
    for (const auto& s : STUN_SERVERS) config.iceServers.emplace_back(s);
    return config;
}

void WebRTCService::onWsConnected() {
    if (onConnectionStatusChanged) onConnectionStatusChanged(true);
    m_reconnectAttempts = 0;
    
    if (m_isAuthenticated) {
        QJsonObject registration;
        registration["type"] = "register";
        registration["id"] = m_localUserId;
        registration["userId"] = m_localUserId;
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
        
        QMutexLocker l(&g_peerMutex);
        bool connected = m_peerConnections.contains(user);
        l.unlock();
        
        if (!connected) {
             QMetaObject::invokeMethod(this, [this, user]() { this->setupPeerConnection(user, true); }, Qt::QueuedConnection);
        }
    }
}

void WebRTCService::handleP2PMessage(const QString& remoteId, const QString& message) {
    if (onGroupDataReceived) {
         onGroupDataReceived(remoteId.toStdString(), message.toStdString());
    }

    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (doc.isNull()) return;
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();

    if (type == "invite-request") {
        QString groupName = obj["group"].toString();
        if (onIncomingInvite) onIncomingInvite(remoteId.toStdString(), groupName.toStdString());
    } 
    else if (type == "invite-accept") {
        QString groupName = obj["group"].toString(); 
        if (groupName.isEmpty() && m_pendingInvites.contains(remoteId)) {
            groupName = m_pendingInvites[remoteId];
        }

        if (!groupName.isEmpty()) {
            QMutexLocker locker(&g_peerMutex);
            if(m_pendingKeys.count(remoteId) && m_pendingEntries.count(remoteId)) {
                auto k = m_pendingKeys[remoteId];
                auto e = m_pendingEntries[remoteId];
                std::string mList = m_pendingMembers.value(remoteId, "[]").toStdString();
                
                locker.unlock();
                
                QTimer::singleShot(200, this, [this, remoteId, groupName, k, e, mList]() {
                    sendGroupData(remoteId.toStdString(), groupName.toStdString(), k, e, mList);
                });
                
                locker.relock();
            }
            m_pendingInvites.remove(remoteId);
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), true);
        }
    }
    else if (type == "invite-reject") {
        if (m_pendingInvites.contains(remoteId)) {
            QString groupName = m_pendingInvites[remoteId];
            QMutexLocker locker(&g_peerMutex);
            m_pendingInvites.remove(remoteId); m_pendingKeys.erase(remoteId); m_pendingEntries.erase(remoteId); m_pendingMembers.remove(remoteId);
            locker.unlock();
            if (onInviteResponse) onInviteResponse(remoteId.toStdString(), groupName.toStdString(), false);
        }
    }
    else if (type == "sync-payload" || type == "sync-ack") {
        if (onSyncMessage) {
            onSyncMessage(remoteId.toStdString(), message.toStdString());
        }
    }
    else if (type == "group-data" || type == "entry-data" || type == "member-list") {
        if (type == "group-data") {
            QString groupName = obj["group"].toString();
            QString keyBase64 = obj["key"].toString();
            QByteArray keyBytes = QByteArray::fromBase64(keyBase64.toUtf8());
            std::vector<unsigned char> groupKey(keyBytes.begin(), keyBytes.end());
            
            if (m_incomingGroups.contains(remoteId)) {
                if (m_incomingGroups[remoteId].completionTimer) {
                    m_incomingGroups[remoteId].completionTimer->stop();
                    m_incomingGroups[remoteId].completionTimer->deleteLater();
                }
            }
            IncomingGroupData& incoming = m_incomingGroups[remoteId];
            incoming.groupName = groupName;
            incoming.groupKey = groupKey;
            incoming.entries.clear();
            incoming.hasHeader = true;
            
            incoming.completionTimer = new QTimer(this);
            incoming.completionTimer->setSingleShot(true);
            connect(incoming.completionTimer, &QTimer::timeout, this, [this, remoteId]() {
                finalizeIncomingGroupData(remoteId);
            });
            incoming.completionTimer->start(3000); 
        }
        else if (type == "entry-data") {
            if (!m_incomingGroups.contains(remoteId) || !m_incomingGroups[remoteId].hasHeader) return;
            IncomingGroupData& incoming = m_incomingGroups[remoteId];
            
            CipherMesh::Core::VaultEntry entry;
            entry.id = -1;
            entry.title = obj["title"].toString().toStdString();
            entry.username = obj["username"].toString().toStdString();
            entry.password = obj["password"].toString().toStdString();
            entry.notes = obj["notes"].toString().toStdString();
            entry.totpSecret = obj["totpSecret"].toString().toStdString();
            entry.url = obj["url"].toString().toStdString();
            QJsonArray locArray = obj["locations"].toArray();
            for (const auto& locVal : locArray) {
                QJsonObject locObj = locVal.toObject();
                entry.locations.push_back(CipherMesh::Core::Location(-1, locObj["type"].toString().toStdString(), locObj["value"].toString().toStdString()));
            }
            incoming.entries.push_back(entry);
            
            if (incoming.completionTimer) {
                incoming.completionTimer->stop();
                incoming.completionTimer->deleteLater();
            }
            incoming.completionTimer = new QTimer(this);
            incoming.completionTimer->setSingleShot(true);
            connect(incoming.completionTimer, &QTimer::timeout, this, [this, remoteId]() { finalizeIncomingGroupData(remoteId); });
            incoming.completionTimer->start(1000);
        }
    }
}

void WebRTCService::inviteUser(const std::string& groupName, const std::string& userEmail, 
                               const std::vector<unsigned char>& groupKey, 
                               const std::vector<CipherMesh::Core::VaultEntry>& entries,
                               const std::string& memberListJson) {
    QString remoteId = QString::fromStdString(userEmail);
    
    QMutexLocker locker(&g_peerMutex);
    if (m_peerConnections.contains(remoteId)) { 
        m_peerConnections[remoteId]->close(); 
        m_peerConnections.remove(remoteId); 
    }
    m_dataChannels.remove(remoteId);
    
    m_pendingInvites[remoteId] = QString::fromStdString(groupName);
    m_pendingKeys[remoteId] = groupKey;
    m_pendingEntries[remoteId] = entries;
    m_pendingMembers[remoteId] = QString::fromStdString(memberListJson);
    locker.unlock();
    
    QMetaObject::invokeMethod(this, [this, remoteId]() { 
        this->setupPeerConnection(remoteId, true); 
    }, Qt::QueuedConnection);
}

void WebRTCService::createAndSendOffer(const QString& remoteId) {
    QMutexLocker locker(&g_peerMutex);
    if (!m_peerConnections.contains(remoteId)) return;
    auto pc = m_peerConnections[remoteId];
    locker.unlock();
    if (!pc->localDescription().has_value()) pc->setLocalDescription();
}

void WebRTCService::setupPeerConnection(const QString& remoteId, bool isOfferer) {
    QMutexLocker locker(&g_peerMutex);
    if (m_peerConnections.contains(remoteId)) return;
    
    auto pc = std::make_shared<rtc::PeerConnection>(getIceConfiguration());
    m_peerConnections[remoteId] = pc;
    locker.unlock();

    std::weak_ptr<rtc::PeerConnection> weakPc = pc;

    pc->onLocalDescription([this, remoteId, isOfferer](const rtc::Description& desc) {
        QMetaObject::invokeMethod(this, [this, remoteId, desc, isOfferer]() {
            QJsonObject payload;
            payload["type"] = isOfferer ? "offer" : "answer";
            payload["sdp"] = QString::fromStdString(std::string(desc));
            sendSignalingMessage(remoteId, payload);
            if (!isOfferer) flushEarlyCandidatesFor(remoteId);
        }, Qt::QueuedConnection);
    });

    pc->onLocalCandidate([this, remoteId](rtc::Candidate candidate) {
        QMetaObject::invokeMethod(this, [this, remoteId, candidate]() {
            QJsonObject payload;
            payload["type"] = "ice-candidate";
            payload["candidate"] = QString::fromStdString(candidate.candidate());
            payload["mid"] = QString::fromStdString(candidate.mid());
            sendSignalingMessage(remoteId, payload);
        }, Qt::QueuedConnection);
    });

    pc->onStateChange([this, remoteId, weakPc](rtc::PeerConnection::State state) {
        QMetaObject::invokeMethod(this, [this, remoteId, state, weakPc]() {
            if (state == rtc::PeerConnection::State::Failed || state == rtc::PeerConnection::State::Closed) {
                QMutexLocker l(&g_peerMutex);
                if (m_dataChannels.contains(remoteId)) m_dataChannels.remove(remoteId);
                m_incomingGroups.remove(remoteId);
                
                if (state == rtc::PeerConnection::State::Failed && m_pendingInvites.contains(remoteId)) {
                    QTimer::singleShot(2000, this, [this, remoteId]() { retryPendingInviteFor(remoteId); });
                }
            }
        }, Qt::QueuedConnection);
    });

    auto setupDataChannel = [this, remoteId](std::shared_ptr<rtc::DataChannel> dc) {
        QMetaObject::invokeMethod(this, [this, remoteId, dc]() {
            QMutexLocker l(&g_peerMutex);
            m_dataChannels[remoteId] = dc;
            l.unlock();
            
            dc->onOpen([this, remoteId]() {
                 if (onPeerOnline) onPeerOnline(remoteId.toStdString());

                 QTimer::singleShot(500, this, [this, remoteId]() {
                     QMutexLocker l(&g_peerMutex);
                     if (m_pendingInvites.contains(remoteId)) {
                         QJsonObject req; req["type"] = "invite-request"; req["group"] = m_pendingInvites[remoteId];
                         l.unlock();
                         sendP2PMessage(remoteId, req);
                     }
                 });
            });
            
            dc->onMessage([this, remoteId](std::variant<rtc::binary, rtc::string> message) {
                QString msg;
                if (std::holds_alternative<rtc::string>(message)) msg = QString::fromStdString(std::get<rtc::string>(message));
                else if (std::holds_alternative<rtc::binary>(message)) {
                     auto bin = std::get<rtc::binary>(message);
                     msg = QString::fromUtf8(reinterpret_cast<const char*>(bin.data()), bin.size());
                }

                if (!msg.isEmpty()) QMetaObject::invokeMethod(this, [this, remoteId, msg]() { try { handleP2PMessage(remoteId, msg); } catch (...) {} }, Qt::QueuedConnection);
            });
        }, Qt::QueuedConnection);
    };

    if (isOfferer) { 
        auto dc = pc->createDataChannel("ciphermesh-data"); 
        setupDataChannel(dc); 
    } else { 
        pc->onDataChannel(setupDataChannel); 
    }
}

void WebRTCService::handleOffer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpOffer = obj["sdp"].toString();
    
    QMetaObject::invokeMethod(this, [this, senderId, sdpOffer]() {
        QMutexLocker locker(&g_peerMutex);
        if (m_peerConnections.contains(senderId)) {
            m_peerConnections[senderId]->close();
            m_peerConnections.remove(senderId);
            m_dataChannels.remove(senderId);
        }
        locker.unlock();
        
        this->setupPeerConnection(senderId, false); 
        
        locker.relock();
        std::shared_ptr<rtc::PeerConnection> pc = m_peerConnections.value(senderId, nullptr);
        locker.unlock();
        
        if (!pc) return;
        try { pc->setRemoteDescription(rtc::Description(sdpOffer.toStdString(), "offer")); } catch (...) {}
    }, Qt::QueuedConnection);
}

void WebRTCService::handleAnswer(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QString sdpAnswer = obj["sdp"].toString();
    
    QMetaObject::invokeMethod(this, [this, senderId, sdpAnswer]() {
        QMutexLocker locker(&g_peerMutex);
        if (!m_peerConnections.contains(senderId)) return;
        auto pc = m_peerConnections[senderId];
        locker.unlock();

        std::string sdpStr = sdpAnswer.toStdString();
        size_t pos = sdpStr.find("a=setup:actpass");
        if (pos != std::string::npos) sdpStr.replace(pos, 15, "a=setup:active ");
        
        try {
            pc->setRemoteDescription(rtc::Description(sdpStr, "answer"));
            flushEarlyCandidatesFor(senderId);
        } catch(...) {}
    }, Qt::QueuedConnection);
}

void WebRTCService::handleCandidate(const QJsonObject& obj) {
    QString senderId = obj["sender"].toString();
    QMutexLocker locker(&g_peerMutex);
    if (!m_peerConnections.contains(senderId)) {
        m_earlyCandidates[senderId].push_back(obj);
        return;
    }
    auto pc = m_peerConnections[senderId];
    locker.unlock();
    
    if (pc->remoteDescription().has_value()) {
        try { pc->addRemoteCandidate(rtc::Candidate(obj["candidate"].toString().toStdString(), "0")); } catch(...) {}
    } else { m_earlyCandidates[senderId].push_back(obj); }
}

void WebRTCService::flushEarlyCandidatesFor(const QString& peerId) {
    if (!m_earlyCandidates.contains(peerId)) return;
    QMutexLocker locker(&g_peerMutex);
    if (!m_peerConnections.contains(peerId)) return;
    auto pc = m_peerConnections[peerId];
    locker.unlock();

    for (const auto& obj : m_earlyCandidates[peerId]) handleCandidate(obj);
    m_earlyCandidates.remove(peerId);
}

void WebRTCService::sendP2PMessage(const QString& remoteId, const QJsonObject& payload) {
    QMutexLocker locker(&g_peerMutex);
    if (m_dataChannels.contains(remoteId) && m_dataChannels[remoteId] && m_dataChannels[remoteId]->isOpen()) {
        auto dc = m_dataChannels[remoteId];
        locker.unlock();
        dc->send(QJsonDocument(payload).toJson(QJsonDocument::Compact).toStdString());
    }
}

void WebRTCService::sendSignalingMessage(const QString& targetId, const QJsonObject& payload) {
    if (m_webSocket) {
        QJsonObject msg = payload; msg["target"] = targetId; msg["sender"] = m_localUserId;
        m_webSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void WebRTCService::retryPendingInviteFor(const QString& remoteId) {
    if (m_pendingInvites.contains(remoteId)) {
        QMutexLocker locker(&g_peerMutex);
        bool needsSetup = !m_peerConnections.contains(remoteId);
        locker.unlock();
        
        if(needsSetup) {
            QMetaObject::invokeMethod(this, [this, remoteId]() { 
                this->setupPeerConnection(remoteId, true); 
            }, Qt::QueuedConnection);
        }
    }
}

void WebRTCService::checkAndSendPendingInvites() {
    QList<QString> keys = m_pendingInvites.keys();
    for(const auto& key : keys) { retryPendingInviteFor(key); }
}

void WebRTCService::queueInvite(const std::string& groupName, const std::string& userEmail, const std::vector<unsigned char>& groupKey, const std::vector<CipherMesh::Core::VaultEntry>& entries) {
    inviteUser(groupName, userEmail, groupKey, entries, "[]");
}

void WebRTCService::finalizeIncomingGroupData(const QString& remoteId) {
    if (!m_incomingGroups.contains(remoteId) || !m_incomingGroups[remoteId].hasHeader) return;
    
    IncomingGroupData& incoming = m_incomingGroups[remoteId];
    
    if (incoming.completionTimer) {
        incoming.completionTimer->stop();
        incoming.completionTimer->deleteLater();
        incoming.completionTimer = nullptr;
    }
    
    m_incomingGroups.remove(remoteId);
}

void WebRTCService::setAuthenticated(bool authenticated) {
    m_isAuthenticated = authenticated;
    if (authenticated && m_webSocket->state() == QAbstractSocket::ConnectedState) onWsConnected();
}

void WebRTCService::sendInvite(const std::string& recipientId, const std::string& groupName) {
    inviteUser(groupName, recipientId, {}, {}, "[]");
}

void WebRTCService::cancelInvite(const std::string& userId) { 
    QMutexLocker locker(&g_peerMutex);
    m_pendingInvites.remove(QString::fromStdString(userId)); 
}

void WebRTCService::respondToInvite(const std::string& senderId, bool accept) { 
    QJsonObject resp; resp["type"] = accept ? "invite-accept" : "invite-reject"; 
    sendP2PMessage(QString::fromStdString(senderId), resp); 
}

void WebRTCService::removeUser(const std::string& groupName, const std::string& userId) {
    QJsonObject payload;
    payload["type"] = "member-kick";
    payload["group"] = QString::fromStdString(groupName);
    sendP2PMessage(QString::fromStdString(userId), payload);
}

void WebRTCService::fetchGroupMembers(const std::string& groupName) {
    QJsonObject payload;
    payload["type"] = "request-members";
    payload["group"] = QString::fromStdString(groupName);
    
    QMutexLocker locker(&g_peerMutex);
    for(auto it = m_dataChannels.begin(); it != m_dataChannels.end(); ++it) {
        if(it.value()->isOpen()) {
            QString pid = it.key();
            locker.unlock();
            sendP2PMessage(pid, payload);
            locker.relock();
        }
    }
}

void WebRTCService::requestData(const std::string& senderId, const std::string& groupName) {
    QJsonObject payload;
    payload["type"] = "invite-accept"; 
    payload["group"] = QString::fromStdString(groupName);
    sendP2PMessage(QString::fromStdString(senderId), payload);
}

void WebRTCService::sendGroupData(const std::string& recipientId, const std::string& groupName, 
                                   const std::vector<unsigned char>& groupKey, 
                                   const std::vector<CipherMesh::Core::VaultEntry>& entries,
                                   const std::string& memberListJson) {
    QString recipient = QString::fromStdString(recipientId);
    
    QJsonObject header;
    header["type"] = "group-data";
    header["group"] = QString::fromStdString(groupName);
    QByteArray keyBytes(reinterpret_cast<const char*>(groupKey.data()), groupKey.size());
    header["key"] = QString::fromUtf8(keyBytes.toBase64());
    sendP2PMessage(recipient, header);
    
    for (const auto& entry : entries) {
        QJsonObject entryObj;
        entryObj["type"] = "entry-data";
        entryObj["group"] = QString::fromStdString(groupName);
        entryObj["title"] = QString::fromStdString(entry.title);
        entryObj["username"] = QString::fromStdString(entry.username);
        entryObj["password"] = QString::fromStdString(entry.password);
        entryObj["notes"] = QString::fromStdString(entry.notes);
        entryObj["totpSecret"] = QString::fromStdString(entry.totpSecret);
        entryObj["url"] = QString::fromStdString(entry.url);
        
        QJsonArray locArray;
        for (const auto& loc : entry.locations) {
            QJsonObject l; l["type"] = QString::fromStdString(loc.type); l["value"] = QString::fromStdString(loc.value); locArray.append(l);
        }
        entryObj["locations"] = locArray;
        
        sendP2PMessage(recipient, entryObj);
        QThread::msleep(20); 
    }
    
    QJsonObject memberListMsg;
    memberListMsg["type"] = "member-list";
    memberListMsg["group"] = QString::fromStdString(groupName);
    
    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(memberListJson));
    if (doc.isArray()) {
        memberListMsg["members"] = doc.array();
    } else {
        memberListMsg["members"] = QJsonArray();
    }
    
    sendP2PMessage(recipient, memberListMsg);
}

void WebRTCService::onRetryTimer() {}
void WebRTCService::broadcastSync(const std::string&) {}

#endif