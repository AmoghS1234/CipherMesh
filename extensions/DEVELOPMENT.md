# CipherMesh Browser Extension - Development Guide

## Architecture Overview

**IMPORTANT: The extension requires the CipherMesh desktop application to be running with an unlocked vault.**

The extension does NOT store passwords itself - it acts as a bridge to the desktop app's encrypted vault:
- Browser Extension → Native Messaging Host → Unix Socket → Desktop App → Encrypted Vault

This means:
- Desktop app MUST be running
- Vault MUST be unlocked for operations to work
- Extension cannot function offline or without the desktop app

## Common Issues & Solutions

### "Icon doesn't appear when I load a page"

**FIXED in latest version**: Icons now appear immediately regardless of desktop app connection status.

If icons still don't appear:
1. Reload the extension: `about:debugging` → Reload button
2. Reload the web page: `Ctrl+R`
3. Check console (`F12` → Console) for `[CipherMesh]` messages
4. Look for errors in the logs

### "Returns take 10 seconds" / "Operations are very slow"

**FIXED in latest version**: Timeout reduced from 30s to 5s.

If operations are still slow:
1. **Check desktop app is running and vault is UNLOCKED** (most common cause!)
2. Run native host manually to see what's happening:
   ```bash
   cd build/extensions/native-host
   ./ciphermesh-native-host
   ```
3. Try the operation again - you'll see detailed logs
4. Look for "IPC Client connected successfully" or connection errors
5. If you see timeouts, the desktop app isn't responding

**Common causes:**
- Vault is locked (unlock it!)
- Desktop app not running (start it!)
- IPC socket issues (restart desktop app)

### "Master password is incorrect even when it's right"

This happens when:
- Vault is locked in desktop app (unlock it first!)
- Desktop app isn't running
- IPC connection failed

**The extension verifies the master password against the ALREADY-UNLOCKED vault in the desktop app.**
You cannot unlock the vault from the extension - unlock it in the desktop app first!

### "Changes don't reflect after editing code"

YES - you MUST reload the extension after making code changes!

## Reloading Extensions During Development

### Firefox

**Method 1: Using about:debugging (Recommended)**
1. Open Firefox and navigate to `about:debugging`
2. Click "This Firefox" in the left sidebar
3. Find "CipherMesh Password Manager" in the list
4. Click the **"Reload"** button next to the extension

**Method 2: Remove and Re-add**
1. Go to `about:debugging` → "This Firefox"
2. Click "Remove" next to the extension
3. Click "Load Temporary Add-on..."
4. Navigate to `extensions/firefox/manifest.json` and select it

**Method 3: Restart Firefox**
- Close Firefox completely and reopen it
- The extension will reload with your changes

### Chrome/Chromium

**Method 1: Using chrome://extensions (Recommended)**
1. Open Chrome and navigate to `chrome://extensions/`
2. Find "CipherMesh Password Manager" in the list
3. Click the **circular refresh icon** next to the extension

**Method 2: Remove and Re-add**
1. Go to `chrome://extensions/`
2. Click "Remove" on the extension
3. Click "Load unpacked"
4. Navigate to and select the `extensions/chrome/` folder

### After Reloading

**Important:** After reloading the extension in Firefox or Chrome, you MUST also:

1. **Reload the web page** where you want to test the extension
   - Press `Ctrl+R` (or `Cmd+R` on Mac) to refresh the page
   - Or click the reload button in your browser

2. **Why?** Content scripts are only injected when pages load. Reloading the extension doesn't re-inject scripts into already-opened pages.

### Quick Development Workflow

```bash
# 1. Make changes to extension code
vim extensions/firefox/content.js

# 2. Reload extension in browser
#    Firefox: about:debugging → Reload button
#    Chrome: chrome://extensions/ → Refresh icon

# 3. Reload the web page you're testing
#    Press Ctrl+R or F5

# 4. Test your changes
```

### Debugging Tips

