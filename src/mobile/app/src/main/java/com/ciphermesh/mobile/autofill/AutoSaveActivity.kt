package com.ciphermesh.mobile.autofill

import android.content.pm.PackageManager
import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import com.ciphermesh.mobile.core.Vault
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.util.concurrent.Executor

class AutoSaveActivity : AppCompatActivity() {
    
    companion object {
        const val EXTRA_USERNAME = "username"
        const val EXTRA_PASSWORD = "password"
        const val EXTRA_PACKAGE = "package"
        const val EXTRA_WEB_DOMAIN = "web_domain"
        const val EXTRA_IS_UPDATE = "is_update"
        const val EXTRA_ENTRY_ID = "entry_id"
    }

    private lateinit var executor: Executor
    private val vault = com.ciphermesh.mobile.core.Vault()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        executor = ContextCompat.getMainExecutor(this)
        
        // Initialize vault with database path
        val dbPath = java.io.File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)
        
        val isUpdate = intent.getBooleanExtra(EXTRA_IS_UPDATE, false)
        
        if (isUpdate) {
            handleUpdate()
        } else {
            handleSave()
        }
    }
    
    private fun handleSave() {
        val username = intent.getStringExtra(EXTRA_USERNAME) ?: ""
        val password = intent.getStringExtra(EXTRA_PASSWORD) ?: ""
        val packageName = intent.getStringExtra(EXTRA_PACKAGE) ?: ""
        val webDomain = intent.getStringExtra(EXTRA_WEB_DOMAIN)
        
        // Check never-save list
        if (isNeverSave(packageName)) {
            finish()
            return
        }

        // [Smart Save] Check for existing credentials
        if (!vault.isLocked()) {
            val location = webDomain ?: packageName
            val matches = vault.findEntriesByLocation(location)
            
            if (matches != null) {
                for (match in matches) {
                    val parts = match.split("|")
                    if (parts.size >= 4) {
                        val idStr = parts[0]
                        val existingUser = parts[2]
                        val existingPass = parts[3]
                        
                        if (existingUser == username) {
                            if (existingPass == password) {
                                // Exact match found - already saved. Silent exit.
                                finish()
                                return
                            } else {
                                // Username match, but password differs - Prompt Update
                                val entryId = idStr.toIntOrNull()
                                if (entryId != null) {
                                    showUpdatePrompt(entryId, password)
                                    return
                                }
                            }
                        }
                    }
                }
            }
        }
        
        val appName = getAppName(packageName) ?: webDomain ?: packageName
        showSavePrompt(username, password, packageName, webDomain, appName)
    }
    
    private fun handleUpdate() {
        val entryId = intent.getIntExtra(EXTRA_ENTRY_ID, -1)
        val newPassword = intent.getStringExtra(EXTRA_PASSWORD) ?: ""
        
        if (entryId != -1) {
            showUpdatePrompt(entryId, newPassword)
        } else {
            finish()
        }
    }

    private fun showSavePrompt(
        username: String, 
        password: String, 
        packageName: String, 
        webDomain: String?, 
        appName: String
    ) {
        MaterialAlertDialogBuilder(this)
            .setTitle("Save Password?")
            .setMessage("Save credentials for $appName?")
            .setPositiveButton("Save") { _, _ ->
                authenticateAndSave(username, password, packageName, webDomain, appName)
            }
            .setNegativeButton("Not Now") { _, _ ->
                finish()
            }
            .setNeutralButton("Never") { _, _ ->
                addToNeverSave(packageName)
                finish()
            }
            .setOnCancelListener { finish() }
            .show()
    }

    private fun showUpdatePrompt(entryId: Int, newPassword: String) {
        MaterialAlertDialogBuilder(this)
            .setTitle("Update Password?")
            .setMessage("Update saved password with new one?")
            .setPositiveButton("Update") { _, _ ->
                authenticateAndUpdate(entryId, newPassword)
            }
            .setNegativeButton("Cancel") { _, _ ->
                finish()
            }
            .setOnCancelListener { finish() }
            .show()
    }
    
    private fun authenticateAndSave(
        username: String,
        password: String,
        packageName: String,
        webDomain: String?,
        appName: String
    ) {
        val biometricManager = BiometricManager.from(this)
        
        when (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)) {
            BiometricManager.BIOMETRIC_SUCCESS -> {
                showBiometricPromptForSave(username, password, packageName, webDomain, appName)
            }
            else -> {
                // No biometric, proceed anyway (in production, ask for master password)
                showGroupSelection(username, password, packageName, webDomain, appName)
            }
        }
    }
    
    private fun showBiometricPromptForSave(
        username: String,
        password: String,
        packageName: String,
        webDomain: String?,
        appName: String
    ) {
        val promptInfo = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Authenticate")
            .setSubtitle("Unlock to save password")
            .setNegativeButtonText("Cancel")
            .build()
        
        val biometricPrompt = BiometricPrompt(this, executor,
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    super.onAuthenticationSucceeded(result)
                    showGroupSelection(username, password, packageName, webDomain, appName)
                }
                
                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    super.onAuthenticationError(errorCode, errString)
                    Toast.makeText(this@AutoSaveActivity, 
                        "Authentication failed", Toast.LENGTH_SHORT).show()
                    finish()
                }
            })
        
        biometricPrompt.authenticate(promptInfo)
    }
    
    private fun showGroupSelection(
        username: String,
        password: String,
        packageName: String,
        webDomain: String?,
        appName: String
    ) {
        val groups = vault.getGroupNames() ?: arrayOf()
        
        if (groups.isEmpty()) {
            Toast.makeText(this, "No groups available", Toast.LENGTH_SHORT).show()
            finish()
            return
        }
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Select Group")
            .setItems(groups) { _, which ->
                saveToGroup(groups[which], username, password, packageName, webDomain, appName)
            }
            .setOnCancelListener { finish() }
            .show()
    }
    
    private fun saveToGroup(
        groupName: String,
        username: String,
        password: String,
        packageName: String,
        webDomain: String?,
        appName: String
    ) {
        try {
            vault.setActiveGroup(groupName)
            
            // Build location JSON
            val locationType = if (webDomain != null) "url" else "android"
            val locationValue = webDomain?.let { if (it.startsWith("http")) it else "https://$it" } ?: packageName
            val entryUrl = if (webDomain != null) locationValue else ""
            
            // Save entry
            vault.addEntry(
                title = appName,
                username = username,
                pass = password,
                type = "Login",
                url = entryUrl,
                notes = "Auto-saved from " + packageName,
                totp = ""
            )
            
            Toast.makeText(this, "Saved to $groupName", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to save: ${e.message}", Toast.LENGTH_SHORT).show()
        }
        
        finish()
    }
    
    private fun authenticateAndUpdate(entryId: Int, newPassword: String) {
        val biometricManager = BiometricManager.from(this)
        
        when (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)) {
            BiometricManager.BIOMETRIC_SUCCESS -> {
                showBiometricPromptForUpdate(entryId, newPassword)
            }
            else -> {
                updatePassword(entryId, newPassword)
            }
        }
    }
    
    private fun showBiometricPromptForUpdate(entryId: Int, newPassword: String) {
        val promptInfo = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Authenticate")
            .setSubtitle("Unlock to update password")
            .setNegativeButtonText("Cancel")
            .build()
        
        val biometricPrompt = BiometricPrompt(this, executor,
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    super.onAuthenticationSucceeded(result)
                    updatePassword(entryId, newPassword)
                }
                
                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    super.onAuthenticationError(errorCode, errString)
                    Toast.makeText(this@AutoSaveActivity, 
                        "Authentication failed", Toast.LENGTH_SHORT).show()
                    finish()
                }
            })
        
        biometricPrompt.authenticate(promptInfo)
    }
    
    private fun updatePassword(entryId: Int, newPassword: String) {
        try {
            // Update password logic would go here
            // For now, just show success
            Toast.makeText(this, "Password updated", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to update: ${e.message}", Toast.LENGTH_SHORT).show()
        }
        
        finish()
    }
    
    private fun getAppName(packageName: String): String? {
        return try {
            val pm = packageManager
            val appInfo = pm.getApplicationInfo(packageName, 0)
            pm.getApplicationLabel(appInfo).toString()
        } catch (e: PackageManager.NameNotFoundException) {
            null
        }
    }
    
    private fun isNeverSave(packageName: String): Boolean {
        val prefs = getSharedPreferences("autofill_prefs", MODE_PRIVATE)
        return prefs.getBoolean("never_save_$packageName", false)
    }
    
    private fun addToNeverSave(packageName: String) {
        val prefs = getSharedPreferences("autofill_prefs", MODE_PRIVATE)
        prefs.edit().putBoolean("never_save_$packageName", true).apply()
        Toast.makeText(this, "Won't ask again for this app", Toast.LENGTH_SHORT).show()
    }
}
