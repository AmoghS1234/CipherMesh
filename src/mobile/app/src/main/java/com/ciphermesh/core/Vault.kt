package com.ciphermesh.mobile.core

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
    external fun createAccount(dbPath: String, username: String, masterPass: String): Boolean
    external fun unlock(masterPass: String): Boolean
    external fun isLocked(): Boolean
    external fun verifyMasterPassword(pass: String): Boolean
    external fun hasUsers(): Boolean
    
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
    external fun getGroupMembers(groupName: String): Array<String> 
    
    // --- User Management ---
    external fun inviteUser(groupName: String, targetUserId: String): Boolean
    external fun removeUser(groupName: String, targetUserId: String): Boolean

    // --- Entries ---
    external fun getEntries(): Array<String> 
    external fun addEntry(title: String, username: String, pass: String, type: String, url: String, notes: String, totp: String): Boolean
    external fun getEntryDetails(id: Int): String 
    external fun deleteEntry(id: Int): Boolean

    // --- P2P Networking (The Bridge) ---
    // 1. Get invites waiting in the database
    external fun getPendingInvites(): Array<String>
    // 2. Accept/Reject a database invite
    external fun respondToInvite(inviteId: Int, accept: Boolean)
    
    // 3. Receive JSON from WebSocket -> Pass to C++
    external fun receiveSignalingMessage(json: String)

    // 4. Register the P2PManager callback so C++ can reply
    external fun registerSignalingCallback(callback: SignalingCallback)
    
    // 5. Trigger an invite from the UI -> C++
    external fun sendP2PInvite(groupName: String, targetUserId: String)
    
    // 6. Check if we need to sync with a newly online user
    external fun getPendingInviteForUser(targetId: String): String
    
    // 7. Accept an invite received over P2P
    external fun acceptP2PInvite(groupName: String, payload: String): Boolean
    
    // 8. Test Method
    external fun testInjectInvite(sender: String, groupName: String)
}