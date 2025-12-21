# Complete Android Studio Guide for Building CipherMesh Android App

This is a detailed, step-by-step guide for building the CipherMesh Android mobile app using Android Studio.

---

## Table of Contents
1. [Prerequisites Installation](#prerequisites-installation)
2. [Android Studio Setup](#android-studio-setup)
3. [Building libsodium for Android](#building-libsodium-for-android)
4. [Importing the Project](#importing-the-project)
5. [Configuring CMake](#configuring-cmake)
6. [Building the APK](#building-the-apk)
7. [Installing and Running](#installing-and-running)
8. [Troubleshooting](#troubleshooting)

---

## Prerequisites Installation

### Step 1: Install Android Studio

1. **Download Android Studio**
   - Go to: https://developer.android.com/studio
   - Download the latest stable version for your operating system
   - **Linux**: `android-studio-xxxx.x.x.xx-linux.tar.gz`
   - **Windows**: `android-studio-xxxx.x.x.xx-windows.exe`
   - **macOS**: `android-studio-xxxx.x.x.xx-mac.dmg`

2. **Install Android Studio**
   
   **Linux:**
   ```bash
   # Extract the archive
   tar -xzf android-studio-*-linux.tar.gz
   
   # Move to /opt (optional but recommended)
   sudo mv android-studio /opt/
   
   # Run Android Studio
   /opt/android-studio/bin/studio.sh
   
   # Or create a desktop entry for easy access
   ```
   
   **Windows:**
   - Double-click the `.exe` installer
   - Follow the installation wizard
   - Accept default installation location: `C:\Program Files\Android\Android Studio`
   
   **macOS:**
   - Open the `.dmg` file
   - Drag Android Studio to Applications folder
   - Open from Applications

3. **First Launch Setup Wizard**
   
   a. **Welcome Screen**:
      - Click "Next"
   
   b. **Install Type**:
      - Select "Standard" installation
      - Click "Next"
   
   c. **Select UI Theme**:
      - Choose your preferred theme (Darcula or Light)
      - Click "Next"
   
   d. **Verify Settings**:
      - Review the installation settings
      - Note the SDK path (you'll need this later):
        - Linux: `/home/yourusername/Android/Sdk`
        - Windows: `C:\Users\YourName\AppData\Local\Android\Sdk`
        - macOS: `/Users/yourusername/Library/Android/sdk`
      - Click "Finish"
   
   e. **Downloading Components**:
      - Wait for Android Studio to download SDK components
      - This may take 10-20 minutes depending on your internet connection

### Step 2: Install Required SDK Components

1. **Open SDK Manager**
   - From welcome screen: Click "More Actions" > "SDK Manager"
   - OR from main window: `Tools > SDK Manager`

2. **SDK Platforms Tab**
   
   Check the following:
   ```
   ☑ Android 13.0 ("Tiramisu") - API Level 33
   ☑ Android 12.0 ("S") - API Level 31       (optional, for broader compatibility)
   ☑ Android 11.0 ("R") - API Level 30       (optional)
   ```
   
3. **SDK Tools Tab**
   
   a. Check "Show Package Details" at the bottom right
   
   b. Select the following:
   ```
   ☑ Android SDK Build-Tools
     └─ ☑ 33.0.0 (or latest)
   
   ☑ Android SDK Command-line Tools (latest)
   
   ☑ Android Emulator
   
   ☑ Android SDK Platform-Tools
   
   ☑ CMake
     └─ ☑ 3.22.1 (specific version for Qt compatibility)
   
   ☑ NDK (Side by side)
     └─ ☑ 27.2.12479018           ← CRITICAL: Specific version required!
   
   ☑ Android SDK Tools (Obsolete)  (if available)
   ```
   
4. **Apply Changes**
   - Click "Apply" at the bottom right
   - Review the changes in the confirmation dialog
   - Click "OK" to download and install
   - Wait for installation to complete (may take 10-15 minutes)
   - Click "Finish" when done

5. **Verify NDK Installation**
   
   Check that NDK 27.2.12479018 is installed:
   
   **Linux/macOS:**
   ```bash
   ls -la ~/Android/Sdk/ndk/27.2.12479018
   ```
   
   **Windows (PowerShell):**
   ```powershell
   dir C:\Users\YourName\AppData\Local\Android\Sdk\ndk\27.2.12479018
   ```
   
   You should see directories like `build`, `toolchains`, `sources`, etc.

### Step 3: Install Qt 6.5.3

Android Studio requires Qt to build Qt-based Android apps.

1. **Download Qt Online Installer**
   - Go to: https://www.qt.io/download-qt-installer
   - Download the installer for your OS

2. **Run Qt Installer**
   
   **Linux:**
   ```bash
   chmod +x qt-online-installer-linux-x64-*.run
   ./qt-online-installer-linux-x64-*.run
   ```
   
   **Windows/macOS:**
   - Double-click the installer

3. **Login/Create Qt Account**
   - Create a free account or login

4. **Select Installation Path**
   - **Recommended paths:**
     - Linux: `/home/yourusername/Qt`
     - Windows: `C:\Qt`
     - macOS: `/Users/yourusername/Qt`
   - Remember this path!

5. **Select Components**
   
   In the component tree, select:
   ```
   ☑ Qt 6.5.3
     ├─ ☑ Desktop gcc 64-bit (Linux)
     │   OR MinGW 11.2.0 64-bit (Windows)
     │   OR macOS (macOS)
     │
     ├─ ☑ Android ARMv7               ← REQUIRED
     ├─ ☑ Android ARM64-v8a            ← RECOMMENDED
     │
     └─ Sources (optional, for debugging)
   
   ☑ Developer and Designer Tools
     ├─ ☑ Qt Creator                   (optional, but useful)
     └─ ☑ CMake and Ninja              (optional)
   ```

6. **Complete Installation**
   - Accept license agreements
   - Click "Install"
   - Wait for installation (15-30 minutes)

### Step 4: Install JDK 17

1. **Check if Already Installed**
   
   Android Studio includes JDK 17. Check the location:
   - **Linux**: `/snap/android-studio/current/android-studio/jbr` or `/opt/android-studio/jbr`
   - **Windows**: `C:\Program Files\Android\Android Studio\jbr`
   - **macOS**: `/Applications/Android Studio.app/Contents/jbr`

2. **Alternative: Install Separately**
   
   If you prefer a separate installation:
   - Download from: https://adoptium.net/
   - Select: **Temurin 17 (LTS)**
   - Install and note the installation directory

### Step 5: Set Environment Variables

Set up environment variables for easier building.

**Linux/macOS** (add to `~/.bashrc` or `~/.zshrc`):

```bash
# Android SDK
export ANDROID_SDK_ROOT=$HOME/Android/Sdk
export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018

# Qt
export QT_ROOT=$HOME/Qt/6.5.3
export QT_ANDROID=$QT_ROOT/android_armv7
export QT_HOST_PATH=$QT_ROOT/gcc_64  # or macos on macOS

# Add to PATH
export PATH=$ANDROID_SDK_ROOT/platform-tools:$PATH
export PATH=$ANDROID_SDK_ROOT/cmdline-tools/latest/bin:$PATH

# JDK (if using Android Studio's JDK)
export JAVA_HOME=/snap/android-studio/current/android-studio/jbr
```

Then reload:
```bash
source ~/.bashrc  # or source ~/.zshrc
```

**Windows** (PowerShell as Administrator):

```powershell
# Set permanently
[Environment]::SetEnvironmentVariable("ANDROID_SDK_ROOT", "C:\Users\YourName\AppData\Local\Android\Sdk", "User")
[Environment]::SetEnvironmentVariable("ANDROID_NDK_ROOT", "C:\Users\YourName\AppData\Local\Android\Sdk\ndk\27.2.12479018", "User")
[Environment]::SetEnvironmentVariable("QT_ROOT", "C:\Qt\6.5.3", "User")
[Environment]::SetEnvironmentVariable("JAVA_HOME", "C:\Program Files\Android\Android Studio\jbr", "User")

# Restart PowerShell after setting
```

---

## Building libsodium for Android

CipherMesh requires libsodium compiled for Android.

### Option 1: Download Pre-built (If Available)

Check if your distribution provides pre-built libsodium for Android. Otherwise, use Option 2.

### Option 2: Build from Source (Recommended)

1. **Install Build Tools**
   
   **Linux:**
   ```bash
   sudo apt-get update
   sudo apt-get install -y autoconf automake libtool build-essential git
   ```
   
   **macOS:**
   ```bash
   brew install autoconf automake libtool
   ```
   
   **Windows:**
   - Install WSL (Windows Subsystem for Linux) or use Git Bash
   - Follow Linux instructions in WSL

2. **Clone libsodium Repository**
   
   ```bash
   cd ~
   git clone --depth 1 --branch stable https://github.com/jedisct1/libsodium.git
   cd libsodium
   ```

3. **Build for Android ARMv7**
   
   ```bash
   # Set NDK path (adjust to your actual path)
   export ANDROID_NDK_ROOT=$HOME/Android/Sdk/ndk/27.2.12479018
   
   # For macOS:
   # export ANDROID_NDK_ROOT=/Users/yourusername/Library/Android/sdk/ndk/27.2.12479018
   
   # For Windows (in WSL):
   # export ANDROID_NDK_ROOT=/mnt/c/Users/YourName/AppData/Local/Android/Sdk/ndk/27.2.12479018
   
   # Set toolchain based on OS
   # Linux:
   export TOOLCHAIN=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64
   # macOS:
   # export TOOLCHAIN=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/darwin-x86_64
   # Windows (WSL):
   # export TOOLCHAIN=$ANDROID_NDK_ROOT/toolchains/llvm/prebuilt/linux-x86_64
   
   export API=21
   export TARGET=armv7a-linux-androideabi
   
   export CC=$TOOLCHAIN/bin/${TARGET}${API}-clang
   export CXX=$TOOLCHAIN/bin/${TARGET}${API}-clang++
   export AR=$TOOLCHAIN/bin/llvm-ar
   export RANLIB=$TOOLCHAIN/bin/llvm-ranlib
   export STRIP=$TOOLCHAIN/bin/llvm-strip
   
   # Generate configure script
   ./autogen.sh
   
   # Configure for Android
   ./configure \
     --host=armv7a-linux-androideabi \
     --prefix=$HOME/libsodium-android-armv7 \
     --disable-shared \
     --enable-static \
     --with-pic
   
   # Build (use nproc on Linux, sysctl on macOS)
   make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
   
   # Install to ~/libsodium-android-armv7
   make install
   
   echo "libsodium installed to: $HOME/libsodium-android-armv7"
   ```

4. **Verify Build**
   
   ```bash
   ls -la ~/libsodium-android-armv7/lib/libsodium.a
   ls -la ~/libsodium-android-armv7/include/sodium.h
   ```
   
   Both files should exist.

5. **Optional: Build for ARM64-v8a**
   
   If you want to support 64-bit ARM devices:
   
   ```bash
   cd ~/libsodium
   make clean
   
   export TARGET=aarch64-linux-android
   export CC=$TOOLCHAIN/bin/${TARGET}${API}-clang
   export CXX=$TOOLCHAIN/bin/${TARGET}${API}-clang++
   
   ./configure \
     --host=aarch64-linux-android \
     --prefix=$HOME/libsodium-android-arm64 \
     --disable-shared \
     --enable-static \
     --with-pic
   
   make -j$(nproc 2>/dev/null || sysctl -n hw.ncpu || echo 4)
   make install
   ```

---

## Importing the Project

### Step 1: Clone CipherMesh Repository

```bash
# Navigate to your projects directory
cd ~/Projects  # or C:\Users\YourName\Projects on Windows

# Clone the repository
git clone https://github.com/AmoghS1234/CipherMesh.git
cd CipherMesh
```

### Step 2: Open Project in Android Studio

1. **Launch Android Studio**

2. **Open Existing Project**
   - Click "Open" on the welcome screen
   - OR: `File > Open`

3. **Navigate to CipherMesh Directory**
   - Browse to where you cloned CipherMesh
   - **IMPORTANT**: You need to import it as a CMake project, but Android Studio doesn't directly support opening `CMakeLists.txt`
   
4. **Create Android Project Wrapper** (Required for Android Studio)
   
   Android Studio works best with Gradle-based Android projects. We need to create a minimal wrapper:
   
   a. In the CipherMesh directory, create a `build.gradle` file:
   
   ```bash
   cd ~/Projects/CipherMesh
   ```
   
   Create `build.gradle`:
   ```gradle
   // Top-level build file for CipherMesh Android wrapper
   
   buildscript {
       repositories {
           google()
           mavenCentral()
       }
       dependencies {
           classpath 'com.android.tools.build:gradle:8.2.0'
       }
   }
   
   allprojects {
       repositories {
           google()
           mavenCentral()
       }
   }
   ```
   
   b. Create `settings.gradle`:
   ```gradle
   rootProject.name = "CipherMesh"
   include ':app'
   ```
   
   c. Create `app/build.gradle`:
   ```bash
   mkdir -p app
   ```
   
   Create `app/build.gradle`:
   ```gradle
   plugins {
       id 'com.android.application'
   }
   
   android {
       namespace 'com.ciphermesh.mobile'
       compileSdk 33
       
       defaultConfig {
           applicationId "com.ciphermesh.mobile"
           minSdk 21
           targetSdk 33
           versionCode 1
           versionName "1.0"
           
           externalNativeBuild {
               cmake {
                   arguments "-DANDROID_STL=c++_shared",
                             "-DANDROID_ABI=armeabi-v7a",
                             "-DQT_ANDROID_DIR=" + System.getenv("QT_ANDROID"),
                             "-DQT_HOST_PATH=" + System.getenv("QT_HOST_PATH"),
                             "-Dsodium_INCLUDE_DIR=" + System.getenv("HOME") + "/libsodium-android-armv7/include",
                             "-Dsodium_LIBRARY_RELEASE=" + System.getenv("HOME") + "/libsodium-android-armv7/lib/libsodium.a"
               }
           }
           
           ndk {
               abiFilters 'armeabi-v7a'
           }
       }
       
       buildTypes {
           release {
               minifyEnabled false
               proguardFiles getDefaultProguardFile('proguard-android-optimize.txt'), 'proguard-rules.pro'
           }
       }
       
       externalNativeBuild {
           cmake {
               path file('../CMakeLists.txt')
               version '3.22.1'
           }
       }
       
       ndkVersion '27.2.12479018'
   }
   
   dependencies {
       // Add any additional Android dependencies here
   }
   ```
   
   d. Create `app/src/main/AndroidManifest.xml`:
   ```bash
   mkdir -p app/src/main
   ```
   
   Create the file:
   ```xml
   <?xml version="1.0" encoding="utf-8"?>
   <manifest xmlns:android="http://schemas.android.com/apk/res/android"
       package="com.ciphermesh.mobile">
       
       <uses-permission android:name="android.permission.INTERNET" />
       <uses-permission android:name="android.permission.ACCESS_NETWORK_STATE" />
       
       <application
           android:allowBackup="true"
           android:icon="@mipmap/ic_launcher"
           android:label="@string/app_name"
           android:roundIcon="@mipmap/ic_launcher_round"
           android:supportsRtl="true"
           android:theme="@style/Theme.AppCompat.DayNight">
           
           <activity
               android:name=".MainActivity"
               android:exported="true"
               android:configChanges="orientation|screenSize|keyboardHidden">
               <intent-filter>
                   <action android:name="android.intent.action.MAIN" />
                   <category android:name="android.intent.category.LAUNCHER" />
               </intent-filter>
           </activity>
       </application>
   </manifest>
   ```

5. **Now Open in Android Studio**
   - `File > Open`
   - Select the CipherMesh directory (the one with the new `build.gradle`)
   - Click "OK"
   - Wait for Gradle sync (may take a few minutes)

---

## Configuring CMake

### Step 1: Update local.properties

Android Studio creates a `local.properties` file. Update it:

```properties
sdk.dir=/home/yourusername/Android/Sdk
ndk.dir=/home/yourusername/Android/Sdk/ndk/27.2.12479018
qt.dir=/home/yourusername/Qt/6.5.3
cmake.dir=/home/yourusername/Android/Sdk/cmake/3.22.1

# For Windows:
# sdk.dir=C\:\\Users\\YourName\\AppData\\Local\\Android\\Sdk
# ndk.dir=C\:\\Users\\YourName\\AppData\\Local\\Android\\Sdk\\ndk\\27.2.12479018
```

### Step 2: Configure CMake Arguments

1. **Open Build Variants**
   - `View > Tool Windows > Build Variants`

2. **Edit CMake Settings**
   - Go to: `File > Settings` (Windows/Linux) or `Android Studio > Preferences` (macOS)
   - Navigate to: `Build, Execution, Deployment > CMake`

3. **Or Edit app/build.gradle**
   
   The CMake arguments are already in the `build.gradle` we created. Verify they point to your actual paths:
   
   ```gradle
   externalNativeBuild {
       cmake {
           arguments "-DANDROID_STL=c++_shared",
                     "-DANDROID_ABI=armeabi-v7a",
                     "-DANDROID_SDK_ROOT=" + System.getenv("ANDROID_SDK_ROOT"),
                     "-DANDROID_NDK_ROOT=" + System.getenv("ANDROID_NDK_ROOT"),
                     "-DQT_ANDROID_DIR=/home/yourusername/Qt/6.5.3/android_armv7",
                     "-DQT_HOST_PATH=/home/yourusername/Qt/6.5.3/gcc_64",
                     "-Dsodium_INCLUDE_DIR=/home/yourusername/libsodium-android-armv7/include",
                     "-Dsodium_LIBRARY_RELEASE=/home/yourusername/libsodium-android-armv7/lib/libsodium.a"
       }
   }
   ```

---

## Building the APK

### Step 1: Sync Project with Gradle

1. **Sync Gradle**
   - Click the "Sync Project with Gradle Files" button (elephant icon with down arrow)
   - OR: `File > Sync Project with Gradle Files`
   - Wait for sync to complete

2. **Check for Errors**
   - Review the "Build" panel at the bottom
   - Fix any configuration errors

### Step 2: Build the APK

1. **Select Build Variant**
   - `View > Tool Windows > Build Variants`
   - Select "debug" or "release"

2. **Build APK**
   - `Build > Build Bundle(s) / APK(s) > Build APK(s)`
   - OR click the hammer icon in the toolbar

3. **Monitor Build Progress**
   - Watch the "Build" panel at the bottom
   - First build will take 10-20 minutes (downloads dependencies)
   
   You'll see:
   ```
   > Task :app:externalNativeBuildDebug
   [1/250] Building CXX object...
   [2/250] Building CXX object...
   ...
   [250/250] Linking CXX shared library...
   
   > Task :app:mergeDebugResources
   > Task :app:packageDebug
   
   BUILD SUCCESSFUL in 12m 34s
   ```

4. **Build Completion**
   - You'll see a notification: "APK(s) generated successfully"
   - Click "locate" to find the APK

### Step 3: Locate the Built APK

The APK is typically located at:

```
CipherMesh/app/build/outputs/apk/debug/app-debug.apk
```

**Full paths:**
- Linux: `/home/yourusername/Projects/CipherMesh/app/build/outputs/apk/debug/app-debug.apk`
- Windows: `C:\Users\YourName\Projects\CipherMesh\app\build\outputs\apk\debug\app-debug.apk`
- macOS: `/Users/yourusername/Projects/CipherMesh/app/build/outputs/apk/debug/app-debug.apk`

---

## Installing and Running

### Option 1: Run from Android Studio

1. **Connect Android Device**
   - Enable USB debugging on your Android device
   - Connect via USB
   - Allow USB debugging when prompted

2. **Select Device**
   - Click the device dropdown in the toolbar
   - Select your connected device

3. **Run App**
   - Click the green play button ▶️
   - OR: `Run > Run 'app'`
   - OR: Press `Shift + F10`

4. **Watch Deployment**
   - Android Studio will install and launch the app
   - Monitor the "Run" panel for output

### Option 2: Install APK Manually

1. **Using ADB** (Android Debug Bridge):
   
   ```bash
   # Make sure device is connected
   adb devices
   
   # Install the APK
   adb install -r app/build/outputs/apk/debug/app-debug.apk
   
   # Launch the app
   adb shell am start -n com.ciphermesh.mobile/.MainActivity
   ```

2. **Transfer APK to Device**:
   - Copy the APK to your device (via USB, cloud, email, etc.)
   - Open the APK on your device
   - Tap "Install"
   - May need to enable "Install from Unknown Sources" in Settings

### Option 3: Use Android Emulator

1. **Create AVD** (Android Virtual Device):
   - `Tools > Device Manager`
   - Click "Create Device"
   - Select hardware (e.g., Pixel 5)
   - Select system image (API 33 recommended)
   - Click "Finish"

2. **Launch Emulator**:
   - In Device Manager, click play button next to your AVD
   - Wait for emulator to boot

3. **Run App**:
   - Select the emulator from device dropdown
   - Click play button ▶️

---

## Troubleshooting

### Problem: "SDK location not found"

**Solution:**
- Ensure `local.properties` has correct `sdk.dir` path
- Check that Android SDK is installed at that location
- Restart Android Studio

### Problem: "NDK not configured"

**Solution:**
- Verify NDK 27.2.12479018 is installed:
  ```bash
  ls ~/Android/Sdk/ndk/27.2.12479018
  ```
- Update `app/build.gradle` with correct NDK version
- Re-sync Gradle

### Problem: "CMake not found" or version mismatch

**Solution:**
- Install CMake 3.22.1 via SDK Manager
- Update `app/build.gradle` to specify CMake version:
  ```gradle
  externalNativeBuild {
      cmake {
          version '3.22.1'
      }
  }
  ```

### Problem: "libsodium not found"

**Solution:**
- Verify libsodium was built correctly:
  ```bash
  ls ~/libsodium-android-armv7/lib/libsodium.a
  ```
- Update CMake arguments in `build.gradle` with correct paths
- Clean and rebuild: `Build > Clean Project` then `Build > Rebuild Project`

### Problem: "Qt not found" errors

**Solution:**
- Ensure Qt 6.5.3 for Android is installed
- Update CMake arguments with correct Qt paths:
  ```
  -DQT_ANDROID_DIR=/path/to/Qt/6.5.3/android_armv7
  -DQT_HOST_PATH=/path/to/Qt/6.5.3/gcc_64
  ```
- Re-sync Gradle

### Problem: Build is very slow

**Solution:**
- First build is always slow (downloads dependencies)
- Subsequent builds should be faster
- Enable parallel builds in `gradle.properties`:
  ```properties
  org.gradle.parallel=true
  org.gradle.configureondemand=true
  org.gradle.jvmargs=-Xmx4096m
  ```

### Problem: "Installed Build Tools revision XX is corrupted"

**Solution:**
- Uninstall and reinstall Build Tools via SDK Manager
- Or manually delete and reinstall:
  ```bash
  rm -rf ~/Android/Sdk/build-tools/33.0.0
  ```
  Then reinstall via SDK Manager

### Problem: App crashes on launch

**Solution:**
- Check logcat in Android Studio: `View > Tool Windows > Logcat`
- Common causes:
  - Missing Qt libraries (should be bundled automatically)
  - Permissions issues (check AndroidManifest.xml)
  - ABI mismatch (ensure device supports armeabi-v7a)
- Try rebuilding with: `Build > Clean Project` then rebuild

---

## Testing the Mobile App

Once installed, test all features:

1. **First Launch - Create Vault**:
   - Enter master password
   - Confirm password
   - Tap "Create Vault"

2. **Add Entry**:
   - Tap ➕ button
   - Fill in title, username, password
   - Tap "Save"

3. **Test Features**:
   - ✅ Password generator
   - ✅ TOTP code generation
   - ✅ Password strength indicator
   - ✅ Breach checker
   - ✅ Search functionality
   - ✅ Groups/folders
   - ✅ Settings (auto-lock, master password change)

---

## Alternative: Using Gradle Command Line

You can also build from command line without opening Android Studio GUI:

```bash
cd ~/Projects/CipherMesh

# On Linux/macOS:
./gradlew assembleDebug

# On Windows:
gradlew.bat assembleDebug

# APK will be at: app/build/outputs/apk/debug/app-debug.apk
```

For release build:
```bash
./gradlew assembleRelease

# APK will be at: app/build/outputs/apk/release/app-release-unsigned.apk
```

---

## Summary

You've now built the CipherMesh Android APK using Android Studio! The build process:

1. ✅ Installed Android Studio, SDK, NDK
2. ✅ Installed Qt 6.5.3 for Android
3. ✅ Built libsodium for Android
4. ✅ Created Gradle wrapper for CipherMesh
5. ✅ Configured CMake with correct paths
6. ✅ Built APK successfully
7. ✅ Installed and tested on device/emulator

The mobile app includes all desktop features with a touch-optimized Material Design interface!

---

## Additional Resources

- **Android Studio Documentation**: https://developer.android.com/studio/intro
- **Qt for Android**: https://doc.qt.io/qt-6/android.html
- **Gradle Plugin User Guide**: https://developer.android.com/studio/build
- **CMake in Android Studio**: https://developer.android.com/studio/projects/configure-cmake

---

**Note**: This guide creates a Gradle wrapper around the existing CMake build system. Qt Creator is still the recommended tool for Qt Android development, but this approach allows using Android Studio if preferred.
