package com.ciphermesh.mobile.p2p

import android.app.Activity
import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ciphermesh.mobile.core.SignalingCallback
import com.ciphermesh.mobile.core.Vault
import okhttp3.*
import org.json.JSONObject
import java.util.concurrent.TimeUnit

class P2PManager(private val activity: Activity, private val vault: Vault) : SignalingCallback {

    private val SIGNALING_URL = "wss://ciphermesh-signal-server.onrender.com" 
    private var webSocket: WebSocket? = null
    
    // Track connection state
    private var isConnected = false
    private var isRegistered = false
    
    // Handlers for retries
    private val reconnectHandler = Handler(Looper.getMainLooper())
    private val retryRegisterHandler = Handler(Looper.getMainLooper())
    
    // OkHttp Client with Aggressive Ping to keep connection alive
    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS) 
        .pingInterval(10, TimeUnit.SECONDS) // Ping every 10s to prevent silent disconnects
        .build()

    fun connect() {
        if (isConnected) return 

        // 1. Register Core Callback so C++ can trigger sends
        vault.registerSignalingCallback(this)

        val request = Request.Builder().url(SIGNALING_URL).build()
        Log.d("P2P", "🚀 Connecting to $SIGNALING_URL ...")

        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                Log.d("P2P", "✅ WebSocket Opened")
                isConnected = true
                isRegistered = false
                
                // 2. Start the Registration Loop immediately
                attemptRegistration()
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                Log.d("P2P", "📩 Received: $text")
                try {
                    val json = JSONObject(text)
                    val type = json.optString("type")
                    
                    // 3. Stop retrying once server confirms
                    if (type == "register-success") {
                        Log.d("P2P", "✅ Registration Confirmed by Server")
                        isRegistered = true
                        retryRegisterHandler.removeCallbacksAndMessages(null) 
                    } else {
                        handleMessage(json, text)
                    }
                } catch (e: Exception) {
                    Log.e("P2P", "Error parsing message: ${e.message}", e)
                }
            }

            override fun onClosing(webSocket: WebSocket, code: Int, reason: String) {
                Log.d("P2P", "⚠️ Closing: $code / $reason")
                isConnected = false
                isRegistered = false
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                Log.d("P2P", "❌ Closed: $code / $reason")
                isConnected = false
                isRegistered = false
                // 4. Auto-Reconnect if closed
                scheduleReconnect()
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                Log.e("P2P", "❌ Connection Failed: ${t.message}")
                isConnected = false
                isRegistered = false
                scheduleReconnect()
            }
        })
    }

    private fun handleMessage(json: JSONObject, rawText: String) {
        val type = json.optString("type")
        val sender = json.optString("sender")

        when (type) {
            "user-online" -> {
                val onlineUser = json.optString("user")
                if (onlineUser.isNotEmpty()) {
                    Log.d("P2P", "Peer Online: $onlineUser")
                    vault.onPeerOnline(onlineUser)
                }
            }
            "sync-packet" -> {
                val payload = json.optString("payload")
                if (payload.isNotEmpty()) {
                    vault.handleSyncMessage(sender, payload)
                }
            }
            "offer", "answer", "ice-candidate", "invite-accept" -> {
                // IMPORTANT: C++ needs to know the sender. 
                // We ensure the JSON passed to JNI has the sender field.
                if (!json.has("sender") && sender.isNotEmpty()) {
                    json.put("sender", sender)
                }
                vault.receiveSignalingMessage(json.toString())
            }
            "error" -> Log.e("P2P", "Server Error: ${json.optString("message")}")
        }
    }

    private fun attemptRegistration() {
        // Stop any existing attempts first
        retryRegisterHandler.removeCallbacksAndMessages(null)
        
        val runnable = object : Runnable {
            override fun run() {
                if (isConnected && !isRegistered && webSocket != null) {
                    val userId = vault.getUserId()
                    if (userId.isNotEmpty()) {
                        val json = JSONObject()
                        json.put("type", "register")
                        json.put("id", userId)
                        json.put("userId", userId) // Backwards compatibility
                        webSocket?.send(json.toString())
                        Log.d("P2P", "🔄 Sending Register Request for: $userId")
                        
                        // Retry again in 3 seconds if not confirmed
                        retryRegisterHandler.postDelayed(this, 3000)
                    } else {
                        Log.w("P2P", "⚠️ User ID empty, cannot register yet.")
                    }
                }
            }
        }
        // Run immediately
        runnable.run()
    }

    private fun scheduleReconnect() {
        // Don't reconnect if already connected
        if (isConnected) {
            Log.d("P2P", "ℹ️ Already connected, skipping reconnect")
            return
        }
        
        reconnectHandler.removeCallbacksAndMessages(null)
        reconnectHandler.postDelayed({
            // Double-check connection state before reconnecting
            if (!isConnected) {
                Log.d("P2P", "🔄 Attempting Reconnect...")
                connect()
            } else {
                Log.d("P2P", "ℹ️ Connection restored, canceling reconnect")
            }
        }, 10000) // Retry every 10s
    }

    override fun sendSignalingMessage(targetId: String, type: String, payload: String) {
        synchronized(this) {
            val ws = webSocket
            if (ws == null || !isConnected) {
                Log.e("P2P", "❌ Cannot send $type: Not Connected")
                return
            }
            
            try {
                val userId = vault.getUserId()
                val json = JSONObject()
                json.put("type", type)
                json.put("sender", userId)
                json.put("target", targetId)
                
                when (type) {
                    "offer", "answer" -> json.put("sdp", payload)
                    "ice-candidate" -> json.put("candidate", payload)
                    else -> json.put("payload", payload)
                }

                ws.send(json.toString())
                Log.d("P2P", "➡️ Sent $type to $targetId")
            } catch (e: Exception) {
                Log.e("P2P", "❌ Error sending message: ${e.message}", e)
            }
        }
    }
    
    fun disconnect() {
        retryRegisterHandler.removeCallbacksAndMessages(null)
        reconnectHandler.removeCallbacksAndMessages(null)
        webSocket?.close(1000, "App closing")
        webSocket = null
        isConnected = false
        isRegistered = false
    }
}