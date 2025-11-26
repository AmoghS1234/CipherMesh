# CipherMesh Browser Extensions

Browser extensions for Firefox and Chrome that enable auto-fill and password capture from web pages.

## Features

- **Auto-Fill**: Click the 🔐 button next to password fields to auto-fill credentials from your CipherMesh vault
- **Password Capture**: Automatically detect new logins and prompt to save to your vault
- **Multi-Account Support**: If multiple accounts exist for a site, choose which one to use
- **Secure**: Requires master password verification before any operation
- **Group Selection**: Choose which group to save passwords to

## Installation & Setup

### Quick Start (Automated)

The build system now handles everything automatically:

```bash
# Install dependencies
sudo apt-get install -y libsodium-dev qt6-base-dev qt6-svg-dev qt6-websockets-dev

# Build everything (manifests are auto-installed!)
mkdir build && cd build
cmake ..
cmake --build . -j$(nproc)
```

**That's it!** The build process automatically:
- ✅ Compiles all components
- ✅ Installs native messaging manifests to browser directories
- ✅ Configures manifests to point to build directory executables
- ✅ Ready to use immediately - no manual steps needed

### What Gets Installed Automatically

During build, manifests are copied to:
- `~/.mozilla/native-messaging-hosts/com.ciphermesh.native.json` (Firefox)
- `~/.config/google-chrome/NativeMessagingHosts/com.ciphermesh.native.json` (Chrome)
- `~/.config/chromium/NativeMessagingHosts/com.ciphermesh.native.json` (Chromium)

The development manifests point to: `build/extensions/native-host/ciphermesh-native-host`

### Loading the Extensions

**Firefox:**
1. Open `about:debugging`
2. Click "This Firefox" → "Load Temporary Add-on"
3. Select `extensions/firefox/manifest.json`

**Chrome:**
1. Open `chrome://extensions/`
2. Enable "Developer mode"
3. Click "Load unpacked"
4. Select the `extensions/chrome/` directory
5. Copy the extension ID and update `allowed_origins` in the manifest if needed

### Using the Extensions

1. Open CipherMesh desktop app and unlock vault
2. Navigate to login pages in your browser
3. Click 🔐 button to auto-fill or save passwords

### Optional: System-Wide Installation

For production use (not required for development):
```bash
cd build
sudo make install
```

This installs to `/usr/local/bin/` and creates system-level manifests.

## Architecture

### Components

1. **Content Script** (`content.js`): Runs on web pages, detects password fields, injects UI
2. **Background Script** (`background.js`): Handles native messaging with desktop app
3. **Popup** (`popup.html/js`): Extension popup showing connection status

### Message Flow

```
Web Page → Content Script → Background Script → Native Host → Desktop App
```

### API Messages

The extension communicates with the desktop app using these message types:

- `PING`: Check connection status
- `GET_CREDENTIALS`: Retrieve credentials for a URL
- `SAVE_CREDENTIALS`: Save new credentials
- `VERIFY_MASTER_PASSWORD`: Verify user's master password
- `LIST_GROUPS`: Get available vault groups

## Development

### Building

No build step is required for the extensions themselves - they're plain JavaScript.

For the native host, see `native-host/README.md`.

### Testing

1. Load the extension in developer mode
2. Open browser developer console
3. Check for CipherMesh logs in background script console
4. Test on various login pages

### Debugging

- Firefox: `about:debugging` → inspect extension
- Chrome: `chrome://extensions/` → click "Details" → "Inspect views: background page"

## Security Notes

- Master password is never stored by the extension
- Passwords are only retrieved after master password verification
- Communication with native app is over secure local messaging
- Extension requires explicit permissions for each operation

## Limitations

- Native messaging host must be manually installed
- Desktop app must be running for extension to work
- Some websites with complex login forms may not be detected
- Dynamic/AJAX forms may require page refresh

## Troubleshooting

**Extension shows "Not connected to app":**
- Ensure CipherMesh desktop app is running
- Check that native messaging host is properly installed
- Verify manifest paths are correct
- Check browser console for errors

**Auto-fill button doesn't appear:**
- Refresh the page
- Check if password field is in a form element
- Some sites may have custom password fields that aren't detected

**Can't save passwords:**
- Verify master password is correct
- Ensure desktop app is unlocked
- Check that a group is selected

## License

Same as CipherMesh desktop application
