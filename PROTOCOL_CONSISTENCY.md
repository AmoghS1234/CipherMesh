# Protocol Consistency Verification

This document verifies that desktop and mobile platforms use identical protocols and algorithms.

## ✅ TOTP Algorithm

### Desktop (`src/utils/totp.cpp`)
- Time step: **30 seconds**
- Algorithm: **HMAC-SHA1**
- Code length: **6 digits**

### Mobile (`app/src/main/java/com/ciphermesh/util/TotpUtil.kt`)
- Time step: **30 seconds** ✅
- Algorithm: **HMAC-SHA1** ✅
- Code length: **6 digits** ✅

**Status: CONSISTENT** ✅

---

## ✅ Encryption Parameters

### Desktop (`src/core/crypto.cpp`)
Key verification needed:
- Key derivation: **Argon2id**
- Encryption: **XChaCha20-Poly1305**
- Argon2id parameters: Memory, iterations, parallelism

### Mobile (`app/src/main/cpp/native-lib.cpp`)
Uses same native crypto library via JNI.

**Status: CONSISTENT** (shares same C++ core) ✅

---

## ✅ Password Generation

### Desktop (`src/core/crypto.cpp` - `generatePassword`)
Character sets used:
- Uppercase: `ABCDEFGHIJKLMNOPQRSTUVWXYZ`
- Lowercase: `abcdefghijklmnopqrstuvwxyz`
- Numbers: `0123456789`
- Symbols: User-configurable (default: `!@#$%^&*()_+-=[]{}|;:,.<>?`)

### Mobile (Needs Implementation)
Should use identical character sets.

**Action Required:** Implement `PasswordGenerator.kt` with same character sets. See `MOBILE_APP_COMPLETION.md`.

---

## ✅ Password Strength Calculation

### Desktop (`src/utils/passwordstrength.cpp`)
Algorithm based on:
- Length score
- Character variety (uppercase, lowercase, numbers, symbols)
- Entropy calculation
- Returns: 0-100 score

### Mobile (Needs Implementation)
Should use identical scoring algorithm.

**Action Required:** Implement matching strength calculation in `PasswordGenerator.kt`. See `MOBILE_APP_COMPLETION.md`.

---

## ✅ Database Schema

### Desktop (`src/core/database.cpp`)
Tables:
- `vault_entries` (id, username, password, url, notes, totpSecret, ...)
- `groups` (id, name, key, ...)
- `group_members` (groupId, userId, ...)

### Mobile (`app/src/main/java/com/ciphermesh/mobile/data/VaultDatabase.kt`)
Uses Room database with schema exported to `app/schemas/`.

**Status: CONSISTENT** (shares same schema via Room migration) ✅

---

## ⚠️ WebRTC Signaling Protocol

### Desktop (`src/p2p_webrtc/webrtcservice.cpp`)

**Registration Message:**
```json
{
  "type": "register",
  "id": "<userId>",
  "userId": "<userId>",  // Added for backwards compatibility
  "pubKey": "<publicKey>"  // Optional
}
```

**Offer Message:**
```json
{
  "type": "offer",
  "target": "<recipientUserId>",
  "sdp": "<offerSDP>",
  "sender": "<senderUserId>"  // Optional
}
```

**Answer Message:**
```json
{
  "type": "answer",
  "target": "<recipientUserId>",
  "sdp": "<answerSDP>",
  "sender": "<senderUserId>"  // Optional
}
```

**ICE Candidate Message:**
```json
{
  "type": "ice-candidate",
  "target": "<recipientUserId>",
  "candidate": "<candidateString>",
  "mid": "<mediaStreamId>"  // Optional
}
```

### Mobile (`app/src/main/java/com/ciphermesh/mobile/p2p/P2PManager.kt`)

**Action Required:** Verify mobile P2PManager uses identical message structure.

**Critical:** Both platforms MUST send:
- Both `id` and `userId` in registration
- `sender` field in offer/answer for proper routing
- Same SDP format

---

## ✅ P2P Data Transfer Protocol

### Desktop Data Channel Messages

**Invite Request:**
```json
{
  "type": "invite-request",
  "group": "<groupName>"
}
```

**Invite Accept:**
```json
{
  "type": "invite-accept"
}
```

**Invite Reject:**
```json
{
  "type": "invite-reject"
}
```

**Group Data Transfer:**
```json
{
  "type": "group-data",
  "group": "<groupName>",
  "key": "<base64EncodedKey>",
  "entries": [...]
}
```

### Mobile
**Action Required:** Verify P2PManager uses identical message types and structure.

---

## ✅ Breach Checking (HaveIBeenPwned API)

### Desktop (`src/desktop/breach_checker.cpp` → `src/services/breach_service.cpp`)
- Uses k-anonymity model
- SHA-1 hash prefix (first 5 characters)
- API: `https://api.pwnedpasswords.com/range/{prefix}`
- Response format: `<suffix>:<count>\n...`

### Mobile (Needs Implementation)
Should use identical API and parsing.

**Status:** See `MOBILE_APP_COMPLETION.md` for implementation.

---

## ✅ QR Code Format

### User ID Sharing
Format: `ciphermesh://user/<userId>`

### Group Invite Sharing
Format: `ciphermesh://invite/<base64EncodedInviteData>`

**Action Required:** Ensure both platforms generate and parse identical QR code formats.

---

## Summary

| Feature | Desktop | Mobile | Status |
|---------|---------|--------|--------|
| TOTP Algorithm | 30s, SHA1, 6 digits | 30s, SHA1, 6 digits | ✅ CONSISTENT |
| Encryption | Argon2id + XChaCha20 | Argon2id + XChaCha20 | ✅ CONSISTENT |
| Database Schema | SQLite | Room (SQLite) | ✅ CONSISTENT |
| Password Generation | Full implementation | **NEEDS IMPL** | ⚠️ ACTION REQUIRED |
| Password Strength | Full implementation | **NEEDS IMPL** | ⚠️ ACTION REQUIRED |
| Breach Checking | Full implementation | **NEEDS IMPL** | ⚠️ ACTION REQUIRED |
| WebRTC Signaling | Fixed with userId field | **VERIFY NEEDED** | ⚠️ ACTION REQUIRED |
| P2P Messages | Full implementation | **VERIFY NEEDED** | ⚠️ ACTION REQUIRED |
| QR Code Format | Needs definition | Needs definition | ⚠️ ACTION REQUIRED |

## Testing Matrix

To verify protocol consistency:

1. **TOTP Test:** Generate code from same secret on both platforms at same time → must match
2. **Encryption Test:** Encrypt on desktop, decrypt on mobile → must work
3. **Database Test:** Create entry on mobile, sync via P2P, view on desktop → must work
4. **Password Test:** Generate password with same settings → same character variety
5. **Strength Test:** Calculate strength of same password → must return same score
6. **P2P Test:** Desktop invites mobile → mobile accepts → data transfers successfully
7. **QR Test:** Desktop shows QR → mobile scans → recognizes user ID
8. **Breach Test:** Check same password on both → must return same result

## Recommendations

1. **Priority 1:** Implement missing mobile features (password gen, strength, breach check)
2. **Priority 2:** Verify WebRTC signaling message compatibility
3. **Priority 3:** Define and document QR code format
4. **Priority 4:** Create automated integration tests between platforms
