# Unused Features Analysis

## Admin Role System - NOT USED (Recommended for Removal)

### Current State:
The admin role system is partially implemented but **not actively used** in either app:

### Database Schema:
- `groups` table has: `admins_only_write`, `admins_only_invite` columns
- `group_members` table stores role as: "owner", "admin", or "member"

### Code References:

#### Core (src/core/):
1. **vault_entry.hpp**: `GroupPermissions` struct with `adminsOnlyWrite`, `adminsOnlyInvite`
2. **database.hpp/cpp**: 
   - `setGroupPermissions(groupId, adminsOnly)`
   - `getGroupPermissions(groupId)`
   - `updateGroupMemberRole(groupId, userId, newRole)`
3. **vault.hpp/cpp**:
   - `setGroupPermissions()` - wrapper
   - `getGroupPermissions()` - used in `canUserEditEntry()` check
   - `updateGroupMemberRole()` - used only in GROUP_SPLIT handler

#### Desktop (src/desktop/):
1. **sharegroupdialog.cpp**:
   - Line 184-186: Blue color for admin role in member list
   - Line 201: Check if user is owner/admin for context menu
   - Line 231-244: "Promote to Admin" context menu option
   - Line 248-253: `demoteUser()` function
2. **mainwindow.cpp**:
   - Line 927: Blue color for admin in group tags

#### Mobile (src/mobile/):
1. **HomeActivity.kt**:
   - Line 533: Blue color with shield emoji for admin role

### Problems:
1. **No way to actually use admin permissions** - no UI to set `adminsOnlyWrite` or `adminsOnlyInvite`
2. **Promote/Demote functions exist but aren't exposed** in mobile
3. **Desktop has context menu** to promote/demote but it's hidden and unused
4. **Inconsistent**: Desktop has promote/demote, mobile doesn't
5. **Confusing**: Role exists but has no functional purpose

### Recommendation: **REMOVE ADMIN ROLE SYSTEM**

#### What to keep:
- "owner" role (essential for group management)
- "member" role (default for invited users)

#### What to remove:
- "admin" role entirely
- `adminsOnlyWrite`, `adminsOnlyInvite` database columns (or deprecate)
- `GroupPermissions` struct
- `setGroupPermissions()`, `getGroupPermissions()` functions
- `updateGroupMemberRole()` function (only used for GROUP_SPLIT now)
- Promote/Demote UI elements and functions
- Admin-specific coloring/symbols (blue with shield)

#### Migration needed:
- Convert existing "admin" members to "member" role
- Database migration on vault load

---

## Feature Parity Analysis

### Features in Desktop but NOT in Mobile:

#### 1. Breach Checker (Desktop Only)
**File**: `src/desktop/breach_checker.hpp`, `breach_checker.cpp`
**Purpose**: Checks passwords against haveibeenpwned database
**Usage**: Referenced in desktop UI
**Mobile**: Does NOT have this feature
**Recommendation**: Either add to mobile or document as desktop-exclusive

#### 2. Password Strength Indicator (Desktop Only)
**File**: `src/desktop/passwordstrength.hpp`, `passwordstrength.cpp`
**Purpose**: Visual password strength indicator
**Usage**: Used in password dialogs
**Mobile**: Does NOT have visual strength indicator
**Recommendation**: Add to mobile for consistency

#### 3. Toast Notifications (Desktop Only)
**File**: `src/desktop/toast.hpp`, `toast.cpp`
**Purpose**: Non-intrusive notification system
**Mobile**: Uses Android's native Toast
**Status**: Platform-specific, no action needed

#### 4. Location Editor Dialog (Desktop Only)
**File**: `src/desktop/locationeditdialog.hpp`, `locationeditdialog.cpp`
**Purpose**: Edit entry locations/URLs
**Mobile**: May have inline editing instead
**Recommendation**: Verify mobile has equivalent functionality

---

### Features in Mobile but NOT in Desktop:

#### 1. Recent Entries Screen
**File**: `src/mobile/app/src/main/java/com/ciphermesh/mobile/RecentEntriesActivity.kt`
**Purpose**: Shows recently accessed passwords
**Desktop**: No equivalent feature
**Recommendation**: Add to desktop or document as mobile-exclusive

#### 2. Biometric Authentication
**File**: `src/mobile/app/src/main/java/com/ciphermesh/mobile/BiometricHelper.kt`
**Purpose**: Fingerprint/face unlock
**Desktop**: Not available (platform limitation)
**Status**: Platform-specific, no action needed

---

## Summary of Actions Needed:

### High Priority:
1. **Remove admin role system** - unused and confusing
   - Remove from both apps
   - Database migration for existing "admin" → "member"
   - Remove UI elements (promote/demote, admin colors)
   - Remove functions: setGroupPermissions, getGroupPermissions, updateGroupMemberRole
   - Clean up database schema (deprecate admin columns)

### Medium Priority:
2. **Add password strength indicator to mobile** - feature parity
3. **Verify location editing works in mobile** - feature parity
4. **Add breach checker to mobile** OR **document as desktop-exclusive**

### Low Priority:
5. **Consider adding Recent Entries to desktop** - nice-to-have feature parity

### No Action:
- Toast notifications (platform-specific implementations are fine)
- Biometric auth (platform limitation)

---

## Files to Modify for Admin Removal:

### Core:
- [ ] `src/core/vault_entry.hpp` - Remove GroupPermissions struct
- [ ] `src/core/database.hpp` - Remove setGroupPermissions, getGroupPermissions, updateGroupMemberRole declarations
- [ ] `src/core/database.cpp` - Remove implementations, add migration
- [ ] `src/core/vault.hpp` - Remove wrapper declarations
- [ ] `src/core/vault.cpp` - Remove wrappers, update canUserEditEntry(), add admin→member migration

### Desktop:
- [ ] `src/desktop/sharegroupdialog.hpp` - Remove demoteUser declaration
- [ ] `src/desktop/sharegroupdialog.cpp` - Remove promote/demote functions, admin coloring
- [ ] `src/desktop/mainwindow.cpp` - Remove admin role coloring

### Mobile:
- [ ] `src/mobile/app/src/main/java/com/ciphermesh/mobile/HomeActivity.kt` - Remove admin role coloring

### Documentation:
- [ ] Update README with supported roles (owner, member only)
- [ ] Add migration notes for users upgrading from versions with admin role
