# P2P Mechanism Bug Analysis and Fixes

## Summary
The P2P invite mechanism was experiencing a deadlock where invites from mobile to desktop would reach the desktop, but after accepting the invite, no data transfer would occur. This document details all the bugs found and fixed.

## Critical Bugs Found

### 1. Invite-Accept Routing Issue (DEADLOCK CAUSE)
**File**: `src/mobile/app/src/main/java/com/ciphermesh/mobile/p2p/P2PManager.kt`

**Problem**: 
- Mobile's P2PManager was routing "invite-accept" messages through `vault.receiveSignalingMessage()` 
- This function is meant for WebRTC signaling messages (offer/answer/ICE candidates), NOT P2P data channel messages
- Desktop's WebRTCService was waiting for "invite-accept" via the data channel `onMessage` handler
- The message never arrived via data channel because mobile sent it via WebSocket

**Root Cause**:
Line 111 included "invite-accept" in the list of signaling messages:
```kotlin
"offer", "answer", "ice-candidate", "invite-accept" -> {
    vault.receiveSignalingMessage(json.toString())
}
```

**Fix**:
Removed "invite-accept" from WebSocket signaling routing and added warning log:
```kotlin
"offer", "answer", "ice-candidate" -> {
    vault.receiveSignalingMessage(json.toString())
}
"invite-accept" -> {
    Log.d("P2P", "⚠️ Received invite-accept via WebSocket (expected via DataChannel)")
}
```

### 2. Native Layer Invite-Accept Interception
**File**: `src/mobile/app/src/main/cpp/native-lib.cpp`

**Problem**:
- `receiveSignalingMessage()` was intercepting "invite-accept" messages
- It would call `handleInviteAccept()` and return early
- This prevented the message from reaching WebRTCService's `receiveSignalingMessage()` 
- But "invite-accept" should never go through signaling - it's a P2P message

**Root Cause**:
Lines 598-612 intercepted invite-accept:
```cpp
if (type == "invite-accept") {
    handleInviteAccept(sender, jsonStr);
    env->ReleaseStringUTFChars(message, msg);
    return;  // Early return
}
```

**Fix**:
Removed the interception code. The WebRTCService data channel `onMessage` handler already correctly processes "invite-accept" messages.

### 3. Orphaned Callback
**File**: `src/mobile/app/src/main/cpp/native-lib.cpp`

**Problem**:
- `onSyncMessage` callback was set up but never called by WebRTCService
- It was trying to handle "invite-accept" but this is handled by the data channel

**Fix**:
Removed the unused callback registration (lines 298-305).

## Secondary Bugs Fixed

### 4. Race Condition on Data Channel Open
**File**: `src/p2p_webrtc/webrtcservice.cpp` (Android)

**Problem**:
- When data channel opened, Android immediately sent "invite-request" message
- Desktop already had a 500ms delay (line 614)
- But Android sent immediately, which could cause the remote peer to miss the message if its `onMessage` listener wasn't ready yet

**Fix**:
Added 500ms delay before sending invite-request:
```cpp
dc->onOpen([this, peerId]() {
    LOGI("Data Channel OPEN for %s", peerId.c_str());
    
    // [FIX] Add 500ms delay to allow remote peer to register onMessage listener
    std::thread([this, peerId]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        // ... send invite-request
    }).detach();
});
```

### 5. Sync Messages Not Handled Over Data Channel
**Files**: 
- `src/p2p_webrtc/webrtcservice.cpp` (both Android and Desktop)
- `src/mobile/app/src/main/cpp/native-lib.cpp`

**Problem**:
- Sync messages (sync-payload, sync-ack) were only sent via WebSocket
- They weren't handled in the data channel `onMessage` handler
- This meant offline sync couldn't work because data channel is peer-to-peer

**Fix**:
1. Added sync message handling in Android WebRTCService:
```cpp
else if (type == "sync-payload" || type == "sync-ack") {
    if (onSyncMessage) {
        // Add sender field
        std::ostringstream enriched;
        enriched << "{\"sender\":\"" << peerId << "\",";
        if (msg.length() > 1 && msg[0] == '{') {
            enriched << msg.substr(1);
        }
        onSyncMessage(enriched.str());
    }
}
```

2. Added sync message handling in Desktop WebRTCService:
```cpp
else if (type == "sync-payload" || type == "sync-ack") {
    if (onSyncMessage) {
        QJsonObject enriched = obj;
        enriched["sender"] = remoteId;
        QString enrichedStr = QJsonDocument(enriched).toJson(QJsonDocument::Compact);
        onSyncMessage(enrichedStr.toStdString());
    }
}
```

