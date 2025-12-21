# Building CipherMesh Android App with Android Studio

This guide explains how to build the CipherMesh Android mobile app using Android Studio instead of GitHub Actions.

## Prerequisites

### Required Software
1. **Android Studio** (Latest stable version recommended - Hedgehog or later)
   - Download from: https://developer.android.com/studio

2. **Qt 6.5.3 for Android**
   - Download Qt Online Installer from: https://www.qt.io/download-qt-installer
   - During installation, select:
     - Qt 6.5.3
     - Android ARMv7 (armeabi-v7a)
     - Android ARM64 (arm64-v8a) - optional but recommended
     - Qt Quick components (included by default)

3. **Android NDK 27.2.12479018**
   - Install via Android Studio SDK Manager or during Qt installation

4. **Java Development Kit (JDK) 17**
   - Install via Android Studio or download from: https://adoptium.net/

## Setup Instructions

### 1. Install Android Studio
1. Download and install Android Studio
2. Open Android Studio and complete the setup wizard
3. Go to **Tools > SDK Manager**
4. In the **SDK Platforms** tab, install:
   - Android 13.0 (Tiramisu) API Level 33 or higher
5. In the **SDK Tools** tab, install:
   - Android SDK Build-Tools
   - Android SDK Command-line Tools
   - Android NDK (Side by side) version 27.2.12479018
   - CMake 3.18.1 or higher

### 2. Install Qt 6.5.3
1. Run the Qt Online Installer
2. Sign in or create a Qt account (free for open source)
3. Select components:
   ```
   Qt 6.5.3
     ├── Android ARMv7 (armeabi-v7a)
     ├── Android ARM64 (arm64-v8a)  [recommended]
     └── Desktop gcc_64 (for host tools)
   ```
4. Note the installation path (e.g., `/opt/Qt/6.5.3` on Linux, `C:\Qt\6.5.3` on Windows)

### 3. Set Environment Variables

#### Linux/macOS
Add to your `~/.bashrc` or `~/.zshrc`:
```bash
export ANDROID_SDK_ROOT=$HOME/Android/Sdk
export ANDROID_NDK_ROOT=$ANDROID_SDK_ROOT/ndk/27.2.12479018
export QT_ANDROID_ROOT=/path/to/Qt/6.5.3/android_armv7
export QT_HOST_PATH=/path/to/Qt/6.5.3/gcc_64
export PATH=$QT_ANDROID_ROOT/bin:$QT_HOST_PATH/bin:$PATH
```

#### Windows
1. Open System Properties > Environment Variables
2. Add/Update:
   ```
   ANDROID_SDK_ROOT = C:\Users\YourName\AppData\Local\Android\Sdk
   ANDROID_NDK_ROOT = %ANDROID_SDK_ROOT%\ndk\27.2.12479018
   QT_ANDROID_ROOT = C:\Qt\6.5.3\android_armv7
   QT_HOST_PATH = C:\Qt\6.5.3\mingw_64
   ```
3. Add to PATH: `%QT_ANDROID_ROOT%\bin;%QT_HOST_PATH%\bin`

### 4. Build libsodium for Android

CipherMesh requires libsodium compiled for Android. You have two options:

#### Option A: Use Pre-built Binaries (Easier)
1. Download from: https://github.com/jedisct1/libsodium/releases
2. Extract to a location like `~/libsodium-android`

#### Option B: Build from Source
1. Clone libsodium:
   ```bash
   git clone --depth 1 --branch stable https://github.com/jedisct1/libsodium.git
   cd libsodium
   ```

2. Build for armeabi-v7a:
   ```bash
   export NDK=$ANDROID_NDK_ROOT
   export TOOLCHAIN=$NDK/toolchains/llvm/prebuilt/linux-x86_64  # or darwin-x86_64 for macOS
   export API=21
   export TARGET=armv7a-linux-androideabi
   
   export CC=$TOOLCHAIN/bin/${TARGET}${API}-clang
   export CXX=$TOOLCHAIN/bin/${TARGET}${API}-clang++
   export AR=$TOOLCHAIN/bin/llvm-ar
   export RANLIB=$TOOLCHAIN/bin/llvm-ranlib
   
   ./autogen.sh
   ./configure \
     --host=armv7a-linux-androideabi \
     --prefix=$HOME/libsodium-android \
     --disable-shared \
     --enable-static \
     --with-pic
   
   make -j$(nproc)
   make install
   ```

## Building the APK

### Method 1: Using Qt Creator (Recommended)

Qt Creator provides the best integration for Qt-based Android projects.

1. **Open Qt Creator**
   - Launch Qt Creator (comes with Qt installation)
   - File > Open File or Project
   - Select `CMakeLists.txt` in the CipherMesh root directory

2. **Configure Project**
   - Select the Android kit (armeabi-v7a or arm64-v8a)
   - Click "Configure Project"

