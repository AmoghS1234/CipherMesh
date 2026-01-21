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

#define JNI_CATCH_RETURN(retval) \
    catch (const std::exception& e) { \
        LOGE("JNI Exception: %s", e.what()); \
        return retval; \
    } catch (...) { \
        LOGE("JNI Unknown Exception"); \
        return retval; \
    }

#define JNI_CATCH_VOID \
    catch (const std::exception& e) { \
        LOGE("JNI Exception: %s", e.what()); \
    } catch (...) { \
        LOGE("JNI Unknown Exception"); \
    }

std::unique_ptr<CipherMesh::Core::Vault> g_vault;
std::unique_ptr<WebRTCService> g_p2p; 

// Key: SenderID + "|" + RemoteGroupName, Value: LocalGroupName
std::map<std::string, std::string> g_groupMappings;

std::recursive_mutex g_vaultMutex;  
std::mutex g_p2pMutex;    
std::mutex g_jniMutex;    

JavaVM* g_jvm = nullptr;
jobject g_context = nullptr;
jobject g_signalingCallback = nullptr; 

// --- Helpers ---
std::string extractJsonValueJNI(const std::string& json, const std::string& key) {
    std::string keyPattern = "\"" + key + "\"";
    size_t keyPos = json.find(keyPattern);
    if (keyPos == std::string::npos) return "";
    size_t colonPos = json.find(":", keyPos + keyPattern.length());
    if (colonPos == std::string::npos) return "";
    size_t valStart = colonPos + 1;
    while (valStart < json.length() && (json[valStart] == ' ' || json[valStart] == '\t' || json[valStart] == '\n')) valStart++;
    if (valStart >= json.length()) return "";
    if (json[valStart] == '"') {
        size_t start = valStart + 1;
        size_t end = start;
        while (end < json.length()) {
            if (json[end] == '"' && json[end-1] != '\\') break;
            end++;
        }
        if (end >= json.length()) return "";
        return json.substr(start, end - start);
    } else {
        size_t end = json.find_first_of(",}", valStart);
        return json.substr(valStart, end - valStart);
    }
}

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
            env->DeleteLocalRef(jTarget); env->DeleteLocalRef(jType); env->DeleteLocalRef(jPayload);
        }
    }
    if (attached) g_jvm->DetachCurrentThread();
}

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
    
    // [FIX] Check for and CLEAR any pending exception (NoSuchMethodError)
    // This prevents the app from crashing if the method is missing/renamed.
    if (env->ExceptionCheck()) {
        env->ExceptionClear(); 
        LOGE("Could not find showToast method in Activity");
    } else if (mid) {
        jstring jMsg = env->NewStringUTF(message.c_str());
        env->CallVoidMethod(g_context, mid, jMsg);
        env->DeleteLocalRef(jMsg);
    }

    if (attached) g_jvm->DetachCurrentThread();
}

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
    jclass cls = env->GetObjectClass(g_context);
    jmethodID mid = env->GetMethodID(cls, "refreshUI", "()V");
    if (mid) env->CallVoidMethod(g_context, mid);
    if (attached) g_jvm->DetachCurrentThread();
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

