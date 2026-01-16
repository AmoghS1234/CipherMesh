# Mobile App Completion Guide

## Current State

The mobile app already has:
- ✅ Kotlin/Java implementation in `app/src/main/java/`
- ✅ TOTP generation (`com.ciphermesh.util.TotpUtil`)
- ✅ P2P Manager (`com.ciphermesh.mobile.p2p.P2PManager`)
- ✅ Vault database (`com.ciphermesh.mobile.data.VaultDatabase`)
- ✅ Entry management (`HomeActivity.kt`, `EntryModel.kt`)
- ✅ Native crypto bridge (`CryptoBridge.kt`)

## Missing Features (Needs Implementation)

### 1. Password Generator Service

Create `app/src/main/java/com/ciphermesh/util/PasswordGenerator.kt`:

```kotlin
package com.ciphermesh.util

import kotlin.random.Random

object PasswordGenerator {
    fun generate(
        length: Int = 16,
        useUppercase: Boolean = true,
        useLowercase: Boolean = true,
        useNumbers: Boolean = true,
        useSymbols: Boolean = true,
        symbols: String = "!@#$%^&*()_+-=[]{}|;:,.<>?"
    ): String {
        val chars = buildString {
            if (useUppercase) append("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
            if (useLowercase) append("abcdefghijklmnopqrstuvwxyz")
            if (useNumbers) append("0123456789")
            if (useSymbols) append(symbols)
        }
        
        if (chars.isEmpty() || length < 4) return ""
        
        return (1..length)
            .map { chars[Random.nextInt(chars.length)] }
            .joinToString("")
    }
    
    fun calculateStrength(password: String): Int {
        // Use the same algorithm as desktop PasswordStrengthCalculator
        // Returns 0-100 score
        
        val hasUpper = password.any { it.isUpperCase() }
        val hasLower = password.any { it.isLowerCase() }
        val hasDigit = password.any { it.isDigit() }
        val hasSymbol = password.any { !it.isLetterOrDigit() }
        
        val varietyScore = (if (hasUpper) 1 else 0) + 
                          (if (hasLower) 1 else 0) + 
                          (if (hasDigit) 1 else 0) + 
                          (if (hasSymbol) 1 else 0)
        
        val lengthScore = when {
            password.length < 8 -> 0
            password.length < 12 -> 1
            password.length < 16 -> 2
            else -> 3
        }
        
        val entropy = calculateEntropy(password)
        val entropyScore = when {
            entropy < 30 -> 0
            entropy < 50 -> 1
            entropy < 70 -> 2
            else -> 3
        }
        
        val totalScore = ((varietyScore + lengthScore + entropyScore) * 100) / 10
        return totalScore.coerceIn(0, 100)
    }
    
    private fun calculateEntropy(password: String): Int {
        val charsetSize = when {
            password.any { it.isUpperCase() } && 
            password.any { it.isLowerCase() } && 
            password.any { it.isDigit() } && 
            password.any { !it.isLetterOrDigit() } -> 94  // Full set
            
            password.any { it.isLetter() } && 
            password.any { it.isDigit() } -> 62  // Letters + numbers
            
            password.any { it.isLetter() } -> 52  // Just letters
            else -> 10  // Just numbers
        }
        
        return (password.length * kotlin.math.log2(charsetSize.toDouble())).toInt()
    }
    
    fun getStrengthText(score: Int): String = when {
        score < 20 -> "Very Weak"
        score < 40 -> "Weak"
        score < 60 -> "Fair"
        score < 80 -> "Strong"
        else -> "Very Strong"
    }
    
    fun getStrengthColor(score: Int): Int = when {
        score < 20 -> 0xFFDC3545.toInt()  // Red
        score < 40 -> 0xFFFD7E14.toInt()  // Orange
        score < 60 -> 0xFFFFC107.toInt()  // Yellow
        score < 80 -> 0xFF28A745.toInt()  // Light green
        else -> 0xFF198754.toInt()        // Dark green
    }
}
```

### 2. Breach Checker Service

Create `app/src/main/java/com/ciphermesh/util/BreachChecker.kt`:

```kotlin
package com.ciphermesh.util

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okhttp3.OkHttpClient
import okhttp3.Request
import java.security.MessageDigest

object BreachChecker {
    private val client = OkHttpClient()
    
    suspend fun checkPassword(password: String): Pair<Boolean, Int> = withContext(Dispatchers.IO) {
        try {
            val sha1 = sha1Hash(password)
            val prefix = sha1.substring(0, 5)
            val suffix = sha1.substring(5)
            
            val request = Request.Builder()
                .url("https://api.pwnedpasswords.com/range/$prefix")
                .build()
            
            val response = client.newCall(request).execute()
            if (!response.isSuccessful) return@withContext Pair(false, -1)
            
            val body = response.body?.string() ?: return@withContext Pair(false, -1)
            
            body.lines().forEach { line ->
                val parts = line.split(":")
                if (parts.size >= 2 && parts[0].trim().equals(suffix, ignoreCase = true)) {
                    return@withContext Pair(true, parts[1].trim().toInt())
                }
            }
            
            Pair(false, 0)  // Not breached
        } catch (e: Exception) {
            Pair(false, -1)  // Error
        }
    }
    
    private fun sha1Hash(input: String): String {
        val bytes = MessageDigest.getInstance("SHA-1").digest(input.toByteArray())
        return bytes.joinToString("") { "%02X".format(it) }
    }
}
```

