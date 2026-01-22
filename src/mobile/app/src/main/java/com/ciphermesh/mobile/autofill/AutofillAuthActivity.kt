package com.ciphermesh.mobile.autofill

import android.app.Activity
import android.content.Intent
import android.os.Build
import android.os.Bundle
import android.os.Parcelable
import androidx.annotation.RequiresApi
import kotlinx.parcelize.Parcelize

// [FIX] Ensure Parcelize is available. If not using kotlin-parcelize plugin, remove @Parcelize and : Parcelable
// Assuming you have 'id("kotlin-parcelize")' in build.gradle.kts
@Parcelize
data class LoginFields(
    val username: String,
    val password: String
) : Parcelable

@RequiresApi(Build.VERSION_CODES.O)
class AutofillAuthActivity : Activity() {
    
    private lateinit var passwordInput: android.widget.EditText
    private lateinit var unlockButton: android.widget.Button
    private val vault = com.ciphermesh.mobile.core.Vault()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // Simple programmatic UI for authentication dialog
        val layout = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            setPadding(50, 50, 50, 50)
            gravity = android.view.Gravity.CENTER
        }

        val title = android.widget.TextView(this).apply {
            text = "Unlock CipherMesh"
            textSize = 20f
            setTypeface(null, android.graphics.Typeface.BOLD)
            setPadding(0, 0, 0, 30)
        }

        passwordInput = android.widget.EditText(this).apply {
            hint = "Master Password"
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_VARIATION_PASSWORD
        }

        unlockButton = android.widget.Button(this).apply {
            text = "Unlock"
            setOnClickListener { attemptUnlock() }
        }

        layout.addView(title)
        layout.addView(passwordInput)
        layout.addView(unlockButton)

        setContentView(layout)
        
        // Initialize vault path
        val dbPath = this.filesDir.absolutePath + "/vault.db"
        vault.init(dbPath)
    }

    private fun attemptUnlock() {
        val pwd = passwordInput.text.toString()
        if (pwd.isEmpty()) return

        if (vault.unlock(pwd)) {
             val replyIntent = Intent()
             // AutofillManager expects the dataset to be ready now, so we return OK
             setResult(RESULT_OK, replyIntent)
             finish()
        } else {
             android.widget.Toast.makeText(this, "Incorrect password", android.widget.Toast.LENGTH_SHORT).show()
        }
    }
}