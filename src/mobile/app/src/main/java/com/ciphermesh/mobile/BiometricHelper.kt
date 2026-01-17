package com.ciphermesh.mobile

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import android.util.Log
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import androidx.fragment.app.FragmentActivity
import java.security.KeyStore
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/**
 * BiometricHelper - Manages biometric authentication and secure password storage
 * Uses Android Keystore to encrypt/decrypt the master password
 */
class BiometricHelper(private val context: Context) {
    
    companion object {
        private const val KEYSTORE_ALIAS = "CipherMeshBiometricKey"
        private const val ANDROID_KEYSTORE = "AndroidKeyStore"
        private const val PREFS_NAME = "biometric_prefs"
        private const val PREF_ENCRYPTED_PASSWORD = "encrypted_password"
        private const val PREF_IV = "encryption_iv"
        private const val PREF_BIOMETRIC_ENABLED = "biometric_enabled"
    }
    
    private val keyStore: KeyStore = KeyStore.getInstance(ANDROID_KEYSTORE).apply {
        load(null)
    }
    
    /**
     * Check if biometric authentication is available on this device
     */
    fun isBiometricAvailable(): Boolean {
        val biometricManager = BiometricManager.from(context)
        return when (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)) {
            BiometricManager.BIOMETRIC_SUCCESS -> true
            else -> false
        }
    }
    
    /**
     * Check if biometric unlock is enabled by user
     */
    fun isBiometricEnabled(): Boolean {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        return prefs.getBoolean(PREF_BIOMETRIC_ENABLED, false)
    }
    
    /**
     * Enable biometric unlock and encrypt the master password
     */
    fun enableBiometric(password: String): Boolean {
        try {
            // Generate or get the key
            val secretKey = getOrCreateKey()
            
            // Encrypt the password
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            cipher.init(Cipher.ENCRYPT_MODE, secretKey)
            val encryptedPassword = cipher.doFinal(password.toByteArray(Charsets.UTF_8))
            val iv = cipher.iv
            
            // Store encrypted password and IV
            val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            prefs.edit().apply {
                putString(PREF_ENCRYPTED_PASSWORD, Base64.encodeToString(encryptedPassword, Base64.NO_WRAP))
                putString(PREF_IV, Base64.encodeToString(iv, Base64.NO_WRAP))
                putBoolean(PREF_BIOMETRIC_ENABLED, true)
                apply()
            }
            
            return true
        } catch (e: Exception) {
            e.printStackTrace()
            return false
        }
    }
    
    /**
     * Disable biometric unlock and clear stored password
     */
    fun disableBiometric() {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        prefs.edit().apply {
            remove(PREF_ENCRYPTED_PASSWORD)
            remove(PREF_IV)
            putBoolean(PREF_BIOMETRIC_ENABLED, false)
            apply()
        }
        
        // Delete the key from keystore
        try {
            keyStore.deleteEntry(KEYSTORE_ALIAS)
        } catch (e: Exception) {
            e.printStackTrace()
        }
    }
    
    /**
     * Show biometric prompt and decrypt password on success
     */
    fun authenticate(
        activity: FragmentActivity,
        onSuccess: (String) -> Unit,
        onError: (String) -> Unit
    ) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        val encryptedPasswordStr = prefs.getString(PREF_ENCRYPTED_PASSWORD, null)
        val ivStr = prefs.getString(PREF_IV, null)
        
        if (encryptedPasswordStr == null || ivStr == null) {
            onError("No encrypted password found")
            return
        }
        
        try {
            val secretKey = getOrCreateKey()
            val cipher = Cipher.getInstance("AES/GCM/NoPadding")
            val iv = Base64.decode(ivStr, Base64.NO_WRAP)
            val spec = GCMParameterSpec(128, iv)
            cipher.init(Cipher.DECRYPT_MODE, secretKey, spec)
            
            val promptInfo = BiometricPrompt.PromptInfo.Builder()
                .setTitle("Unlock CipherMesh")
                .setSubtitle("Use your fingerprint or face to unlock")
                .setNegativeButtonText("Use Password")
                .build()
            
            // Use WeakReference to prevent memory leaks
            val activityRef = java.lang.ref.WeakReference(activity)
            
            val biometricPrompt = BiometricPrompt(activity,
                ContextCompat.getMainExecutor(context),
                object : BiometricPrompt.AuthenticationCallback() {
                    override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                        super.onAuthenticationSucceeded(result)
                        val act = activityRef.get()
                        if (act == null || act.isFinishing || act.isDestroyed) {
                            Log.w("BiometricHelper", "Activity destroyed, ignoring biometric success")
                            return
                        }
                        try {
                            val encryptedPassword = Base64.decode(encryptedPasswordStr, Base64.NO_WRAP)
                            val decryptedPassword = cipher.doFinal(encryptedPassword)
                            val password = String(decryptedPassword, Charsets.UTF_8)
                            onSuccess(password)
                        } catch (e: Exception) {
                            onError("Failed to decrypt password: ${e.message}")
                        }
                    }
                    
                    override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                        super.onAuthenticationError(errorCode, errString)
                        val act = activityRef.get()
                        if (act == null || act.isFinishing || act.isDestroyed) return
                        onError(errString.toString())
                    }
                    
                    override fun onAuthenticationFailed() {
                        super.onAuthenticationFailed()
                        val act = activityRef.get()
                        if (act == null || act.isFinishing || act.isDestroyed) return
                        onError("Authentication failed")
                    }
                }
            )
            
            biometricPrompt.authenticate(promptInfo)
            
        } catch (e: Exception) {
            onError("Error setting up biometric: ${e.message}")
        }
    }
    
    /**
     * Get or create the encryption key in Android Keystore
     */
    private fun getOrCreateKey(): SecretKey {
        // Check if key exists
        if (keyStore.containsAlias(KEYSTORE_ALIAS)) {
            try {
                val entry = keyStore.getEntry(KEYSTORE_ALIAS, null) as? KeyStore.SecretKeyEntry
                if (entry != null) {
                    return entry.secretKey
                }
                // Key is corrupted, delete and recreate
                Log.w("BiometricHelper", "Key entry corrupted, deleting...")
                keyStore.deleteEntry(KEYSTORE_ALIAS)
            } catch (e: Exception) {
                Log.e("BiometricHelper", "Error retrieving key: ${e.message}", e)
                // Delete corrupted key
                try {
                    keyStore.deleteEntry(KEYSTORE_ALIAS)
                } catch (deleteError: Exception) {
                    Log.e("BiometricHelper", "Error deleting corrupted key: ${deleteError.message}")
                }
            }
        }
        
        // Generate new key
        val keyGenerator = KeyGenerator.getInstance(
            KeyProperties.KEY_ALGORITHM_AES,
            ANDROID_KEYSTORE
        )
        
        val keyGenParameterSpec = KeyGenParameterSpec.Builder(
            KEYSTORE_ALIAS,
            KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT
        )
            .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
            .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
            .setUserAuthenticationRequired(false) // Allow access without user auth for encryption
            .build()
        
        keyGenerator.init(keyGenParameterSpec)
        return keyGenerator.generateKey()
    }
}
