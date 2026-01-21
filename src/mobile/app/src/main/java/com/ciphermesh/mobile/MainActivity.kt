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
                if (p1.length < 1) {
                    passLayout.error = "Password too short (min 1 chars)"
                    return@setOnClickListener
                }
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
            R.style.Theme_CipherMesh_ModernLight,
            R.style.Theme_CipherMesh_Ocean,
            R.style.Theme_CipherMesh_Warm,
            R.style.Theme_CipherMesh_Vibrant
        )
        if (themeIndex in themes.indices) {
            setTheme(themes[themeIndex])
        }
    }
}