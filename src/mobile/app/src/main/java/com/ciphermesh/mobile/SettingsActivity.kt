package com.ciphermesh.mobile

import android.os.Bundle
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.appbar.MaterialToolbar
import com.google.android.material.switchmaterial.SwitchMaterial

class SettingsActivity : AppCompatActivity() {
    
    private lateinit var biometricHelper: BiometricHelper
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Apply FLAG_SECURE
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE
        )
        
        setContentView(R.layout.activity_settings)
        
        val toolbar = findViewById<MaterialToolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = "Settings"
        
        toolbar.setNavigationOnClickListener {
            finish()
        }
        
        biometricHelper = BiometricHelper(this)
        
        val biometricSwitch = findViewById<SwitchMaterial>(R.id.switchBiometric)
        
        // Check if biometric is available
        if (!biometricHelper.isBiometricAvailable()) {
            biometricSwitch.isEnabled = false
            biometricSwitch.text = "Biometric Unlock (Not Available)"
        } else {
            biometricSwitch.isChecked = biometricHelper.isBiometricEnabled()
            
            biometricSwitch.setOnCheckedChangeListener { _, isChecked ->
                if (isChecked) {
                    // Enable biometric - need to get current password
                    showPasswordPrompt { password ->
                        if (biometricHelper.enableBiometric(password)) {
                            Toast.makeText(this, "Biometric unlock enabled", Toast.LENGTH_SHORT).show()
                        } else {
                            biometricSwitch.isChecked = false
                            Toast.makeText(this, "Failed to enable biometric", Toast.LENGTH_SHORT).show()
                        }
                    }
                } else {
                    // Disable biometric
                    biometricHelper.disableBiometric()
                    Toast.makeText(this, "Biometric unlock disabled", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }
    
    private fun showPasswordPrompt(onPassword: (String) -> Unit) {
        val input = android.widget.EditText(this)
        input.inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
        
        com.google.android.material.dialog.MaterialAlertDialogBuilder(this)
            .setTitle("Enter Master Password")
            .setMessage("Please enter your master password to enable biometric unlock")
            .setView(input)
            .setPositiveButton("Confirm") { _, _ ->
                val password = input.text.toString()
                if (password.isNotEmpty()) {
                    onPassword(password)
                } else {
                    Toast.makeText(this, "Password cannot be empty", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Cancel") { dialog, _ ->
                dialog.dismiss()
                findViewById<SwitchMaterial>(R.id.switchBiometric).isChecked = false
            }
            .show()
    }
}