### 3. QR Scanner Activity

Create `app/src/main/java/com/ciphermesh/mobile/QRScannerActivity.kt`:

```kotlin
package com.ciphermesh.mobile

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.camera.core.*
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import com.google.mlkit.vision.barcode.BarcodeScanning
import com.google.mlkit.vision.common.InputImage

class QRScannerActivity : ComponentActivity() {
    
    private var cameraProvider: ProcessCameraProvider? = null
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        if (checkCameraPermission()) {
            startCamera()
        } else {
            requestCameraPermission()
        }
    }
    
    private fun checkCameraPermission(): Boolean {
        return ContextCompat.checkSelfPermission(
            this,
            Manifest.permission.CAMERA
        ) == PackageManager.PERMISSION_GRANTED
    }
    
    private fun requestCameraPermission() {
        ActivityCompat.requestPermissions(
            this,
            arrayOf(Manifest.permission.CAMERA),
            CAMERA_PERMISSION_CODE
        )
    }
    
    private fun startCamera() {
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        
        cameraProviderFuture.addListener({
            cameraProvider = cameraProviderFuture.get()
            bindCameraUseCases()
        }, ContextCompat.getMainExecutor(this))
    }
    
    private fun bindCameraUseCases() {
        val preview = Preview.Builder().build()
        val imageAnalysis = ImageAnalysis.Builder()
            .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
            .build()
            .also {
                it.setAnalyzer(ContextCompat.getMainExecutor(this)) { imageProxy ->
                    processImageProxy(imageProxy)
                }
            }
        
        val cameraSelector = CameraSelector.DEFAULT_BACK_CAMERA
        
        try {
            cameraProvider?.unbindAll()
            cameraProvider?.bindToLifecycle(
                this,
                cameraSelector,
                preview,
                imageAnalysis
            )
        } catch (e: Exception) {
            // Handle error
        }
    }
    
    @androidx.camera.core.ExperimentalGetImage
    private fun processImageProxy(imageProxy: ImageProxy) {
        val mediaImage = imageProxy.image
        if (mediaImage != null) {
            val image = InputImage.fromMediaImage(
                mediaImage,
                imageProxy.imageInfo.rotationDegrees
            )
            
            val scanner = BarcodeScanning.getClient()
            scanner.process(image)
                .addOnSuccessListener { barcodes ->
                    barcodes.firstOrNull()?.rawValue?.let { qrCode ->
                        // Handle QR code (user ID or invite data)
                        handleQRCode(qrCode)
                    }
                }
                .addOnCompleteListener {
                    imageProxy.close()
                }
        } else {
            imageProxy.close()
        }
    }
    
    private fun handleQRCode(qrCode: String) {
        // Return result to calling activity
        setResult(RESULT_OK, intent.apply {
            putExtra("QR_CODE", qrCode)
        })
        finish()
    }
    
    companion object {
        private const val CAMERA_PERMISSION_CODE = 100
    }
}
```

### 4. Biometric Authentication

Create `app/src/main/java/com/ciphermesh/mobile/BiometricHelper.kt`:

```kotlin
package com.ciphermesh.mobile

import android.content.Context
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity

object BiometricHelper {
    
    fun isBiometricAvailable(context: Context): Boolean {
        val biometricManager = BiometricManager.from(context)
        return when (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)) {
            BiometricManager.BIOMETRIC_SUCCESS -> true
            else -> false
        }
    }
    
    fun authenticate(
        activity: FragmentActivity,
        title: String = "Unlock CipherMesh",
        subtitle: String = "Use your fingerprint or face",
        onSuccess: () -> Unit,
        onError: (String) -> Unit
    ) {
        val executor = ContextCompat.getMainExecutor(activity)
        
        val biometricPrompt = BiometricPrompt(activity, executor,
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    super.onAuthenticationError(errorCode, errString)
                    onError(errString.toString())
                }
                
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    super.onAuthenticationSucceeded(result)
                    onSuccess()
                }
                
                override fun onAuthenticationFailed() {
                    super.onAuthenticationFailed()
                    onError("Authentication failed")
                }
            })
        
        val promptInfo = BiometricPrompt.PromptInfo.Builder()
            .setTitle(title)
            .setSubtitle(subtitle)
            .setNegativeButtonText("Use Master Password")
            .build()
        
        biometricPrompt.authenticate(promptInfo)
    }
}
```

### 5. Autofill Service

