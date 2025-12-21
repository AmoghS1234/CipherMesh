# Complete Qt Creator Guide for Building CipherMesh Android App

This is a detailed, step-by-step guide for building the CipherMesh Android mobile app using Qt Creator.

---

## Table of Contents
1. [Prerequisites Installation](#prerequisites-installation)
2. [Qt Creator Setup](#qt-creator-setup)
3. [Opening the Project](#opening-the-project)
4. [Configuring the Build](#configuring-the-build)
5. [Building the APK](#building-the-apk)
6. [Running on Device/Emulator](#running-on-deviceemulator)
7. [Troubleshooting](#troubleshooting)

---

## Prerequisites Installation

### Step 1: Install Qt 6.5.3 with Qt Creator

1. **Download Qt Online Installer**
   - Go to: https://www.qt.io/download-qt-installer
   - Download the installer for your operating system
   - **Windows**: `qt-online-installer-windows-x64-x.x.x.exe`
   - **Linux**: `qt-online-installer-linux-x64-x.x.x.run`
   - **macOS**: `qt-online-installer-macOS-x64-x.x.x.dmg`

2. **Run the Installer**
   - **Linux**: Make executable and run
     ```bash
     chmod +x qt-online-installer-linux-x64-*.run
     ./qt-online-installer-linux-x64-*.run
     ```
   - **Windows/macOS**: Double-click the installer

3. **Login/Create Qt Account**
   - You'll need a Qt account (free for open source development)
   - Click "Sign up" if you don't have an account
   - Or login with existing credentials

4. **Select Installation Directory**
   - **Default Locations:**
     - Linux: `/home/yourusername/Qt`
     - Windows: `C:\Qt`
     - macOS: `/Users/yourusername/Qt`
   - You can change this, but remember the path for later

5. **Select Components** (This is crucial!)
   
   In the component selection screen, expand **Qt 6.5.3** and select:
   
   ```
   ☑ Qt 6.5.3
     ├─ ☑ Desktop gcc 64-bit              (Linux)
     │   OR
     ├─ ☑ MinGW 11.2.0 64-bit            (Windows) 
     │   OR
     ├─ ☑ macOS                           (macOS)
     │
     ├─ ☑ Android ARMv7                  ← IMPORTANT
     ├─ ☑ Android ARM64-v8a              ← RECOMMENDED
     │
     └─ ☑ Qt Quick 3D                    (Usually checked by default)
   
   ☑ Developer and Designer Tools
     ├─ ☑ Qt Creator 13.x.x              ← IMPORTANT
     ├─ ☑ CMake 3.27.x
     └─ ☑ Ninja 1.11.x
   ```

6. **Complete Installation**
   - Accept the license agreements
   - Click "Install"
   - Wait for installation (this may take 20-30 minutes depending on your connection)

### Step 2: Install Android Studio and SDK

1. **Download Android Studio**
   - Go to: https://developer.android.com/studio
   - Download the latest stable version
   - Install using your system's package manager or installer

2. **First Launch Setup**
   - Open Android Studio
   - Go through the setup wizard
   - Select "Standard" installation type
   - This will install:
     - Android SDK
     - Android SDK Platform
     - Android Virtual Device

3. **Install Required SDK Components**
   
   a. Open **SDK Manager**:
      - Menu: `Tools > SDK Manager`
      - Or click SDK Manager icon in toolbar
   
   b. **SDK Platforms Tab**:
      ```
      ☑ Android 13.0 ("Tiramisu") - API Level 33
      ☑ Android 12.0 ("S") - API Level 31       (optional, for older devices)
      ```
   
   c. **SDK Tools Tab** (check "Show Package Details"):
      ```
      ☑ Android SDK Build-Tools 33.0.0 (or latest)
      ☑ Android SDK Command-line Tools (latest)
      ☑ Android Emulator
      ☑ Android SDK Platform-Tools
      ☑ CMake (check version 3.18.1 or 3.22.1)
      ☑ NDK (Side by side)
        └─ ☑ 27.2.12479018                    ← SPECIFIC VERSION REQUIRED
      ```
   
   d. Click **Apply** and wait for downloads to complete

4. **Note SDK Location**
   - In SDK Manager, look at the top for "Android SDK Location"
   - **Default locations:**
     - Linux: `/home/yourusername/Android/Sdk`
     - Windows: `C:\Users\YourName\AppData\Local\Android\Sdk`
     - macOS: `/Users/yourusername/Library/Android/sdk`
   - **Write this down** - you'll need it later!

### Step 3: Install Java Development Kit (JDK) 17

Qt Creator's Android builds require JDK 17.

**Option A: Install via Android Studio (Easiest)**
- Android Studio comes with JDK 17
- Location (write this down):
  - Linux: `/snap/android-studio/current/android-studio/jbr`
  - Windows: `C:\Program Files\Android\Android Studio\jbr`
  - macOS: `/Applications/Android Studio.app/Contents/jbr`

**Option B: Install Separately**
1. Download from: https://adoptium.net/
2. Select: **Temurin 17 (LTS)**
3. Download and install for your platform
4. Note the installation directory

### Step 4: Build libsodium for Android

CipherMesh requires libsodium compiled for Android.

**Option A: Download Pre-built (Easier)**

For armeabi-v7a and arm64-v8a, you can download pre-built binaries from libsodium releases.

1. Visit: https://github.com/jedisct1/libsodium/releases
2. Download `libsodium-x.x.x-stable.tar.gz`
3. Extract it
4. Inside, find the Android builds or use Option B

**Option B: Build from Source (Recommended)**

1. **Install Build Tools** (if not already installed):
   
   **Linux:**
   ```bash
   sudo apt-get update
   sudo apt-get install -y autoconf automake libtool build-essential
   ```
   
   **macOS:**
   ```bash
   brew install autoconf automake libtool
   ```
   
   **Windows:**
   - Use WSL (Windows Subsystem for Linux) or Git Bash
   - Install build tools in WSL

2. **Clone and Build libsodium**:
   
   ```bash
   # Clone the repository
   git clone --depth 1 --branch stable https://github.com/jedisct1/libsodium.git
   cd libsodium
   
   # Set NDK path (adjust to your actual path)
   export ANDROID_NDK_ROOT=/home/yourusername/Android/Sdk/ndk/27.2.12479018
   # For Windows: export ANDROID_NDK_ROOT=/c/Users/YourName/AppData/Local/Android/Sdk/ndk/27.2.12479018
   # For macOS: export ANDROID_NDK_ROOT=/Users/yourusername/Library/Android/sdk/ndk/27.2.12479018
   
   # Detect your OS for toolchain
   if [[ "$OSTYPE" == "darwin"* ]]; then
     export TOOLCHAIN=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64
   else
     export TOOLCHAIN=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64
   fi
   
   export API=21
   export TARGET=armv7a-linux-androideabi
   
   export CC=$TOOLCHAIN/bin/${TARGET}${API}-clang
   export CXX=$TOOLCHAIN/bin/${TARGET}${API}-clang++
   export AR=$TOOLCHAIN/bin/llvm-ar
   export RANLIB=$TOOLCHAIN/bin/llvm-ranlib
   export STRIP=$TOOLCHAIN/bin/llvm-strip
   
   # Generate configure script
   ./autogen.sh
   
   # Configure for Android armeabi-v7a
   ./configure \
     --host=armv7a-linux-androideabi \
     --prefix=$HOME/libsodium-android \
     --disable-shared \
     --enable-static \
     --with-pic
   
   # Build (use nproc on Linux, sysctl on macOS)
   make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)
   
   # Install to ~/libsodium-android
   make install
   
   echo "libsodium installed to: $HOME/libsodium-android"
   ```

3. **Verify Installation**:
   ```bash
   ls -la ~/libsodium-android/lib/libsodium.a
   ls -la ~/libsodium-android/include/sodium.h
   ```
   
   You should see both files exist.

4. **Write down the path**: `~/libsodium-android` or `/home/yourusername/libsodium-android`

---

## Qt Creator Setup

### Step 1: Launch Qt Creator

1. **Open Qt Creator**
   - **Linux**: Look for "Qt Creator" in applications menu, or run `qtcreator` from terminal
   - **Windows**: Start Menu > Qt > Qt Creator
   - **macOS**: Applications > Qt Creator

2. **First Time Setup**
   - Qt Creator may ask to configure kits on first launch
   - Click "Continue" through any welcome screens

### Step 2: Configure Android Kit

1. **Open Devices & Simulator Settings**
   - Menu: `Edit > Preferences` (Linux/Windows)
   - Or: `Qt Creator > Preferences` (macOS)

2. **Navigate to Devices > Android**
   
   You'll see several fields to configure:

   a. **JDK Location**:
      - Click "Browse..."
      - Navigate to your JDK 17 installation
      - Examples:
        - Linux: `/usr/lib/jvm/java-17-openjdk-amd64` or `/snap/android-studio/current/android-studio/jbr`
        - Windows: `C:\Program Files\Android\Android Studio\jbr`
        - macOS: `/Applications/Android Studio.app/Contents/jbr`
   
   b. **Android SDK Location**:
      - Click "Browse..."
      - Navigate to your Android SDK (noted in Step 2 of Prerequisites)
      - Examples:
        - Linux: `/home/yourusername/Android/Sdk`
        - Windows: `C:\Users\YourName\AppData\Local\Android\Sdk`
        - macOS: `/Users/yourusername/Library/Android/sdk`
   
   c. **Android NDK Location**:
      - Should auto-detect from SDK
      - If not, manually set to: `<SDK_PATH>/ndk/27.2.12479018`
   
   d. Click **Apply**

3. **Verify Android Kit is Available**
   
   a. Go to: `Preferences > Kits`
   
   b. You should see kits like:
      - `Android Qt 6.5.3 Clang armeabi-v7a`
      - `Android Qt 6.5.3 Clang arm64-v8a`
   
   c. Select one (e.g., armeabi-v7a kit) and verify:
      - **Device type**: Android Device
      - **Compiler**: Android Clang (arm, armeabi-v7a)
      - **Qt version**: Qt 6.5.3 for Android ARMv7
      - **CMake Tool**: Should show a CMake version
   
   d. If any are red/missing:
      - Click on the field
      - Select the correct option from dropdown
   
   e. Click **OK**

---

## Opening the Project

### Step 1: Clone CipherMesh Repository

If you haven't already:

```bash
# Navigate to where you want the project
cd ~/Projects  # or C:\Users\YourName\Projects on Windows

# Clone the repository
git clone https://github.com/AmoghS1234/CipherMesh.git
cd CipherMesh
```

### Step 2: Open Project in Qt Creator

1. **Open CMakeLists.txt**
   - In Qt Creator: `File > Open File or Project...`
   - Navigate to your CipherMesh directory
   - Select `CMakeLists.txt`
   - Click **Open**

2. **Configure Project Dialog**
   
   You'll see a "Configure Project" screen with available kits.
   
   a. **Select Android Kit(s)**:
      ```
      ☑ Android Qt 6.5.3 Clang armeabi-v7a
      ☑ Android Qt 6.5.3 Clang arm64-v8a     (optional but recommended)
      ☐ Desktop Qt 6.5.3 GCC 64bit           (uncheck this for now)
      ```
   
   b. Click **Configure Project**

3. **Wait for CMake Initial Configuration**
   - Qt Creator will run CMake in the background
   - You'll see progress in the bottom status bar
   - **This will fail initially** because we need to set libsodium paths

---

## Configuring the Build

### Step 1: Set CMake Variables

1. **Access Build Settings**
   - In Qt Creator left sidebar, click **Projects** button (wrench icon)
   - Make sure you're on the **Build** tab
   - Select your Android kit (e.g., `Android Qt 6.5.3 Clang armeabi-v7a`)

2. **Scroll to "CMake" Section**
   - Under "Build Settings", find the "CMake" section
   - You'll see "Initial Configuration" and "Current Configuration"

3. **Add CMake Arguments**
   
   Find the "CMake" configuration area. There are two ways to set variables:
   
   **Method A: Using Initial CMake Parameters** (Recommended)
   
   Look for "Initial CMake parameters" text field and add:
   
   ```
   -DANDROID_SDK_ROOT=/home/yourusername/Android/Sdk
   -DANDROID_NDK_ROOT=/home/yourusername/Android/Sdk/ndk/27.2.12479018
   -DANDROID_ABI=armeabi-v7a
   -Dsodium_INCLUDE_DIR=/home/yourusername/libsodium-android/include
   -Dsodium_LIBRARY_RELEASE=/home/yourusername/libsodium-android/lib/libsodium.a
   -DQT_HOST_PATH=/home/yourusername/Qt/6.5.3/gcc_64
   ```
   
   **IMPORTANT: Adjust all paths to match YOUR system!**
   
   **Windows Example:**
   ```
   -DANDROID_SDK_ROOT=C:/Users/YourName/AppData/Local/Android/Sdk
   -DANDROID_NDK_ROOT=C:/Users/YourName/AppData/Local/Android/Sdk/ndk/27.2.12479018
   -DANDROID_ABI=armeabi-v7a
   -Dsodium_INCLUDE_DIR=C:/Users/YourName/libsodium-android/include
   -Dsodium_LIBRARY_RELEASE=C:/Users/YourName/libsodium-android/lib/libsodium.a
   -DQT_HOST_PATH=C:/Qt/6.5.3/mingw_64
   ```
   
   **macOS Example:**
   ```
   -DANDROID_SDK_ROOT=/Users/yourusername/Library/Android/sdk
   -DANDROID_NDK_ROOT=/Users/yourusername/Library/Android/sdk/ndk/27.2.12479018
   -DANDROID_ABI=armeabi-v7a
   -Dsodium_INCLUDE_DIR=/Users/yourusername/libsodium-android/include
   -Dsodium_LIBRARY_RELEASE=/Users/yourusername/libsodium-android/lib/libsodium.a
   -DQT_HOST_PATH=/Users/yourusername/Qt/6.5.3/macos
   ```
   
   **Method B: Using Build Environment Variables**
   
   Alternatively, in the "Build Environment" section, add these variables:
   - `ANDROID_SDK_ROOT` = `/home/yourusername/Android/Sdk`
   - `ANDROID_NDK_ROOT` = `/home/yourusername/Android/Sdk/ndk/27.2.12479018`

4. **Re-run CMake**
   - After adding the parameters, click the **"Run CMake"** button
   - Or right-click on `CMakeLists.txt` in the project tree and select **"Run CMake"**
   - Watch the "Compile Output" pane at the bottom

5. **Verify Configuration Success**
   
   In the output, you should see:
   ```
   -- Using prebuilt libsodium: /home/yourusername/libsodium-android/lib/libsodium.a
   -- Found Qt6: ...
   -- Configuring done
   -- Generating done
   ```
   
   If you see errors, double-check your paths in the CMake parameters.

### Step 2: Verify Build Target

1. **In the left sidebar**, above the build/run buttons, you'll see a dropdown menu
   
2. **Select Build Target**:
   - Click the dropdown (it might say "all" or "CipherMesh-Mobile")
   - Select: **`CipherMesh-Mobile_make_apk`**
   
   This target will build the APK file.

---

## Building the APK

### Step 1: Start the Build

1. **Clean Previous Builds** (Optional but recommended for first build):
   - Menu: `Build > Clean All Projects`
   - Wait for cleaning to complete

2. **Build the Project**:
   - Click the **hammer icon** 🔨 in the left sidebar
   - Or press `Ctrl+B` (Linux/Windows) or `Cmd+B` (macOS)
   - Or menu: `Build > Build Project "CipherMesh"`

3. **Monitor Build Progress**:
   - Watch the **"Compile Output"** pane at the bottom
   - First build may take 5-15 minutes (fetches mbedtls, libdatachannel)
   
   You'll see output like:
   ```
   -- Configuring done
   -- Generating done
   -- Build files written to: .../build-CipherMesh-Android_Qt_6_5_3...
   [1/250] Building CXX object ...
   [2/250] Building CXX object ...
   ...
   [250/250] Creating APK
   ```

### Step 2: Locate the Built APK

Once build completes successfully:

1. **Check Build Output**:
   - Look for line near the end: `Creating APK` or `BUILD SUCCESSFUL`

2. **Find APK Location**:
   
   The APK is typically at:
   ```
   <build-directory>/android-build/build/outputs/apk/debug/android-build-debug.apk
   ```
   
   Full example path:
   ```
   ~/Projects/CipherMesh/build-CipherMesh-Android_Qt_6_5_3_Clang_armeabi_v7a-Debug/android-build/build/outputs/apk/debug/android-build-debug.apk
   ```

3. **Open Containing Folder**:
   - Right-click on the build output
   - Look for an option to open the folder
   - Or navigate manually using file manager

---

## Running on Device/Emulator

### Option A: Run on Physical Android Device

#### Step 1: Prepare Your Device

1. **Enable Developer Options**:
   - Go to: `Settings > About Phone`
   - Tap "Build Number" 7 times
   - You'll see: "You are now a developer!"

2. **Enable USB Debugging**:
   - Go to: `Settings > System > Developer Options`
   - Toggle ON: **USB Debugging**
   - Toggle ON: **Install via USB** (if available)

3. **Connect Device via USB**:
   - Use a USB cable to connect your Android phone/tablet to computer
   - On device, you may see a prompt: "Allow USB debugging?"
   - Check "Always allow from this computer"
   - Tap **OK**

#### Step 2: Verify Device Detection

1. **Check Device in Qt Creator**:
   - Look at the device dropdown (next to build target)
   - You should see your device name
   - If not visible, try:
     - Reconnecting USB cable
     - Running `adb devices` in terminal to verify

2. **Test ADB Connection**:
   ```bash
   # In terminal
   adb devices
   ```
   
   Output should show:
   ```
   List of devices attached
   ABC123XYZ    device
   ```
   
   If you see "unauthorized", re-check USB debugging prompt on phone.

#### Step 3: Deploy and Run

1. **Click the Run Button** ▶️:
   - Green play button in left sidebar
   - Or press `Ctrl+R` (Linux/Windows) or `Cmd+R` (macOS)

2. **Qt Creator Will**:
   - Build the APK (if needed)
   - Deploy APK to device
   - Launch the app

3. **Watch Your Device**:
   - The CipherMesh app should appear and launch
   - You'll see the unlock screen

### Option B: Run on Android Emulator

#### Step 1: Create an Android Virtual Device (AVD)

1. **Open Android Studio**:
   - Launch Android Studio (even though we're using Qt Creator, AVD Manager is here)

2. **Open AVD Manager**:
   - Menu: `Tools > Device Manager`
   - Or click the device icon in toolbar

3. **Create Virtual Device**:
   - Click **"Create Device"**
   
   a. **Select Hardware**:
      - Choose a device definition (e.g., "Pixel 5")
      - Click **Next**
   
   b. **Select System Image**:
      - Choose an API level: **API 33 (Android 13)** recommended
      - Download the system image if needed (click "Download")
      - Click **Next**
   
   c. **Verify Configuration**:
      - Give it a name: e.g., "CipherMesh Test Device"
      - Click **Finish**

4. **Launch Emulator**:
   - In AVD Manager, click the ▶️ play button next to your device
   - Wait for emulator to boot (may take 1-2 minutes)

#### Step 2: Deploy to Emulator

1. **Return to Qt Creator**

2. **Select Emulator**:
   - In device dropdown (next to build target)
   - You should now see your emulator listed

3. **Click Run** ▶️:
   - Qt Creator will deploy and launch the app on emulator

### Step 3: Test the Mobile App

Once the app launches:

1. **First Launch - Create Vault**:
   - Enter a master password (e.g., "TestPassword123!")
   - Confirm the password
   - Tap "Create Vault"

2. **Add a Test Entry**:
   - Tap the ➕ button
   - Fill in:
     - Title: "Test Entry"
     - Username: "testuser"
     - Password: "testpass123"
   - Tap "Save"

3. **Test Features**:
   - ✅ Password generator (tap generate button)
   - ✅ TOTP (add a TOTP secret like `JBSWY3DPEHPK3PXP`)
   - ✅ Password strength indicator (should update as you type)
   - ✅ Search (use search bar at top)
   - ✅ Groups (create a new group)
   - ✅ Settings (auto-lock, master password change)

---

## Troubleshooting

### Problem: "Could not find Qt6::Quick"

**Solution:**
- Ensure you installed "Android ARMv7" component in Qt installer
- Re-run CMake configuration
- Check that Qt 6.5.3 for Android is correctly installed

### Problem: "libsodium not found" or "sodium_INCLUDE_DIR not specified"

**Solution:**
1. Verify libsodium was built correctly:
   ```bash
   ls -la ~/libsodium-android/lib/libsodium.a
   ```
2. Double-check your CMake parameters have correct absolute paths
3. Re-run CMake after fixing paths

### Problem: "ANDROID_NDK_ROOT not found"

**Solution:**
1. Check NDK is installed:
   ```bash
   ls -la ~/Android/Sdk/ndk/27.2.12479018
   ```
2. If missing, install NDK 27.2.12479018 via Android Studio SDK Manager
3. Update CMake parameter with correct path

### Problem: Build fails with "ninja: build stopped"

**Solution:**
1. Check "Compile Output" for actual error message
2. Common causes:
   - Missing dependencies (mbedtls, libdatachannel) - these auto-download on first build
   - Incorrect paths in CMake parameters
3. Try: `Build > Clean All Projects` then rebuild

### Problem: "Error while deploying APK"

**Solution:**
1. Check device is connected: `adb devices`
2. Ensure USB debugging is enabled
3. Check device storage is not full
4. Try manually installing:
   ```bash
   adb install -r path/to/android-build-debug.apk
   ```

### Problem: App crashes on launch

**Solution:**
1. Check logcat in Qt Creator:
   - View > Output > "Application Output" tab
2. Common causes:
   - Missing Qt libraries (Qt Creator should bundle these automatically)
   - Permissions issues (check AndroidManifest.xml)
3. Try rebuilding with `Clean All Projects` first

### Problem: Can't see emulator in device list

**Solution:**
1. Make sure emulator is running (check Android Studio AVD Manager)
2. Restart Qt Creator
3. Check `adb devices` shows emulator
4. Restart adb:
   ```bash
   adb kill-server
   adb start-server
   ```

### Problem: Build is very slow

**Solution:**
- First build is slow (downloads dependencies)
- Subsequent builds should be much faster
- If still slow:
  - Close other applications
  - Disable antivirus temporarily
  - Use SSD if possible

---

## Next Steps

After successfully building:

1. **Test All Features**:
   - Go through each screen
   - Test password generation, TOTP, breach checking
   - Test P2P sharing (requires two devices/instances)

2. **Build Release APK**:
   - In Projects > Build, change build type to "Release"
   - Rebuild project
   - Release APK will be in: `.../android-build/build/outputs/apk/release/`

3. **Sign APK** (for distribution):
   - Create keystore
   - Configure signing in Qt Creator
   - See: https://doc.qt.io/qt-6/android-publishing-to-googleplay.html

---

## Complete Mobile Feature List

✅ **Vault Management**
- Create new vault with master password
- Unlock existing vault
- Lock vault manually
- Auto-lock after timeout
- Change master password

✅ **Entry Management**
- Add password entries
- Add secure note entries
- Edit entries
- Delete entries
- Duplicate entries
- Search entries
- View entry details
- Track created/modified timestamps

✅ **Password Features**
- Password generator (customizable length, character sets)
- Password strength meter
- Breach checking via HaveIBeenPwned API
- Password history tracking
- Copy password to clipboard

✅ **TOTP (2FA)**
- Add TOTP secrets to entries
- Real-time code generation
- Countdown timer (30-second refresh)
- Copy TOTP code to clipboard

✅ **Organization**
- Create groups/folders
- Switch between groups
- Delete groups
- Organize entries by group

✅ **Locations**
- Add multiple URLs per entry
- Add app identifiers
- Edit/delete locations
- Open URLs from app

✅ **P2P Sharing**
- Share groups via WebRTC
- Send/receive group invites
- Accept/reject invites
- Peer-to-peer encrypted transfer

✅ **Settings**
- Auto-lock timeout configuration
- Theme support (Dark mode by default)
- Master password change
- Connection status display

✅ **UI/UX**
- Material Design dark theme
- Responsive layouts for all screen sizes
- Touch-optimized interface
- Smooth animations
- Intuitive navigation

---

## Additional Resources

- **Qt for Android Documentation**: https://doc.qt.io/qt-6/android.html
- **Qt Creator Manual**: https://doc.qt.io/qtcreator/
- **Android Developer Docs**: https://developer.android.com/docs
- **CMake Documentation**: https://cmake.org/documentation/
- **libsodium Documentation**: https://doc.libsodium.org/

---

## Support

If you encounter issues not covered in this guide:

1. Check the [BUILD_ANDROID_STUDIO.md](BUILD_ANDROID_STUDIO.md) file for additional troubleshooting
2. Review Qt Creator's "Issues" panel for detailed error messages
3. Check the "Application Output" tab for runtime logs
4. Ensure all prerequisites are correctly installed and configured

---

**Happy Building! 🚀**
