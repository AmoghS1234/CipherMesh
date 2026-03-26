# CipherMesh 🔐

![Platform: Android & Linux](https://img.shields.io/badge/Platform-Android%20%7C%20Desktop-brightgreen)
![Core: C++](https://img.shields.io/badge/Core-C%2B%2B-blue)
![Networking: WebRTC](https://img.shields.io/badge/Networking-WebRTC-orange)
![Encryption: libsodium](https://img.shields.io/badge/Encryption-libsodium-red)

**CipherMesh** is a secure, decentralized peer-to-peer (P2P) password and credential management system. 

Unlike traditional password managers that store your most sensitive data on centralized cloud servers, CipherMesh operates as a strictly local, encrypted vault. When you need to sync your passwords between your phone and your computer, CipherMesh uses WebRTC data channels to establish a direct, encrypted P2P connection, transferring your data locally and securely without a middleman.

## ✨ Key Features

* **Decentralized P2P Syncing:** Zero-knowledge, serverless synchronization. Devices connect and sync directly via WebRTC (utilizing ICE/STUN for NAT traversal).
* **End-to-End Encryption:** Powered by `libsodium`, all vault data, group keys, and passwords are encrypted locally before any network transmission occurs.
* **Group-Based Access Control:** Organize credentials into distinct groups (e.g., "Personal", "Work"). You can securely invite other devices/peers to specific groups and sync only that group's data.
* **Shared Native Core:** A highly optimized C++ core handles all cryptography, database management (SQLite), and WebRTC networking, ensuring identical logic across all platforms.
* **Rich Credential Management:** Store passwords, usernames, URLs, and notes. Includes built-in local TOTP (Time-Based One-Time Password) generation and password history tracking.

## 🛠️ Technology Stack

* **Core Logic:** C++17/20
* **Cryptography:** `libsodium`
* **Database:** SQLite3
* **Networking:** WebRTC (P2P Data Channels & Signaling)
* **Desktop Client:** Qt6 (C++)
* **Mobile Client:** Android NDK, Kotlin, JNI

## 🚀 Building from Source

### Prerequisites
* CMake (3.15+)
* Android Studio & NDK (for Mobile)
* Qt6 (for Desktop)

### Desktop (Linux)
```bash
# Navigate to the desktop source
cd CipherMesh/src/desktop

# Configure and build
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

# Run the application
./CipherMesh-Desktop
```
### Mobile (Android)
```bash
# Navigate to the mobile source
cd CipherMesh/src/mobile

# Build the Debug APK using Gradle
./gradlew assembleDebug

# The APK will be generated at:
# app/build/outputs/apk/debug/app-debug.apk
```
