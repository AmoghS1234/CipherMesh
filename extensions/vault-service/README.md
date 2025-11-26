# CipherMesh Vault Service

## Overview

The **CipherMesh Vault Service** is a standalone service that provides secure access to the encrypted password vault. It operates independently from the desktop application and can be used by:

- Browser extensions (Firefox, Chrome, etc.)
- Desktop application
- Future mobile applications (Android, iOS)
- Command-line tools
- Any application that needs vault access

## Architecture

```
┌─────────────────┐
│ Browser Ext     │
└────────┬────────┘
         │
         │ Native Messaging
         │
┌────────▼────────────────────────────┐
│  Vault Service (Standalone Process) │
│  - Master password verification     │
│  - Vault access & decryption        │
│  - No GUI, runs in background       │
└────────┬────────────────────────────┘
         │
         │ Direct file access
         │
┌────────▼────────┐
│  vault.db       │
│  (SQLite +      │
│   libsodium)    │
└─────────────────┘
```

## Key Features

### 1. **Standalone Operation**
- Runs as independent process
- No dependency on desktop application
- Can be used by multiple clients simultaneously

### 2. **Secure Vault Access**
- Direct access to encrypted vault database
- Master password verification
- Decrypt passwords on-demand
- No password caching (passwords decrypted per-request)

### 3. **Session Management**
- Unlocks vault once with master password
- Keeps vault unlocked for session lifetime
- Locks automatically when service exits

### 4. **Multi-Client Support**
- Browser extensions
- Desktop app (can use service instead of embedding vault logic)
- Future mobile apps via network protocol
- CLI tools

## API

The service communicates via **Native Messaging Protocol** (stdin/stdout with JSON messages):

### Request Format
```json
{
  "action": "ACTION_NAME",
  "param1": "value1",
  "param2": "value2"
}
```

### Response Format
```json
{
  "status": "success|error|multiple",
  "data": {},
  "error": "error message if status is error"
}
```

### Supported Actions

#### 1. VERIFY_MASTER_PASSWORD
Unlock the vault with master password.

**Request:**
```json
{
  "action": "VERIFY_MASTER_PASSWORD",
  "masterPassword": "user's master password",
  "vaultPath": "/path/to/vault.db"  // optional, defaults to ~/.ciphermesh/vault.db
}
```

**Response (Success):**
```json
{
  "status": "success",
  "message": "Master password verified"
}
```

**Response (Error):**
```json
{
  "status": "error",
  "error": "Incorrect master password"
}
```

#### 2. GET_CREDENTIALS
Retrieve credentials for a URL.

**Request:**
```json
{
  "action": "GET_CREDENTIALS",
  "url": "https://github.com",
  "username": "optional_username"  // if omitted, returns all matches
}
```

**Response (Single Match):**
```json
{
  "status": "success",
  "username": "user@example.com",
  "password": "decrypted_password",
  "title": "GitHub Account"
}
```

**Response (Multiple Matches):**
```json
{
  "status": "multiple",
  "credentials": [
    {"id": 1, "username": "user1@example.com", "title": "Work Account"},
    {"id": 2, "username": "user2@example.com", "title": "Personal Account"}
  ]
}
```

#### 3. GET_CREDENTIAL_BY_ID
Get specific credential after user selects from multiple matches.

**Request:**
```json
{
  "action": "GET_CREDENTIAL_BY_ID",
  "entryId": 1
}
```

**Response:**
```json
{
  "status": "success",
  "username": "user@example.com",
  "password": "decrypted_password",
  "title": "Account Name"
}
```

#### 4. SAVE_CREDENTIALS
Save new credentials to vault.

**Request:**
```json
{
  "action": "SAVE_CREDENTIALS",
  "url": "https://example.com",
  "username": "user@example.com",
  "password": "user_password",
  "title": "Example Account",  // optional
  "group": "Work"  // optional, defaults to "Default"
}
```

**Response:**
```json
{
  "status": "success",
  "message": "Credentials saved successfully"
}
```

#### 5. LIST_GROUPS
Get list of all vault groups.

**Request:**
```json
{
  "action": "LIST_GROUPS"
}
```

**Response:**
```json
{
  "status": "success",
  "groups": ["Default", "Work", "Personal", "Banking"]
}
```

#### 6. PING
Health check.

**Request:**
```json
{
  "action": "PING"
}
```

**Response:**
```json
{
  "status": "success",
  "message": "pong"
}
```

## Building

The vault service is built automatically with the main project:

```bash
cd build
cmake ..
cmake --build . -j$(nproc)
```

This produces:
- `build/extensions/vault-service/ciphermesh-vault-service` - The service executable
- Native messaging manifests installed to browser directories

## Installation

