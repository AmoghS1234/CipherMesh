package com.ciphermesh.mobile.autofill

import android.app.Activity
import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.service.autofill.Dataset
import android.view.autofill.AutofillManager
import android.view.autofill.AutofillValue
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.biometric.BiometricManager
import androidx.biometric.BiometricPrompt
import androidx.core.content.ContextCompat
import com.ciphermesh.mobile.R
import com.ciphermesh.mobile.core.Vault
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.util.concurrent.Executor

class AutofillAuthActivity : AppCompatActivity() {
    
    private lateinit var loginFields: LoginFields
    private lateinit var matches: Array<MatchedEntry>
    private lateinit var executor: Executor
    private lateinit var biometricPrompt: BiometricPrompt
    
    companion object {
        const val EXTRA_LOGIN_FIELDS = "login_fields"
        const val EXTRA_MATCHES = "matches"
        
        fun createPendingIntent(
            context: Context,
            loginFields: LoginFields,
            matches: List<MatchedEntry>
        ): PendingIntent {
            val intent = Intent(context, AutofillAuthActivity::class.java).apply {
                putExtra(EXTRA_LOGIN_FIELDS, loginFields)
                putExtra(EXTRA_MATCHES, matches.toTypedArray())
            }
            
            return PendingIntent.getActivity(
                context,
                1001,
                intent,
                PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_IMMUTABLE
            )
        }
    }
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Get data from intent
        loginFields = intent.getParcelableExtra(EXTRA_LOGIN_FIELDS) ?: run {
            finish()
            return
        }
        
        matches = intent.getParcelableArrayExtra(EXTRA_MATCHES)?.map {
            it as MatchedEntry
        }?.toTypedArray() ?: run {
            finish()
            return
        }
        
        executor = ContextCompat.getMainExecutor(this)
        
        // Authenticate user
        authenticateUser()
    }
    
    private fun authenticateUser() {
        val biometricManager = BiometricManager.from(this)
        
        when (biometricManager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)) {
            BiometricManager.BIOMETRIC_SUCCESS -> {
                // Biometric available, use it
                showBiometricPrompt()
            }
            else -> {
                // Biometric not available, proceed without it
                // In production, you'd ask for master password here
                showAccountSelection()
            }
        }
    }
    
    private fun showBiometricPrompt() {
        val promptInfo = BiometricPrompt.PromptInfo.Builder()
            .setTitle("Unlock CipherMesh")
            .setSubtitle("Authenticate to autofill password")
            .setNegativeButtonText("Cancel")
            .build()
        
        biometricPrompt = BiometricPrompt(this, executor,
            object : BiometricPrompt.AuthenticationCallback() {
                override fun onAuthenticationSucceeded(result: BiometricPrompt.AuthenticationResult) {
                    super.onAuthenticationSucceeded(result)
                    showAccountSelection()
                }
                
                override fun onAuthenticationError(errorCode: Int, errString: CharSequence) {
                    super.onAuthenticationError(errorCode, errString)
                    Toast.makeText(this@AutofillAuthActivity, 
                        "Authentication failed: $errString", Toast.LENGTH_SHORT).show()
                    setResult(Activity.RESULT_CANCELED)
                    finish()
                }
                
                override fun onAuthenticationFailed() {
                    super.onAuthenticationFailed()
                    Toast.makeText(this@AutofillAuthActivity, 
                        "Authentication failed", Toast.LENGTH_SHORT).show()
                }
            })
        
        biometricPrompt.authenticate(promptInfo)
    }
    
    private fun showAccountSelection() {
        if (matches.size == 1) {
            // Only one match, auto-select it
            fillCredentials(matches[0])
            return
        }
        
        // Multiple matches, let user choose
        val titles = matches.map { "${it.title} (${it.username})" }.toTypedArray()
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Select Account")
            .setItems(titles) { _, which ->
                fillCredentials(matches[which])
            }
            .setOnCancelListener {
                setResult(Activity.RESULT_CANCELED)
                finish()
            }
            .show()
    }
    
    private fun fillCredentials(entry: MatchedEntry) {
        val vault = Vault()
        
        try {
            // Get decrypted password
            val fullDetails = vault.getEntryFullDetails(entry.id)
            val parts = fullDetails.split("|")
            val password = if (parts.size >= 3) parts[2] else ""
            
            // Build dataset
            val dataset = Dataset.Builder()
                .setValue(
                    loginFields.usernameAutofillId,
                    AutofillValue.forText(entry.username)
                )
                .setValue(
                    loginFields.passwordAutofillId,
                    AutofillValue.forText(password)
                )
                .build()
            
            // Return result
            val replyIntent = Intent().apply {
                putExtra(AutofillManager.EXTRA_AUTHENTICATION_RESULT, dataset)
            }
            
            setResult(Activity.RESULT_OK, replyIntent)
            finish()
            
        } catch (e: Exception) {
            Toast.makeText(this, "Failed to get password: ${e.message}", Toast.LENGTH_SHORT).show()
            setResult(Activity.RESULT_CANCELED)
            finish()
        }
    }
}
