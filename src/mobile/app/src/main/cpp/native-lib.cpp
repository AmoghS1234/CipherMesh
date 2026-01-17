#include <jni.h>
#include <string>
#include <vector>
#include <android/log.h>
#include <thread>
#include <mutex>
#include <memory>
#include <algorithm> // for find
#include <sodium.h> // [FIX] Add sodium for randombytes_buf
#include "vault.hpp"
#include "webrtcservice.hpp" // [FIX] Include the correct service header

#define TAG "CipherMesh-Native"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

// --- Globals ---
std::unique_ptr<CipherMesh::Core::Vault> g_vault;
std::unique_ptr<WebRTCService> g_p2p; // [FIX] Use WebRTCService directly
std::mutex g_inviteMutex;
std::vector<CipherMesh::Core::PendingInvite> g_pendingInvites;

// --- JNI Callbacks ---
JavaVM* g_jvm = nullptr;
jobject g_context = nullptr;
jobject g_signalingCallback = nullptr; // Reference to P2PManager.kt

// --- Helper: Send Signaling Message to Kotlin ---
void sendSignalingToKotlin(const std::string& target, const std::string& type, const std::string& payload) {
    if (!g_jvm || !g_signalingCallback) return;
    
    JNIEnv* env;
    bool attached = false;
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }

    jclass cls = env->GetObjectClass(g_signalingCallback);
    // Signature matches Kotlin: sendSignalingMessage(String targetId, String type, String payload)
    jmethodID mid = env->GetMethodID(cls, "sendSignalingMessage", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
    
    if (mid) {
        jstring jTarget = env->NewStringUTF(target.c_str());
        jstring jType = env->NewStringUTF(type.c_str());
        jstring jPayload = env->NewStringUTF(payload.c_str());
        
        env->CallVoidMethod(g_signalingCallback, mid, jTarget, jType, jPayload);
        
        env->DeleteLocalRef(jTarget);
        env->DeleteLocalRef(jType);
        env->DeleteLocalRef(jPayload);
    }

    if (attached) g_jvm->DetachCurrentThread();
}

