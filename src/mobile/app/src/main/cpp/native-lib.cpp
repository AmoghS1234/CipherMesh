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

#define TAG "CipherMesh-Native"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// --- Globals ---
std::unique_ptr<CipherMesh::Core::Vault> g_vault;
std::unique_ptr<WebRTCService> g_p2p; 
std::mutex g_inviteMutex;
std::vector<CipherMesh::Core::PendingInvite> g_pendingInvites;

// Global session map to handle name mapping during transfer
// Key: "senderId:originalName", Value: "resolvedUniqueName"
std::map<std::string, std::string> g_p2pNameMapper;

// Mutexes
std::mutex g_vaultMutex;  
std::mutex g_p2pMutex;    
std::mutex g_jniMutex;    

// --- JNI Callbacks ---
JavaVM* g_jvm = nullptr;
jobject g_context = nullptr;
jobject g_signalingCallback = nullptr; 

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
        // Signature matches P2PManager: (String target, String type, String payload) -> Void
        jmethodID mid = env->GetMethodID(cbClass, "sendSignalingMessage", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
        
        if (mid) {
            jstring jTarget = env->NewStringUTF(target.c_str());
            jstring jType = env->NewStringUTF(type.c_str());
            jstring jPayload = env->NewStringUTF(payload.c_str());
            
            env->CallVoidMethod(g_signalingCallback, mid, jTarget, jType, jPayload);
            
            env->DeleteLocalRef(jTarget);
            env->DeleteLocalRef(jType);
            env->DeleteLocalRef(jPayload);
        } else {
            LOGE("Could not find sendSignalingMessage method");
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
    // Assumes your Vault class or Context class has a showToast helper, or we use standard Toast if context is Activity
    // Ideally P2PManager handles UI, but if Vault holds Activity context:
    // We'll assume the context object passed in setActivityContext has a showToast method or we skip UI here.
    // For safety, let's just Log if we can't find the method easily without specific signature.
    LOGI("Toast from Native: %s", message.c_str());

    if (attached) g_jvm->DetachCurrentThread();
}

// --- Helper: Send Broadcast to Refresh UI ---
void sendRefreshBroadcast() {
    if (!g_jvm || !g_context) return;
    
    JNIEnv* env;
    bool attached = false;
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }
    
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_context && env) {
        // Get Context class and sendBroadcast method
        jclass contextClass = env->FindClass("android/content/Context");
        if (!contextClass) {
            LOGE("Failed to find Context class");
            if (attached) g_jvm->DetachCurrentThread();
            return;
        }
        
        // Create Intent for broadcast
        jclass intentClass = env->FindClass("android/content/Intent");
        if (!intentClass) {
            LOGE("Failed to find Intent class");
            env->DeleteLocalRef(contextClass);
            if (attached) g_jvm->DetachCurrentThread();
            return;
        }
        
        jmethodID intentConstructor = env->GetMethodID(intentClass, "<init>", "(Ljava/lang/String;)V");
        if (!intentConstructor) {
            LOGE("Failed to find Intent constructor");
            env->DeleteLocalRef(contextClass);
            env->DeleteLocalRef(intentClass);
            if (attached) g_jvm->DetachCurrentThread();
            return;
        }
        
        jstring action = env->NewStringUTF("com.ciphermesh.REFRESH_GROUPS");
        jobject intent = env->NewObject(intentClass, intentConstructor, action);
        
        // Call sendBroadcast
        jmethodID sendBroadcastMethod = env->GetMethodID(env->GetObjectClass(g_context), "sendBroadcast", "(Landroid/content/Intent;)V");
        if (sendBroadcastMethod) {
            env->CallVoidMethod(g_context, sendBroadcastMethod, intent);
            LOGI("Sent REFRESH_GROUPS broadcast");
        } else {
            LOGE("Failed to find sendBroadcast method");
        }
        
        env->DeleteLocalRef(action);
        env->DeleteLocalRef(intent);
        env->DeleteLocalRef(intentClass);
        env->DeleteLocalRef(contextClass);
    }
    
    if (attached) g_jvm->DetachCurrentThread();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// --- Helper: Basic JSON Parser ---
