# CipherMesh P2P Invite Workflow - Implementation Requirements

## Overview
This document specifies the remaining work needed to complete the P2P invite workflow between desktop and mobile applications. The WebRTC infrastructure is now functional - connections establish, data channels open, and invite messages are delivered. What remains is implementing the actual data transfer protocol and UI improvements.

## Current State (What Works)
✅ Desktop ↔ Mobile WebRTC connection establishment  
✅ ICE candidate handling with early queueing  
✅ SDP generation with complete ICE credentials  
✅ Data channel messaging infrastructure  
✅ Invite messages delivered successfully (toast appears on mobile)  
✅ Desktop → Mobile: Invite notification received  
✅ Mobile → Desktop: **NEEDS VERIFICATION** - flow may be broken

## Issues to Fix

### 1. Mobile → Desktop Invite Flow (CRITICAL)
**Problem:** User reports that mobile → desktop invites "seem to not work"

**Investigation needed:**
- Check if mobile sends invite-request properly when initiating invite
- Verify desktop receives and displays the invite dialog
- Check mobile WebRTC service logs for any errors during mobile-initiated flow
- Compare with desktop → mobile flow (which works)

**Expected behavior:**
1. User on mobile clicks "invite" button with desktop peer selected
2. Mobile initiates WebRTC connection to desktop
3. Once data channel opens, mobile sends invite-request message
4. Desktop shows invite dialog with accept/reject buttons
5. Desktop can accept or reject the invite

**Files to check:**
- `src/mobile/app/src/main/cpp/native-lib.cpp` - JNI invite sending
- `src/mobile/app/src/main/java/com/ciphermesh/mobile/viewmodels/HomeViewModel.kt` - Mobile invite UI
- `src/desktop/mainwindow.cpp` - Desktop invite reception and dialog display

### 2. Group Data Transfer on Invite Accept (CRITICAL)
**Problem:** When mobile accepts desktop invite, the group data doesn't transfer to mobile

**Current behavior:**
- Desktop sends invite to mobile ✅
- Mobile receives invite and shows toast ✅
- User opens pending invites screen ✅
- User clicks "Accept" ✅
- Mobile sends "invite-accept" message ✅
- **Desktop does NOT transfer group data** ❌
- **Mobile does NOT receive or save the group** ❌

**Required implementation:**

#### Desktop Side (Sender):
1. **Listen for invite-accept messages** in data channel handler
   - Location: `src/p2p_webrtc/webrtcservice.cpp` - onMessage callback
   - Currently only handles invite-request, needs accept/reject handling

2. **Serialize group data** when accept is received
   - Include all group metadata: name, icon, members
   - Include all items (passwords/TOTP) in the group
   - Format: JSON with structure like:
   ```json
   {
     "type": "group-data",
     "group": {
       "id": "uuid",
       "name": "Work Passwords",
       "icon": "🔐",
       "members": ["user1", "user2"],
       "items": [
         {
           "id": "uuid",
           "type": "password",
           "title": "GitHub",
           "username": "user@example.com",
           "password": "encrypted_data",
           "url": "https://github.com",
           "notes": "encrypted_notes"
         },
         {
           "id": "uuid",
           "type": "totp",
           "title": "Google Auth",
           "secret": "encrypted_secret",
           "algorithm": "SHA1",
           "digits": 6,
           "period": 30
         }
       ]
     }
   }
   ```

3. **Send via data channel** to the accepting peer
   - Use existing sendP2PMessage functionality
   - Handle large payloads (may need chunking if >256KB)

4. **Update invite status** in UI
   - Change member status from "invited" to "accepted"
   - Show confirmation that data was sent
   - Location: `src/desktop/mainwindow.cpp` - members list update

#### Mobile Side (Receiver):
1. **Listen for group-data messages** in data channel handler
   - Location: `src/p2p_webrtc/webrtcservice.cpp` - onMessage callback
   - Add new message type handler for "group-data"

2. **Deserialize and validate** received group data
   - Parse JSON structure
   - Validate all required fields present
   - Decrypt encrypted fields using vault key

3. **Save to local database**
   - Use Room database to insert new group
   - Insert all items belonging to the group
   - Location: `src/mobile/app/src/main/java/com/ciphermesh/mobile/data/`

4. **Update UI** to show new group
   - Refresh groups list in HomeActivity
   - Show success notification
   - Remove accepted invite from pending list

5. **Error handling**
   - If parsing fails, notify user
   - If database save fails, rollback and notify
   - Log all errors for debugging

