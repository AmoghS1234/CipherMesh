package com.ciphermesh.mobile

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.DocumentsContract
import android.view.LayoutInflater
import android.view.View
import android.widget.LinearLayout
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import com.ciphermesh.mobile.core.Vault
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.switchmaterial.SwitchMaterial
import com.google.android.material.textfield.TextInputEditText
import java.io.File
import java.text.SimpleDateFormat
import java.util.*
import javax.crypto.Cipher
import javax.crypto.SecretKeyFactory
import javax.crypto.spec.IvParameterSpec
import javax.crypto.spec.PBEKeySpec
import javax.crypto.spec.SecretKeySpec
import android.util.Base64
import android.widget.CheckBox

class SettingsActivity : AppCompatActivity() {
    
    companion object {
        const val PREF_OFFLINE_MODE = "offline_mode"
        const val PREF_AUTOFILL_ENABLED = "autofill_enabled"
        private const val EXPORT_FILE_PREFIX = "ciphermesh_backup_"
        private const val SALT = "CipherMeshExport2026"
        private const val ITERATIONS = 65536
        private const val KEY_LENGTH = 256
    }
    
    private var pendingExportPassword: String? = null
    
    private lateinit var biometricHelper: BiometricHelper
    private val vault = Vault()
    
    // File picker launchers
    private val exportLauncher = registerForActivityResult(
        ActivityResultContracts.CreateDocument("application/json")
    ) { uri ->
        uri?.let { exportVaultToUri(it) }
    }
    
    private val importLauncher = registerForActivityResult(
        ActivityResultContracts.OpenDocument()
    ) { uri ->
        uri?.let { importVaultFromUri(it) }
    }
    
    private fun applySavedTheme() {
        val idx = getSharedPreferences("app_prefs", Context.MODE_PRIVATE).getInt("theme_index", 0)
        val themes = listOf(
            R.style.Theme_CipherMesh_Professional, 
            R.style.Theme_CipherMesh_ModernLight
        )
        if(idx in themes.indices) setTheme(themes[idx])
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        applySavedTheme()
        super.onCreate(savedInstanceState)
        
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE
        )
        
        setContentView(R.layout.activity_settings)
        
