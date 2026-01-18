package com.ciphermesh.mobile

import android.content.Context
import android.os.Bundle
import android.view.LayoutInflater
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.ciphermesh.mobile.core.Vault
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.switchmaterial.SwitchMaterial
import com.google.android.material.textfield.TextInputEditText
import java.io.File

class SettingsActivity : AppCompatActivity() {
    
    private lateinit var biometricHelper: BiometricHelper
    private val vault = Vault() // [FIX] Added Vault instance
    
    private fun applySavedTheme() {
        val idx = getSharedPreferences("app_prefs", Context.MODE_PRIVATE).getInt("theme_index", 0)
        val themes = listOf(
            R.style.Theme_CipherMesh_Professional, 
            R.style.Theme_CipherMesh_ModernLight, 
            R.style.Theme_CipherMesh_Ocean, 
            R.style.Theme_CipherMesh_Warm, 
            R.style.Theme_CipherMesh_Vibrant
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
        
        // [FIX] Initialize Vault to perform password checks
        val dbPath = File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)
        
        val toolbar = findViewById<MaterialToolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "Settings"
        
        toolbar.setNavigationOnClickListener { finish() }
        
        biometricHelper = BiometricHelper(this)
        
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
                            // [FIX] Verify logic moved inside the prompt callback
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
    
    private fun showPasswordPrompt(onSuccess: (String) -> Unit) {
        // [FIX] Use the themed layout
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_confirm_password, null)
        val input = view.findViewById<TextInputEditText>(R.id.inputConfirmPass)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Confirm Password")
            .setView(view)
            .setPositiveButton("Confirm") { _, _ ->
                val password = input.text.toString()
                if (password.isNotEmpty()) {
                    // [FIX] ACTUAL VALIDATION
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
}