#### Protocol Specification:
```
Desktop → Mobile:
1. offer (WebRTC)
2. answer (WebRTC)
3. ice-candidate (WebRTC) [multiple]
4. invite-request (via data channel)
   {
     "type": "invite-request",
     "groupId": "uuid",
     "groupName": "Work Passwords",
     "fromUser": "amogh_8040e634aae3ed7b"
   }

Mobile → Desktop:
5. invite-accept (via data channel)
   {
     "type": "invite-accept",
     "groupId": "uuid",
     "accepted": true
   }

Desktop → Mobile:
6. group-data (via data channel)
   {
     "type": "group-data",
     "group": { ... } // full group object
   }

Mobile → Desktop:
7. group-received (acknowledgment)
   {
     "type": "group-received",
     "groupId": "uuid",
     "success": true
   }
```

### 3. Invite Status Updates on Desktop (HIGH PRIORITY)
**Problem:** When invite is accepted/rejected, desktop members list still shows "invited"

**Current behavior:**
- Desktop sends invite
- Member added to group with "invited" status
- Mobile accepts or rejects
- **Desktop UI doesn't update** ❌

**Required implementation:**

1. **Desktop receives invite-accept**
   - Parse message in WebRTCService onMessage
   - Extract groupId and acceptance status
   - Emit signal/callback to desktop UI

2. **Update database**
   - Change member status from "pending" to "accepted" or "rejected"
   - Location: `src/desktop/vault.cpp` - update member status method

3. **Refresh UI**
   - Update members list widget
   - Change status badge from "Invited" to "Member" or remove if rejected
   - Location: `src/desktop/mainwindow.cpp` - members list

4. **Handle rejection**
   - Remove member from group if rejected
   - Show notification that user declined
   - Clean up any pending invite state

**Files to modify:**
- `src/p2p_webrtc/webrtcservice.cpp` - Add accept/reject message handling
- `src/desktop/vault.cpp` - Add updateMemberStatus() method
- `src/desktop/mainwindow.cpp` - Wire up UI refresh on status change

### 4. Mobile Pending Invites UI Improvements (MEDIUM PRIORITY)
**Problem:** "terrible in terms of UI we have to improve it and match it with theme"

**Current issues:**
- UI doesn't match app theme
- Poor UX/layout
- Needs polish

**Required improvements:**

1. **Theme matching**
   - Use theme_service colors for consistency
   - Match desktop app's color scheme
   - Support light/dark mode properly

2. **Layout improvements**
   - Better card design for each pending invite
   - Show group icon and name prominently
   - Show sender's username
   - Show timestamp of when invite was received
   - Clear accept/reject buttons with icons

3. **UX enhancements**
   - Add swipe-to-accept/reject gestures
   - Show loading indicator while processing
   - Animate removal of accepted/rejected invites
   - Add empty state when no pending invites

4. **Suggested layout:**
   ```
   ┌─────────────────────────────────────┐
   │  Pending Invites                    │
   ├─────────────────────────────────────┤
   │ ┌─────────────────────────────────┐ │
   │ │ 🔐 Work Passwords               │ │
   │ │ From: @amogh_8040e634aae3ed7b   │ │
   │ │ 2 minutes ago                   │ │
   │ │                                 │ │
   │ │ [✓ Accept]  [✗ Reject]         │ │
   │ └─────────────────────────────────┘ │
   │ ┌─────────────────────────────────┐ │
   │ │ 🏠 Personal                     │ │
   │ │ From: @user123                  │ │
   │ │ 1 hour ago                      │ │
   │ │                                 │ │
   │ │ [✓ Accept]  [✗ Reject]         │ │
   │ └─────────────────────────────────┘ │
   └─────────────────────────────────────┘
   ```

**Files to modify:**
- `src/mobile/app/src/main/res/layout/fragment_pending_invites.xml`
- `src/mobile/app/src/main/res/layout/item_pending_invite.xml` (create)
- `src/mobile/app/src/main/java/com/ciphermesh/mobile/ui/PendingInvitesFragment.kt`
- `src/mobile/app/src/main/res/values/colors.xml` - Use theme_service colors

## Implementation Order (Recommended)

### Phase 1: Fix Critical Functionality
1. **Investigate and fix mobile → desktop invite flow**
   - Debug WebRTC connection from mobile side
   - Verify invite-request message sending
   - Test desktop reception and dialog display
   - Estimate: 2-4 hours

2. **Implement group data transfer protocol**
   - Desktop: Add invite-accept handler
   - Desktop: Implement group serialization
   - Desktop: Send group-data message
   - Mobile: Add group-data handler
   - Mobile: Implement deserialization and save
   - Mobile: Update UI on receipt
   - Estimate: 6-8 hours

3. **Implement invite status updates**
   - Desktop: Listen for accept/reject
   - Desktop: Update database status
   - Desktop: Refresh members list UI
   - Estimate: 2-3 hours

### Phase 2: Polish UI
4. **Improve mobile pending invites screen**
   - Apply theme matching
   - Redesign card layout
   - Add animations and better UX
   - Estimate: 3-4 hours