// --- Helper: Send Toast to Android ---
void showToastFromNative(const std::string& message) {
    if (!g_jvm || !g_context) return;
    JNIEnv* env;
    bool attached = false;
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) != 0) return;
        attached = true;
    }

    jclass contextClass = env->GetObjectClass(g_context);
    jmethodID getToastMethod = env->GetMethodID(contextClass, "showToast", "(Ljava/lang/String;)V");
    if (getToastMethod) {
        jstring jMsg = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(g_context, getToastMethod, jMsg);
        env->DeleteLocalRef(jMsg);
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
        if (valb >= 0) {
            out.push_back(char((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

// =============================================================
// JNI METHODS
// =============================================================

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_init(JNIEnv* env, jobject thiz, jstring db_path, jobject context) {
    if (g_context) env->DeleteGlobalRef(g_context);
    g_context = env->NewGlobalRef(context);

    const char* path = env->GetStringUTFChars(db_path, 0);
    g_vault = std::make_unique<CipherMesh::Core::Vault>();
    g_vault->connect(path);
    env->ReleaseStringUTFChars(db_path, path);
    LOGI("Vault Initialized at: %s", path);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_unlock(JNIEnv* env, jobject thiz, jstring password) {
    if (!g_vault) return false;
    const char* pwd = env->GetStringUTFChars(password, 0);
    bool result = g_vault->unlock(pwd);
    env->ReleaseStringUTFChars(password, pwd);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_registerUser(JNIEnv* env, jobject thiz, jstring db_path, jstring password) {
    const char* path = env->GetStringUTFChars(db_path, 0);
    const char* pwd = env->GetStringUTFChars(password, 0);
    
    // Re-create vault instance just in case
    g_vault = std::make_unique<CipherMesh::Core::Vault>();
    bool result = g_vault->createNewVault(path, pwd);
    if(result) {
        g_vault->connect(path); // Connect immediately after creation
        g_vault->unlock(pwd);
        // Ensure "Personal" group exists
        g_vault->addGroup("Personal"); 
    }
    
    env->ReleaseStringUTFChars(db_path, path);
    env->ReleaseStringUTFChars(password, pwd);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getUserId(JNIEnv* env, jobject thiz) {
    if(!g_vault) return env->NewStringUTF("");
    return env->NewStringUTF(g_vault->getUserId().c_str());
}

// [NEW] Register P2PManager as the signaling callback
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_registerSignalingCallback(JNIEnv* env, jobject thiz, jobject callback) {
    if (g_signalingCallback) env->DeleteGlobalRef(g_signalingCallback);
    g_signalingCallback = env->NewGlobalRef(callback);
}

// [NEW] Receive message from Kotlin and pass to C++ service
extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_receiveSignalingMessage(JNIEnv* env, jobject thiz, jstring message) {
    if (!g_p2p) return;
    const char* msg = env->GetStringUTFChars(message, 0);
    g_p2p->receiveSignalingMessage(msg); 
    env->ReleaseStringUTFChars(message, msg);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_initP2P(JNIEnv* env, jobject thiz, jstring signaling_url) {
    if (!g_vault) return;
    
    // [FIX] Initialize WebRTCService
    // We pass "" as URL because Kotlin handles the WebSocket connection.
    g_p2p = std::make_unique<WebRTCService>("", g_vault->getUserId(), nullptr);

    // [BRIDGE] Connect C++ Signals to Kotlin
    g_p2p->onSignalingMessage = [](std::string target, std::string type, std::string payload) {
        sendSignalingToKotlin(target, type, payload);
    };

    // 1. Handle Incoming Invites
    g_p2p->onIncomingInvite = [](std::string sender, std::string groupName) {
        std::lock_guard<std::mutex> lock(g_inviteMutex);
        // [FIX] Use secure random instead of weak rand()
        unsigned char randomBytes[4];
        randombytes_buf(randomBytes, 4);
        int id = (randomBytes[0] << 24) | (randomBytes[1] << 16) | (randomBytes[2] << 8) | randomBytes[3];
        if (id < 0) id = -id; // Ensure positive ID
        
        CipherMesh::Core::PendingInvite inv;
        inv.id = id; 
        inv.senderId = sender;
        inv.groupName = groupName; 
        inv.status = "pending";
        g_pendingInvites.push_back(inv);
        
        showToastFromNative("Invite received from: " + sender);
    };

    // 2. Handle Received Group Data (Sync/Accept)
    g_p2p->onGroupDataReceived = [](std::string senderId, std::string json) {
        if (!g_vault || g_vault->isLocked()) return;
        
        std::string type = extractJsonValueJNI(json, "type");

        if (type == "group-data") {
            std::string groupName = extractJsonValueJNI(json, "group");
            std::string keyBase64 = extractJsonValueJNI(json, "key");
            std::vector<unsigned char> groupKey = decodeBase64(keyBase64);
            
            try {
                if (g_vault->groupExists(groupName)) {
                    // Group exists
                } else {
                    // [CRITICAL FIX] Pass 'senderId' as ownerId
                    g_vault->addGroup(groupName, groupKey, senderId);
                    showToastFromNative("Joined Group: " + groupName);
                }
            } catch (...) {
                LOGE("Failed to add group from P2P");
            }

            std::lock_guard<std::mutex> l(g_inviteMutex);
            for(auto it=g_pendingInvites.begin(); it!=g_pendingInvites.end();) {
                if(it->senderId == senderId && it->groupName == groupName) {
                    it = g_pendingInvites.erase(it); 
                } else {
                    ++it;
                }
            }
        }
        else if (type == "entry-data") {
            std::string group = extractJsonValueJNI(json, "group");
            if (g_vault->setActiveGroup(group)) {
                CipherMesh::Core::VaultEntry e;
                e.title = extractJsonValueJNI(json, "title");
                e.username = extractJsonValueJNI(json, "username");
                e.password = extractJsonValueJNI(json, "password");
                e.notes = extractJsonValueJNI(json, "notes");
                e.totpSecret = extractJsonValueJNI(json, "totpSecret");
                
                // Add entry
                g_vault->addEntry(e, e.password);
            }
        }
    };
    
    // Note: g_p2p->startSignaling() is not called because Kotlin handles the socket.
    // However, we must ensure the C++ service is ready to process messages.
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_respondToInvite(JNIEnv* env, jobject thiz, jstring groupName, jstring senderId, jboolean accept) {
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* snd = env->GetStringUTFChars(senderId, 0);
    
    if (g_p2p) {
        if (accept) {
            // Send accept message
            g_p2p->respondToInvite(snd, true);
            
            // Create placeholder group locally
            // [CRITICAL FIX] Pass 'snd' (sender) as the owner!
            std::vector<unsigned char> emptyKey(32, 0); 
            if (g_vault) {
                g_vault->addGroup(grp, emptyKey, snd);
            }
        } else {
            g_p2p->respondToInvite(snd, false);
        }
        
        std::lock_guard<std::mutex> lock(g_inviteMutex);
        for (auto it = g_pendingInvites.begin(); it != g_pendingInvites.end(); ) {
            if (it->senderId == snd && it->groupName == grp) {
                it = g_pendingInvites.erase(it);
            } else {
                ++it;
            }
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
    env->DeleteLocalRef(emptyStr); // [FIX] Clean up local reference

    for (size_t i = 0; i < g_pendingInvites.size(); i++) {
        // Format: "senderId|groupName"
        std::string item = g_pendingInvites[i].senderId + "|" + g_pendingInvites[i].groupName;
        jstring jItem = env->NewStringUTF(item.c_str());
        env->SetObjectArrayElement(result, i, jItem);
        env->DeleteLocalRef(jItem); // [FIX] Clean up local reference
    }
    return result;
}

// --- Standard Vault Methods ---

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupNames(JNIEnv* env, jobject thiz) {
    if (!g_vault) return nullptr;
    std::vector<std::string> groups = g_vault->getGroupNames();
    jclass strClass = env->FindClass("java/lang/String");
    jstring emptyStr = env->NewStringUTF("");
    jobjectArray result = env->NewObjectArray(groups.size(), strClass, emptyStr);
    env->DeleteLocalRef(emptyStr); // [FIX] Clean up local reference
    
    for (size_t i = 0; i < groups.size(); i++) {
        jstring jGroup = env->NewStringUTF(groups[i].c_str());
        env->SetObjectArrayElement(result, i, jGroup);
        env->DeleteLocalRef(jGroup); // [FIX] Clean up local reference
    }
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_addGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    if (!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    // [NOTE] addGroup defaults owner to self ("") if not provided
    bool res = g_vault->addGroup(name); 
    env->ReleaseStringUTFChars(groupName, name);
    return res;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_setActiveGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    if (!g_vault) return;
    const char* name = env->GetStringUTFChars(groupName, 0);
    g_vault->setActiveGroup(name);
    env->ReleaseStringUTFChars(groupName, name);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntries(JNIEnv* env, jobject thiz) {
    if (!g_vault) return nullptr;
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->getEntries();
    
    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(entries.size(), strClass, env->NewStringUTF(""));

    for (size_t i = 0; i < entries.size(); i++) {
        // Format: title|username|notes|type
        std::string s = entries[i].title + "|" + entries[i].username + "|" + entries[i].notes + "|" + entries[i].entryType;
        env->SetObjectArrayElement(result, i, env->NewStringUTF(s.c_str()));
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_addEntry(JNIEnv* env, jobject thiz, jstring title, jstring user, jstring pass, jstring url, jstring notes) {
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
    if (!g_vault) return env->NewStringUTF("");
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->getEntries();
    if (index >= 0 && index < entries.size()) {
        return env->NewStringUTF(entries[index].password.c_str());
    }
    return env->NewStringUTF("");
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupMembers(JNIEnv* env, jobject thiz, jstring groupName) {
    if (!g_vault) return nullptr;
    const char* name = env->GetStringUTFChars(groupName, 0);
    if (!name) return nullptr; // [FIX] Check for null
    
    std::vector<CipherMesh::Core::GroupMember> members = g_vault->getGroupMembers(name);
    env->ReleaseStringUTFChars(groupName, name);
    
    jclass strClass = env->FindClass("java/lang/String");
    jstring emptyStr = env->NewStringUTF("");
    jobjectArray result = env->NewObjectArray(members.size(), strClass, emptyStr);
    env->DeleteLocalRef(emptyStr); // [FIX] Clean up local reference

    for (size_t i = 0; i < members.size(); i++) {
        // Format: userId|role|status
        std::string s = members[i].userId + "|" + members[i].role + "|" + members[i].status;
        jstring jMember = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(result, i, jMember);
        env->DeleteLocalRef(jMember); // [FIX] Clean up local reference
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
    
    env->ReleaseStringUTFChars(groupName, grp);
    env->ReleaseStringUTFChars(targetUser, tgt);
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
Java_com_ciphermesh_mobile_core_Vault_broadcastSync(JNIEnv* env, jobject thiz, jstring groupName) {
    LOGI("Broadcasting Sync for group (Stub)");
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_removeUser(JNIEnv* env, jobject thiz, jstring groupName, jstring targetUser) {
     if(!g_vault) return;
     const char* grp = env->GetStringUTFChars(groupName, 0);
     const char* tgt = env->GetStringUTFChars(targetUser, 0);
     g_vault->removeGroupMember(grp, tgt);
     env->ReleaseStringUTFChars(groupName, grp);
     env->ReleaseStringUTFChars(targetUser, tgt);
}