**View Console Logs:**
- **Background Script Logs**: 
  - Firefox: `about:debugging` → Inspect button → Console tab
  - Chrome: `chrome://extensions/` → Inspect background page → Console tab

- **Content Script Logs**:
  - Open Developer Tools on the web page (`F12`)
  - Go to Console tab
  - Look for logs from content.js prefixed with `[CipherMesh]`

- **Native Host Logs**:
  - The native host writes to stderr
  - To see logs, run it manually from terminal:
    ```bash
    cd build/extensions/native-host
    ./ciphermesh-native-host
    ```
  - Then use the extension - you'll see all IPC communication in the terminal
  - Look for messages like "IPC Client sending:", "IPC Client received:", "Failed to connect"

**Common Issues:**

1. **Icon doesn't appear after making changes**
   - Did you reload the extension? (`about:debugging` → Reload)
   - Did you reload the web page? (Press `Ctrl+R`)
   - Check Console for errors (`F12` → Console)
   - Check if CipherMesh desktop app is running

2. **Changes not reflecting**
   - Browser caches extension files
   - Use "Remove" then "Re-add" instead of just "Reload"
   - Or restart the browser completely

3. **Native messaging not working / "quite a while" for responses**
   - Make sure CipherMesh desktop app is running
   - Check that vault is **unlocked** in desktop app (this is critical!)
   - Check native host was built: `build/extensions/native-host/ciphermesh-native-host`
   - Verify manifest was installed: `~/.mozilla/native-messaging-hosts/com.ciphermesh.native.json`
   - Check native host logs by running it manually in terminal
   - Look for "IPC Client connected successfully" in native host output
   - If you see "Timeout waiting for response", the desktop app isn't responding
   - **Most common issue**: Vault is locked - unlock it in the desktop app!

4. **Master password incorrect even when it's correct**
   - Make sure vault is unlocked in desktop app FIRST
   - The extension verifies against the already-unlocked vault
   - Check native host logs - it will show if connection fails
   - Look for "Failed to verify password" or timeout errors

### Testing Checklist

When testing changes:

- [ ] Build project: `cd build && cmake --build . -j$(nproc)`
- [ ] Start CipherMesh desktop app
- [ ] Unlock vault in desktop app
- [ ] Reload extension in browser
- [ ] Reload web page where testing
- [ ] Check browser console for errors
- [ ] Check background script console for errors
- [ ] Test autofill functionality
- [ ] Test password saving functionality

### File Structure

```
extensions/
├── firefox/
│   ├── manifest.json      # Firefox extension manifest
│   ├── background.js      # Background script (native messaging)
│   ├── content.js         # Content script (password field detection, UI)
│   ├── popup.html        # Extension popup UI
│   └── popup.js          # Popup logic
├── chrome/
│   ├── manifest.json      # Chrome extension manifest (Manifest v3)
│   ├── background.js      # Service worker (native messaging)
│   ├── content.js         # Content script (same as Firefox)
│   ├── popup.html        # Extension popup UI
│   └── popup.js          # Popup logic
└── native-host/
    ├── main.cpp           # Native messaging host
    ├── message_handler.cpp
    └── ipc_client.cpp     # Communicates with desktop app
```

### Key Files to Edit

- **UI/Button appearance**: `content.js` (search for "createCipherMeshButton" or "createModal")
- **Auto-fill logic**: `content.js` (search for "handleAutoFill")
- **Password saving**: `content.js` (search for "handlePasswordCapture")
- **Native messaging**: `background.js` (search for "nativePort" or "sendNativeMessage")
- **Popup**: `popup.html` and `popup.js`

### Performance Tips

- Content scripts run on every page - keep them lightweight
- Use `document_end` instead of `document_idle` for faster injection
- Debounce MutationObserver callbacks to avoid performance issues
- Only process visible password fields

### Security Reminders

- Never log passwords or master passwords to console
- All sensitive operations require master password verification
- Native messaging uses Unix socket - local only
- Passwords never stored in extension storage
- All data encrypted in desktop app vault