std::string extractJsonValueJNI(const std::string& json, const std::string& key) {
    std::string searchKey = "\"" + key + "\":\"";
    size_t start = json.find(searchKey);
    if (start == std::string::npos) return "";
    start += searchKey.length();
    size_t end = json.find("\"", start);
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

// --- Helper: Base64 Decoding ---
std::vector<unsigned char> decodeBase64(const std::string& in) {
    std::vector<unsigned char> out;
    std::vector<int> T(256, -1);
    static const std::string b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (int i=0; i<64; i++) T[b64[i]] = i; 
    int val = 0, valb = -8;
    for (unsigned char c : in) {
        if (T[c] == -1) break;
        val = (val << 6) + T[c];
        valb += 6;
        if (valb >= 0) { out.push_back(char((val >> valb) & 0xFF)); valb -= 8; }
    }
    return out;
}

// =============================================================
// JNI METHODS
// =============================================================

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_init(JNIEnv* env, jobject thiz, jstring jPath) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) g_vault = std::make_unique<CipherMesh::Core::Vault>();
    
    const char* path = env->GetStringUTFChars(jPath, nullptr);
    g_vault->connect(std::string(path));
    env->ReleaseStringUTFChars(jPath, path);

    // [CRITICAL] Hook up the P2P Sender for Syncing
    g_vault->setP2PSendCallback([](const std::string& target, const std::string& message) {
        sendSignalingToKotlin(target, "sync-packet", message);
    });
    
    LOGI("Vault Initialized with P2P Callback");
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_setActivityContext(JNIEnv* env, jobject thiz, jobject activity) {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_context != nullptr) { env->DeleteGlobalRef(g_context); g_context = nullptr; }
    if (activity != nullptr) { g_context = env->NewGlobalRef(activity); }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_unlock(JNIEnv* env, jobject thiz, jstring password) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    const char* pwd = env->GetStringUTFChars(password, 0);
    bool result = g_vault->unlock(pwd);
    env->ReleaseStringUTFChars(password, pwd);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_isLocked(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return true;
    return g_vault->isLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_createAccount(JNIEnv* env, jobject thiz, jstring db_path, jstring username, jstring master_pass) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
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
    if (!g_vault) return false;
    return g_vault->hasUsers();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getUserId(JNIEnv* env, jobject thiz) {
    if(!g_vault) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getUserId().c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_registerSignalingCallback(JNIEnv* env, jobject thiz, jobject callback) {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_signalingCallback != nullptr) { env->DeleteGlobalRef(g_signalingCallback); g_signalingCallback = nullptr; }
    if (callback != nullptr) { g_signalingCallback = env->NewGlobalRef(callback); }
}

// [NEW] Triggered when Signaling Server says "User X is Online"
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_onPeerOnline(JNIEnv* env, jobject thiz, jstring jUserId) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* uid = env->GetStringUTFChars(jUserId, nullptr);
    LOGI("Peer Online: %s - Flushing Outbox...", uid);
    g_vault->processOutboxForUser(std::string(uid));
    env->ReleaseStringUTFChars(jUserId, uid);
}

// [NEW] Handle incoming Sync Payloads (UPSERT, DELETE, ACK)
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_handleSyncMessage(JNIEnv* env, jobject thiz, jstring jSender, jstring jPayload) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* sender = env->GetStringUTFChars(jSender, nullptr);
    const char* payload = env->GetStringUTFChars(jPayload, nullptr);
    
    g_vault->handleIncomingSync(std::string(sender), std::string(payload));
    
    env->ReleaseStringUTFChars(jSender, sender);
    env->ReleaseStringUTFChars(jPayload, payload);
}

