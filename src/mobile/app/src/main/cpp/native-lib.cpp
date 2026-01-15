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
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

CipherMesh::Core::Vault* g_vault = nullptr;
WebRTCService* g_p2p = nullptr;

JavaVM* g_jvm = nullptr;
jobject g_callbackObj = nullptr;
jmethodID g_sendMethod = nullptr;

// [FIX] Callback for notifying UI of incoming invites
jobject g_uiCallbackObj = nullptr;
jmethodID g_onInviteMethod = nullptr;

// [NEW] Helper function to call Java invite callback from C++
void notifyInviteToJava(const std::string& senderId, const std::string& groupName) {
    if (!g_jvm || !g_uiCallbackObj || !g_onInviteMethod) {
        LOGD("⚠️  [INVITE] UI callback not set - cannot notify Java");
        return;
    }
    
    JNIEnv* env;
    bool attached = false;
    if (g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        g_jvm->AttachCurrentThread(&env, nullptr);
        attached = true;
    }
    
    if (!g_vault || g_vault->isLocked()) {
        LOGE("❌ [INVITE] Vault locked - cannot store invite");
        if (attached) g_jvm->DetachCurrentThread();
        return;
    }
    
    // Check for duplicate group name and add number suffix if needed
    std::string finalGroupName = groupName;
    auto existingGroups = g_vault->getGroupNames();
    bool isDuplicate = false;
    for (const auto& existing : existingGroups) {
        if (existing == groupName) {
            isDuplicate = true;
            break;
        }
    }
    
    if (isDuplicate) {
        int suffix = 2;
        while (true) {
            std::string testName = groupName + " (" + std::to_string(suffix) + ")";
            bool found = false;
            for (const auto& existing : existingGroups) {
                if (existing == testName) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                finalGroupName = testName;
                break;
            }
            suffix++;
        }
        LOGD("📝 [INVITE] Group name '%s' exists, using '%s'", groupName.c_str(), finalGroupName.c_str());
    }
    
    // Store in database
    g_vault->storePendingInvite(senderId, finalGroupName, "");
    LOGD("💾 [INVITE] Stored invite from %s for group '%s'", senderId.c_str(), finalGroupName.c_str());
    
    // Notify UI
    jstring jSender = env->NewStringUTF(senderId.c_str());
    jstring jGroup = env->NewStringUTF(finalGroupName.c_str());
    env->CallVoidMethod(g_uiCallbackObj, g_onInviteMethod, jSender, jGroup);
    env->DeleteLocalRef(jSender);
    env->DeleteLocalRef(jGroup);
    LOGD("🔔 [INVITE] Notified UI of invite from %s for group '%s'", senderId.c_str(), finalGroupName.c_str());
    
    if (attached) g_jvm->DetachCurrentThread();
}

// --- Helpers ---

std::string generateRandomSuffix() {
    const char hex_chars[] = "0123456789abcdef";
    std::string id = "";
    srand(time(0));
    for(int i=0; i<16; ++i) id += hex_chars[rand() % 16];
    return id;
}

// [FIX] More robust parser that handles spaces and validates JSON
std::string extractJsonValueJNI(const std::string& json, const std::string& key) {
    if (json.empty() || key.empty()) {
        LOGE("⚠️  [JSON] Empty input to parser");
        return "";
    }
    
    // Find "key"
    std::string keyPattern = "\"" + key + "\"";
    size_t keyPos = json.find(keyPattern);
    if (keyPos == std::string::npos) {
        LOGD("⚠️  [JSON] Key '%s' not found", key.c_str());
        return "";
    }
    
    // Find colon after key
    size_t colonPos = json.find(":", keyPos);
    if (colonPos == std::string::npos) {
        LOGE("⚠️  [JSON] Malformed JSON - no colon after key '%s'", key.c_str());
        return "";
    }
    
    // Find value start (skip whitespace/quotes)
    size_t start = colonPos + 1;
    while (start < json.length() && (json[start] == ' ' || json[start] == '\t' || json[start] == '\n')) {
        start++;
    }
    
    if (start >= json.length()) {
        LOGE("⚠️  [JSON] Unexpected end of JSON after key '%s'", key.c_str());
        return "";
    }
    
    bool isString = (json[start] == '\"');
    if (isString) start++; // Skip opening quote
    
    // Find value end
    size_t end;
    if (isString) {
        // It's a string value, find closing quote
        end = json.find("\"", start);
        if (end == std::string::npos) {
            LOGE("⚠️  [JSON] Unclosed string for key '%s'", key.c_str());
            return "";
        }
    } else {
        // It's a number/boolean, find comma or bracket
        end = json.find_first_of(",}", start);
        if (end == std::string::npos) {
            LOGE("⚠️  [JSON] Malformed JSON - no delimiter after key '%s'", key.c_str());
            return "";
        }
    }
    
    return json.substr(start, end - start);
}

