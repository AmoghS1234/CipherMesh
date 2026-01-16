# Signal Server Fixes Required

This document outlines the required fixes for the CipherMesh Signal Server repository (`AmoghS1234/CipherMesh-Signal-Server`).

## Current Issues

The signal server (`server.js`) has the following critical bugs:
1. ❌ No error handling for malformed JSON
2. ❌ No validation for required fields
3. ❌ Missing heartbeat/ping-pong mechanism
4. ❌ No graceful handling of duplicate connections
5. ❌ Doesn't handle `userId` field (client sends `id` but might also send `userId`)

## Required Implementation

### 1. Message Type Support

The server must support these message types:

**Incoming Messages:**
- `register`: `{ type: "register", id: string, userId?: string, pubKey?: string }`
- `check-user`: `{ type: "check-user", target: string }`
- `offer`: `{ type: "offer", target: string, sdp: string, sender?: string }`
- `answer`: `{ type: "answer", target: string, sdp: string, sender?: string }`
- `ice-candidate`: `{ type: "ice-candidate", target: string, candidate: object }`
- `invite`: `{ type: "invite", target: string, data: object }`

**Outgoing Responses:**
- `register-success`: `{ type: "register-success" }`
- `user-online`: `{ type: "user-online", user: string }`
- `user-offline`: `{ type: "user-offline", user: string }`
- `user-status`: `{ type: "user-status", target: string, status: "online"|"offline" }`
- `error`: `{ type: "error", message: string }`

### 2. Error Handling Requirements

```javascript
// JSON parsing with error handling
ws.on('message', (rawMessage) => {
    let message;
    try {
        message = JSON.parse(rawMessage);
    } catch (error) {
        ws.send(JSON.stringify({ type: 'error', message: 'Invalid JSON' }));
        return;
    }
    
    // Validate message.type exists
    if (!message.type) {
        ws.send(JSON.stringify({ type: 'error', message: 'Missing message type' }));
        return;
    }
    
    // Handle message...
});

// WebSocket error handlers
ws.on('error', (error) => {
    console.error('WebSocket error:', error);
});

ws.on('close', () => {
    // Cleanup connections
    // Broadcast user-offline
});
```

### 3. Backwards Compatibility

Accept both `id` and `userId` fields:

```javascript
if (message.type === 'register') {
    const userId = message.userId || message.id; // Support both fields
    if (!userId) {
        ws.send(JSON.stringify({ type: 'error', message: 'Missing user ID' }));
        return;
    }
    // Register user...
}
```

### 4. Field Validation

Validate required fields per message type:

```javascript
const validators = {
    'register': ['id'], // or 'userId'
    'check-user': ['target'],
    'offer': ['target', 'sdp'],
    'answer': ['target', 'sdp'],
    'ice-candidate': ['target', 'candidate'],
    'invite': ['target', 'data']
};

function validateMessage(message) {
    const required = validators[message.type];
    if (!required) return { valid: true };
    
    for (const field of required) {
        if (!message[field]) {
            return { 
                valid: false, 
                error: `Missing required field: ${field}` 
            };
        }
    }
    return { valid: true };
}
```

### 5. Heartbeat/Ping-Pong Mechanism

```javascript
const HEARTBEAT_INTERVAL = 30000; // 30 seconds
const HEARTBEAT_TIMEOUT = 90000;  // 90 seconds

function setupHeartbeat(ws, userId) {
    let lastPong = Date.now();
    
    // Send ping every 30 seconds
    const pingInterval = setInterval(() => {
        if (ws.readyState !== WebSocket.OPEN) {
            clearInterval(pingInterval);
            return;
        }
        
        // Check if client is still alive
        if (Date.now() - lastPong > HEARTBEAT_TIMEOUT) {
            console.log(`Client ${userId} timeout - disconnecting`);
            ws.terminate();
            clearInterval(pingInterval);
            return;
        }
        
        ws.ping();
    }, HEARTBEAT_INTERVAL);
    
    ws.on('pong', () => {
        lastPong = Date.now();
    });
    
    ws.on('close', () => {
        clearInterval(pingInterval);
    });
}
```

### 6. Duplicate Connection Handling

```javascript
const connections = new Map(); // userId -> ws

wss.on('connection', (ws) => {
    let userId = null;
    
    ws.on('message', (rawMessage) => {
        // ... parse message ...
        
        if (message.type === 'register') {
            userId = message.userId || message.id;
            
            // Handle duplicate connection
            if (connections.has(userId)) {
                console.log(`Duplicate connection for ${userId} - closing old connection`);
                const oldWs = connections.get(userId);
                oldWs.close(1000, 'Replaced by new connection');
            }
            
            connections.set(userId, ws);
            setupHeartbeat(ws, userId);
            
            // Broadcast user-online to all other users
            broadcastUserStatus(userId, 'online');
            
            ws.send(JSON.stringify({ type: 'register-success' }));
        }
    });
    
    ws.on('close', () => {
        if (userId && connections.get(userId) === ws) {
            connections.delete(userId);
            broadcastUserStatus(userId, 'offline');
        }
    });
});
```

### 7. Detailed Logging

```javascript
function log(level, message, data = {}) {
    const timestamp = new Date().toISOString();
    console.log(JSON.stringify({
        timestamp,
        level,
        message,
        ...data
    }));
}

// Usage:
log('info', 'User registered', { userId, connectionCount: connections.size });
log('error', 'Invalid message', { type: message.type, error: 'Missing field' });
log('debug', 'Relaying ICE candidate', { from: sender, to: target });
```

### 8. Memory Leak Prevention

```javascript
// Cleanup orphaned connections
const CONNECTION_CLEANUP_INTERVAL = 60000; // 1 minute

setInterval(() => {
    const now = Date.now();
    for (const [userId, ws] of connections.entries()) {
        if (ws.readyState === WebSocket.CLOSED || ws.readyState === WebSocket.CLOSING) {
            console.log(`Cleaning up orphaned connection for ${userId}`);
            connections.delete(userId);
        }
    }
}, CONNECTION_CLEANUP_INTERVAL);
```

## Complete Example Implementation

See the attached `server-fixed.js` file for a complete production-ready implementation incorporating all fixes above.

## Testing Criteria

After implementation, verify:
- [ ] Server handles 100+ concurrent connections without crashes
- [ ] Malformed JSON returns error message (doesn't crash)
- [ ] Missing required fields return appropriate error
- [ ] Duplicate connections are handled gracefully (old connection closed)
- [ ] Both `id` and `userId` fields work for registration
- [ ] Heartbeat mechanism detects dead connections within 90 seconds
- [ ] Memory doesn't leak over 24+ hours of operation
- [ ] All messages are logged with timestamps
- [ ] WebRTC signaling (offer/answer/ice-candidate) relay correctly

## Deployment Notes

1. Use process manager (PM2, systemd) to auto-restart on crashes
2. Enable logging to file for debugging
3. Set up monitoring/alerting for connection counts
4. Consider rate limiting per IP to prevent abuse