**Total estimated effort:** 13-19 hours

## Testing Checklist

### Mobile → Desktop Flow
- [ ] Mobile can initiate invite to desktop peer
- [ ] Desktop receives invite and shows dialog
- [ ] Desktop can accept invite
- [ ] Desktop can reject invite
- [ ] Mobile receives accept/reject confirmation

### Desktop → Mobile Flow
- [ ] Desktop can initiate invite to mobile peer
- [ ] Mobile receives invite notification (toast)
- [ ] Mobile shows invite in pending invites screen
- [ ] Mobile can accept invite
- [ ] Mobile receives complete group data
- [ ] Group appears in mobile groups list
- [ ] All passwords/TOTP items transferred correctly
- [ ] Mobile can reject invite
- [ ] Desktop receives accept/reject confirmation
- [ ] Desktop updates member status to "Member" on accept
- [ ] Desktop removes member on reject

### Edge Cases
- [ ] Handle accept/reject timeout (30 seconds)
- [ ] Handle duplicate invites (already invited)
- [ ] Handle invites to offline peers
- [ ] Handle connection drop during data transfer
- [ ] Handle malformed group data
- [ ] Handle very large groups (100+ items)
- [ ] Handle concurrent invites (both peers invite each other)

### UI Testing
- [ ] Pending invites screen matches theme
- [ ] Accept button works correctly
- [ ] Reject button works correctly
- [ ] Loading states show properly
- [ ] Success/error messages appear
- [ ] Animations are smooth
- [ ] Empty state displays when no invites

## Files That Need Modification

### Desktop (C++/Qt)
1. `src/p2p_webrtc/webrtcservice.cpp`
   - Add handler for "invite-accept" messages
   - Add handler for "invite-reject" messages
   - Implement group data serialization
   - Send "group-data" message on accept

2. `src/desktop/vault.cpp`
   - Add `updateMemberStatus(groupId, userId, status)` method
   - Add `getGroupData(groupId)` method for serialization

3. `src/desktop/mainwindow.cpp`
   - Connect invite status signals to UI updates
   - Refresh members list when status changes
   - Show notifications on accept/reject

### Mobile (Kotlin/Java + C++ JNI)
1. `src/p2p_webrtc/webrtcservice.cpp`
   - Add handler for "group-data" messages
   - Deserialize and validate group data
   - Call JNI callback to save to database

2. `src/mobile/app/src/main/cpp/native-lib.cpp`
   - Add JNI method for group data receipt
   - Marshal data to Kotlin layer

3. `src/mobile/app/src/main/java/com/ciphermesh/mobile/data/GroupRepository.kt`
   - Add `insertReceivedGroup(group)` method
   - Handle transaction for group + items

4. `src/mobile/app/src/main/java/com/ciphermesh/mobile/viewmodels/HomeViewModel.kt`
   - Add method to process received group
   - Update LiveData to refresh UI

5. `src/mobile/app/src/main/java/com/ciphermesh/mobile/ui/PendingInvitesFragment.kt`
   - Redesign UI with theme matching
   - Implement better UX

6. `src/mobile/app/src/main/res/layout/fragment_pending_invites.xml`
   - Update layout with new design

## Security Considerations

1. **Data encryption**: Ensure group data is encrypted in transit (already handled by WebRTC DTLS)
2. **Validation**: Validate all incoming group data before saving
3. **Authorization**: Verify the sender is actually a member of the group
4. **Injection prevention**: Sanitize all string fields
5. **Rate limiting**: Prevent spam invites from malicious peers

## Success Criteria

The implementation is complete when:
1. ✅ Mobile → Desktop invites work reliably
2. ✅ Desktop → Mobile invites transfer complete group data
3. ✅ Mobile saves received groups to database
4. ✅ Desktop updates member status on accept/reject
5. ✅ Mobile pending invites UI matches theme and has good UX
6. ✅ All edge cases handled gracefully
7. ✅ No crashes or data loss
8. ✅ All tests pass

## Notes for Implementation

- The WebRTC infrastructure is solid - focus on application layer protocol
- Use existing JSON message format for consistency
- Follow existing code patterns for desktop and mobile
- Add comprehensive logging for debugging
- Consider chunking for large group transfers (>100 items)
- Think about bandwidth - don't send unnecessary data
- Handle backward compatibility if protocol changes

## References

- WebRTC fixes PR: Current PR with commits d9d0cad through 649c5bf
- P2P protocol: `src/p2p_webrtc/webrtcservice.cpp`
- Desktop vault: `src/desktop/vault.cpp`
- Mobile database: `src/mobile/app/src/main/java/com/ciphermesh/mobile/data/`
- Existing invite flow: Search for "invite-request" in codebase