// --- JNI Bridge ---

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    return JNI_VERSION_1_6;
}

void sendToKotlin(const std::string& target, const std::string& type, const std::string& payload) {
    if (!g_jvm || !g_callbackObj || !g_sendMethod) {
        LOGE("❌ [SIGNAL] sendToKotlin FAILED: g_jvm=%p g_callbackObj=%p g_sendMethod=%p", 
             (void*)g_jvm, (void*)g_callbackObj, (void*)g_sendMethod);
        return;
    }
    
    LOGD("📤 [SIGNAL] sendToKotlin: Sending %s to %s", type.c_str(), target.c_str());
    
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
    
    LOGD("✅ [SIGNAL] sendToKotlin: Successfully called Kotlin callback");

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
            g_p2p->connect();  // [FIX] Critical: Enable P2P service
            g_p2p->onSendSignaling = sendToKotlin;
            
            // [NEW] Register callback for incoming invites
            g_p2p->onIncomingInvite = [](const std::string& senderId, const std::string& groupName) {
                LOGD("🔔 [P2P] onIncomingInvite callback triggered: sender=%s group=%s", senderId.c_str(), groupName.c_str());
                notifyInviteToJava(senderId, groupName);
            };
            
            // [NEW] Register callback for data requests
            g_p2p->onDataRequested = [](const std::string& requesterId, const std::string& groupName, const std::string& requesterPubKey) {
                LOGD("📥 [P2P] onDataRequested from=%s group=%s", requesterId.c_str(), groupName.c_str());
                
                if (!g_vault || g_vault->isLocked()) {
                    LOGE("❌ [P2P] Cannot send data - vault locked");
                    return;
                }
                
                // Get group data
                try {
                    auto key = g_vault->getGroupKey(groupName);
                    auto entries = g_vault->exportGroupEntries(groupName);
                    
                    if (g_p2p) {
                        g_p2p->sendGroupData(requesterId, groupName, key, entries);
                        LOGD("✅ [P2P] Sent group data to %s", requesterId.c_str());
                    }
                } catch (const std::exception& e) {
                    LOGE("❌ [P2P] Error sending group data: %s", e.what());
                }
            };
            
            // [NEW] Register callback for peer online notifications
            g_p2p->onPeerOnline = [](const std::string& userId) {
                LOGD("🟢 [P2P] Peer online: %s", userId.c_str());
                
                // Check if we have pending accepted invites FROM this user
                if (g_vault && !g_vault->isLocked()) {
                    auto invites = g_vault->getPendingInvites();
                    for (const auto& inv : invites) {
                        if (inv.senderId == userId && inv.status == "accepted") {
                            LOGD("🔄 [P2P] Found accepted invite from %s - requesting data", userId.c_str());
                            if (g_p2p) {
                                g_p2p->requestData(userId, inv.groupName);
                            }
                            break;
                        }
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
    LOGD("✅ [P2P] Registered signaling callback");
}

// [NEW] Register UI callback for invite notifications
extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_registerInviteCallback(JNIEnv* env, jobject, jobject callback) {
    if (g_uiCallbackObj) env->DeleteGlobalRef(g_uiCallbackObj);
    g_uiCallbackObj = env->NewGlobalRef(callback);
    jclass cls = env->GetObjectClass(callback);
    g_onInviteMethod = env->GetMethodID(cls, "onIncomingInvite", "(Ljava/lang/String;Ljava/lang/String;)V");
    LOGD("✅ [UI] Registered invite callback");
}

// [FIX] Thread-safe Receive with persistent storage and UI callback
extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_receiveSignalingMessage(JNIEnv* env, jobject, jstring json) {
    const char* c_json = env->GetStringUTFChars(json, 0);
    std::string msg(c_json);
    std::string type = extractJsonValueJNI(msg, "type");
    std::string sender = extractJsonValueJNI(msg, "sender");
    
    LOGD("📩 [INVITE] receiveSignalingMessage type=%s sender=%s", type.c_str(), sender.c_str());

    if (type == "offer" && !sender.empty()) {
        // [FIX] Don't store invite yet - wait for invite-request with actual group name
        // Just log that we received the offer for WebRTC handshake
        LOGD("📨 [SIGNAL] Received WebRTC offer from %s - handshake will begin", sender.c_str());
    }
    
    // Also pass to WebRTC core for handshake processing
    if (!g_p2p && g_vault && !g_vault->isLocked()) initP2P();
    if (g_p2p) {
        g_p2p->handleSignalingMessage(msg);
        LOGD("✅ [INVITE] Passed to WebRTCService for processing");
    }

    env->ReleaseStringUTFChars(json, c_json);
}

// [FIX] Get invites from database
extern "C" JNIEXPORT jobjectArray JNICALL Java_com_ciphermesh_mobile_core_Vault_getPendingInvites(JNIEnv* env, jobject) {
    if (!g_vault || g_vault->isLocked()) {
        jclass strCls = env->FindClass("java/lang/String");
        return env->NewObjectArray(0, strCls, nullptr);
    }
    
    auto invites = g_vault->getPendingInvites();
    LOGD("📋 [INVITE] getPendingInvites found %zu invites", invites.size());
    
    jclass strCls = env->FindClass("java/lang/String");
    jobjectArray res = env->NewObjectArray(invites.size(), strCls, nullptr);
    
    for (size_t i = 0; i < invites.size(); ++i) {
        std::string row = std::to_string(invites[i].id) + "|" + invites[i].senderId + "|" + invites[i].groupName;
        env->SetObjectArrayElement(res, i, env->NewStringUTF(row.c_str()));
    }
    return res;
}

extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_respondToInvite(JNIEnv* env, jobject, jint inviteId, jboolean accept) {
    LOGD("🎯 [INVITE] respondToInvite id=%d accept=%s", inviteId, accept ? "YES" : "NO");
    
    if (!g_vault || g_vault->isLocked()) {
        LOGE("❌ [INVITE] Cannot respond - vault locked");
        return;
    }
    
    // Get invite details from database
    auto invites = g_vault->getPendingInvites();
    for (const auto& inv : invites) {
        if (inv.id == inviteId) {
            if (accept) {
                // Create group locally
                g_vault->addGroup(inv.groupName);
                LOGD("✅ [INVITE] Created group '%s'", inv.groupName.c_str());
                
                // [FIX] Send P2P acceptance response
                if (g_p2p) {
                    g_p2p->respondToInvite(inv.senderId, true);
                    LOGD("📤 [INVITE] Sent acceptance to %s", inv.senderId.c_str());
                } else {
                    LOGE("⚠️  [INVITE] P2P service not initialized - cannot send response");
                }
                
                // Update status in database to 'accepted' so we can request data when peer comes online
                g_vault->updatePendingInviteStatus(inviteId, "accepted");
            } else {
                // Send rejection
                if (g_p2p) {
                    g_p2p->respondToInvite(inv.senderId, false);
                    LOGD("📤 [INVITE] Sent rejection to %s", inv.senderId.c_str());
                }
                // Delete invite from database
                g_vault->deletePendingInvite(inviteId);
            }
            break;
        }
    }
}

extern "C" JNIEXPORT void JNICALL Java_com_ciphermesh_mobile_core_Vault_sendP2PInvite(JNIEnv* env, jobject, jstring groupName, jstring targetId) {
    const char* gName = env->GetStringUTFChars(groupName, 0);
    const char* tId = env->GetStringUTFChars(targetId, 0);
    
    LOGD("📤 [INVITE] sendP2PInvite group='%s' to='%s'", gName, tId);
    
    if (g_p2p) {
        g_p2p->inviteUser(gName, tId, {}, {});
        LOGD("✅ [INVITE] Triggered P2P invite to %s", tId);
    } else {
        LOGE("❌ [INVITE] P2P service not initialized");
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
extern "C" JNIEXPORT jboolean JNICALL Java_com_ciphermesh_mobile_core_Vault_renameGroup(JNIEnv* env, jobject, jstring oldN, jstring newN) { if(!g_vault||g_vault->isLocked()) return false; const char* cOld=env->GetStringUTFChars(oldN,0); const char* cNew=env->GetStringUTFChars(newN,0); bool r=g_vault->renameGroup(cOld, cNew); env->ReleaseStringUTFChars(oldN,cOld); env->ReleaseStringUTFChars(newN,cNew); return r; }
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