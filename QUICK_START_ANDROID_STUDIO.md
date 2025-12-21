# Quick Start: Building CipherMesh APK with Android Studio

**For First-Time Android Studio Users**

This guide walks you through building the CipherMesh Android app from scratch, starting right after installing Android Studio.

---

## Table of Contents

1. [Complete Setup (First Time Only)](#complete-setup-first-time-only)
2. [Building the APK](#building-the-apk)
3. [Installing on Your Phone](#installing-on-your-phone)
4. [Troubleshooting](#troubleshooting)

---

## Complete Setup (First Time Only)

### Step 1: Install Required Software

You'll need to install 4 things:

#### 1.1 Android Studio (Already Done ✓)
You mentioned you just installed it - great!

#### 1.2 Install Android SDK & NDK

1. **Open Android Studio**
2. Go to **Tools** → **SDK Manager** (or click the SDK Manager icon in the toolbar)
3. In the **SDK Platforms** tab:
   - Check **Android 14.0 ("UpsideDownCake")** or latest version
   - Click **Apply** and wait for installation

4. In the **SDK Tools** tab, check these items:
   - ✅ **Android SDK Build-Tools**
   - ✅ **NDK (Side by side)** - Version **27.2.12479018** specifically
   - ✅ **CMake**
   - ✅ **Android SDK Command-line Tools**
   - Click **Show Package Details** at bottom right
   - Expand **NDK (Side by side)** and ensure version **27.2.12479018** is checked
   - Click **Apply** and wait for installation (this may take 10-15 minutes)

5. **Note the SDK location** shown at the top of SDK Manager window (e.g., `C:\Users\YourName\AppData\Local\Android\Sdk` on Windows)

#### 1.3 Install Java Development Kit (JDK) 17

**Windows:**
1. Download from: https://adoptium.net/temurin/releases/?version=17
2. Choose **Windows x64** installer (.msi)
3. Run installer, use default options
4. After installation, open Command Prompt and verify:
   ```
   java -version
   ```
   Should show version 17.x.x

**Linux:**
```bash
sudo apt update
sudo apt install openjdk-17-jdk
java -version
```

**macOS:**
```bash
brew install openjdk@17
java -version
```

#### 1.4 Install Qt 6.5.3 for Android

1. **Download Qt Online Installer:**
   - Go to: https://www.qt.io/download-qt-installer
   - Download and run the installer

2. **During Installation:**
   - Sign in or create a free Qt account
   - Choose **Custom Installation**
   - In component selection, expand **Qt 6.5.3**:
     - ✅ Check **Android** → **Android ARMv7**
     - ✅ Check **Android** → **Android ARM64-v8a** (recommended for modern devices)
     - ✅ Check **Desktop gcc 64-bit** (for Linux) or **MSVC 2019 64-bit** (for Windows) or **macOS** (for macOS)
     - ✅ Check **Qt Creator** (IDE, but we'll use Android Studio)
   - Installation location example: `C:\Qt\6.5.3` (Windows) or `/home/username/Qt/6.5.3` (Linux)
   - Wait for installation (may take 20-30 minutes)

3. **Note the Qt installation path** - you'll need it later!

---

### Step 2: Build libsodium for Android

libsodium is a cryptography library needed by CipherMesh.

**Option A: Use Pre-built Binaries (Easier)**

1. Download pre-built libsodium for Android from:
   https://download.libsodium.org/libsodium/releases/

2. Look for a file like `libsodium-1.0.19-stable-android.tar.gz`

3. Extract it to a folder, e.g.:
   - Windows: `C:\libsodium-android\`
   - Linux: `/home/username/libsodium-android/`
   - macOS: `/Users/username/libsodium-android/`

4. **Note this path** - you'll need it!

**Option B: Build from Source (Advanced)**

If pre-built binaries don't work, see the ANDROID_STUDIO_GUIDE.md file for detailed build instructions.

---

### Step 3: Configure the Project

Now we need to tell the project where to find Qt, libsodium, and Android SDK/NDK.

1. **Navigate to your CipherMesh project folder**

2. **Open** `app/build.gradle` in any text editor (Notepad++, VS Code, etc.)

3. **Find this section** (around line 17-22):
   ```gradle
   externalNativeBuild {
       cmake {
           arguments "-DANDROID_STL=c++_shared",
                     "-DQT_HOST_PATH=/path/to/Qt/6.5.3/gcc_64",
                     "-Dsodium_INCLUDE_DIR=/path/to/libsodium-android/include",
                     "-Dsodium_LIBRARY_RELEASE=/path/to/libsodium-android/lib/armeabi-v7a/libsodium.a"
   ```

4. **Replace the paths** with your actual paths:

   **Example for Windows:**
   ```gradle
   arguments "-DANDROID_STL=c++_shared",
             "-DQT_HOST_PATH=C:/Qt/6.5.3/msvc2019_64",
             "-Dsodium_INCLUDE_DIR=C:/libsodium-android/include",
             "-Dsodium_LIBRARY_RELEASE=C:/libsodium-android/lib/armeabi-v7a/libsodium.a"
   ```

   **Example for Linux:**
   ```gradle
   arguments "-DANDROID_STL=c++_shared",
             "-DQT_HOST_PATH=/home/username/Qt/6.5.3/gcc_64",
             "-Dsodium_INCLUDE_DIR=/home/username/libsodium-android/include",
             "-Dsodium_LIBRARY_RELEASE=/home/username/libsodium-android/lib/armeabi-v7a/libsodium.a"
   ```

   **Example for macOS:**
   ```gradle
   arguments "-DANDROID_STL=c++_shared",
             "-DQT_HOST_PATH=/Users/username/Qt/6.5.3/macos",
             "-Dsodium_INCLUDE_DIR=/Users/username/libsodium-android/include",
             "-Dsodium_LIBRARY_RELEASE=/Users/username/libsodium-android/lib/armeabi-v7a/libsodium.a"
   ```

   **Important Notes:**
   - Use forward slashes `/` even on Windows (Gradle requires this)
   - Replace `username` with your actual username
   - Adjust Qt version path if you installed to a different location

5. **Save the file**

---

## Building the APK

Now you're ready to build!

### Step 1: Open Project in Android Studio

1. **Launch Android Studio**

2. **Close any open projects** (File → Close Project if needed)

3. Click **Open** on the welcome screen

4. **Navigate to your CipherMesh folder** and click **OK**

5. Android Studio will analyze the project and sync Gradle
   - This first sync may take 5-10 minutes
   - You'll see progress at the bottom of the window
   - If you see any errors about Gradle, wait - it may still be downloading

### Step 2: Wait for Gradle Sync

- Look for **"Gradle sync finished"** message at the bottom
- If you see errors:
  - Check that all paths in `app/build.gradle` are correct
  - Make sure NDK 27.2.12479018 is installed
  - See [Troubleshooting](#troubleshooting) section below

### Step 3: Configure Build Variant (Optional)

1. In Android Studio, go to **Build** → **Select Build Variant**
2. Choose **debug** for testing (default) or **release** for production

### Step 4: Build the APK

**Method 1: Using Menu (Recommended)**

1. Go to **Build** → **Build Bundle(s) / APK(s)** → **Build APK(s)**
2. Wait for the build process (5-15 minutes for first build)
3. Watch the **Build** tab at the bottom for progress
4. When finished, you'll see: **"BUILD SUCCESSFUL"** with a notification

**Method 2: Using Gradle (Alternative)**

1. Open the **Gradle** panel on the right side of Android Studio
2. Expand **CipherMesh** → **app** → **Tasks** → **build**
3. Double-click **assembleDebug**
4. Wait for build to complete

### Step 5: Find Your APK

After a successful build:

1. Click **locate** in the notification, or
2. Navigate manually to:
   ```
   YourCipherMeshFolder/app/build/outputs/apk/debug/app-debug.apk
   ```

**Congratulations!** You've built the APK! 🎉

---

## Installing on Your Phone

### Option 1: Direct Install from Android Studio

1. **Enable USB Debugging** on your Android phone:
   - Go to **Settings** → **About Phone**
   - Tap **Build Number** 7 times to enable Developer Mode
   - Go back to **Settings** → **Developer Options**
   - Enable **USB Debugging**

2. **Connect your phone** to computer via USB cable

3. On your phone, **Allow USB Debugging** when prompted

4. In Android Studio:
   - Click the **Run** button ▶️ (green play icon) in the toolbar
   - Or press **Shift + F10**
   - Select your device from the list
   - App will install and launch automatically

### Option 2: Manual Installation via ADB

1. Open Command Prompt (Windows) or Terminal (Linux/macOS)

2. Navigate to the folder containing the APK:
   ```bash
   cd path/to/CipherMesh/app/build/outputs/apk/debug
   ```

3. Install on connected device:
   ```bash
   adb install app-debug.apk
   ```

### Option 3: Direct Installation on Phone

1. **Copy** `app-debug.apk` to your phone (via USB, cloud, email, etc.)

2. On your phone, open the APK file

3. If prompted, **allow installation from unknown sources**:
   - Settings → Security → Install unknown apps
   - Enable for your file manager

4. Tap **Install**

---

## Troubleshooting

### Problem: "SDK location not found"

**Solution:**
1. Create a file named `local.properties` in your CipherMesh folder (next to `build.gradle`)
2. Add this line (adjust path to your SDK location):
   ```
   sdk.dir=C\:\\Users\\YourName\\AppData\\Local\\Android\\Sdk
   ```
   (Note: Use double backslashes `\\` on Windows)

### Problem: "NDK not configured"

**Solution:**
1. Open Android Studio
2. Tools → SDK Manager → SDK Tools tab
3. Check **NDK (Side by side)** and install version 27.2.12479018
4. Click **Apply**

### Problem: "Qt not found" or "qt_host_path not set"

**Solution:**
1. Check the `-DQT_HOST_PATH` in `app/build.gradle`
2. Make sure the path points to the correct Qt installation
3. Use forward slashes `/` even on Windows
4. Example: `-DQT_HOST_PATH=C:/Qt/6.5.3/gcc_64` not `C:\Qt\6.5.3\gcc_64`

### Problem: "libsodium not found"

**Solution:**
1. Verify libsodium is extracted correctly
2. Check that `libsodium.a` exists in the `lib/armeabi-v7a/` folder
3. Update paths in `app/build.gradle` to match your actual location
4. Make sure to use forward slashes `/`

### Problem: "Gradle sync failed"

**Solution:**
1. Check your internet connection (Gradle downloads dependencies)
2. File → Invalidate Caches / Restart → Invalidate and Restart
3. Try: Tools → Gradle → Refresh all Gradle projects
4. Delete `.gradle` folder in your project and sync again

### Problem: Build is very slow

**Solution:**
1. First build always takes longer (10-20 minutes is normal)
2. Subsequent builds will be much faster (2-5 minutes)
3. Close other applications to free up RAM
4. Consider increasing Gradle memory in `gradle.properties`:
   ```
   org.gradle.jvmargs=-Xmx4096m -Dfile.encoding=UTF-8
   ```

### Problem: "Java version mismatch"

**Solution:**
1. Make sure JDK 17 is installed
2. In Android Studio: File → Project Structure → SDK Location
3. Set JDK location to your JDK 17 installation
4. Restart Android Studio

---

## Testing the App

Once installed, test these features:

1. **First Launch:**
   - Create a master password (vault creation)
   - Unlock the vault

2. **Add Entry:**
   - Tap **+** button
   - Fill in username, password, website
   - Save

3. **Password Generator:**
   - When editing password field, tap the dice icon
   - Adjust length and character options
   - Generate password

4. **TOTP (2FA):**
   - Add a TOTP secret to an entry
   - Watch the code update every 30 seconds

5. **Groups:**
   - Create a new group (folder)
   - Move entries between groups

6. **Search:**
   - Use the search bar to find entries

7. **Settings:**
   - Change auto-lock timer
   - Change master password

---

## Need More Help?

- **Detailed Guide:** See `ANDROID_STUDIO_GUIDE.md` for more comprehensive instructions
- **Qt Creator:** See `QT_CREATOR_GUIDE.md` for an alternative (and easier) build method
- **General Reference:** See `BUILD_ANDROID_STUDIO.md` for all build options

---

## Summary

You've successfully:
- ✅ Installed Android Studio, Qt, JDK, and NDK
- ✅ Configured the CipherMesh project
- ✅ Built the APK file
- ✅ Installed it on your Android device

**Enjoy using CipherMesh!** 🔐
