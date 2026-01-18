package com.ciphermesh.mobile.core

import android.app.Activity

// [CRITICAL] Defines the callback for P2P messages (C++ -> Kotlin)
interface SignalingCallback {
    fun sendSignalingMessage(targetId: String, type: String, payload: String)
}

class Vault {
    companion object {
        init {
            System.loadLibrary("ciphermesh_jni")
        }
    }

    // --- Core Lifecycle ---
    external fun init(dbPath: String)
    external fun setActivityContext(activity: Activity)
    external fun createAccount(dbPath: String, username: String, masterPass: String): Boolean
    external fun unlock(password: String): Boolean
    external fun isLocked(): Boolean
    external fun verifyMasterPassword(password: String): Boolean
    external fun hasUsers(): Boolean
    external fun generateTOTP(secret: String): String
    
    // Sync Hooks
    external fun onPeerOnline(userId: String)
    external fun handleSyncMessage(senderId: String, payload: String)
    
    // --- Identity ---
    external fun getUserId(): String
    external fun getDisplayUsername(): String
    
    // --- Groups ---
    external fun getGroupNames(): Array<String>
    external fun addGroup(groupName: String): Boolean
    external fun deleteGroup(groupName: String): Boolean
    external fun setActiveGroup(groupName: String): Boolean
    external fun groupExists(groupName: String): Boolean
    external fun getGroupOwner(groupName: String): String
    external fun isGroupOwner(groupName: String): Boolean
    external fun getGroupMembers(groupName: String): Array<String>
    
    // --- User Management ---
    // 'inviteUser' removed because it didn't exist in native-lib. Use sendP2PInvite instead.
    external fun removeUser(groupName: String, targetUserId: String)

    // --- Entries ---
    external fun getEntries(): Array<String>?
    
    // [Wrapper] Bridges HomeActivity (7 args) to Native (5 args)
    // Note: 'type' and 'totp' are currently dropped because native-lib addEntryNative only takes 5 args.
    fun addEntry(title: String, username: String, pass: String, type: String, url: String, notes: String, totp: String): Boolean {
        addEntryNative(title, username, pass, url, notes)
        return true
    }
    
    // Matches Java_com_ciphermesh_mobile_core_Vault_addEntryNative in native-lib.cpp
    external fun addEntryNative(title: String, user: String, pass: String, url: String, notes: String)

    // 'getEntryDetails' removed (duplicate/missing). Use getEntryFullDetails.
    external fun deleteEntry(id: Int): Boolean
    external fun updateEntry(id: Int, title: String, user: String, pass: String, notes: String): Boolean

    // --- P2P Networking (The Bridge) ---
    external fun initP2P(signalingUrl: String)
    external fun getPendingInvites(): Array<String>
    
    external fun respondToInvite(groupName: String, senderId: String, accept: Boolean)
    external fun updatePendingInviteStatus(id: Int, status: String)
    
    external fun receiveSignalingMessage(json: String)
    external fun sendP2PInvite(groupName: String, targetUser: String)
    external fun broadcastSync(groupName: String)
    external fun queueGroupSplitSync(groupName: String, targetUser: String) // [NEW] For group split on deletion
    external fun registerSignalingCallback(callback: SignalingCallback)
    
    // Removed methods not implemented in C++ to prevent crashes:
    // - getPendingInviteForUser
    // - acceptP2PInvite
    // - testInjectInvite
    
    // --- Search and History ---
    external fun searchEntries(searchTerm: String): Array<String>
    external fun getRecentlyAccessedEntries(limit: Int): Array<String>
    external fun getPasswordHistory(entryId: Int): Array<String>
    external fun decryptPasswordFromHistory(encryptedPassword: String): String
    external fun updateEntryAccessTime(entryId: Int)
    external fun getEntryFullDetails(entryId: Int): String
}