### Development Setup
Manifests are automatically installed during build to:
- `~/.mozilla/native-messaging-hosts/com.ciphermesh.vault.json`
- `~/.config/google-chrome/NativeMessagingHosts/com.ciphermesh.vault.json`
- `~/.config/chromium/NativeMessagingHosts/com.ciphermesh.vault.json`

### System-Wide Installation
```bash
cd build
sudo make install
```

This installs:
- `/usr/local/bin/ciphermesh-vault-service`
- `/usr/local/share/ciphermesh/native-messaging/com.ciphermesh.vault.json`

## Usage

### From Browser Extension
The extension automatically connects to the vault service via native messaging.

1. User clicks autofill button
2. Extension prompts for master password
3. Extension sends `VERIFY_MASTER_PASSWORD` to vault service
4. Extension sends `GET_CREDENTIALS` to retrieve password
5. Extension fills password field

### Manual Testing
You can test the service manually:

```bash
cd build/extensions/vault-service
echo '{"action":"PING"}' | ./ciphermesh-vault-service
```

### From Desktop App (Future)
The desktop app can use the vault service instead of embedding vault logic:

```cpp
// Instead of loading vault directly
vault->loadVault(path, masterPassword);

// Use vault service via IPC/socket
VaultServiceClient client;
client.verifyMasterPassword(masterPassword);
auto credentials = client.getCredentials(url);
```

### From Mobile Apps (Future)
Mobile apps can connect via:
- Local socket (on-device service)
- REST API wrapper around vault service
- gRPC/protobuf for cross-platform

## Security Considerations

### ✅ What's Secure

1. **Master password never stored** - Only used for verification, then discarded
2. **Passwords decrypted on-demand** - Not kept in memory
3. **Session-based unlocking** - Vault unlocked once per service instance
4. **Local-only communication** - No network exposure
5. **Same encryption as desktop app** - Uses libsodium with same keys

### ⚠️ Security Notes

1. **Process lifetime** - Vault remains unlocked while service process runs
2. **No auto-lock** - Service doesn't implement timeout-based locking (client responsibility)
3. **Single user** - Designed for single-user systems
4. **No authentication** - Any local process can connect (relying on OS security)

### 🔒 Recommendations

1. **Desktop app should use the service too** - Avoid code duplication
2. **Add D-Bus support** - For better Linux integration
3. **Add auto-lock timer** - Lock vault after inactivity
4. **Add client authentication** - Verify connecting process identity
5. **Mobile apps need REST wrapper** - HTTP API for remote access (with TLS)

## Future Enhancements

### Phase 1: Current
- ✅ Browser extension support
- ✅ Direct vault access
- ✅ Master password verification
- ✅ Credential retrieval and storage

### Phase 2: Desktop Integration
- [ ] Desktop app uses vault service
- [ ] D-Bus interface for Linux
- [ ] System tray integration
- [ ] Auto-lock after timeout

### Phase 3: Mobile Support
- [ ] REST API wrapper
- [ ] TLS encryption for network
- [ ] OAuth/token authentication
- [ ] Sync capabilities

### Phase 4: Advanced Features
- [ ] Biometric authentication
- [ ] Hardware key support (YubiKey)
- [ ] Multiple vault support
- [ ] Vault sharing between devices

## Troubleshooting

### Service Won't Start
```bash
# Check if service is built
ls -l build/extensions/vault-service/ciphermesh-vault-service

# Check manifest is installed
ls -l ~/.mozilla/native-messaging-hosts/com.ciphermesh.vault.json

# Run service manually to see errors
cd build/extensions/vault-service
./ciphermesh-vault-service
# Then paste JSON request like: {"action":"PING"}
```

### Connection Fails
```bash
# Check manifest path is correct
cat ~/.mozilla/native-messaging-hosts/com.ciphermesh.vault.json

# Verify extension ID matches
grep "allowed_extensions" ~/.mozilla/native-messaging-hosts/com.ciphermesh.vault.json
```

### Master Password Fails
```bash
# Check vault file exists
ls -l ~/.ciphermesh/vault.db

# Try unlocking with desktop app first
# If that works, service should work too
```

## Architecture Benefits

### Code Reuse
```
┌──────────────────┐
│   Browser Ext    │
└────────┬─────────┘
         │
         ├─────────────────┐
         │                 │
┌────────▼────────┐ ┌──────▼──────┐
│  Vault Service  │ │ Desktop App │
│  (Shared Core)  │ │ (Uses same  │
│                 │ │  service)   │
└─────────────────┘ └─────────────┘
```

All applications use the same vault service, ensuring:
- Consistent behavior
- Single source of truth
- Easier maintenance
- Better security (one code path to audit)

## Conclusion

The vault service provides a clean, secure, and reusable way to access the CipherMesh vault from any application. It eliminates the need for the desktop app to be running and creates a foundation for future multi-platform support.