3. **Set CMake Arguments**
   In the Projects tab > Build Settings > CMake, add:
   ```
   -DANDROID_SDK_ROOT=/path/to/Android/Sdk
   -DANDROID_NDK_ROOT=/path/to/Android/Sdk/ndk/27.2.12479018
   -DANDROID_ABI=armeabi-v7a
   -Dsodium_INCLUDE_DIR=/path/to/libsodium-android/include
   -Dsodium_LIBRARY_RELEASE=/path/to/libsodium-android/lib/libsodium.a
   -DQT_HOST_PATH=/path/to/Qt/6.5.3/gcc_64
   ```

4. **Build**
   - Click the Build button (hammer icon) or press Ctrl+B
   - Select the target: `CipherMesh-Mobile_make_apk`

5. **Deploy**
   - Connect your Android device via USB (with USB debugging enabled)
   - Click the Run button (play icon) or press Ctrl+R
   - The APK will be built, deployed, and launched on your device

### Method 2: Using Command Line

1. **Create Build Directory**
   ```bash
   cd /path/to/CipherMesh
   mkdir -p build-android
   cd build-android
   ```

2. **Configure with CMake**
   ```bash
   qt-cmake .. \
     -DQT_HOST_PATH=$QT_HOST_PATH \
     -DANDROID_SDK_ROOT=$ANDROID_SDK_ROOT \
     -DANDROID_NDK_ROOT=$ANDROID_NDK_ROOT \
     -DANDROID_ABI=armeabi-v7a \
     -Dsodium_INCLUDE_DIR=/path/to/libsodium-android/include \
     -Dsodium_LIBRARY_RELEASE=/path/to/libsodium-android/lib/libsodium.a
   ```

3. **Build APK**
   ```bash
   cmake --build . --target CipherMesh-Mobile_make_apk
   ```

4. **Install APK**
   The APK will be generated in `build-android/android-build/build/outputs/apk/debug/`
   
   Install on device:
   ```bash
   adb install android-build/build/outputs/apk/debug/android-build-debug.apk
   ```

### Method 3: Import into Android Studio (Alternative)

While Qt projects work best with Qt Creator, you can also use Android Studio:

1. **Generate Gradle Project**
   First build using command line method to generate the gradle files

2. **Import in Android Studio**
   - Open Android Studio
   - File > Open
   - Navigate to `build-android/android-build`
   - Click OK

3. **Build**
   - Build > Make Project
   - Run > Run 'app'

## Troubleshooting

### Common Issues

**1. "Could not find Qt6::Quick"**
- Ensure Qt 6.5.3 for Android is installed correctly
- Verify QT_ANDROID_ROOT points to correct Qt installation

**2. "libsodium not found"**
- Check that sodium_INCLUDE_DIR and sodium_LIBRARY_RELEASE paths are correct
- Ensure libsodium.a exists at the specified path

**3. "NDK not found"**
- Verify ANDROID_NDK_ROOT environment variable
- Install NDK 27.2.12479018 via Android Studio SDK Manager

**4. "ABI mismatch"**
- Ensure Qt Android installation matches ANDROID_ABI (armeabi-v7a or arm64-v8a)
- libsodium must be compiled for the same ABI

**5. Build errors with mbedtls or libdatachannel**
- These are fetched automatically by CMake
- Ensure you have internet connection during first build
- Clear CMake cache and rebuild if issues persist

### Clean Build

If you encounter issues, try a clean build:
```bash
rm -rf build-android
mkdir build-android
cd build-android
# Run qt-cmake command again
```

## Testing the App

### On Physical Device
1. Enable Developer Options on your Android device
2. Enable USB Debugging
3. Connect device via USB
4. Run `adb devices` to verify connection
5. Deploy using Qt Creator or `adb install`

### On Emulator
1. Open Android Studio AVD Manager
2. Create a new virtual device (API 33 or higher recommended)
3. Start the emulator
4. Deploy using Qt Creator or `adb install`

## App Features

The mobile app includes all desktop features:
- ✅ Password vault with encryption
- ✅ Groups/folders organization
- ✅ Password generator (customizable)
- ✅ TOTP 2FA code generation
- ✅ Password strength indicator
- ✅ Breach checker (HaveIBeenPwned)
- ✅ Password history tracking
- ✅ Multiple locations per entry
- ✅ Secure notes support
- ✅ Search & filter
- ✅ P2P sharing via WebRTC
- ✅ Auto-lock timer
- ✅ Master password change
- ✅ Dark theme
- ✅ Clipboard operations

## Additional Resources

- Qt for Android documentation: https://doc.qt.io/qt-6/android.html
- Qt Creator manual: https://doc.qt.io/qtcreator/
- Android Developer docs: https://developer.android.com/docs
- CMake documentation: https://cmake.org/documentation/

## Need Help?

If you encounter issues:
1. Check the troubleshooting section above
2. Ensure all prerequisites are installed correctly
3. Verify environment variables are set
4. Try a clean build
5. Check Qt Creator's "Issues" panel for detailed error messages
