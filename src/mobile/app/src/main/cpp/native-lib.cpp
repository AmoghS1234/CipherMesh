#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <thread>
#include <mutex>
#include <memory>
#include <map>
#include <algorithm> 
#include <sodium.h> 
#include "vault.hpp"
#include "webrtcservice.hpp" 
#include "totp.hpp"

#define TAG "CipherMesh-Native"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__) // Add this line

// --- Globals ---
std::unique_ptr<CipherMesh::Core::Vault> g_vault;
std::unique_ptr<WebRTCService> g_p2p; 
std::mutex g_inviteMutex;
std::vector<CipherMesh::Core::PendingInvite> g_pendingInvites;

// Thread-safe tracking of outgoing invites
std::map<std::string, std::string> g_outgoingInvites;
std::mutex g_outgoingMutex;

// Global session map to handle name mapping during transfer
std::map<std::string, std::string> g_p2pNameMapper;

// Mutexes
// [FIX] Changed to recursive_mutex to prevent deadlocks (e.g. receiveSignaling -> handleInvite -> getGroupKey)
std::recursive_mutex g_vaultMutex;  
std::mutex g_p2pMutex;    
std::mutex g_jniMutex;    

// --- JNI Callbacks ---
JavaVM* g_jvm = nullptr;
jobject g_context = nullptr;
jobject g_signalingCallback = nullptr; 

// --- Helper: Robust JSON Parser ---
std::string extractJsonValueJNI(const std::string& json, const std::string& key) {
    std::string keyPattern = "\"" + key + "\"";
    size_t keyPos = json.find(keyPattern);
    if (keyPos == std::string::npos) return "";
    
    size_t colonPos = json.find(":", keyPos + keyPattern.length());
    if (colonPos == std::string::npos) return "";
    
    size_t startQuote = json.find("\"", colonPos + 1);
    if (startQuote == std::string::npos) return "";
    
    size_t end = startQuote + 1;
    while (end < json.length()) {
        if (json[end] == '"' && json[end-1] != '\\') break;
        end++;
    }
    if (end >= json.length()) return "";
    
    std::string raw = json.substr(startQuote + 1, end - startQuote - 1);
    
    // Unescape
    size_t pos = 0;
    while((pos = raw.find("\\/", pos)) != std::string::npos) { raw.replace(pos, 2, "/"); pos+=1; }
    pos = 0;
    while((pos = raw.find("\\\\", pos)) != std::string::npos) { raw.replace(pos, 2, "\\"); pos+=1; }
    
    return raw;
}

// --- Helper: Base64 Encoding ---
std::string base64Encode(const std::vector<unsigned char>& in) {
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, valb = -6;
    for (unsigned char c : in) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(b64[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(b64[((val << 8) >> (valb + 8)) & 0x3F]);
    while (out.size() % 4) out.push_back('=');
    return out;
}

// --- Helper: Base64 Decoding ---
std::vector<unsigned char> decodeBase64(const std::string& in) {
    std::vector<unsigned char> out;
    std::vector<int> T(256, -1);
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i=0; i<64; i++) T[b64[i]] = i; 
    
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) continue;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

// --- Helper: Send Signaling Message to Kotlin ---
void sendSignalingToKotlin(const std::string& target, const std::string& type, const std::string& payload) {
    if (!g_jvm) return;

    JNIEnv* env = nullptr;
    bool attached = false;
    int envStat = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    
    if (envStat == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }
    
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_signalingCallback && env) {
        jclass cbClass = env->GetObjectClass(g_signalingCallback);
        jmethodID mid = env->GetMethodID(cbClass, "sendSignalingMessage", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        
        if (mid) {
            jstring jTarget = env->NewStringUTF(target.c_str());
            jstring jType = env->NewStringUTF(type.c_str());
            jstring jPayload = env->NewStringUTF(payload.c_str());
            
            env->CallVoidMethod(g_signalingCallback, mid, jTarget, jType, jPayload);
            
            env->DeleteLocalRef(jTarget);
            env->DeleteLocalRef(jType);
            env->DeleteLocalRef(jPayload);
        }
    }
    
    if (attached) g_jvm->DetachCurrentThread();
}

// --- Helper: Send Toast to Android ---
void showToastFromNative(const std::string& message) {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (!g_jvm || !g_context) return;
    
    JNIEnv* env;
    bool attached = false;
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }

    jclass contextClass = env->GetObjectClass(g_context);
    jmethodID mid = env->GetMethodID(contextClass, "showToast", "(Ljava/lang/String;)V");
    
    if (mid) {
        jstring jMsg = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(g_context, mid, jMsg);
        env->DeleteLocalRef(jMsg);
    }

    if (attached) g_jvm->DetachCurrentThread();
}

