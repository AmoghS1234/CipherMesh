# Remaining P2P Issues Analysis and Implementation Plan

## Issues Reported by User

1. ✅ **Locations not transferred (mobile→desktop)** - FIXED (f8aa464)
2. ⚠️ **Desktop→Mobile password transfer not working** - NEEDS INVESTIGATION
3. ⚠️ **Offline handling** - Needs verification
4. ⚠️ **Member list syncing** - Partially fixed
5. ⚠️ **Desktop/Mobile feature parity** - Needs implementation
6. ⚠️ **Group ownership display** - Needs fixes
7. ⚠️ **Member list UI** - Desktop done, Mobile needs work
8. ⚠️ **Member list auto-refresh** - Needs implementation

---

## Detailed Analysis

### Issue 2: Desktop→Mobile Password Transfer

**Current State:**
- Desktop sends encrypted passwords via `exportGroupEntries()` (line 771 vault.cpp)
- Passwords are base64-encoded encrypted blobs
- Mobile receives via `addEncryptedEntry()` and stores correctly
- Code flow appears correct

**Possible Causes:**
1. Group key mismatch
2. Member not added to group properly
3. Entries not displayed in UI after receiving
4. Encryption/decryption issue

**Investigation Needed:**
- Check mobile logs when receiving data from desktop
- Verify group is created on mobile
- Verify entries are stored in database
- Check if UI refreshes after data received

---

### Issue 3: Offline Handling

**Current Implementation:**
- `sync_queue` table stores pending jobs
- `processOutboxForUser()` called when peer comes online
- Desktop: `handlePeerOnline()` triggers outbox processing
- Mobile: `vault.onPeerOnline()` triggers outbox processing

**Status:** Should be working, but needs runtime verification

---

### Issue 4: Member List Syncing

**Fixed (746fad2):**
- ✅ Mobile adds invited user with "pending" status
- ✅ Mobile updates to "accepted" on accept
- ✅ Calls `updateGroupMemberStatus()` which broadcasts MEMBER_ADD

**Still Needed:**
- Member list should auto-refresh when MEMBER_ADD/MEMBER_REMOVE received
- Desktop: Needs to reload members when sync message received
- Mobile: Needs to trigger UI refresh

---

### Issue 5: Desktop/Mobile Feature Parity

**Current State:**
- **Desktop:** Shows "Manage Group" for all users (incorrect)
- **Mobile:** Only shows manage options if owner (correct)

**Needed Changes:**

**Desktop mainwindow.cpp:**
```cpp
// In loadGroups() or when group selected:
std::string myId = m_vault->getUserId();
std::string ownerId = m_vault->getGroupOwner(groupId);
bool isOwner = (myId == ownerId);

// Show/hide buttons based on ownership:
m_manageGroupButton->setVisible(isOwner);
m_inviteButton->setVisible(isOwner);
m_groupSettingsButton->setVisible(isOwner);
// Always show members list
```

**Specific Controls to Hide/Show:**
- "Manage Group" button → Only owner
- "Group Settings" button → Only owner
- "Invite User" in ShareGroupDialog → Only owner (already done)
- "Members List" → All users can view

---

### Issue 6: Group Ownership Display

**Problem:** 
Desktop shows user as "owner" when they're actually a member

**Root Cause:**
- Group display logic doesn't check member role
- Might be showing group owner instead of current user's role

**Fix Needed:**
In desktop group list display:
```cpp
std::string myId = m_vault->getUserId();
auto members = m_vault->getGroupMembers(groupName);
std::string myRole = "member"; // default
for (const auto& m : members) {
    if (m.userId == myId) {
        myRole = m.role;
        break;
    }
}
// Display myRole, not group owner
```

---

### Issue 7: Member List UI

**Desktop (ShareGroupDialog.cpp) - ALREADY DONE ✅:**
- Line 183-195: Status-based coloring
  - Pending → Orange "(Invite Sent)"
  - Owner → Green with ★
  - Admin → Blue with 🛡️
  - Member → Default white
- Members list shows correct status

**Mobile (HomeActivity.kt) - NEEDS IMPLEMENTATION:**

Current state: Probably shows plain list

Needed changes in `showGroupDetails()`:
```kotlin
// Get members with roles and status
val members = vault.getGroupMembers(currentGroup)

// Parse format: "userId|role|status"
val membersList = members.map { memberStr ->
    val parts = memberStr.split("|")
    val userId = parts[0]
    val role = if (parts.size > 1) parts[1] else "member"
    val status = if (parts.size > 2) parts[2] else "accepted"
    
    // Format display
    val displayText = when {
        status == "pending" -> "$userId (Pending)" // Red
        role == "owner" -> "$userId ★ Owner" // Green
        role == "admin" -> "$userId 🛡️ Admin" // Blue
        else -> userId // Default
    }
    
    // Return with color info
    Triple(displayText, role, status)
}

// Create TextView for each with appropriate color
```

---

### Issue 8: Member List Auto-Refresh

**Desktop Implementation:**
Add to `handleIncomingSync()` or create new handler:

```cpp
// In mainwindow.cpp
void MainWindow::handleMemberSync(const QString& groupName) {
    // If ShareGroupDialog is open for this group, refresh it
    if (m_shareGroupDialog && m_shareGroupDialog->groupName() == groupName) {
        m_shareGroupDialog->loadMembers();
    }
    
    // Refresh main group list display
    loadGroups();
}
```

Call this when MEMBER_ADD or MEMBER_REMOVE sync received.

**Mobile Implementation:**
In `onGroupDataReceived` callback for "member-list" type:
```kotlin
// After processing member-list
runOnUiThread {
    if (currentGroup == targetGroup) {
        loadGroupDetails(currentGroup)
    }
}
```

---

## Implementation Priority

**Phase 1: Critical Fixes (Now)**
1. ✅ Fix locations transfer - DONE
2. ✅ Add members with pending status - DONE
3. ⚠️ Debug desktop→mobile password transfer
4. ⚠️ Fix group ownership display

**Phase 2: UI Parity (Next)**
5. ⚠️ Desktop role-based button visibility
6. ⚠️ Mobile member list UI with colors/status
7. ⚠️ Member list auto-refresh (both platforms)

**Phase 3: Verification (Final)**
8. ⚠️ Test offline sync queueing
9. ⚠️ Test all sync operations
10. ⚠️ End-to-end testing

---

## Files Requiring Changes

### Desktop:
- `src/desktop/mainwindow.cpp` - Role-based UI, group display, member sync handling
- `src/desktop/mainwindow.hpp` - New member sync handler
- `src/desktop/sharegroupdialog.cpp` - Already done! ✅

### Mobile:
- `src/mobile/app/src/main/java/com/ciphermesh/mobile/HomeActivity.kt` - Member list UI, auto-refresh
- `src/mobile/app/src/main/cpp/native-lib.cpp` - Already updated ✅

### Core:
- No changes needed - sync logic is correct

---

## Testing Checklist

- [ ] Mobile→Desktop with multiple entries and locations
- [ ] Desktop→Mobile with passwords
- [ ] Member list shows correct roles (owner/admin/member)
- [ ] Member list shows correct status (pending/accepted)
- [ ] Pending invites show in red/orange
- [ ] Owner has star symbol
- [ ] Non-owners can't access manage functions
- [ ] Member list auto-updates on sync
- [ ] Offline invites retry when online
- [ ] Multiple concurrent sync messages handled correctly
