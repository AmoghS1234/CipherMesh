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
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val replyIntent = Intent()
        setResult(RESULT_OK, replyIntent)
        finish()
    }
}