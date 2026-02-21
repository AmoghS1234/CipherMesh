package com.ciphermesh.mobile.core

import android.app.Activity

interface SignalingCallback {
    fun sendSignalingMessage(targetId: String, type: String, payload: String)
}

interface VaultUpdateListener {
    fun onVaultUpdated()
}

class Vault {

    companion object {
        init {
            System.loadLibrary("ciphermesh_jni")
        }
    }

    private var updateListener: VaultUpdateListener? = null

    fun registerUpdateListener(listener: VaultUpdateListener) {
        updateListener = listener
    }

    external fun init(dbPath: String)
    external fun setActivityContext(activity: Activity)
    external fun createAccount(dbPath: String, username: String, masterPass: String): Boolean
    external fun unlock(password: String): Boolean
    external fun lock()
    external fun isLocked(): Boolean
    external fun verifyMasterPassword(password: String): Boolean
    external fun hasUsers(): Boolean
    external fun generateTOTP(secret: String): String

    external fun onPeerOnline(userId: String)
    external fun handleSyncMessage(senderId: String, payload: String)

    fun handleIncomingSync(senderId: String, payload: String) {
        handleSyncMessage(senderId, payload)
        updateListener?.onVaultUpdated()
    }

    external fun getUserId(): String
    external fun getDisplayUsername(): String

    external fun getGroupNames(): Array<String>
    external fun addGroup(groupName: String): Boolean
    external fun deleteGroup(groupName: String): Boolean
    external fun setActiveGroup(groupName: String): Boolean
    external fun groupExists(groupName: String): Boolean
    external fun getGroupOwner(groupName: String): String
    external fun getGroupMembers(groupName: String): Array<String>

    external fun removeUser(groupName: String, targetUserId: String)

    external fun getEntries(): Array<String>?

    // [FIX] Added missing declaration for AutoFill
    external fun findEntriesByLocation(location: String): Array<String>?
    
    // [FIX] Added missing declaration for Password Copying
    external fun getDecryptedPassword(id: Int): String

    fun addEntry(
        title: String,
        username: String,
        pass: String,
        type: String, 
        url: String,
        notes: String,
        totp: String
    ): Boolean {
        addEntryNative(title, username, pass, url, notes, totp)
        return true
    }

    external fun addEntryNative(
        title: String,
        user: String,
        pass: String,
        url: String,
        notes: String,
        totp: String
    )

    external fun deleteEntry(id: Int): Boolean
    
    external fun updateEntry(
        id: Int,
        title: String,
        user: String,
        pass: String,
        url: String, 
        notes: String,
        totp: String
    ): Boolean

    external fun renameGroup(oldName: String, newName: String): Boolean
    external fun leaveGroup(groupName: String)
    external fun isGroupOwner(groupName: String): Boolean

    external fun initP2P(signalingUrl: String)
    external fun getPendingInvites(): Array<String>

    external fun respondToInvite(groupName: String, senderId: String, accept: Boolean)
    external fun updatePendingInviteStatus(id: Int, status: String)

    external fun receiveSignalingMessage(json: String)
    external fun sendP2PInvite(groupName: String, targetUser: String)
    external fun broadcastSync(groupName: String)
    external fun queueGroupSplitSync(groupName: String, targetUser: String)

    external fun registerSignalingCallback(callback: SignalingCallback)

    external fun searchEntries(searchTerm: String): Array<String>
    external fun getRecentlyAccessedEntries(limit: Int): Array<String>
    external fun getPasswordHistory(entryId: Int): Array<String>
    external fun decryptPasswordFromHistory(encryptedPassword: String): String
    external fun updateEntryAccessTime(entryId: Int)
    external fun getEntryFullDetails(entryId: Int): String
    
    // Password management
    external fun changeMasterPassword(currentPassword: String, newPassword: String): Boolean
    
    // Export/Import vault data
    external fun exportVault(): String
    external fun importVault(data: String, password: String): Boolean
}