// =============================================================
// JNI METHODS
// =============================================================

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_init(JNIEnv* env, jobject thiz, jstring jPath) {
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    if (!g_vault) g_vault = std::make_unique<CipherMesh::Core::Vault>();
    const char* path = env->GetStringUTFChars(jPath, nullptr);
    if (path) {
        g_vault->connect(std::string(path));
        env->ReleaseStringUTFChars(jPath, path);
    }
    g_vault->setP2PSendCallback([](const std::string& target, const std::string& message) {
        std::thread([target, message]() {
            std::lock_guard<std::mutex> lock(g_p2pMutex);
            if (g_p2p) g_p2p->sendP2PMessage(target, message);
        }).detach();
    });
    g_vault->setSyncCallback([](const std::string& type, const std::string& payload) { triggerJavaRefresh(); });
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_setActivityContext(JNIEnv* env, jobject thiz, jobject activity) {
    std::lock_guard<std::mutex> lock(g_jniMutex);
    if (g_context) env->DeleteGlobalRef(g_context);
    if (activity) g_context = env->NewGlobalRef(activity);
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
    return !g_vault || g_vault->isLocked();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_createAccount(JNIEnv* env, jobject thiz, jstring db_path, jstring username, jstring master_pass) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    const char* path = env->GetStringUTFChars(db_path, 0);
    const char* user = env->GetStringUTFChars(username, 0);
    const char* pwd = env->GetStringUTFChars(master_pass, 0);
    if (!path || !user || !pwd) { return false; }
    
    g_vault = std::make_unique<CipherMesh::Core::Vault>();
    bool result = g_vault->createNewVault(path, pwd, user);
    if(result) {
        g_vault->connect(path);
        g_vault->unlock(pwd);
        g_vault->setActiveGroup("Personal"); 
    }
    
    env->ReleaseStringUTFChars(db_path, path);
    env->ReleaseStringUTFChars(username, user);
    env->ReleaseStringUTFChars(master_pass, pwd);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_hasUsers(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    return g_vault && g_vault->hasUsers();
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getUserId(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
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
    g_vault->processOutboxForUser(std::string(uid));
    env->ReleaseStringUTFChars(jUserId, uid);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_handleSyncMessage(JNIEnv* env, jobject thiz, jstring jSender, jstring jPayload) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* sender = env->GetStringUTFChars(jSender, nullptr);
    const char* payload = env->GetStringUTFChars(jPayload, nullptr);
    try { g_vault->handleIncomingSync(std::string(sender), std::string(payload)); } catch(...) {}
    env->ReleaseStringUTFChars(jSender, sender);
    env->ReleaseStringUTFChars(jPayload, payload);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_broadcastSync(JNIEnv* env, jobject thiz, jstring groupName) { 
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* grp = env->GetStringUTFChars(groupName, nullptr);
    g_vault->broadcastSync(grp);
    env->ReleaseStringUTFChars(groupName, grp);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_receiveSignalingMessage(JNIEnv* env, jobject, jstring message) {
    if (!message) return;
    const char* msg = env->GetStringUTFChars(message, nullptr);
    WebRTCService* p2p = nullptr;
    {
        std::lock_guard<std::mutex> lock(g_p2pMutex);
        p2p = g_p2p.get();
    }
    if (p2p) p2p->receiveSignalingMessage(msg);
    env->ReleaseStringUTFChars(message, msg);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_respondToInvite(JNIEnv* env, jobject, jstring groupName, jstring senderId, jboolean accept) {
    if (!groupName || !senderId) return;
    const char* grp = env->GetStringUTFChars(groupName, nullptr);
    const char* snd = env->GetStringUTFChars(senderId, nullptr);
    std::string group(grp);
    std::string sender(snd);
    bool accepted = (accept == JNI_TRUE);
    env->ReleaseStringUTFChars(groupName, grp);
    env->ReleaseStringUTFChars(senderId, snd);

    {
        std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
        if (g_vault && accepted) g_vault->respondToInvite(group, sender, true);
        else if (g_vault && !accepted) g_vault->respondToInvite(group, sender, false);
    }
    {
        std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
        if (g_p2p) {
            g_p2p->respondToInvite(sender, accepted);
            if (accepted) {
                std::string msg = "{\"type\":\"invite-accept\",\"group\":\"" + group + "\"}";
                g_p2p->sendP2PMessage(sender, msg);
            }
        }
    }
    triggerJavaRefresh();
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getPendingInvites(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewObjectArray(0, env->FindClass("java/lang/String"), env->NewStringUTF(""));
    std::vector<CipherMesh::Core::PendingInvite> invites = g_vault->getPendingInvites();
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray res = env->NewObjectArray(invites.size(), cls, env->NewStringUTF(""));
    for (size_t i = 0; i < invites.size(); i++) {
        std::string s = std::to_string(invites[i].id) + "|" + invites[i].senderId + "|" + invites[i].groupName;
        jstring js = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(res, i, js);
        env->DeleteLocalRef(js);
    }
    return res;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupNames(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || g_vault->isLocked()) return env->NewObjectArray(0, env->FindClass("java/lang/String"), env->NewStringUTF(""));
    std::vector<std::string> groups = g_vault->getGroupNames();
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray res = env->NewObjectArray(groups.size(), cls, env->NewStringUTF(""));
    for (size_t i = 0; i < groups.size(); i++) {
        jstring s = env->NewStringUTF(groups[i].c_str());
        env->SetObjectArrayElement(res, i, s);
        env->DeleteLocalRef(s);
    }
    return res;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_addGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
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
    return result;
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntries(JNIEnv* env, jobject thiz) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || g_vault->isLocked()) return env->NewObjectArray(0, env->FindClass("java/lang/String"), env->NewStringUTF(""));
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->getEntries();
    jclass strClass = env->FindClass("java/lang/String");
    jobjectArray result = env->NewObjectArray(entries.size(), strClass, env->NewStringUTF(""));
    for (size_t i = 0; i < entries.size(); i++) {
        std::string s = std::to_string(entries[i].id) + "|" + entries[i].title + "|" + entries[i].username + "|" + entries[i].notes + "|" + entries[i].entryType;
        jstring jStr = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(result, i, jStr);
        env->DeleteLocalRef(jStr);
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_initP2P(JNIEnv* env, jobject thiz, jstring signaling_url) {
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
    
    if (!g_vault || g_p2p) return;
    
    g_p2p = std::make_unique<WebRTCService>("", g_vault->getUserId(), nullptr);

    g_p2p->onSignalingMessage = [](std::string target, std::string type, std::string payload) {
        sendSignalingToKotlin(target, type, payload);
    };

    // [FIX] Enable incremental sync
    g_vault->setP2PSendCallback([](const std::string& userId, const std::string& msg) {
        std::lock_guard<std::mutex> pLock(g_p2pMutex);
        if (g_p2p) {
            g_p2p->sendP2PMessage(userId, msg);
        }
    });

    // [FIX] Updated lambda signature to match DataCallback (sender, message)
    g_p2p->onSyncMessage = [](std::string sender, std::string message) {
        std::lock_guard<std::recursive_mutex> vLock(g_vaultMutex);
        if (g_vault && !g_vault->isLocked()) {
            if (!sender.empty()) {
                g_vault->handleIncomingSync(sender, message);
            } else {
                // Fallback: extract from JSON if sender arg is empty
                std::string extractedSender = extractJsonValueJNI(message, "sender");
                if (!extractedSender.empty()) {
                    g_vault->handleIncomingSync(extractedSender, message);
                }
            }
        }
    };

    g_p2p->onPeerOnline = [](std::string userId) {
        std::lock_guard<std::recursive_mutex> vLock(g_vaultMutex);
        if (g_vault) g_vault->onPeerOnline(userId);
    };

    g_p2p->onIncomingInvite = [](std::string sender, std::string groupName) {
        {
            std::lock_guard<std::recursive_mutex> vLock(g_vaultMutex);
            if (g_vault) {
                std::string payload = "{ \"group\": \"" + groupName + "\", \"owner\": \"" + sender + "\" }";
                g_vault->storePendingInvite(sender, groupName, payload);
            }
        }
        showToastFromNative("Invite from " + sender);
        triggerJavaRefresh();
    };

    // [FIX] Route all group data through the core handleIncomingSync function
    // This ensures consistent behavior between Android and Desktop, and proper
    // active group context management
    g_p2p->onGroupDataReceived = [&](std::string senderId, std::string json) {
        std::lock_guard<std::recursive_mutex> vLock(g_vaultMutex);
        if (!g_vault) return;

        LOGI("onGroupDataReceived from %s", senderId.c_str());
        
        // Use the core vault's handleIncomingSync for consistent behavior
        try {
            g_vault->handleIncomingSync(senderId, json);
        } catch (const std::exception& ex) {
            LOGE("Error handling group data: %s", ex.what());
        } catch (...) {
            LOGE("Unknown error handling group data");
        }
        
        triggerJavaRefresh();
    };
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_addEntryNative(JNIEnv* env, jobject thiz, jstring title, jstring user, jstring pass, jstring url, jstring notes) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    CipherMesh::Core::VaultEntry e;
    const char* t = env->GetStringUTFChars(title, 0); e.title = t;
    const char* u = env->GetStringUTFChars(user, 0); e.username = u;
    const char* p = env->GetStringUTFChars(pass, 0); e.password = p;
    const char* l = env->GetStringUTFChars(url, 0); 
    e.url = l; // [FIX] Set main URL field
    e.locations.push_back(CipherMesh::Core::Location(-1, "url", l));
    const char* n = env->GetStringUTFChars(notes, 0); e.notes = n;
    try { g_vault->addEntry(e, e.password); } JNI_CATCH_VOID
    env->ReleaseStringUTFChars(title, t); env->ReleaseStringUTFChars(user, u);
    env->ReleaseStringUTFChars(pass, p); env->ReleaseStringUTFChars(url, l);
    env->ReleaseStringUTFChars(notes, n);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getDecryptedPassword(JNIEnv* env, jobject thiz, jint index) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewStringUTF("");
    try { return env->NewStringUTF(g_vault->getDecryptedPassword(index).c_str()); } JNI_CATCH_RETURN(env->NewStringUTF("Error"))
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupMembers(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return nullptr;
    const char* name = env->GetStringUTFChars(groupName, 0);
    std::vector<CipherMesh::Core::GroupMember> members = g_vault->getGroupMembers(name);
    env->ReleaseStringUTFChars(groupName, name);
    jclass cls = env->FindClass("java/lang/String");
    jobjectArray res = env->NewObjectArray(members.size(), cls, env->NewStringUTF(""));
    for (size_t i = 0; i < members.size(); i++) {
        std::string s = members[i].userId + "|" + members[i].role + "|" + members[i].status;
        jstring jMember = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(res, i, jMember);
        env->DeleteLocalRef(jMember); 
    }
    return res;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_sendP2PInvite(JNIEnv* env, jobject thiz, jstring groupName, jstring targetUser) {
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* tgt = env->GetStringUTFChars(targetUser, 0);
    std::string sGroup(grp);
    std::string sTarget(tgt);
    env->ReleaseStringUTFChars(groupName, grp); 
    env->ReleaseStringUTFChars(targetUser, tgt);

    std::thread([sGroup, sTarget]() {
        std::vector<unsigned char> key;
        std::vector<CipherMesh::Core::VaultEntry> entries;
        bool dataLoaded = false;
        {
            std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
            if (g_vault) {
                try {
                    g_vault->addGroupMember(sGroup, sTarget, "member", "pending");
                    key = g_vault->getGroupKey(sGroup);
                    entries = g_vault->exportGroupEntries(sGroup);
                    dataLoaded = true;
                } catch (...) {}
            }
        } 
        if (dataLoaded) {
            std::lock_guard<std::mutex> p2pLock(g_p2pMutex);
            if (g_p2p) {
                g_p2p->queueInvite(sGroup, sTarget, key, entries);
                showToastFromNative("Handshake started with " + sTarget);
            }
        }
    }).detach();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_queueGroupSplitSync(JNIEnv* env, jobject thiz, jstring groupName, jstring targetUser) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* grp = env->GetStringUTFChars(groupName, 0);
    const char* tgt = env->GetStringUTFChars(targetUser, 0);
    try { g_vault->queueSyncForMember(grp, tgt, "GROUP_SPLIT", "{}"); } JNI_CATCH_VOID
    env->ReleaseStringUTFChars(groupName, grp); env->ReleaseStringUTFChars(targetUser, tgt);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_isGroupOwner(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if(!g_vault) return false;
    const char* name = env->GetStringUTFChars(groupName, 0);
    bool isOwner = g_vault->isGroupOwner(name);
    env->ReleaseStringUTFChars(groupName, name);
    return isOwner;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_removeUser(JNIEnv* env, jobject thiz, jstring groupName, jstring targetUser) {
     std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
     if(!g_vault) return;
     const char* grp = env->GetStringUTFChars(groupName, 0);
     const char* tgt = env->GetStringUTFChars(targetUser, 0);
     try { g_vault->removeUser(std::string(grp), std::string(tgt)); } JNI_CATCH_VOID
     env->ReleaseStringUTFChars(groupName, grp); env->ReleaseStringUTFChars(targetUser, tgt);
}

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_searchEntries(JNIEnv* env, jobject thiz, jstring searchTerm) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !searchTerm) return env->NewObjectArray(0, env->FindClass("java/lang/String"), nullptr);
    const char* term = env->GetStringUTFChars(searchTerm, 0);
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->searchEntries(term);
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
    std::vector<CipherMesh::Core::PasswordHistoryEntry> history = g_vault->getPasswordHistory(entryId);
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
    std::string decrypted;
    try { decrypted = g_vault->decryptPasswordFromHistory(encrypted); } JNI_CATCH_RETURN(env->NewStringUTF("Error"))
    env->ReleaseStringUTFChars(encryptedPassword, encrypted);
    return env->NewStringUTF(decrypted.c_str());
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_updateEntryAccessTime(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    try { g_vault->updateEntryAccessTime(entryId); } JNI_CATCH_VOID
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getEntryFullDetails(JNIEnv* env, jobject thiz, jint entryId) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return env->NewStringUTF("");
    try {
        return env->NewStringUTF(g_vault->getEntryFullDetails(entryId).c_str());
    } JNI_CATCH_RETURN(env->NewStringUTF("Error|Decryption Failed|||"))
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_verifyMasterPassword(JNIEnv* env, jobject thiz, jstring password) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !password) return false;
    const char* pwd = env->GetStringUTFChars(password, 0);
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
    bool result = g_vault->deleteGroup(name);
    env->ReleaseStringUTFChars(groupName, name);
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ciphermesh_mobile_core_Vault_getGroupOwner(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || !groupName) return env->NewStringUTF("");
    const char* name = env->GetStringUTFChars(groupName, 0);
    std::string owner = g_vault->getGroupOwner(name);
    env->ReleaseStringUTFChars(groupName, name);
    return env->NewStringUTF(owner.c_str());
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
    bool result = g_vault->groupExists(name);
    env->ReleaseStringUTFChars(groupName, name);
    return result;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_deleteEntry(JNIEnv* env, jobject, jint id) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    try { return g_vault && g_vault->deleteEntry(id); } JNI_CATCH_RETURN(false)
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_updateEntry(JNIEnv* env, jobject thiz, jint id, jstring title, jstring user, jstring pass, jstring url, jstring notes) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    
    CipherMesh::Core::VaultEntry e;
    e.id = id;
    const char* t = env->GetStringUTFChars(title, 0); e.title = t;
    const char* u = env->GetStringUTFChars(user, 0); e.username = u;
    const char* n = env->GetStringUTFChars(notes, 0); e.notes = n;
    const char* p = env->GetStringUTFChars(pass, 0); 
    const char* l = env->GetStringUTFChars(url, 0); 
    
    e.url = l; 
    if (e.url.length() > 0) {
        e.locations.push_back(CipherMesh::Core::Location(-1, "url", l));
    }
    
    bool res = false;
    try { res = g_vault->updateEntry(e, std::string(p)); } JNI_CATCH_RETURN(false)
    
    env->ReleaseStringUTFChars(title, t);
    env->ReleaseStringUTFChars(user, u);
    env->ReleaseStringUTFChars(notes, n);
    env->ReleaseStringUTFChars(pass, p);
    env->ReleaseStringUTFChars(url, l);
    return res;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ciphermesh_mobile_core_Vault_renameGroup(JNIEnv* env, jobject thiz, jstring oldName, jstring newName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return false;
    const char* cOld = env->GetStringUTFChars(oldName, 0);
    const char* cNew = env->GetStringUTFChars(newName, 0);
    bool result = false;
    try {
        if (!g_vault->groupExists(cNew)) {
            result = g_vault->renameGroup(cOld, cNew);
            if (result) triggerJavaRefresh();
        }
    } JNI_CATCH_RETURN(false)
    env->ReleaseStringUTFChars(oldName, cOld);
    env->ReleaseStringUTFChars(newName, cNew);
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ciphermesh_mobile_core_Vault_leaveGroup(JNIEnv* env, jobject thiz, jstring groupName) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault) return;
    const char* grp = env->GetStringUTFChars(groupName, 0);
    try { g_vault->leaveGroup(grp); triggerJavaRefresh(); } JNI_CATCH_VOID
    env->ReleaseStringUTFChars(groupName, grp);
}

// =============================================================
// AUTOFILL SUPPORT
// =============================================================

extern "C" JNIEXPORT jobjectArray JNICALL
Java_com_ciphermesh_mobile_core_Vault_findEntriesByLocation(JNIEnv* env, jobject thiz, jstring locationValue) {
    std::lock_guard<std::recursive_mutex> lock(g_vaultMutex);
    if (!g_vault || g_vault->isLocked()) return nullptr;

    const char* loc = env->GetStringUTFChars(locationValue, 0);
    std::vector<CipherMesh::Core::VaultEntry> entries = g_vault->findEntriesByLocation(loc);
    env->ReleaseStringUTFChars(locationValue, loc);

    jclass cls = env->FindClass("java/lang/String");
    jobjectArray res = env->NewObjectArray(entries.size(), cls, env->NewStringUTF(""));
    
    for (size_t i = 0; i < entries.size(); i++) {
        std::string pass = g_vault->getDecryptedPassword(entries[i].id);
        std::string s = std::to_string(entries[i].id) + "|" + entries[i].title + "|" + entries[i].username + "|" + pass;
        
        jstring js = env->NewStringUTF(s.c_str());
        env->SetObjectArrayElement(res, i, js);
        env->DeleteLocalRef(js);
        
        CipherMesh::Core::Crypto::secureWipe(pass);
    }
    return res;
}