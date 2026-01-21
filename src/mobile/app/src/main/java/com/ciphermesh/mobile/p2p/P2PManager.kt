package com.ciphermesh.mobile.p2p

import android.os.Handler
import android.os.Looper
import android.util.Log
import com.ciphermesh.mobile.core.SignalingCallback
import com.ciphermesh.mobile.core.Vault
import okhttp3.*
import org.json.JSONObject
import java.util.concurrent.TimeUnit

// Interface for HomeActivity to listen to connection changes
interface P2PConnectionListener {
    fun onConnectionStateChanged(state: P2PManager.ConnectionState)
}

class P2PManager(
    private val vault: Vault,
    private val listener: P2PConnectionListener
) : SignalingCallback {

    companion object {
        private const val TAG = "P2PManager"
        private const val SIGNALING_URL = "wss://ciphermesh-signal-server.onrender.com"
    }

    enum class ConnectionState { DISCONNECTED, CONNECTING, CONNECTED }

    private var webSocket: WebSocket? = null
    private var isConnected = false
    private var isRegistered = false
    
    // Handlers
    private val mainHandler = Handler(Looper.getMainLooper())
    
    // Client with Ping Interval to keep mobile connection alive
    private val client = OkHttpClient.Builder()
        .readTimeout(0, TimeUnit.MILLISECONDS)
        .connectTimeout(15, TimeUnit.SECONDS)
        .pingInterval(20, TimeUnit.SECONDS) 
        .build()

    fun connect() {
        if (isConnected) return
        
        notifyState(ConnectionState.CONNECTING)
        Log.d(TAG, "Connecting to $SIGNALING_URL")

        // 1. Register THIS manager as the C++ callback handler
        vault.registerSignalingCallback(this)

        val request = Request.Builder().url(SIGNALING_URL).build()

        webSocket = client.newWebSocket(request, object : WebSocketListener() {
            override fun onOpen(webSocket: WebSocket, response: Response) {
                Log.d(TAG, "WebSocket Connected")
                isConnected = true
                notifyState(ConnectionState.CONNECTED)

                // 2. Register User Identity
                val userId = vault.getUserId()
                if (userId.isNotEmpty()) {
                    val json = JSONObject()
                    json.put("type", "register")
                    json.put("id", userId)
                    json.put("userId", userId)
                    webSocket.send(json.toString())
                    Log.d(TAG, "Sent Register for $userId")
                }

                // 3. Initialize C++ P2P Service (Must happen after socket open)
                mainHandler.post { vault.initP2P(SIGNALING_URL) }
            }

            override fun onMessage(webSocket: WebSocket, text: String) {
                // Pass incoming signals directly to C++
                vault.receiveSignalingMessage(text)
            }

            override fun onFailure(webSocket: WebSocket, t: Throwable, response: Response?) {
                Log.e(TAG, "WebSocket Failure: ${t.message}")
                cleanup()
                notifyState(ConnectionState.DISCONNECTED)
                // Auto-retry in 5s
                mainHandler.postDelayed({ connect() }, 5000)
            }

            override fun onClosed(webSocket: WebSocket, code: Int, reason: String) {
                Log.d(TAG, "WebSocket Closed: $reason")
                cleanup()
                notifyState(ConnectionState.DISCONNECTED)
            }
        })
    }

    fun disconnect() {
        try {
            webSocket?.close(1000, "User Disconnect")
        } catch (e: Exception) {}
        cleanup()
    }

    private fun cleanup() {
        isConnected = false
        isRegistered = false
        webSocket = null
    }

    // --- C++ Callback Implementation ---
    override fun sendSignalingMessage(targetId: String, type: String, payload: String) {
        val socket = webSocket
        if (socket == null || !isConnected) {
            Log.w(TAG, "Cannot send $type - Disconnected")
            return
        }

        try {
            val json = JSONObject()
            json.put("type", type)
            json.put("target", targetId)
            json.put("sender", vault.getUserId())

            // Fix double-wrapping of SDP/Candidates
            if (payload.trim().startsWith("{")) {
                val payloadObj = JSONObject(payload)
                if (type == "offer" || type == "answer") {
                    json.put("sdp", payloadObj.optString("sdp"))
                } else if (type == "ice-candidate") {
                    json.put("candidate", payloadObj.optString("candidate"))
                    json.put("mid", payloadObj.optString("mid", "0"))
                }
            } else {
                val key = if (type == "offer" || type == "answer") "sdp" else "payload"
                json.put(key, payload)
            }

            socket.send(json.toString())
        } catch (e: Exception) {
            Log.e(TAG, "Error sending signal: ${e.message}")
        }
    }

    private fun notifyState(state: ConnectionState) {
        mainHandler.post { listener.onConnectionStateChanged(state) }
    }
}