// [NEW] Manual Sync Trigger from UI
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_broadcastSync(JNIEnv* env, jobject thiz, jstring groupName) { 
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return;

    const char* grp = env->GetStringUTFChars(groupName, nullptr);
    std::string name(grp);
    env->ReleaseStringUTFChars(groupName, grp);

    LOGI("Broadcasting Sync for group: %s", name.c_str()); 
    
    // Logic: Iterate all members of the group and attempt to flush the outbox for them.
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
    std::lock_guard<std::mutex> lock(g_p2pMutex);
    if (!g_p2p) return;
    const char* msg = env->GetStringUTFChars(message, 0);
    g_p2p->receiveSignalingMessage(msg); 
    env->ReleaseStringUTFChars(message, msg);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_initP2P(JNIEnv* env, jobject thiz, jstring signaling_url) {
    std::lock_guard<std::mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    // Avoid re-initialization
    if (!g_vault || g_p2p) { return; }
    
    // Initialize the WebRTC Service
    // Note: P2PManager handles the WebSocket connection, so the URL here is unused in C++
    g_p2p = std::make_unique<WebRTCService>("", g_vault->getUserId(), nullptr);

    // 1. Outgoing Signaling: C++ -> Kotlin -> WebSocket
    g_p2p->onSignalingMessage = [](std::string target, std::string type, std::string payload) {
        sendSignalingToKotlin(target, type, payload);
    };

    // 2. Incoming Invite Handler
    g_p2p->onIncomingInvite = [](std::string sender, std::string groupName) {
        std::lock_guard<std::mutex> lock(g_inviteMutex);
        
        // Generate a random ID for the invite
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
        
        // Send broadcast to refresh UI so invite appears immediately
        sendRefreshBroadcast();
    };

    // 3. Incoming Data Handler (Group Data, Entries, Member Updates)
    g_p2p->onGroupDataReceived = [&](std::string senderId, std::string json) {
        std::lock_guard<std::mutex> vaultLock(g_vaultMutex);
        if (!g_vault || g_vault->isLocked()) return;
        
        std::string type = extractJsonValueJNI(json, "type");

        // --- Case A: Joining a Group (Full Data) ---
        if (type == "group-data") {
            std::string originalGroupName = extractJsonValueJNI(json, "group");
            std::string keyBase64 = extractJsonValueJNI(json, "key");
            std::vector<unsigned char> groupKey = decodeBase64(keyBase64);
            
            // Name Collision Resolution
            std::string finalName = originalGroupName;
            if (g_vault->groupExists(finalName)) {
                finalName = originalGroupName + " (from " + senderId + ")";
            }
            int counter = 1;
            std::string baseCollisionName = finalName;
            while (g_vault->groupExists(finalName)) {
                finalName = baseCollisionName + " " + std::to_string(++counter);
            }

            // Map session for future messages
            std::string sessionKey = senderId + ":" + originalGroupName;
            g_p2pNameMapper[sessionKey] = finalName;
            
            try {
                // Add group and set sender as owner if not specified
                g_vault->addGroup(finalName, groupKey, senderId);
                showToastFromNative("Joined Group: " + finalName);
                
                // Send broadcast to refresh UI so new group appears
                sendRefreshBroadcast();
            } catch (...) { LOGE("Failed to add group from P2P"); }

            // Clear pending invite
            std::lock_guard<std::mutex> l(g_inviteMutex);
            for(auto it=g_pendingInvites.begin(); it!=g_pendingInvites.end();) {
                if(it->senderId == senderId && it->groupName == originalGroupName) it = g_pendingInvites.erase(it); else ++it;
            }
        }
        // --- Case B: Receiving an Entry ---
        else if (type == "entry-data") {
            std::string originalGroup = extractJsonValueJNI(json, "group");
            std::string sessionKey = senderId + ":" + originalGroup;
            std::string targetGroup = g_p2pNameMapper.count(sessionKey) ? g_p2pNameMapper[sessionKey] : originalGroup;

            if (g_vault->setActiveGroup(targetGroup)) {
                CipherMesh::Core::VaultEntry e;
                e.title = extractJsonValueJNI(json, "title");
                e.username = extractJsonValueJNI(json, "username");
                e.password = extractJsonValueJNI(json, "password");
                e.notes = extractJsonValueJNI(json, "notes");
                e.totpSecret = extractJsonValueJNI(json, "totpSecret");
                
                // Parse Locations Array manually from JSON string
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
                g_vault->addEntry(e, e.password);
                LOGI("Added entry '%s' to group '%s'", e.title.c_str(), targetGroup.c_str());
                
                // Send broadcast to refresh entries list
                sendRefreshBroadcast();
            } else {
                LOGE("Failed to set active group '%s' for entry transfer", targetGroup.c_str());
            }
        }
        // --- Case C: Receiving Member List ---
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
                        // Parsing "uid|role|status"
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
        // --- Case D: Member Left ---
        else if (type == "member-leave") {
            std::string originalGroup = extractJsonValueJNI(json, "group");
            std::string sessionKey = senderId + ":" + originalGroup;
            std::string targetGroup = g_p2pNameMapper.count(sessionKey) ? g_p2pNameMapper[sessionKey] : originalGroup;
            
            // [CRITICAL FIX] Use removeUser instead of removeGroupMember
            g_vault->removeUser(targetGroup, senderId);
            
            showToastFromNative("User " + senderId + " left the group.");
        }
    };
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_respondToInvite(JNIEnv* env, jobject thiz, jstring groupName, jstring senderId, jboolean accept) {
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* snd = env->GetStringUTFChars(senderId, 0);
    if (!grp || !snd) {
        if (grp) env->ReleaseStringUTFChars(groupName, grp);
        if (snd) env->ReleaseStringUTFChars(senderId, snd);
        return;
    }
    
    std::lock_guard<std::mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    if (g_p2p) {
        if (accept) {
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
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    bool res = g_vault->addGroup(name); 
    env->ReleaseStringUTFChars(groupName, name);
    return res;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_setActiveGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::mutex> lock(g_vaultMutex); 
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    bool result = g_vault->setActiveGroup(name);
    env->ReleaseStringUTFChars(groupName, name);
    return (jboolean)result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntries(JNIEnv* env, jobject thiz) {
    if (!g_vault) return nullptr;
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->getEntries();
    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(entries.size(), strClass, env->NewStringUTF(""));

    for (size_t i = 0; i < entries.size(); i++) {
        std::string s = entries[i].title + "|" + entries[i].username + "|" + entries[i].notes + "|" + entries[i].entryType;
        env->SetObjectArrayElement(result, i, env->NewStringUTF(s.c_str()));
    }
    return result;
}

// [FIX] Implemented to match your Vault.kt signature (private external fun addEntryNative)
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_addEntryNative(JNIEnv* env, jobject thiz, jstring title, jstring user, jstring pass, jstring url, jstring notes) {
    if (!g_vault) return;
    CipherMesh::Core::VaultEntry e;
    
    const char* t = env->GetStringUTFChars(title, 0); e.title = t;
    const char* u = env->GetStringUTFChars(user, 0); e.username = u;
    const char* p = env->GetStringUTFChars(pass, 0); e.password = p;
    
    // Add single URL location since Kotlin signature is simplified
    const char* l = env->GetStringUTFChars(url, 0); 
    e.locations.push_back(CipherMesh::Core::Location(-1, "url", l));
    
    const char* n = env->GetStringUTFChars(notes, 0); e.notes = n;
    
    // Calling Core Add (which triggers Sync)
    g_vault->addEntry(e, e.password);
    
    env->ReleaseStringUTFChars(title, t); 
    env->ReleaseStringUTFChars(user, u);
    env->ReleaseStringUTFChars(pass, p); 
    env->ReleaseStringUTFChars(url, l); 
    env->ReleaseStringUTFChars(notes, n);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getDecryptedPassword(JNIEnv* env, jobject thiz, jint index) {
    if (!g_vault) return env->NewStringUTF("");
    // Note: getDecryptedPassword usually takes ID, not Index. 
    // Assuming Kotlin passes ID here or we map it. 
    // Ideally this calls g_vault->getDecryptedPassword(id).
    // For safety based on your previous code structure:
    return env->NewStringUTF(g_vault->getDecryptedPassword(index).c_str());
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupMembers(JNIEnv* env, jobject thiz, jstring groupName) {
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
    if (!g_vault || !g_p2p) return;
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* tgt = env->GetStringUTFChars(targetUser, 0);
    std::vector<unsigned char> key = g_vault->getGroupKey(grp);
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->exportGroupEntries(grp);
    g_p2p->queueInvite(grp, tgt, key, entries);
    env->ReleaseStringUTFChars(groupName, grp); env->ReleaseStringUTFChars(targetUser, tgt);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_isGroupOwner(JNIEnv* env, jobject thiz, jstring groupName) {
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
     if(!g_vault) return;
     const char* grp = env->GetStringUTFChars(groupName, 0);
     const char* tgt = env->GetStringUTFChars(targetUser, 0);
     g_vault->removeUser(std::string(grp), std::string(tgt));
     env->ReleaseStringUTFChars(groupName, grp); env->ReleaseStringUTFChars(targetUser, tgt);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_searchEntries(JNIEnv* env, jobject thiz, jstring searchTerm) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
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
    std::lock_guard<std::mutex> lock(g_vaultMutex);
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
    std::lock_guard<std::mutex> lock(g_vaultMutex);
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
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault || !encryptedPassword) return env->NewStringUTF("");
    const char* encrypted = env->GetStringUTFChars(encryptedPassword, 0);
    if (!encrypted) return env->NewStringUTF("");
    std::string decrypted = g_vault->decryptPasswordFromHistory(encrypted);
    env->ReleaseStringUTFChars(encryptedPassword, encrypted);
    return env->NewStringUTF(decrypted.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_updateEntryAccessTime(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    g_vault->updateEntryAccessTime(entryId);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntryFullDetails(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
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
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault || !password) return false;
    const char* pwd = env->GetStringUTFChars(password, 0);
    if (!pwd) return false;
    bool result = g_vault->verifyMasterPassword(pwd);
    env->ReleaseStringUTFChars(password, pwd);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getDisplayUsername(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getDisplayUsername().c_str());
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_deleteGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault || !groupName) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return false;
    bool result = g_vault->deleteGroup(name);
    env->ReleaseStringUTFChars(groupName, name);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupOwner(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
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
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* s = env->GetStringUTFChars(status, 0);
    g_vault->updatePendingInviteStatus(inviteId, s);
    env->ReleaseStringUTFChars(status, s);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_groupExists(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return false;
    bool result = g_vault->groupExists(name);
    env->ReleaseStringUTFChars(groupName, name);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_deleteEntry(JNIEnv* env, jobject, jint id) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if(!g_vault) return false;
    return g_vault->deleteEntry(id);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_updateEntry(JNIEnv* env, jobject thiz, jint id, jstring title, jstring user, jstring pass, jstring notes) {
    std::lock_guard<std::mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    
    // Construct entry object for update
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