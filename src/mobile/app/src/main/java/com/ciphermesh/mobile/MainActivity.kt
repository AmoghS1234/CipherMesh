package com.ciphermesh.mobile

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.View
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import com.ciphermesh.mobile.core.Vault
import com.google.android.material.button.MaterialButton
import com.google.android.material.textfield.TextInputEditText
import com.google.android.material.textfield.TextInputLayout
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File

class MainActivity : AppCompatActivity() {

    private val vault = Vault()
    private lateinit var dbPath: String

    override fun onCreate(savedInstanceState: Bundle?) {
        applySavedTheme()
        super.onCreate(savedInstanceState)
        
        // [SECURITY] Prevent screenshots and screen recording
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE
        )
        
        setContentView(R.layout.activity_main)

        // Setup DB
        val dbFile = File(filesDir, "vault.db")
        dbPath = dbFile.absolutePath
        vault.init(dbPath)

        // Bind Views
        val userLayout = findViewById<TextInputLayout>(R.id.layoutUsername)
        val userInput = findViewById<TextInputEditText>(R.id.inputUsername)
        val passInput = findViewById<TextInputEditText>(R.id.inputPassword)
        val passLayout = findViewById<TextInputLayout>(R.id.layoutPassword)
        val confirmLayout = findViewById<TextInputLayout>(R.id.layoutConfirm)
        val confirmInput = findViewById<TextInputEditText>(R.id.inputConfirm)
        val btnAction = findViewById<MaterialButton>(R.id.btnUnlock)
        val titleText = findViewById<TextView>(R.id.textTitle)
        val subtitleText = findViewById<TextView>(R.id.textSubtitle)

        if (vault.hasUsers()) {
            // --- LOGIN MODE ---
            // Only show Password field
            userLayout.visibility = View.GONE
            confirmLayout.visibility = View.GONE
            
            titleText.text = getString(R.string.app_name)
            subtitleText.text = "Enter your master password to unlock."
            btnAction.text = "Unlock Vault"
            
            // [NEW] Try biometric unlock if enabled
            val biometricHelper = BiometricHelper(this)
            if (biometricHelper.isBiometricEnabled() && biometricHelper.isBiometricAvailable()) {
                biometricHelper.authenticate(
                    this,
                    onSuccess = { password ->
                        if (vault.unlock(password)) {
                            navigateToHome()
                        } else {
                            Toast.makeText(this, "Biometric unlock failed", Toast.LENGTH_SHORT).show()
                        }
                    },
                    onError = { error ->
                        // User clicked "Use Password" or biometric failed
                        // Just show the password field (already visible)
                    }
                )
            }

            btnAction.setOnClickListener {
                val password = passInput.text.toString()
                if (password.isNotEmpty()) {
                    if (vault.unlock(password)) {
                        navigateToHome()
                    } else {
                        passLayout.error = "Incorrect Password"
                    }
                }
            }
            
            // Forgot Password handler
            val btnForgot = findViewById<TextView>(R.id.btnForgotPassword)
            btnForgot.visibility = View.VISIBLE
            btnForgot.setOnClickListener {
                showForgotPasswordDialog()
            }
        } else {
            // --- REGISTER MODE ---
            // Show all fields (Username, Password, Confirm)
            userLayout.visibility = View.VISIBLE
            confirmLayout.visibility = View.VISIBLE
            
            titleText.text = "Create Vault"
            subtitleText.text = "Set up your secure storage."
            btnAction.text = "Create Account"

            btnAction.setOnClickListener {
                val username = userInput.text.toString().trim()
                val p1 = passInput.text.toString()
                val p2 = confirmInput.text.toString()

                if (username.isEmpty()) {
                    userLayout.error = "Username required"
                    return@setOnClickListener
                }
                if (username.isEmpty()) {
                    userLayout.error = "Username required"
                    return@setOnClickListener
                }
                // Password length check removed per user request
                if (p1 != p2) {
                    confirmLayout.error = "Passwords do not match"
                    return@setOnClickListener
                }

                if (vault.createAccount(dbPath, username, p1)) {
                    navigateToHome()
                } else {
                    Toast.makeText(this, "Failed to create vault", Toast.LENGTH_SHORT).show()
                }
            }
        }
    }

    private fun navigateToHome() {
        val intent = Intent(this, HomeActivity::class.java)
        startActivity(intent)
        finish()
    }

    private fun applySavedTheme() {
        val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
        val themeIndex = prefs.getInt("theme_index", 0)
        val themes = listOf(
            R.style.Theme_CipherMesh_Professional,
            R.style.Theme_CipherMesh_ModernLight
        )
        if (themeIndex in themes.indices) {
            setTheme(themes[themeIndex])
        }
    }
    
    private fun showForgotPasswordDialog() {
        val view = layoutInflater.inflate(R.layout.dialog_forgot_password, null)
        val inputConfirm = view.findViewById<TextInputEditText>(R.id.inputResetConfirm)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("⚠️ Reset Vault")
            .setView(view)
            .setPositiveButton("Reset") { _, _ ->
                val confirmation = inputConfirm.text.toString().trim()
                if (confirmation.equals("RESET", ignoreCase = false)) {
                    performFactoryReset()
                } else {
                    Toast.makeText(this, "Please type RESET exactly", Toast.LENGTH_SHORT).show()
                }
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
            
            Toast.makeText(this, "Vault reset. Please restart the app.", Toast.LENGTH_LONG).show()
            
            // Restart activity
            val intent = Intent(this, MainActivity::class.java)
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
            startActivity(intent)
            finish()
        } catch (e: Exception) {
            Toast.makeText(this, "Reset failed: ${e.message}", Toast.LENGTH_SHORT).show()
        }
    }
}