        // Initialize Vault
        val dbPath = File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)
        
        val toolbar = findViewById<MaterialToolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "Settings"
        
        toolbar.setNavigationOnClickListener { finish() }
        
        biometricHelper = BiometricHelper(this)
        
        setupBiometricSwitch()
        setupOfflineModeSwitch()
        setupAutofillSwitch()
        setupAutoLock()
        setupChangePassword()
        setupExportImport()
        setupFactoryReset()
    }
    
    private fun setupBiometricSwitch() {
        val biometricSwitch = findViewById<SwitchMaterial>(R.id.switchBiometric)
        
        if (!biometricHelper.isBiometricAvailable()) {
            biometricSwitch.isEnabled = false
            biometricSwitch.text = "Biometric Unlock (Not Available)"
        } else {
            biometricSwitch.isChecked = biometricHelper.isBiometricEnabled()
            
            biometricSwitch.setOnCheckedChangeListener { _, isChecked ->
                if (isChecked) {
                    if (!biometricHelper.isBiometricEnabled()) {
                        showPasswordPrompt { password ->
                            if (biometricHelper.enableBiometric(password)) {
                                Toast.makeText(this, "Biometric unlock enabled", Toast.LENGTH_SHORT).show()
                            } else {
                                biometricSwitch.isChecked = false
                                Toast.makeText(this, "Failed to enable biometric", Toast.LENGTH_SHORT).show()
                            }
                        }
                    }
                } else {
                    biometricHelper.disableBiometric()
                    Toast.makeText(this, "Biometric unlock disabled", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
    
    private fun setupOfflineModeSwitch() {
        val offlineSwitch = findViewById<SwitchMaterial>(R.id.switchOfflineMode)
        val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
        
        // Default to offline mode = true
        offlineSwitch.isChecked = prefs.getBoolean(PREF_OFFLINE_MODE, true)
        
        offlineSwitch.setOnCheckedChangeListener { _, isChecked ->
            prefs.edit().putBoolean(PREF_OFFLINE_MODE, isChecked).apply()
            val msg = if (isChecked) "Offline mode enabled" else "Online mode enabled"
            Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun setupAutofillSwitch() {
        val autofillSwitch = findViewById<SwitchMaterial>(R.id.switchAutofill)
        val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
        
        // Default to autofill DISABLED
        autofillSwitch.isChecked = prefs.getBoolean(PREF_AUTOFILL_ENABLED, false)
        
        autofillSwitch.setOnCheckedChangeListener { _, isChecked ->
            prefs.edit().putBoolean(PREF_AUTOFILL_ENABLED, isChecked).apply()
            if (isChecked) {
                // Direct user to enable autofill in system settings
                Toast.makeText(this, "Enable CipherMesh in System Autofill settings", Toast.LENGTH_LONG).show()
                try {
                    val intent = android.content.Intent(android.provider.Settings.ACTION_REQUEST_SET_AUTOFILL_SERVICE)
                    intent.data = android.net.Uri.parse("package:$packageName")
                    startActivity(intent)
                } catch (e: Exception) {
                    Toast.makeText(this, "Autofill enabled", Toast.LENGTH_SHORT).show()
                }
            } else {
                Toast.makeText(this, "Autofill disabled", Toast.LENGTH_SHORT).show()
            }
        }
    }

    private fun setupAutoLock() {
        val layout = findViewById<LinearLayout>(R.id.layoutAutoLock)
        val summary = findViewById<android.widget.TextView>(R.id.textAutoLockSummary)
        val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)

        fun updateSummary() {
            val current = prefs.getString(CipherMeshApplication.PREF_LOCK_TIMEOUT, CipherMeshApplication.DEFAULT_TIMEOUT.toString())
            summary.text = when(current) {
                "0" -> "Immediately"
                "60000" -> "1 minute"
                "300000" -> "5 minutes"
                "600000" -> "10 minutes"
                "900000" -> "15 minutes"
                "-1" -> "Off"
                else -> "10 minutes"
            }
        }
        updateSummary()

        layout.setOnClickListener {
            val options = arrayOf("Off", "Immediately", "1 minute", "5 minutes", "10 minutes", "15 minutes")
            val values = arrayOf("-1", "0", "60000", "300000", "600000", "900000")
            
            val currentVal = prefs.getString(CipherMeshApplication.PREF_LOCK_TIMEOUT, CipherMeshApplication.DEFAULT_TIMEOUT.toString())
            var selectedIdx = values.indexOf(currentVal)
            if (selectedIdx == -1) selectedIdx = 4 // Default to 10 minutes

            MaterialAlertDialogBuilder(this)
                .setTitle("Auto Lock Timeout")
                .setSingleChoiceItems(options, selectedIdx) { dialog, which ->
                    prefs.edit().putString(CipherMeshApplication.PREF_LOCK_TIMEOUT, values[which]).apply()
                    updateSummary()
                    dialog.dismiss()
                }
                .setNegativeButton("Cancel", null)
                .show()
        }
    }
    
    private fun setupChangePassword() {
        findViewById<LinearLayout>(R.id.layoutChangePassword).setOnClickListener {
            showChangePasswordDialog()
        }
    }
    
    private fun setupExportImport() {
        findViewById<LinearLayout>(R.id.layoutExport).setOnClickListener {
            showExportDialog()
        }
        
        findViewById<LinearLayout>(R.id.layoutImport).setOnClickListener {
            showImportDialog()
        }
    }
    
    private fun showPasswordPrompt(onSuccess: (String) -> Unit) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_confirm_password, null)
        val input = view.findViewById<TextInputEditText>(R.id.inputConfirmPass)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Confirm Password")
            .setView(view)
            .setPositiveButton("Confirm") { _, _ ->
                val password = input.text.toString()
                if (password.isNotEmpty()) {
                    if (vault.verifyMasterPassword(password)) {
                        onSuccess(password)
                    } else {
                        Toast.makeText(this, "Incorrect Password", Toast.LENGTH_SHORT).show()
                        findViewById<SwitchMaterial>(R.id.switchBiometric).isChecked = false
                    }
                } else {
                    Toast.makeText(this, "Password required", Toast.LENGTH_SHORT).show()
                    findViewById<SwitchMaterial>(R.id.switchBiometric).isChecked = false
                }
            }
            .setNegativeButton("Cancel") { dialog, _ ->
                dialog.dismiss()
                findViewById<SwitchMaterial>(R.id.switchBiometric).isChecked = false
            }
            .setCancelable(false)
            .show()
    }
    
    private fun showChangePasswordDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_change_password, null)
        val currentPass = view.findViewById<TextInputEditText>(R.id.inputCurrentPassword)
        val newPass = view.findViewById<TextInputEditText>(R.id.inputNewPassword)
        val confirmPass = view.findViewById<TextInputEditText>(R.id.inputConfirmPassword)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Change Master Password")
            .setView(view)
            .setPositiveButton("Change") { _, _ ->
                val current = currentPass.text.toString()
                val new = newPass.text.toString()
                val confirm = confirmPass.text.toString()
                
                when {
                    current.isEmpty() || new.isEmpty() || confirm.isEmpty() -> {
                        Toast.makeText(this, "All fields required", Toast.LENGTH_SHORT).show()
                    }
                    new != confirm -> {
                        Toast.makeText(this, "New passwords don't match", Toast.LENGTH_SHORT).show()
                    }
                    new.length < 8 -> {
                        Toast.makeText(this, "Password must be at least 8 characters", Toast.LENGTH_SHORT).show()
                    }
                    !vault.verifyMasterPassword(current) -> {
                        Toast.makeText(this, "Current password is incorrect", Toast.LENGTH_SHORT).show()
                    }
                    else -> {
                        // Try to change password using native method
                        try {
                            val success = vault.changeMasterPassword(current, new)
                            if (success) {
                                Toast.makeText(this, "Password changed successfully", Toast.LENGTH_SHORT).show()
                                // Update biometric if enabled
                                if (biometricHelper.isBiometricEnabled()) {
                                    biometricHelper.enableBiometric(new)
                                }
                            } else {
                                Toast.makeText(this, "Failed to change password", Toast.LENGTH_SHORT).show()
                            }
                        } catch (e: UnsatisfiedLinkError) {
                            Toast.makeText(this, "Feature coming soon", Toast.LENGTH_SHORT).show()
                        }
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
    
    private fun showExportDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_export_password, null)
        val inputPassword = view.findViewById<TextInputEditText>(R.id.inputExportPassword)
        val inputConfirm = view.findViewById<TextInputEditText>(R.id.inputConfirmExportPassword)
        val checkUseMaster = view.findViewById<CheckBox>(R.id.checkUseMasterPassword)
        val layoutPassword = view.findViewById<com.google.android.material.textfield.TextInputLayout>(R.id.layoutExportPassword)
        val layoutConfirm = view.findViewById<com.google.android.material.textfield.TextInputLayout>(R.id.layoutConfirmExportPassword)
        
        checkUseMaster.setOnCheckedChangeListener { _, isChecked ->
            layoutPassword.isEnabled = !isChecked
            layoutConfirm.isEnabled = !isChecked
            inputPassword.isEnabled = !isChecked
            inputConfirm.isEnabled = !isChecked
        }
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Export Vault")
            .setView(view)
            .setPositiveButton("Export") { _, _ ->
                val password: String
                if (checkUseMaster.isChecked) {
                    // Prompt for master password
                    showMasterPasswordPromptForExport()
                    return@setPositiveButton
                } else {
                    val pass1 = inputPassword.text.toString()
                    val pass2 = inputConfirm.text.toString()
                    
                    if (pass1.isEmpty()) {
                        Toast.makeText(this, "Password required", Toast.LENGTH_SHORT).show()
                        return@setPositiveButton
                    }
                    if (pass1 != pass2) {
                        Toast.makeText(this, "Passwords don't match", Toast.LENGTH_SHORT).show()
                        return@setPositiveButton
                    }
                    if (pass1.length < 6) {
                        Toast.makeText(this, "Password must be at least 6 characters", Toast.LENGTH_SHORT).show()
                        return@setPositiveButton
                    }
                    password = pass1
                }
                
                pendingExportPassword = password
                val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                val filename = "${EXPORT_FILE_PREFIX}${timestamp}.enc"
                exportLauncher.launch(filename)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
    
    private fun showMasterPasswordPromptForExport() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_confirm_password, null)
        val input = view.findViewById<TextInputEditText>(R.id.inputConfirmPass)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Enter Master Password")
            .setView(view)
            .setPositiveButton("Export") { _, _ ->
                val password = input.text.toString()
                if (password.isEmpty()) {
                    Toast.makeText(this, "Password required", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                if (!vault.verifyMasterPassword(password)) {
                    Toast.makeText(this, "Incorrect password", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                
                pendingExportPassword = password
                val timestamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.getDefault()).format(Date())
                val filename = "${EXPORT_FILE_PREFIX}${timestamp}.enc"
                exportLauncher.launch(filename)
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
    
    private fun exportVaultToUri(uri: Uri) {
        val password = pendingExportPassword
        pendingExportPassword = null
        
        if (password.isNullOrEmpty()) {
            Toast.makeText(this, "No password set", Toast.LENGTH_SHORT).show()
            return
        }
        
        try {
            val exportData = vault.exportVault()
            if (exportData.isEmpty()) {
                Toast.makeText(this, "Nothing to export", Toast.LENGTH_SHORT).show()
                return
            }
            
            // Encrypt the data
            val encryptedData = encryptData(exportData, password)
            
            contentResolver.openOutputStream(uri)?.use { outputStream ->
                outputStream.write(encryptedData.toByteArray())
                Toast.makeText(this, "Vault exported successfully", Toast.LENGTH_SHORT).show()
            }
        } catch (e: UnsatisfiedLinkError) {
            Toast.makeText(this, "Export feature coming soon", Toast.LENGTH_SHORT).show()
        } catch (e: Exception) {
            Toast.makeText(this, "Export failed: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun encryptData(plainText: String, password: String): String {
        val factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
        val spec = PBEKeySpec(password.toCharArray(), SALT.toByteArray(), ITERATIONS, KEY_LENGTH)
        val tmp = factory.generateSecret(spec)
        val secretKey = SecretKeySpec(tmp.encoded, "AES")
        
        val cipher = Cipher.getInstance("AES/CBC/PKCS5Padding")
        cipher.init(Cipher.ENCRYPT_MODE, secretKey)
        val iv = cipher.iv
        val encrypted = cipher.doFinal(plainText.toByteArray(Charsets.UTF_8))
        
        // Combine IV + encrypted data
        val combined = iv + encrypted
        return Base64.encodeToString(combined, Base64.NO_WRAP)
    }
    
    private fun decryptData(encryptedText: String, password: String): String? {
        return try {
            val combined = Base64.decode(encryptedText, Base64.NO_WRAP)
            val iv = combined.sliceArray(0 until 16)
            val encrypted = combined.sliceArray(16 until combined.size)
            
            val factory = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256")
            val spec = PBEKeySpec(password.toCharArray(), SALT.toByteArray(), ITERATIONS, KEY_LENGTH)
            val tmp = factory.generateSecret(spec)
            val secretKey = SecretKeySpec(tmp.encoded, "AES")
            
            val cipher = Cipher.getInstance("AES/CBC/PKCS5Padding")
            cipher.init(Cipher.DECRYPT_MODE, secretKey, IvParameterSpec(iv))
            String(cipher.doFinal(encrypted), Charsets.UTF_8)
        } catch (e: Exception) {
            null
        }
    }
    
    private fun showImportDialog() {
        MaterialAlertDialogBuilder(this)
            .setTitle("Import Vault")
            .setMessage("This will import passwords from a backup file. You'll need the password used when exporting.")
            .setPositiveButton("Select File") { _, _ ->
                importLauncher.launch(arrayOf("*/*"))
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
    
    private fun importVaultFromUri(uri: Uri) {
        try {
            val encryptedData = contentResolver.openInputStream(uri)?.bufferedReader()?.readText() ?: ""
            if (encryptedData.isEmpty()) {
                Toast.makeText(this, "File is empty", Toast.LENGTH_SHORT).show()
                return
            }
            
            // Prompt for password to decrypt import
            val view = LayoutInflater.from(this).inflate(R.layout.dialog_confirm_password, null)
            val input = view.findViewById<TextInputEditText>(R.id.inputConfirmPass)
            
            MaterialAlertDialogBuilder(this)
                .setTitle("Enter Backup Password")
                .setMessage("Enter the password that was used when creating this backup.")
                .setView(view)
                .setPositiveButton("Import") { _, _ ->
                    val password = input.text.toString()
                    if (password.isEmpty()) {
                        Toast.makeText(this, "Password required", Toast.LENGTH_SHORT).show()
                        return@setPositiveButton
                    }
                    
                    // Try to decrypt
                    val decrypted = decryptData(encryptedData, password)
                    if (decrypted == null) {
                        Toast.makeText(this, "Wrong password or corrupted file", Toast.LENGTH_SHORT).show()
                        return@setPositiveButton
                    }
                    
                    // Import the decrypted data
                    try {
                        val success = vault.importVault(decrypted, password)
                        if (success) {
                            Toast.makeText(this, "Import successful! Restart app to see changes.", Toast.LENGTH_LONG).show()
                        } else {
                            Toast.makeText(this, "Import failed", Toast.LENGTH_SHORT).show()
                        }
                    } catch (e: UnsatisfiedLinkError) {
                        Toast.makeText(this, "Import not fully implemented yet", Toast.LENGTH_SHORT).show()
                    }
                }
                .setNegativeButton("Cancel", null)
                .show()
        } catch (e: Exception) {
            Toast.makeText(this, "Import failed: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun setupFactoryReset() {
        findViewById<View>(R.id.layoutFactoryReset).setOnClickListener {
            showFactoryResetDialog()
        }
    }
    
    private fun showFactoryResetDialog() {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_factory_reset, null)
        val passwordInput = view.findViewById<TextInputEditText>(R.id.inputResetPassword)
        val confirmInput = view.findViewById<TextInputEditText>(R.id.inputResetConfirm)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("⚠️ Factory Reset")
            .setMessage("This will DELETE ALL your passwords and data. This action cannot be undone.")
            .setView(view)
            .setPositiveButton("Reset") { _, _ ->
                val password = passwordInput.text.toString()
                val confirmation = confirmInput.text.toString()
                
                if (confirmation != "RESET") {
                    Toast.makeText(this, "Type RESET to confirm", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                
                if (!vault.verifyMasterPassword(password)) {
                    Toast.makeText(this, "Incorrect master password", Toast.LENGTH_SHORT).show()
                    return@setPositiveButton
                }
                
                performFactoryReset()
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
    
    private fun performFactoryReset() {
        try {
            // Delete vault database
            val dbFile = File(filesDir, "vault.db")
            if (dbFile.exists()) dbFile.delete()
            
            // Clear biometric data
            val biometricPrefs = getSharedPreferences("biometric_prefs", Context.MODE_PRIVATE)
            biometricPrefs.edit().clear().apply()
            
            // Clear app preferences except theme
            val appPrefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
            val savedTheme = appPrefs.getInt("theme_index", 0)
            appPrefs.edit().clear().putInt("theme_index", savedTheme).apply()
            
            Toast.makeText(this, "Factory reset complete. Restarting...", Toast.LENGTH_LONG).show()
            
            // Restart to MainActivity
            val intent = Intent(this, MainActivity::class.java)
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
            startActivity(intent)
            finish()
        } catch (e: Exception) {
            Toast.makeText(this, "Reset failed: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
}