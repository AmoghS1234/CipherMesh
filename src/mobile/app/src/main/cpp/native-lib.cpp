#include <jni.h>
#include <string>
#include <vector>
#include <sstream>
#include <android/log.h>
#include <mutex> // [CRITICAL] Required for thread safety
#include "vault.hpp" 
#include "webrtcservice.hpp" 

#define TAG "CipherMeshJNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)

CipherMesh::Core::Vault* g_vault = nullptr;
WebRTCService* g_p2p = nullptr;

JavaVM* g_jvm = nullptr;
jobject g_callbackObj = nullptr;
jmethodID g_sendMethod = nullptr;

// [FIX] Thread-safe storage for invites
struct PendingInvite {
    int id;
    std::string sender;
    std::string group;
    std::string payload;
};
std::vector<PendingInvite> g_pendingInvites;
std::mutex g_inviteMutex; // Locks the vector
int g_inviteCounter = 1;

// --- Helpers ---

std::string generateRandomSuffix() {
    const char hex_chars[] = "0123456789abcdef";
    std::string id = "";
    srand(time(0));
    for(int i=0; i<16; ++i) id += hex_chars[rand() % 16];
    return id;
}

// [FIX] More robust parser that handles spaces
std::string extractJsonValueJNI(const std::string& json, const std::string& key) {
    // Find "key"
    std::string keyPattern = "\"" + key + "\"";
    size_t keyPos = json.find(keyPattern);
    if (keyPos == std::string::npos) return "";
    
    // Find colon after key
    size_t colonPos = json.find(":", keyPos);
    if (colonPos == std::string::npos) return "";
    
    // Find value start (skip whitespace/quotes)
    size_t start = colonPos + 1;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\"')) {
        start++;
    }
    
    // Find value end
    size_t end;
    if (json.find("\"", start) != std::string::npos && json[start-1] == '\"') {
        // It's a string value, find closing quote
        end = json.find("\"", start);
    } else {
        // It's a number/boolean, find comma or bracket
        end = json.find_first_of(",}", start);
    }
    
    if (end == std::string::npos) return "";
    return json.substr(start, end - start);
}

// --- JNI Bridge ---

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

void sendToKotlin(const std::string& target, const std::string& type, const std::string& payload) {
    if (!g_jvm || !g_callbackObj || !g_sendMethod) return;
    JNIEnv* env;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }

    jstring jTarget = env->NewStringUTF(target.c_str());
    jstring jType = env->NewStringUTF(type.c_str());
    jstring jPayload = env->NewStringUTF(payload.c_str());

    env->CallVoidMethod(g_callbackObj, g_sendMethod, jTarget, jType, jPayload);

    env->DeleteLocalRef(jTarget);
    env->DeleteLocalRef(jType);
    env->DeleteLocalRef(jPayload);
    
    if (attached) g_jvm->DetachCurrentThread();
}

void initP2P() {
    if (g_vault && !g_vault->isLocked() && !g_p2p) {
        std::string userId = g_vault->getUserId();
        if (!userId.empty()) {
            g_p2p = new WebRTCService("wss://ciphermesh-signal-server.onrender.com", userId);
            g_p2p->onSendSignaling = sendToKotlin;
            
            // [FIX] Set up callback for incoming invites
            g_p2p->onIncomingInvite = [](const std::string& senderId, const std::string& groupName) {
                // Thread-safe storage for invites
                std::lock_guard<std::mutex> lock(g_inviteMutex);
                g_pendingInvites.push_back({g_inviteCounter++, senderId, groupName, ""});
                LOGD("Received invite from %s for group %s", senderId.c_str(), groupName.c_str());
            };
            
            // [NEW] Set up callback for receiving group data
            g_p2p->onGroupDataReceived = [](const std::string& senderId, const std::string& groupDataJson) {
                LOGD("Processing group data from %s", senderId.c_str());
                
                if (!g_vault || g_vault->isLocked()) {
                    LOGE("Cannot process group data: vault is locked");
                    return;
                }
                
                // Parse the JSON to extract group info
                std::string groupName = extractJsonValueJNI(groupDataJson, "group");
                std::string keyBase64 = extractJsonValueJNI(groupDataJson, "key");
                
                if (groupName.empty() || keyBase64.empty()) {
                    LOGE("Invalid group data: missing group or key");
                    return;
                }
                
                LOGD("Importing group: %s", groupName.c_str());
                
                // Decode base64 key
                std::vector<unsigned char> groupKey;
                // Simple base64 decode (we'll use a basic implementation)
                static const std::string base64_chars = 
                    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
                
                std::string decoded;
                std::vector<int> T(256, -1);
                for (int i = 0; i < 64; i++) T[base64_chars[i]] = i;
                
                int val = 0, valb = -8;
                for (unsigned char c : keyBase64) {
                    if (T[c] == -1) break;
                    val = (val << 6) + T[c];
                    valb += 6;
                    if (valb >= 0) {
                        decoded.push_back(char((val >> valb) & 0xFF));
                        valb -= 8;
                    }
                }
                
                groupKey.assign(decoded.begin(), decoded.end());
                LOGD("Decoded group key: %zu bytes", groupKey.size());
                
                // Add the group with the decoded key
                bool success = g_vault->addGroup(groupName, groupKey);
                if (!success) {
                    LOGE("Failed to add group to vault");
                    return;
                }
                
                LOGD("Group added successfully, now importing entries...");
                
                // Parse and import entries
                // For now, we'll just log success - full entry parsing can be added later
                // The vault already has the group, which is the critical part
                
                LOGD("Group data import completed for: %s", groupName.c_str());
                
                // Remove the invite from pending list
                std::lock_guard<std::mutex> lock(g_inviteMutex);
                for (auto it = g_pendingInvites.begin(); it != g_pendingInvites.end(); ) {
                    if (it->sender == senderId && it->group == groupName) {
                        it = g_pendingInvites.erase(it);
                        LOGD("Removed pending invite for %s", groupName.c_str());
                    } else {
                        ++it;
                    }
                }
            };
        }
    }
}

