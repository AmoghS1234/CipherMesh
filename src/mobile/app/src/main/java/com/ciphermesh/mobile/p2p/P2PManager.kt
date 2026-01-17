package com.ciphermesh.mobile.p2p

import android.app.Activity
import android.util.Log
import android.widget.Toast
import com.ciphermesh.mobile.core.SignalingCallback
import com.ciphermesh.mobile.core.Vault
import okhttp3.*
import org.json.JSONObject
import java.util.concurrent.TimeUnit

class P2PManager(private val activity: Activity, private val vault: Vault) : SignalingCallback {

    private val SIGNALING_URL = "wss://ciphermesh-signal-server.onrender.com" 
    
    private var webSocket: WebSocket? = null
    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS) 
        .pingInterval(30, TimeUnit.SECONDS)
        .build()

    fun connect() {
        // [CRITICAL] Register this class so C++ can call sendSignalingMessage
        vault.registerSignalingCallback(this)

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
                // Pass everything to C++ logic
                vault.receiveSignalingMessage(text)
                
                // Also handle UI updates manually for now
                activity.runOnUiThread { handleUiMessage(text) }
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                Log.e("P2P", "❌ Connection Failed: ${t.message}")
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
        // Validate WebSocket is connected before sending
        if (webSocket == null) {
            Log.e("P2P", "❌ Cannot send $type: WebSocket is null")
            activity.runOnUiThread {
                Toast.makeText(activity, "Connection lost. Please reconnect.", Toast.LENGTH_SHORT).show()
            }
            return
        }
        
        val userId = vault.getUserId()
        val json = JSONObject()
        json.put("type", type)
        json.put("sender", userId)
        json.put("target", targetId)
        
        // Format payload based on type
        if (type == "offer" || type == "answer") {
            json.put("sdp", payload)
        } else if (type == "ice-candidate") {
            json.put("payload", payload)
        } else {
            json.put("payload", payload)
        }

        val str = json.toString()
        Log.d("P2P", "📤 C++ Sending: $str")
        val sent = webSocket?.send(str) ?: false
        
        if (!sent) {
            Log.e("P2P", "❌ Failed to send $type message")
        }
    }

    private fun handleUiMessage(jsonStr: String) {
        try {
            val json = JSONObject(jsonStr)
            val type = json.optString("type")
            // [REMOVED] Don't show toast on 'offer' - wait for actual invite-request
            // The toast is now shown from native code when invite-request arrives
        } catch (e: Exception) { }
    }
}