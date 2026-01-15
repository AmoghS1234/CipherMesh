package com.ciphermesh.mobile.p2p

import android.app.Activity
import android.util.Log
import android.widget.Toast
import com.ciphermesh.mobile.core.InviteCallback
import com.ciphermesh.mobile.core.SignalingCallback
import com.ciphermesh.mobile.core.Vault
import okhttp3.*
import org.json.JSONObject
import java.util.concurrent.TimeUnit

class P2PManager(private val activity: Activity, private val vault: Vault) : SignalingCallback, InviteCallback {

    private val SIGNALING_URL = "wss://ciphermesh-signal-server.onrender.com" 
    
    private var webSocket: WebSocket? = null
    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS) 
        .pingInterval(30, TimeUnit.SECONDS)
        .build()

    fun connect() {
        // [CRITICAL] Register this class so C++ can call sendSignalingMessage
        vault.registerSignalingCallback(this)
        
        // [NEW] Register for invite notifications
        vault.registerInviteCallback(this)

        val request = Request.Builder().url(SIGNALING_URL).build()
        Log.d("P2P", "Connecting to $SIGNALING_URL")

        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                Log.d("P2P", "✅ Connected to Server")
                activity.runOnUiThread { 
                    Toast.makeText(activity, "Connected!", Toast.LENGTH_SHORT).show() 
                }
                registerUser()
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                Log.d("P2P", "📩 Received: $text")
                try {
                    // Pass everything to C++ logic
                    vault.receiveSignalingMessage(text)
                    
                    // Also handle UI updates manually for now
                    activity.runOnUiThread { 
                        try {
                            handleUiMessage(text)
                        } catch (e: Exception) {
                            Log.e("P2P", "Error in handleUiMessage", e)
                        }
                    }
                } catch (e: Exception) {
                    Log.e("P2P", "Error processing signaling message", e)
                    activity.runOnUiThread {
                        Toast.makeText(activity, "P2P Error: ${e.message}", Toast.LENGTH_SHORT).show()
                    }
                }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                Log.e("P2P", "❌ Connection Failed: ${t.message}", t)
                activity.runOnUiThread {
                    Toast.makeText(activity, "P2P Connection Lost", Toast.LENGTH_SHORT).show()
                }
            }
        })
    }

    private fun registerUser() {
        val userId = vault.getUserId()
        if (userId.isNotEmpty() && userId != "Locked") {
            val regMsg = JSONObject()
            regMsg.put("type", "register")
            regMsg.put("id", userId)
            regMsg.put("pubKey", "") 
            webSocket?.send(regMsg.toString())
            
            sendPing() // Announce presence immediately
        }
    }

    // [FIX] This method was missing in previous builds
    fun sendPing() {
        val userId = vault.getUserId()
        if (webSocket != null && userId.isNotEmpty()) {
            val pingMsg = JSONObject()
            pingMsg.put("type", "online-ping")
            pingMsg.put("sender", userId)
            webSocket?.send(pingMsg.toString())
            Log.d("P2P", "📡 Sent Ping")
        }
    }

    // Called BY C++ (via JNI) to send data out
    override fun sendSignalingMessage(targetId: String, type: String, payload: String) {
        val userId = vault.getUserId()
        val json = JSONObject()
        json.put("type", type)
        json.put("sender", userId)
        json.put("target", targetId)
        
        // Format payload based on type
        if (type == "offer" || type == "answer") {
            json.put("sdp", payload)
        } else if (type == "candidate") {
            json.put("candidate", payload)
        } else {
            json.put("payload", payload)
        }

        val str = json.toString()
        Log.d("P2P", "📤 C++ Sending: $str")
        webSocket?.send(str)
    }

    private fun handleUiMessage(jsonStr: String) {
        try {
            val json = JSONObject(jsonStr)
            val type = json.optString("type")
            Log.d("P2P", "🔄 [INVITE] Received signaling message type=$type")
            if (type == "offer") {
                val sender = json.optString("sender")
                Log.d("P2P", "📩 [INVITE] Processing offer from $sender")
            }
        } catch (e: Exception) { 
            Log.e("P2P", "Error parsing signaling message", e)
        }
    }
    
    // [NEW] Implement InviteCallback - called from C++ when invite received
    override fun onIncomingInvite(senderId: String, groupName: String) {
        Log.d("P2P", "🔔 [INVITE] onIncomingInvite from=$senderId group=$groupName")
        activity.runOnUiThread {
            // Show notification that invite was received
            Toast.makeText(activity, "📨 Group invite from $senderId\nGroup: $groupName\nCheck 'Pending Invites' menu", Toast.LENGTH_LONG).show()
            
            // You can also trigger a notification here or update a badge
            // For now, the user can access invites via the navigation menu
        }
    }
}