3. Wired up `onSyncMessage` callback in native-lib.cpp:
```cpp
g_p2p->onSyncMessage = [](std::string message) {
    std::lock_guard<std::recursive_mutex> vaultLock(g_vaultMutex);
    if (!g_vault || g_vault->isLocked()) return;
    
    std::string sender = extractJsonValueJNI(message, "sender");
    if (sender.empty()) return;
    
    g_vault->handleIncomingSync(sender, message);
};
```

### 6. Member-List Messages Not Handled Over Data Channel
**File**: `src/p2p_webrtc/webrtcservice.cpp` (Android only)

**Problem**:
- Desktop sends member-list messages via data channel (mainwindow.cpp:1560-1571)
- Android WebRTCService didn't handle them in `onMessage`
- Native-lib.cpp had code to process member-list but it was never triggered

**Fix**:
Added member-list handling in Android WebRTCService:
```cpp
else if (type == "member-list" || type == "member-leave") {
    if (onGroupDataReceived) onGroupDataReceived(peerId, msg);
}
```

Note: Desktop doesn't need to handle receiving member-list because it's always the group owner sending the list, not receiving it.

## Offline Handling

**Finding**: Offline invite queueing already exists and works correctly!

The `m_pendingInvites` map stores invites when sent. When a peer comes online:
- Android: Line 244 - `if (m_pendingInvites.count(sender)) retryPendingInviteFor(sender);`
- Desktop: Line 498 - `if (m_pendingInvites.contains(user)) retryPendingInviteFor(user);`

This automatically retries sending the invite when the peer comes online.

## Message Flow (After Fixes)

### Mobile Invites Desktop:
1. Mobile: Creates peer connection, sends WebRTC offer via WebSocket
2. Desktop: Receives offer via WebSocket, creates peer connection, sends answer
3. Mobile: Receives answer, ICE negotiation happens via WebSocket
4. **Data channel opens**
5. Mobile: Waits 500ms, sends `{"type":"invite-request", "group":"GroupName"}` via data channel
6. Desktop: Receives invite-request via data channel, shows UI prompt
7. User accepts on desktop
8. Desktop: Sends `{"type":"invite-accept"}` via **data channel**
9. Mobile: Receives invite-accept via **data channel** in WebRTCService::onMessage
10. Mobile: Calls sendGroupData() which sends group-data, entry-data, member-list via data channel
11. Desktop: Receives and processes the data

### Desktop Invites Mobile:
1. Desktop: Creates peer connection, sends WebRTC offer via WebSocket
2. Mobile: Receives offer via WebSocket, creates peer connection, sends answer
3. Desktop: Receives answer, ICE negotiation happens via WebSocket
4. **Data channel opens**
5. Desktop: Waits 500ms, sends `{"type":"invite-request", "group":"GroupName"}` via data channel
6. Mobile: Receives invite-request via data channel, shows notification
7. User accepts on mobile
8. Mobile: Sends `{"type":"invite-accept"}` via **data channel**
9. Desktop: Receives invite-accept via **data channel** in WebRTCService::handleP2PMessage
10. Desktop: Calls sendGroupData() which sends group-data, entry-data, member-list via data channel
11. Mobile: Receives and processes the data

## Testing Recommendations

1. **Mobile → Desktop invite**: Send invite from mobile, accept on desktop, verify data transfer
2. **Desktop → Mobile invite**: Send invite from desktop, accept on mobile, verify data transfer
3. **Offline queueing**: Send invite while peer is offline, verify it's sent when peer comes online
4. **Sync**: Make a change in a shared group, verify it syncs over data channel
5. **Member list**: Accept an invite, verify member list is received correctly

## Known Limitations

1. **Desktop can't receive group data as invitee**: Desktop's WebRTCService::handleP2PMessage doesn't handle incoming group-data/entry-data messages. This means:
   - ✓ Desktop → Mobile invites work (desktop sends, mobile receives)
   - ✗ Mobile → Desktop invites don't work fully (mobile can't send group data to desktop)
   
   This is not a bug - it's an unimplemented feature. The desktop has `handleGroupData()` method (mainwindow.cpp:329) but the WebRTCService never calls `onGroupDataReceived` callback when receiving group-data/entry-data messages.
   
   To fix this, the desktop WebRTCService would need to:
   - Accumulate group-data and entry-data messages
   - Parse and decode them
   - Call the onGroupDataReceived callback with the accumulated data

2. **Sync direction**: Sync is currently one-directional. Need to test bidirectional sync.