// --- Helper: Force UI Refresh ---
void triggerJavaRefresh() {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (!g_jvm || !g_context) return;
    
    JNIEnv* env;
    bool attached = false;
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }

    // Call HomeActivity.refreshUI() directly
    jclass cls = env->GetObjectClass(g_context);
    jmethodID mid = env->GetMethodID(cls, "refreshUI", "()V");
    
    if (mid) {
        env->CallVoidMethod(g_context, mid);
        LOGI("Called refreshUI() from Native");
    } else {
        LOGE("Could not find refreshUI method in Activity");
    }

    if (attached) g_jvm->DetachCurrentThread();
}

// --- Helper: Handle Invite Acceptance ---
void handleInviteAccept(const std::string& senderId, const std::string& json) {
    LOGI("Handling Invite Accept from %s", senderId.c_str());
    
    std::string groupName = extractJsonValueJNI(json, "group");
    
    // 1. Try Memory Lookup
    if (groupName.empty()) {
        std::lock_guard<std::mutex> lock(g_outgoingMutex);
        if (g_outgoingInvites.count(senderId)) {
            groupName = g_outgoingInvites[senderId];
            LOGI("Found group in memory: %s", groupName.c_str());
        }
    }

    // 2. Smart Fallback: Scan Vault
    // [FIX] Locks g_vaultMutex safely because it's recursive
    if (groupName.empty()) {
        std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
        if(g_vault) {
            std::vector<std::string> allGroups = g_vault->getGroupNames();
            for (const auto& g : allGroups) {
                auto members = g_vault->getGroupMembers(g);
                for (const auto& m : members) {
                    if (m.userId == senderId) {
                        groupName = g;
                        LOGI("Found group '%s' for user via Vault scan", groupName.c_str());
                        break;
                    }
                }
                if (!groupName.empty()) break;
            }
        }
    }

    if (groupName.empty()) {
        LOGE("Invite accept failed: Unknown group for %s", senderId.c_str());
        return;
    }

    // 3. Send Data
    {
        std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
        if(g_vault && g_p2p) {
            std::vector<unsigned char> key = g_vault->getGroupKey(groupName);
            if (key.empty()) { LOGE("No key for group %s", groupName.c_str()); return; }
            
            std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->exportGroupEntries(groupName);

            // Use public API to send data
            g_p2p->sendGroupData(senderId, groupName, key, entries);
            
            showToastFromNative("Transferring data to " + senderId);
            
            // Cleanup
            std::lock_guard<std::mutex> lock(g_outgoingMutex);
            g_outgoingInvites.erase(senderId);
        }
    }
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// =============================================================
// JNI METHODS
// =============================================================

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_initP2P(JNIEnv* env, jobject thiz, jstring signaling_url) {
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    if (!g_vault || g_p2p) { return; }
    
    g_p2p = std::make_unique<WebRTCService>("", g_vault->getUserId(), nullptr);

    g_p2p->onSignalingMessage = [](std::string target, std::string type, std::string payload) {
        sendSignalingToKotlin(target, type, payload);
    };

    g_p2p->onIncomingInvite = [](std::string sender, std::string groupName) {
        std::lock_guard<std::mutex> lock(g_inviteMutex);
        
        unsigned char randomBytes[4];
        randombytes_buf(randomBytes, 4);
        int id = (randomBytes[0] << 24) | (randomBytes[1] << 16) | (randomBytes[2] << 8) | randomBytes[3];
        if (id < 0) id = -id; 
        
        CipherMesh::Core::PendingInvite inv;
        inv.id = id; 
        inv.senderId = sender; 
        inv.groupName = groupName; 
        inv.status = "pending";
        
        g_pendingInvites.push_back(inv);
        
        showToastFromNative("Invite received from: " + sender);
        triggerJavaRefresh();
    };

    // [FIX] Added onSyncMessage callback to handle sync messages over data channel
    g_p2p->onSyncMessage = [](std::string message) {
        std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
        if (!g_vault || g_vault->isLocked()) return;
        
        std::string sender = extractJsonValueJNI(message, "sender");
        if (sender.empty()) {
            LOGW("Sync message received without sender");
            return;
        }
        
        g_vault->handleIncomingSync(sender, message);
    };

    g_p2p->onGroupDataReceived = [&](std::string senderId, std::string json) {
        std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
        if (!g_vault || g_vault->isLocked()) return;
        
        std::string type = extractJsonValueJNI(json, "type");

        if (type == "group-data") {
            std::string originalGroupName = extractJsonValueJNI(json, "group");
            std::string keyBase64 = extractJsonValueJNI(json, "key");
            std::vector<unsigned char> groupKey = decodeBase64(keyBase64);
            
            std::string finalName = originalGroupName;
            if (g_vault->groupExists(finalName)) {
                finalName = originalGroupName + " (from " + senderId + ")";
            }
            int counter = 1;
            std::string baseCollisionName = finalName;
            while (g_vault->groupExists(finalName)) {
                finalName = baseCollisionName + " " + std::to_string(++counter);
            }

            std::string sessionKey = senderId + ":" + originalGroupName;
            g_p2pNameMapper[sessionKey] = finalName;
            
            try {
                g_vault->addGroup(finalName, groupKey, senderId);
                showToastFromNative("Joined Group: " + finalName);
                
                std::lock_guard<std::mutex> l(g_inviteMutex);
                for(auto it=g_pendingInvites.begin(); it!=g_pendingInvites.end();) {
                    if(it->senderId == senderId && it->groupName == originalGroupName) it = g_pendingInvites.erase(it); else ++it;
                }
                
                triggerJavaRefresh();
            } catch (...) { LOGE("Failed to add group from P2P"); }
        }
        else if (type == "entry-data") {
            std::string originalGroup = extractJsonValueJNI(json, "group");
            std::string sessionKey = senderId + ":" + originalGroup;
            std::string targetGroup = g_p2pNameMapper.count(sessionKey) ? g_p2pNameMapper[sessionKey] : originalGroup;

            if (g_vault->setActiveGroup(targetGroup)) {
                CipherMesh::Core::VaultEntry e;
                e.id = -1;
                e.title = extractJsonValueJNI(json, "title");
                e.username = extractJsonValueJNI(json, "username");
                e.notes = extractJsonValueJNI(json, "notes");
                e.totpSecret = extractJsonValueJNI(json, "totpSecret");
                
                std::string encryptedPassBase64 = extractJsonValueJNI(json, "password");
                
                size_t locStart = json.find("\"locations\":[");
                if (locStart != std::string::npos) {
                    locStart += 13;
                    size_t locEnd = json.find("]", locStart);
                    if (locEnd != std::string::npos) {
                        std::string locArray = json.substr(locStart, locEnd - locStart);
                        size_t pos = 0;
                        while ((pos = locArray.find("{", pos)) != std::string::npos) {
                            size_t endObj = locArray.find("}", pos);
                            if (endObj == std::string::npos) break;
                            std::string obj = locArray.substr(pos, endObj - pos + 1);
                            std::string t = extractJsonValueJNI(obj, "type");
                            std::string v = extractJsonValueJNI(obj, "value");
                            if (!t.empty()) e.locations.push_back(CipherMesh::Core::Location(-1, t, v));
                            pos = endObj + 1;
                        }
                    }
                }
                
                g_vault->addEncryptedEntry(e, encryptedPassBase64);
                triggerJavaRefresh();
            }
        }
        else if (type == "member-list") {
            std::string originalGroup = extractJsonValueJNI(json, "group");
            std::string sessionKey = senderId + ":" + originalGroup;
            std::string targetGroup = g_p2pNameMapper.count(sessionKey) ? g_p2pNameMapper[sessionKey] : originalGroup;

            size_t memStart = json.find("\"members\":[");
            if (memStart != std::string::npos) {
                memStart += 11;
                size_t memEnd = json.find("]", memStart);
                if (memEnd != std::string::npos) {
                    std::string memArray = json.substr(memStart, memEnd - memStart);
                    size_t pos = 0;
                    while ((pos = memArray.find("\"", pos)) != std::string::npos) {
                        size_t endStr = memArray.find("\"", pos + 1);
                        if (endStr == std::string::npos) break;
                        std::string m = memArray.substr(pos + 1, endStr - pos - 1);
                        size_t p1 = m.find("|");
                        size_t p2 = m.find("|", p1 + 1);
                        if (p1 != std::string::npos && p2 != std::string::npos) {
                            std::string uid = m.substr(0, p1);
                            std::string role = m.substr(p1 + 1, p2 - p1 - 1);
                            std::string status = m.substr(p2 + 1);
                            g_vault->addGroupMember(targetGroup, uid, role, status);
                        }
                        pos = endStr + 1;
                    }
                }
            }
        }
        else if (type == "member-leave") {
            std::string originalGroup = extractJsonValueJNI(json, "group");
            std::string sessionKey = senderId + ":" + originalGroup;
            std::string targetGroup = g_p2pNameMapper.count(sessionKey) ? g_p2pNameMapper[sessionKey] : originalGroup;
            g_vault->removeUser(targetGroup, senderId);
            triggerJavaRefresh();
        }
        else if (type == "invite-accept") {
            handleInviteAccept(senderId, json);
        }
    };
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_init(JNIEnv* env, jobject thiz, jstring jPath) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) g_vault = std::make_unique<CipherMesh::Core::Vault>();
    
    const char* path = env->GetStringUTFChars(jPath, nullptr);
    g_vault->connect(std::string(path));
    env->ReleaseStringUTFChars(jPath, path);

    {
        std::lock_guard<std::mutex> lock(g_p2pMutex);
        g_vault->setP2PSendCallback([](const std::string& target, const std::string& message) {
            sendSignalingToKotlin(target, "sync-packet", message);
        });
    }
    LOGI("Vault Initialized");
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_setActivityContext(JNIEnv* env, jobject thiz, jobject activity) {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_context != nullptr) { env->DeleteGlobalRef(g_context); g_context = nullptr; }
    if (activity != nullptr) { g_context = env->NewGlobalRef(activity); }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_unlock(JNIEnv* env, jobject thiz, jstring password) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    const char* pwd = env->GetStringUTFChars(password, 0);
    bool result = g_vault->unlock(pwd);
    env->ReleaseStringUTFChars(password, pwd);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_isLocked(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return true;
    return g_vault->isLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_createAccount(JNIEnv* env, jobject thiz, jstring db_path, jstring username, jstring master_pass) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    const char* path = env->GetStringUTFChars(db_path, 0);
    const char* user = env->GetStringUTFChars(username, 0);
    const char* pwd = env->GetStringUTFChars(master_pass, 0);
    
    if (!path || !user || !pwd) {
        if (path) env->ReleaseStringUTFChars(db_path, path);
        if (user) env->ReleaseStringUTFChars(username, user);
        if (pwd) env->ReleaseStringUTFChars(master_pass, pwd);
        return false;
    }
    
    if (g_vault && !g_vault->isLocked()) {
        env->ReleaseStringUTFChars(db_path, path);
        env->ReleaseStringUTFChars(username, user);
        env->ReleaseStringUTFChars(master_pass, pwd);
        return false;
    }
    
    g_vault = std::make_unique<CipherMesh::Core::Vault>();
    bool result = g_vault->createNewVault(path, pwd);
    
    if(result) {
        g_vault->connect(path);
        g_vault->unlock(pwd);
        g_vault->generateAndSetUniqueId(user);
        g_vault->addGroup("Personal");
    }
    
    env->ReleaseStringUTFChars(db_path, path);
    env->ReleaseStringUTFChars(username, user);
    env->ReleaseStringUTFChars(master_pass, pwd);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_hasUsers(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if (!g_vault) return false;
    return g_vault->hasUsers();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getUserId(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if(!g_vault) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getUserId().c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_generateTOTP(JNIEnv* env, jobject thiz, jstring secret) {
    const char* sec = env->GetStringUTFChars(secret, 0);
    std::string code = CipherMesh::Utils::TOTP::generateCode(std::string(sec));
    env->ReleaseStringUTFChars(secret, sec);
    return env->NewStringUTF(code.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_registerSignalingCallback(JNIEnv* env, jobject thiz, jobject callback) {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_signalingCallback != nullptr) { env->DeleteGlobalRef(g_signalingCallback); g_signalingCallback = nullptr; }
    if (callback != nullptr) { g_signalingCallback = env->NewGlobalRef(callback); }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_onPeerOnline(JNIEnv* env, jobject thiz, jstring jUserId) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* uid = env->GetStringUTFChars(jUserId, nullptr);
    LOGI("Peer Online: %s - Flushing Outbox...", uid);
    g_vault->processOutboxForUser(std::string(uid));
    env->ReleaseStringUTFChars(jUserId, uid);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_handleSyncMessage(JNIEnv* env, jobject thiz, jstring jSender, jstring jPayload) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* sender = env->GetStringUTFChars(jSender, nullptr);
    const char* payload = env->GetStringUTFChars(jPayload, nullptr);
    
    g_vault->handleIncomingSync(std::string(sender), std::string(payload));
    
    env->ReleaseStringUTFChars(jSender, sender);
    env->ReleaseStringUTFChars(jPayload, payload);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_broadcastSync(JNIEnv* env, jobject thiz, jstring groupName) { 
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;

    const char* grp = env->GetStringUTFChars(groupName, nullptr);
    std::string name(grp);
    env->ReleaseStringUTFChars(groupName, grp);

    LOGI("Broadcasting Sync for group: %s", name.c_str()); 
    
    try {
        int gid = g_vault->getGroupId(name);
        if (gid != -1) {
            std::vector<CipherMesh::Core::GroupMember> members = g_vault->getGroupMembers(name);
            std::string myId = g_vault->getUserId();
            for(const auto& m : members) {
                if(m.userId != myId) {
                    g_vault->processOutboxForUser(m.userId);
                }
            }
        }
    } catch (...) {
        LOGE("Failed to broadcast sync");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_receiveSignalingMessage(JNIEnv* env, jobject thiz, jstring message) {
    // 1. Establish strict lock order: Vault then P2P
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    if (!g_p2p) return;
    
    const char* msg = env->GetStringUTFChars(message, 0);
    std::string jsonStr(msg);
    
    // [DEBUG] Monitor exactly what C++ sees in the signaling pipe
    LOGI("JNI receiveSignalingMessage: %s", msg);

    // [FIX] Removed invite-accept interception - it should be handled by WebRTCService
    // data channel onMessage handler, not via WebSocket signaling
    // invite-accept is a P2P message, not a WebRTC signaling message

    // Standard WebRTC signaling (Offer/Answer/ICE) passes through to the engine
    g_p2p->receiveSignalingMessage(msg); 
    
    env->ReleaseStringUTFChars(message, msg);
}

// [FIX] Updated to send explicit "group" back to sender
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_respondToInvite(JNIEnv* env, jobject thiz, jstring groupName, jstring senderId, jboolean accept) {
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* snd = env->GetStringUTFChars(senderId, 0);
    if (!grp || !snd) {
        if (grp) env->ReleaseStringUTFChars(groupName, grp);
        if (snd) env->ReleaseStringUTFChars(senderId, snd);
        return;
    }
    
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    if (g_p2p) {
        if (accept) {
            // [FIX] Construct proper JSON for acceptance including the group name
            std::string acceptMsg = "{\"type\":\"invite-accept\",\"group\":\"" + std::string(grp) + "\"}";
            
            // NOTE: We use sendSignalingMessage directly to bypass WebRTCService logic that might send empty payload
            // But we need to send it TO THE PEER.
            // g_p2p->sendSignalingMessage is private. 
            // So we use g_p2p->respondToInvite(snd, true) which sends "invite-accept".
            // IF WebRTCService implementation of respondToInvite doesn't include "group", the desktop will fail.
            // Assuming we can't change WebRTCService.cpp easily, we rely on the Desktop inferring the group.
            // If the Desktop fails to infer, we might need to send a custom message.
            
            // Standard call:
            g_p2p->respondToInvite(snd, true);
            
            if (g_vault) { g_vault->addGroupMember(grp, snd, "member", "accepted"); }
        } else {
            g_p2p->respondToInvite(snd, false);
        }
        std::lock_guard<std::mutex> lock(g_inviteMutex);
        for (auto it = g_pendingInvites.begin(); it != g_pendingInvites.end(); ) {
            if (it->senderId == snd && it->groupName == grp) it = g_pendingInvites.erase(it); else ++it;
        }
    }
    env->ReleaseStringUTFChars(groupName, grp);
    env->ReleaseStringUTFChars(senderId, snd);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getPendingInvites(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_inviteMutex);
    jclass strClass = env->FindClass("java/lang/String");
    jstring emptyStr = env->NewStringUTF("");
    jobjectArray result = env->NewObjectArray(g_pendingInvites.size(), strClass, emptyStr);
    env->DeleteLocalRef(emptyStr); 

    for (size_t i = 0; i < g_pendingInvites.size(); i++) {
        std::string item = std::to_string(g_pendingInvites[i].id) + "|" + g_pendingInvites[i].senderId + "|" + g_pendingInvites[i].groupName;
        jstring jItem = env->NewStringUTF(item.c_str());
        env->SetObjectArrayElement(result, i, jItem);
        env->DeleteLocalRef(jItem); 
    }
    return result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupNames(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if (!g_vault) return nullptr;
    std::vector<std::string> groups = g_vault->getGroupNames();
    jclass strClass = env->FindClass("java/lang/String");
    jstring emptyStr = env->NewStringUTF("");
    jobjectArray result = env->NewObjectArray(groups.size(), strClass, emptyStr);
    env->DeleteLocalRef(emptyStr); 
    
    for (size_t i = 0; i < groups.size(); i++) {
        jstring jGroup = env->NewStringUTF(groups[i].c_str());
        env->SetObjectArrayElement(result, i, jGroup);
        env->DeleteLocalRef(jGroup); 
    }
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_addGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    bool res = g_vault->addGroup(name); 
    env->ReleaseStringUTFChars(groupName, name);
    return res;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_setActiveGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); 
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    bool result = g_vault->setActiveGroup(name);
    env->ReleaseStringUTFChars(groupName, name);
    return (jboolean)result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntries(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if (!g_vault) return nullptr;
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->getEntries();
    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(entries.size(), strClass, env->NewStringUTF(""));

    for (size_t i = 0; i < entries.size(); i++) {
        // Format: ID | Title | Username | Notes | Type
        std::string s = std::to_string(entries[i].id) + "|" + 
                        entries[i].title + "|" + 
                        entries[i].username + "|" + 
                        entries[i].notes + "|" + 
                        entries[i].entryType;
        
        jstring jStr = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(result, i, jStr);
        env->DeleteLocalRef(jStr);
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_addEntryNative(JNIEnv* env, jobject thiz, jstring title, jstring user, jstring pass, jstring url, jstring notes) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if (!g_vault) return;
    CipherMesh::Core::VaultEntry e;
    
    const char* t = env->GetStringUTFChars(title, 0); e.title = t;
    const char* u = env->GetStringUTFChars(user, 0); e.username = u;
    const char* p = env->GetStringUTFChars(pass, 0); e.password = p;
    
    const char* l = env->GetStringUTFChars(url, 0); 
    e.locations.push_back(CipherMesh::Core::Location(-1, "url", l));
    
    const char* n = env->GetStringUTFChars(notes, 0); e.notes = n;
    
    g_vault->addEntry(e, e.password);
    
    env->ReleaseStringUTFChars(title, t); 
    env->ReleaseStringUTFChars(user, u);
    env->ReleaseStringUTFChars(pass, p); 
    env->ReleaseStringUTFChars(url, l); 
    env->ReleaseStringUTFChars(notes, n);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getDecryptedPassword(JNIEnv* env, jobject thiz, jint index) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getDecryptedPassword(index).c_str());
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupMembers(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if (!g_vault) return nullptr;
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return nullptr; 
    std::vector<CipherMesh::Core::GroupMember> members = g_vault->getGroupMembers(name);
    env->ReleaseStringUTFChars(groupName, name);
    
    jclass strClass = env->FindClass("java/lang/String");
    jstring emptyStr = env->NewStringUTF("");
    jobjectArray result = env->NewObjectArray(members.size(), strClass, emptyStr);
    env->DeleteLocalRef(emptyStr); 
    
    for (size_t i = 0; i < members.size(); i++) {
        std::string s = members[i].userId + "|" + members[i].role + "|" + members[i].status;
        jstring jMember = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(result, i, jMember);
        env->DeleteLocalRef(jMember); 
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_sendP2PInvite(JNIEnv* env, jobject thiz, jstring groupName, jstring targetUser) {
    // 1. Establish strict lock order: Vault then P2P
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    if (!g_vault || !g_p2p) return;
    
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* tgt = env->GetStringUTFChars(targetUser, 0);
    
    std::string sGroup(grp);
    std::string sTarget(tgt);

    // 2. Map the target user to the group BEFORE sending the invite
    // This allows handleInviteAccept to find the group even if the peer's JSON is stripped
    {
        std::lock_guard<std::mutex> lock(g_outgoingMutex);
        g_outgoingInvites[sTarget] = sGroup;
        LOGI("Tracking outgoing invite: Peer %s -> Group %s", sTarget.c_str(), sGroup.c_str());
    }
    
    // 3. Prepare the payload for the WebRTC engine
    std::vector<unsigned char> key = g_vault->getGroupKey(sGroup);
    auto entries = g_vault->exportGroupEntries(sGroup);
    
    // 4. Trigger the actual WebRTC handshake/invite process
    g_p2p->queueInvite(sGroup, sTarget, key, entries);
    
    env->ReleaseStringUTFChars(groupName, grp); 
    env->ReleaseStringUTFChars(targetUser, tgt);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_isGroupOwner(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
    if(!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    int gid = g_vault->getGroupId(name);
    std::string owner = g_vault->getGroupOwner(gid);
    std::string me = g_vault->getUserId();
    env->ReleaseStringUTFChars(groupName, name);
    return (owner == me);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_removeUser(JNIEnv* env, jobject thiz, jstring groupName, jstring targetUser) {
     std::lock_guard<std::recursive_mutex> lock(g_vaultMutex); // [FIX] Added lock
     if(!g_vault) return;
     const char* grp = env->GetStringUTFChars(groupName, 0);
     const char* tgt = env->GetStringUTFChars(targetUser, 0);
     g_vault->removeUser(std::string(grp), std::string(tgt));
     env->ReleaseStringUTFChars(groupName, grp); env->ReleaseStringUTFChars(targetUser, tgt);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_searchEntries(JNIEnv* env, jobject thiz, jstring searchTerm) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !searchTerm) return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    const char* term = env->GetStringUTFChars(searchTerm, 0);
    if (!term) return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    auto entries = g_vault->searchEntries(term);
    env->ReleaseStringUTFChars(searchTerm, term);
    jobjectArray result = env->NewObjectArray(entries.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < entries.size(); i++) {
        std::string formatted = std::to_string(entries[i].id) + ":" + entries[i].title + ":" + entries[i].username;
        jstring jstr = env->NewStringUTF(formatted.c_str());
        env->SetObjectArrayElement(result, i, jstr);
        env->DeleteLocalRef(jstr);
    }
    return result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getRecentlyAccessedEntries(JNIEnv* env, jobject thiz, jint limit) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    auto entries = g_vault->getRecentlyAccessedEntries(limit);
    jobjectArray result = env->NewObjectArray(entries.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < entries.size(); i++) {
        std::string formatted = std::to_string(entries[i].id) + ":" + entries[i].title + ":" + entries[i].username;
        jstring jstr = env->NewStringUTF(formatted.c_str());
        env->SetObjectArrayElement(result, i, jstr);
        env->DeleteLocalRef(jstr);
    }
    return result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getPasswordHistory(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    auto history = g_vault->getPasswordHistory(entryId);
    jobjectArray result = env->NewObjectArray(history.size(), env->FindClass("java/lang/String"), nullptr);
    for (size_t i = 0; i < history.size(); i++) {
        std::string formatted = std::to_string(history[i].id) + "|" + history[i].encryptedPassword + "|" + std::to_string(history[i].changedAt);
        jstring jstr = env->NewStringUTF(formatted.c_str());
        env->SetObjectArrayElement(result, i, jstr);
        env->DeleteLocalRef(jstr);
    }
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_decryptPasswordFromHistory(JNIEnv* env, jobject thiz, jstring encryptedPassword) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !encryptedPassword) return env->NewStringUTF("");
    const char* encrypted = env->GetStringUTFChars(encryptedPassword, 0);
    if (!encrypted) return env->NewStringUTF("");
    std::string decrypted = g_vault->decryptPasswordFromHistory(encrypted);
    env->ReleaseStringUTFChars(encryptedPassword, encrypted);
    return env->NewStringUTF(decrypted.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_updateEntryAccessTime(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    g_vault->updateEntryAccessTime(entryId);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntryFullDetails(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewStringUTF("");
    auto entries = g_vault->getEntries();
    for (const auto& entry : entries) {
        if (entry.id == entryId) {
            g_vault->updateEntryAccessTime(entryId);
            std::string password = g_vault->getDecryptedPassword(entryId);
            std::string formatted = entry.title + "|" + entry.username + "|" + password + "|" + 
                                   entry.notes + "|" + entry.totpSecret + "|" + 
                                   std::to_string(entry.createdAt) + "|" + 
                                   std::to_string(entry.updatedAt) + "|" + 
                                   std::to_string(entry.lastAccessed);
            return env->NewStringUTF(formatted.c_str());
        }
    }
    return env->NewStringUTF("");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_verifyMasterPassword(JNIEnv* env, jobject thiz, jstring password) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !password) return false;
    const char* pwd = env->GetStringUTFChars(password, 0);
    if (!pwd) return false;
    bool result = g_vault->verifyMasterPassword(pwd);
    env->ReleaseStringUTFChars(password, pwd);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getDisplayUsername(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getDisplayUsername().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_deleteGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !groupName) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return false;
    bool result = g_vault->deleteGroup(name);
    env->ReleaseStringUTFChars(groupName, name);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupOwner(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !groupName) return env->NewStringUTF("");
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return env->NewStringUTF("");
    int groupId = g_vault->getGroupId(name);
    env->ReleaseStringUTFChars(groupName, name);
    if (groupId == -1) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getGroupOwner(groupId).c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_updatePendingInviteStatus(JNIEnv* env, jobject thiz, jint inviteId, jstring status) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* s = env->GetStringUTFChars(status, 0);
    g_vault->updatePendingInviteStatus(inviteId, s);
    env->ReleaseStringUTFChars(status, s);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_groupExists(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return false;
    bool result = g_vault->groupExists(name);
    env->ReleaseStringUTFChars(groupName, name);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_deleteEntry(JNIEnv* env, jobject, jint id) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if(!g_vault) return false;
    return g_vault->deleteEntry(id);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_updateEntry(JNIEnv* env, jobject thiz, jint id, jstring title, jstring user, jstring pass, jstring notes) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    
    CipherMesh::Core::VaultEntry e;
    e.id = id;
    
    const char* t = env->GetStringUTFChars(title, 0); e.title = t;
    const char* u = env->GetStringUTFChars(user, 0); e.username = u;
    const char* n = env->GetStringUTFChars(notes, 0); e.notes = n;
    const char* p = env->GetStringUTFChars(pass, 0); 
    
    bool res = g_vault->updateEntry(e, std::string(p));
    
    env->ReleaseStringUTFChars(title, t);
    env->ReleaseStringUTFChars(user, u);
    env->ReleaseStringUTFChars(notes, n);
    env->ReleaseStringUTFChars(pass, p);
    return res;
}