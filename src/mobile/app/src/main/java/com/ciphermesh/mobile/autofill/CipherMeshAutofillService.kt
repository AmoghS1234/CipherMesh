package com.ciphermesh.mobile.autofill

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.os.CancellationSignal
import android.service.autofill.AutofillService
import android.service.autofill.FillCallback
import android.service.autofill.FillRequest
import android.service.autofill.FillResponse
import android.service.autofill.SaveCallback
import android.service.autofill.SaveRequest
import android.service.autofill.Dataset
import android.view.autofill.AutofillValue
import android.widget.RemoteViews
import com.ciphermesh.mobile.R

/**
 * CipherMesh AutoFill Service
 * Provides automatic credential filling for apps and websites
 */
class CipherMeshAutofillService : AutofillService() {
    
    override fun onFillRequest(
        request: FillRequest,
        cancellationSignal: CancellationSignal,
        callback: FillCallback
    ) {
        // Parse the view structure to find login fields
        val structure = request.fillContexts.last().structure
        val parser = StructureParser(structure)
        
        // Find username and password fields
        val loginFields = parser.parseLoginFields()
        if (loginFields == null) {
            // No login fields found
            callback.onSuccess(null)
            return
        }
        
        // Get app context (package name or web domain)
        val appPackage = parser.getPackageName()
        val webDomain = parser.getWebDomain()
        
        // Find matching entries in vault
        val matcher = EntryMatcher(this)
        val matches = matcher.findMatches(appPackage, webDomain)
        
        if (matches.isEmpty()) {
            // No matching credentials found
            callback.onSuccess(null)
            return
        }
        
        // Build fill response with authentication requirement
        val response = buildAuthResponse(matches, loginFields)
        callback.onSuccess(response)
    }
    
    override fun onSaveRequest(
        request: SaveRequest,
        callback: SaveCallback
    ) {
        // Handle auto-save of new credentials
        val structure = request.fillContexts.last().structure
        val parser = StructureParser(structure)
        
        val loginFields = parser.parseLoginFields()
        if (loginFields == null) {
            callback.onSuccess()
            return
        }
        
        // Extract entered credentials
        val username = loginFields.usernameText ?: ""
        val password = loginFields.passwordText ?: ""
        
        if (username.isEmpty() || password.isEmpty()) {
            callback.onSuccess()
            return
        }
        
        // Get app context
        val packageName = parser.getPackageName()
        val webDomain = parser.getWebDomain()
        
        // Check never-save list
        if (isNeverSave(packageName)) {
            callback.onSuccess()
            return
        }
        
        // Show save prompt activity
        val intent = Intent(this, AutoSaveActivity::class.java).apply {
            flags = Intent.FLAG_ACTIVITY_NEW_TASK
            putExtra(AutoSaveActivity.EXTRA_USERNAME, username)
            putExtra(AutoSaveActivity.EXTRA_PASSWORD, password)
            putExtra(AutoSaveActivity.EXTRA_PACKAGE, packageName)
            putExtra(AutoSaveActivity.EXTRA_WEB_DOMAIN, webDomain)
        }
        
        startActivity(intent)
        callback.onSuccess()
    }
    
    private fun buildAuthResponse(
        matches: List<MatchedEntry>,
        loginFields: LoginFields
    ): FillResponse {
        // Create intent for authentication activity
        val authIntent = AutofillAuthActivity.createPendingIntent(
            this,
            loginFields,
            matches
        )
        
        // Create presentation view (what user sees before authentication)
        val presentation = RemoteViews(packageName, R.layout.autofill_item).apply {
            setTextViewText(
                R.id.text,
                if (matches.size == 1) {
                    "AutoFill with ${matches[0].title}"
                } else {
                    "AutoFill (${matches.size} accounts)"
                }
            )
        }
        
        // Build response requiring authentication
        return FillResponse.Builder()
            .setAuthentication(
                arrayOf(loginFields.usernameAutofillId, loginFields.passwordAutofillId),
                authIntent.intentSender,
                presentation
            )
            .build()
    }
    
    private fun isNeverSave(packageName: String): Boolean {
        val prefs = getSharedPreferences("autofill_prefs", Context.MODE_PRIVATE)
        return prefs.getBoolean("never_save_$packageName", false)
    }
}
