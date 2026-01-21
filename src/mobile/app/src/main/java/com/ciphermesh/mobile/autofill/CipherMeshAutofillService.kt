package com.ciphermesh.mobile.autofill

import android.app.PendingIntent
import android.content.Intent
import android.os.Build
import android.os.CancellationSignal
import android.service.autofill.*
import android.view.autofill.AutofillId
import android.view.autofill.AutofillValue
import android.widget.RemoteViews
import android.util.Log
import androidx.annotation.RequiresApi
import com.ciphermesh.mobile.MainActivity
import com.ciphermesh.mobile.R
import com.ciphermesh.mobile.core.Vault

@RequiresApi(Build.VERSION_CODES.O)
class CipherMeshAutofillService : AutofillService() {

    private val vault = Vault()

    override fun onConnected() {
        super.onConnected()
        Log.d("AutoFill", "Service Connected")
        val dbPath = this.filesDir.absolutePath + "/vault.db"
        vault.init(dbPath)
    }

    override fun onFillRequest(
        request: FillRequest,
        cancellationSignal: CancellationSignal,
        callback: FillCallback
    ) {
        Log.d("AutoFill", "onFillRequest")

        val structure = request.fillContexts.last().structure
        val parser = StructureParser(structure)
        parser.parse()

        var targetDomain = parser.webDomain
        if (targetDomain == null) {
            targetDomain = parser.packageName
        }

        if (targetDomain == null) {
            callback.onSuccess(null)
            return
        }

        // Collect fields to attach authentication to
        val authIds = ArrayList<AutofillId>()
        authIds.addAll(parser.usernameFields)
        authIds.addAll(parser.passwordFields)

        if (vault.isLocked()) {
            // [FIX] Check if we found fields. We cannot pass null to setAuthentication's first arg.
            if (authIds.isNotEmpty()) {
                val response = FillResponse.Builder()
                    .setAuthentication(
                        authIds.toTypedArray(), // Pass the array of IDs we found
                        getAuthIntent(), 
                        getRemoteViews("Unlock CipherMesh")
                    )
                    .build()
                callback.onSuccess(response)
            } else {
                // If we can't find fields to attach auth to, we can't offer autofill securely
                callback.onSuccess(null)
            }
            return
        }

        val matches = vault.findEntriesByLocation(targetDomain)
        
        if (matches == null || matches.isEmpty()) {
            Log.d("AutoFill", "No matches found for $targetDomain")
            callback.onSuccess(null)
            return
        }

        val responseBuilder = FillResponse.Builder()

        for (matchStr in matches) {
            val parts = matchStr.split("|")
            if (parts.size < 4) continue

            val title = parts[1]
            val username = parts[2]
            val password = parts[3]

            val datasetBuilder = Dataset.Builder()
            val presentation = getRemoteViews("$title\n$username")
            
            var valid = false
            if (parser.usernameFields.isNotEmpty()) {
                datasetBuilder.setValue(parser.usernameFields[0], AutofillValue.forText(username), presentation)
                valid = true
            }
            if (parser.passwordFields.isNotEmpty()) {
                datasetBuilder.setValue(parser.passwordFields[0], AutofillValue.forText(password), presentation)
                valid = true
            } 
            
            if (valid) {
                responseBuilder.addDataset(datasetBuilder.build())
            }
        }

        callback.onSuccess(responseBuilder.build())
    }

    override fun onSaveRequest(request: SaveRequest, callback: SaveCallback) {
        callback.onSuccess()
    }

    private fun getRemoteViews(text: String): RemoteViews {
        val pkg = packageName ?: "com.ciphermesh.mobile" // Safety fallback
        val presentation = RemoteViews(pkg, android.R.layout.simple_list_item_1)
        presentation.setTextViewText(android.R.id.text1, text)
        return presentation
    }

    private fun getAuthIntent(): android.content.IntentSender {
        val intent = Intent(this, MainActivity::class.java)
        // Ensure FLAG_IMMUTABLE is used for Android 12+
        return PendingIntent.getActivity(this, 1001, intent, PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_IMMUTABLE).intentSender
    }
}