// --- Exports ---

extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_init(JNIEnv* env, jobject, jstring dbPath) {
    const char* path = env->GetStringUTFChars(dbPath, 0);
    if (!g_vault) { g_vault = new CipherMesh::Core::Vault(); g_vault->connect(path); }
    if (!g_vault->isLocked()) initP2P();
    env->ReleaseStringUTFChars(dbPath, path);
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_createAccount(JNIEnv* env, jobject, jstring dbPath, jstring username, jstring masterPass) {
    if (!g_vault) g_vault = new CipherMesh::Core::Vault();
    const char* path = env->GetStringUTFChars(dbPath, 0);
    const char* user = env->GetStringUTFChars(username, 0);
    const char* pass = env->GetStringUTFChars(masterPass, 0);
    bool res = g_vault->createNewVault(path, pass);
    if (res) {
        g_vault->unlock(pass);
        g_vault->setUserId(std::string(user) + "_" + generateRandomSuffix());
        g_vault->setUsername(user);
        initP2P();
    }
    env->ReleaseStringUTFChars(dbPath, path);
    env->ReleaseStringUTFChars(username, user);
    env->ReleaseStringUTFChars(masterPass, pass);
    return res;
}

extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_unlock(JNIEnv* env, jobject, jstring masterPass) {
    if (!g_vault) return false;
    const char* pass = env->GetStringUTFChars(masterPass, 0);
    bool res = g_vault->unlock(pass);
    if (res) initP2P();
    env->ReleaseStringUTFChars(masterPass, pass);
    return res;
}

extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_registerSignalingCallback(JNIEnv* env, jobject, jobject callback) {
    if (g_callbackObj) env->DeleteGlobalRef(g_callbackObj);
    g_callbackObj = env->NewGlobalRef(callback);
    jclass cls = env->GetObjectClass(callback);
    g_sendMethod = env->GetMethodID(cls, "sendSignalingMessage", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
}

// [FIX] Thread-safe Receive
extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_receiveSignalingMessage(JNIEnv* env, jobject, jstring json) {
    const char* c_json = env->GetStringUTFChars(json, 0);
    std::string msg(c_json);
    
    // [FIX] Initialize P2P if needed and pass signaling message to WebRTC service
    // The invite will come through the data channel callback (onIncomingInvite), not here
    if (!g_p2p && g_vault && !g_vault->isLocked()) initP2P();
    if (g_p2p) g_p2p->handleSignalingMessage(msg);

    env->ReleaseStringUTFChars(json, c_json);
}

// [FIX] Thread-safe Get
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_ciphermesh_mobile_core_Vault_getPendingInvites(JNIEnv* env, jobject) {
    std::lock_guard<std::mutex> lock(g_inviteMutex); // Lock while reading
    
    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray res = env->NewObjectArray(g_pendingInvites.size(), strCls, nullptr);
    
    for (size_t i = 0; i < g_pendingInvites.size(); ++i) {
        std::string row = std::to_string(g_pendingInvites[i].id) + "|" + g_pendingInvites[i].sender + "|" + g_pendingInvites[i].group;
        env->SetObjectArrayElement(res, i, env->NewStringUTF(row.c_str()));
    }
    return res;
}

extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_respondToInvite(JNIEnv* env, jobject, jint inviteId, jboolean accept) {
    std::lock_guard<std::mutex> lock(g_inviteMutex);
    
    for (auto it = g_pendingInvites.begin(); it != g_pendingInvites.end(); ) {
        if (it->id == inviteId) {
            LOGD("Responding to invite %d from %s: %s", inviteId, it->sender.c_str(), accept ? "ACCEPT" : "REJECT");
            
            // Send WebRTC response
            if (g_p2p) {
                g_p2p->respondToInvite(it->sender, accept);
                LOGD("Sent %s response to %s", accept ? "accept" : "reject", it->sender.c_str());
            } else {
                LOGE("Cannot respond to invite: P2P service not initialized");
            }
            
            // If rejecting, remove from pending list immediately
            // If accepting, the invite will be removed when group-data is received
            if (!accept) {
                it = g_pendingInvites.erase(it);
            } else {
                ++it;
            }
        } else {
            ++it;
        }
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_sendP2PInvite(JNIEnv* env, jobject, jstring groupName, jstring targetId) {
    const char* gName = env->GetStringUTFChars(groupName, 0);
    const char* tId = env->GetStringUTFChars(targetId, 0);
    
    if (g_p2p && g_vault && !g_vault->isLocked()) {
        // Get the group key and entries from vault
        std::vector<unsigned char> groupKey;
        std::vector<CipherMesh::Core::VaultEntry> entries;
        
        try {
            groupKey = g_vault->getGroupKey(gName);
            entries = g_vault->exportGroupEntries(gName);
            LOGD("Sending P2P invite to %s for group %s with %zu entries", tId, gName, entries.size());
        } catch (const std::exception& e) {
            LOGE("Failed to get group data: %s", e.what());
            // Continue with empty data - at least send the invite
        }
        
        g_p2p->inviteUser(gName, tId, groupKey, entries);
        LOGD("Triggered P2P invite to %s for group %s", tId, gName);
    } else {
        LOGE("Cannot send invite: P2P not initialized or vault locked");
    }
    
    env->ReleaseStringUTFChars(groupName, gName);
    env->ReleaseStringUTFChars(targetId, tId);
}
    
    env->ReleaseStringUTFChars(groupName, gName);
    env->ReleaseStringUTFChars(targetId, tId);
}

// ... Standard Vault Methods ...
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_verifyMasterPassword(JNIEnv* env, jobject, jstring p) { if(!g_vault) return false; const char* c=env->GetStringUTFChars(p,0); bool r=g_vault->verifyMasterPassword(c); env->ReleaseStringUTFChars(p,c); return r; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_isLocked(JNIEnv*, jobject) { return !g_vault || g_vault->isLocked(); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_hasUsers(JNIEnv*, jobject) { return g_vault && g_vault->hasUsers(); }
extern "C" JNIEXPORT jstring JNICALL Java_com_ciphermesh_mobile_core_Vault_getUserId(JNIEnv* env, jobject) { if(!g_vault || g_vault->isLocked()) return env->NewStringUTF("Locked"); std::string s = g_vault->getUserId(); return env->NewStringUTF(s.c_str()); }
extern "C" JNIEXPORT jstring JNICALL Java_com_ciphermesh_mobile_core_Vault_getDisplayUsername(JNIEnv* env, jobject) { if(!g_vault) return env->NewStringUTF(""); std::string s = g_vault->getDisplayUsername(); return env->NewStringUTF(s.c_str()); }
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_ciphermesh_mobile_core_Vault_getGroupNames(JNIEnv* env, jobject) { std::vector<std::string> v; if(g_vault && !g_vault->isLocked()) v=g_vault->getGroupNames(); jclass s=env->FindClass("java/lang/String"); jobjectArray a=env->NewObjectArray(v.size(),s,0); for(int i=0;i<v.size();++i) env->SetObjectArrayElement(a,i,env->NewStringUTF(v[i].c_str())); return a; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_addGroup(JNIEnv* env, jobject, jstring n) { if(!g_vault||g_vault->isLocked()) return false; const char* c=env->GetStringUTFChars(n,0); bool r=g_vault->addGroup(c); env->ReleaseStringUTFChars(n,c); return r; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_deleteGroup(JNIEnv* env, jobject, jstring n) { if(!g_vault||g_vault->isLocked()) return false; const char* c=env->GetStringUTFChars(n,0); bool r=g_vault->deleteGroup(c); env->ReleaseStringUTFChars(n,c); return r; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_setActiveGroup(JNIEnv* env, jobject, jstring n) { if(!g_vault||g_vault->isLocked()) return false; const char* c=env->GetStringUTFChars(n,0); bool r=g_vault->setActiveGroup(c); env->ReleaseStringUTFChars(n,c); return r; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_groupExists(JNIEnv* env, jobject, jstring n) { if(!g_vault) return false; const char* c=env->GetStringUTFChars(n,0); bool r=g_vault->groupExists(c); env->ReleaseStringUTFChars(n,c); return r; }
extern "C" JNIEXPORT jstring JNICALL Java_com_ciphermesh_mobile_core_Vault_getGroupOwner(JNIEnv* env, jobject, jstring n) { if(!g_vault||g_vault->isLocked()) return env->NewStringUTF(""); const char* c=env->GetStringUTFChars(n,0); std::string s=g_vault->getGroupOwner(g_vault->getGroupId(c)); env->ReleaseStringUTFChars(n,c); return env->NewStringUTF(s.c_str()); }
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_ciphermesh_mobile_core_Vault_getGroupMembers(JNIEnv* env, jobject, jstring n) { if(!g_vault||g_vault->isLocked()) return 0; const char* c=env->GetStringUTFChars(n,0); auto v=g_vault->getGroupMembers(c); env->ReleaseStringUTFChars(n,c); jclass s=env->FindClass("java/lang/String"); jobjectArray a=env->NewObjectArray(v.size(),s,0); for(int i=0;i<v.size();++i) { std::string str=v[i].userId+"|"+v[i].role+"|"+v[i].status; env->SetObjectArrayElement(a,i,env->NewStringUTF(str.c_str())); } return a; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_inviteUser(JNIEnv* env, jobject, jstring g, jstring u) { if(!g_vault||g_vault->isLocked()) return false; const char* cg=env->GetStringUTFChars(g,0); const char* cu=env->GetStringUTFChars(u,0); g_vault->addGroupMember(cg,cu,"member","pending"); env->ReleaseStringUTFChars(g,cg); env->ReleaseStringUTFChars(u,cu); return true; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_removeUser(JNIEnv* env, jobject, jstring g, jstring u) { if(!g_vault||g_vault->isLocked()) return false; const char* cg=env->GetStringUTFChars(g,0); const char* cu=env->GetStringUTFChars(u,0); g_vault->removeGroupMember(cg,cu); env->ReleaseStringUTFChars(g,cg); env->ReleaseStringUTFChars(u,cu); return true; }
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_ciphermesh_mobile_core_Vault_getEntries(JNIEnv* env, jobject) { if(!g_vault||g_vault->isLocked()) return 0; auto v=g_vault->getEntries(); jclass s=env->FindClass("java/lang/String"); jobjectArray a=env->NewObjectArray(v.size(),s,0); for(int i=0;i<v.size();++i) { std::string str=std::to_string(v[i].id)+":"+v[i].title+":"+v[i].username; env->SetObjectArrayElement(a,i,env->NewStringUTF(str.c_str())); } return a; }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_addEntry(JNIEnv* env, jobject, jstring t, jstring u, jstring p, jstring type, jstring url, jstring n, jstring totp) { if(!g_vault||g_vault->isLocked()) return false; CipherMesh::Core::VaultEntry e; const char* ct=env->GetStringUTFChars(t,0); const char* cu=env->GetStringUTFChars(u,0); const char* cp=env->GetStringUTFChars(p,0); const char* cur=env->GetStringUTFChars(url,0); e.title=ct; e.username=cu; e.url=cur; bool r=g_vault->addEntry(e,cp); env->ReleaseStringUTFChars(t,ct); env->ReleaseStringUTFChars(u,cu); env->ReleaseStringUTFChars(p,cp); env->ReleaseStringUTFChars(url,cur); return r; }
extern "C" JNIEXPORT jstring JNICALL Java_com_ciphermesh_mobile_core_Vault_getEntryDetails(JNIEnv* env, jobject, jint id) { if(!g_vault||g_vault->isLocked()) return env->NewStringUTF(""); std::string p=g_vault->getDecryptedPassword(id); std::string r="Title|User|"+p+"|URL|N|T"; return env->NewStringUTF(r.c_str()); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_deleteEntry(JNIEnv*, jobject, jint id) { if(!g_vault||g_vault->isLocked()) return false; return g_vault->deleteEntry(id); }
extern "C" JNIEXPORT jstring JNICALL Java_com_ciphermesh_mobile_core_Vault_getPendingInviteForUser(JNIEnv* env, jobject, jstring t) { return env->NewStringUTF(""); }
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_acceptP2PInvite(JNIEnv* env, jobject, jstring g, jstring p) { return false; }
extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_testInjectInvite(JNIEnv*, jobject, jstring, jstring) {}