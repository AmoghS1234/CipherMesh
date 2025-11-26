// Background script for CipherMesh Firefox Extension
// Handles native messaging with the standalone vault service

// Native messaging connection to vault service
let nativePort = null;
const NATIVE_APP_NAME = "com.ciphermesh.vault";
let pendingRequests = new Map();
let requestId = 0;

// Connect to vault service
function connectNative() {
    try {
        nativePort = browser.runtime.connectNative(NATIVE_APP_NAME);
        console.log("[CipherMesh] Connected to vault service");
        nativePort.onMessage.addListener((message) => {
            console.log("Received from native:", message);
            handleNativeMessage(message);
        });
        
        nativePort.onDisconnect.addListener(() => {
            console.log("[CipherMesh] Disconnected from vault service");
            nativePort = null;
            
            // Notify content scripts that connection is lost
            browser.tabs.query({}).then(tabs => {
                tabs.forEach(tab => {
                    browser.tabs.sendMessage(tab.id, {
                        type: "CONNECTION_STATUS",
                        connected: false
                    }).catch(() => {});
                });
            });
        });
        
        console.log("[CipherMesh] Connected to vault service");
        return true;
    } catch (error) {
        console.error("[CipherMesh] Failed to connect to vault service:", error);
        return false;
    }
}

// Send message to vault service
function sendToNative(message) {
    return new Promise((resolve, reject) => {
        if (!nativePort) {
            if (!connectNative()) {
                reject(new Error("Failed to connect to vault service. Make sure it's installed."));
                return;
            }
        }
        
        const id = requestId++;
        message.requestId = id;
        
        pendingRequests.set(id, { resolve, reject });
        
        try {
            nativePort.postMessage(message);
            
            // Timeout after 5 seconds
            setTimeout(() => {
                if (pendingRequests.has(id)) {
                    pendingRequests.delete(id);
                    reject(new Error("Request timeout - vault service not responding. Make sure the vault database exists."));
                }
            }, 5000);
        } catch (error) {
            pendingRequests.delete(id);
            reject(error);
        }
    });
}

// Handle messages from native host
function handleNativeMessage(message) {
    const { requestId, type, success, data, error } = message;
    
    // Log response type only, not sensitive data
    console.log("Received from native:", { requestId, type, success, hasData: !!data, error });
    
    if (pendingRequests.has(requestId)) {
        const { resolve, reject } = pendingRequests.get(requestId);
        pendingRequests.delete(requestId);
        
        if (success) {
            resolve(data);
        } else {
            reject(new Error(error || "Unknown error"));
        }
    }
}

// Handle messages from content scripts
browser.runtime.onMessage.addListener((message, sender, sendResponse) => {
    // Log message type only, not sensitive data
    if (message.type === "VERIFY_MASTER_PASSWORD") {
        console.log("Message from content script:", { type: message.type, hasPassword: !!message.password });
    } else {
        console.log("Message from content script:", { type: message.type, url: message.url || '' });
    }
    
    switch (message.type) {
        case "GET_CREDENTIALS":
            sendToNative({
                type: "GET_CREDENTIALS",
                url: message.url,
                username: message.username
            }).then(data => {
                sendResponse({ success: true, data });
            }).catch(error => {
                sendResponse({ success: false, error: error.message });
            });
            return true; // Keep channel open for async response
            
        case "SAVE_CREDENTIALS":
            sendToNative({
                type: "SAVE_CREDENTIALS",
                url: message.url,
                username: message.username,
                password: message.password,
                title: message.title,
                group: message.group
            }).then(data => {
                sendResponse({ success: true, data });
            }).catch(error => {
                sendResponse({ success: false, error: error.message });
            });
            return true;
            
        case "CHECK_CONNECTION":
            // Check if already connected, don't reconnect unnecessarily
            if (nativePort) {
                sendResponse({ connected: true });
                return false;
            }
            
            // Try to connect
            sendToNative({
                type: "PING"
            }).then(() => {
                sendResponse({ connected: true });
            }).catch(() => {
                sendResponse({ connected: false });
            });
            return true;
            
        case "VERIFY_MASTER_PASSWORD":
            // Don't log password
            sendToNative({
                type: "VERIFY_MASTER_PASSWORD",
                password: message.password
            }).then(data => {
                sendResponse({ success: true, verified: data.verified });
            }).catch(error => {
                sendResponse({ success: false, error: error.message });
            });
            return true;
            
        case "LIST_GROUPS":
            sendToNative({
                type: "LIST_GROUPS"
            }).then(data => {
                sendResponse({ success: true, groups: data.groups });
            }).catch(error => {
                sendResponse({ success: false, error: error.message });
            });
            return true;
    }
    
    return false;
});

// Initialize connection on startup
connectNative();

console.log("CipherMesh background script loaded");