Create `app/src/main/java/com/ciphermesh/mobile/AutofillService.kt`:

```kotlin
package com.ciphermesh.mobile

import android.app.assist.AssistStructure
import android.os.CancellationSignal
import android.service.autofill.*
import android.view.autofill.AutofillValue
import android.widget.RemoteViews

class CipherMeshAutofillService : AutofillService() {
    
    override fun onFillRequest(
        request: FillRequest,
        cancellationSignal: CancellationSignal,
        callback: FillCallback
    ) {
        val structure = request.fillContexts.last().structure
        val parsedStructure = parseStructure(structure)
        
        if (parsedStructure == null) {
            callback.onSuccess(null)
            return
        }
        
        // Get matching entries from vault
        val entries = getMatchingEntries(parsedStructure.domain)
        
        val response = FillResponse.Builder()
        entries.forEach { entry ->
            val dataset = createDataset(entry, parsedStructure)
            response.addDataset(dataset)
        }
        
        callback.onSuccess(response.build())
    }
    
    override fun onSaveRequest(request: SaveRequest, callback: SaveCallback) {
        // Handle saving new credentials
        callback.onSuccess()
    }
    
    private fun parseStructure(structure: AssistStructure): ParsedStructure? {
        // Parse the structure to find username/password fields
        // Extract domain/package name
        return null  // Implement parsing logic
    }
    
    private fun getMatchingEntries(domain: String?): List<VaultEntry> {
        // Query vault database for matching entries
        return emptyList()  // Implement vault query
    }
    
    private fun createDataset(entry: VaultEntry, structure: ParsedStructure): Dataset {
        val presentation = RemoteViews(packageName, android.R.layout.simple_list_item_1)
        presentation.setTextViewText(android.R.id.text1, entry.username)
        
        return Dataset.Builder()
            .setValue(
                structure.usernameId,
                AutofillValue.forText(entry.username),
                presentation
            )
            .setValue(
                structure.passwordId,
                AutofillValue.forText(entry.password),
                presentation
            )
            .build()
    }
    
    private data class ParsedStructure(
        val domain: String?,
        val usernameId: AutofillId,
        val passwordId: AutofillId
    )
    
    private data class VaultEntry(
        val username: String,
        val password: String
    )
}
```

## Required Gradle Dependencies

Add to `app/build.gradle.kts`:

```kotlin
dependencies {
    // Existing dependencies...
    
    // For QR Scanner
    implementation("com.google.mlkit:barcode-scanning:17.2.0")
    implementation("androidx.camera:camera-camera2:1.3.0")
    implementation("androidx.camera:camera-lifecycle:1.3.0")
    implementation("androidx.camera:camera-view:1.3.0")
    
    // For Biometric
    implementation("androidx.biometric:biometric:1.2.0-alpha05")
    
    // For Breach Checker (HTTP)
    implementation("com.squareup.okhttp3:okhttp:4.12.0")
    
    // For Coroutines
    implementation("org.jetbrains.kotlinx:kotlinx-coroutines-android:1.7.3")
}
```

## AndroidManifest.xml Updates

Add to `app/src/main/AndroidManifest.xml`:

```xml
<manifest>
    <!-- Permissions -->
    <uses-permission android:name="android.permission.CAMERA" />
    <uses-permission android:name="android.permission.INTERNET" />
    <uses-permission android:name="android.permission.USE_BIOMETRIC" />
    
    <application>
        <!-- QR Scanner Activity -->
        <activity
            android:name=".QRScannerActivity"
            android:theme="@style/Theme.AppCompat.NoActionBar"
            android:exported="false" />
        
        <!-- Autofill Service -->
        <service
            android:name=".CipherMeshAutofillService"
            android:permission="android.permission.BIND_AUTOFILL_SERVICE"
            android:exported="true">
            <intent-filter>
                <action android:name="android.service.autofill.AutofillService" />
            </intent-filter>
            <meta-data
                android:name="android.autofill"
                android:resource="@xml/autofill_service" />
        </service>
    </application>
</manifest>
```

## Testing Checklist

After implementation:
- [ ] Password generator creates strong passwords
- [ ] Password strength calculation matches desktop
- [ ] TOTP codes match desktop (same secret = same code)
- [ ] Breach checker successfully queries HaveIBeenPwned API
- [ ] QR scanner can read QR codes from desktop app
- [ ] Biometric unlock works on supported devices
- [ ] Autofill service fills credentials in browsers/apps
- [ ] P2P sharing works between mobile and desktop

## Architecture Consistency

Ensure:
- ✅ Mobile TOTP uses same algorithm as desktop (30s interval, SHA1)
- ✅ Password generation uses same character sets as desktop
- ✅ Encryption parameters match desktop (Argon2id + XChaCha20-Poly1305)
- ✅ Database schema matches desktop
- ✅ WebRTC signaling messages match desktop protocol
- ✅ P2P invite/accept flow matches desktop
