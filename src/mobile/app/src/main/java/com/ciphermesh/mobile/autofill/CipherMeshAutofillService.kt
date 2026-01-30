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
// import android.service.autofill.InlinePresentation
// import android.widget.inline.InlinePresentationSpec
// import androidx.autofill.inline.v1.InlineSuggestionUi

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
        Log.d("AutoFill", "onFillRequest: Starting")
        
        // Check if autofill is enabled in settings
        val prefs = getSharedPreferences("app_prefs", android.content.Context.MODE_PRIVATE)
        val isAutofillEnabled = prefs.getBoolean("autofill_enabled", false)
        if (!isAutofillEnabled) {
            Log.d("AutoFill", "Autofill is disabled in settings, returning null")
            callback.onSuccess(null)
            return
        }
        
        try {
            val structure = request.fillContexts.last().structure
            Log.d("AutoFill", "Structure retrieved. WindowNodeCount: ${structure.windowNodeCount}")
            
            val parser = StructureParser(structure)
            parser.parse()
            
            Log.d("AutoFill", "Parsing finished. Found: User=${parser.usernameFields.size}, Pass=${parser.passwordFields.size}, Domain=${parser.webDomain}")

            var targetDomain = parser.webDomain
            if (targetDomain == null) {
                targetDomain = parser.packageName
            }

            if (targetDomain == null) {
                Log.d("AutoFill", "Target domain is null, returning null response")
                callback.onSuccess(null)
                return
            }

            // Collect fields to attach authentication to
            val authIds = ArrayList<AutofillId>()
            if (parser.usernameFields.isNotEmpty()) authIds.add(parser.usernameFields[0])
            if (parser.passwordFields.isNotEmpty()) authIds.add(parser.passwordFields[0])

            // 1. Initial Response Builder
            val responseBuilder = FillResponse.Builder()

            // 2. Handle Locked State
            if (vault.isLocked()) {
                Log.d("AutoFill", "Vault is locked")
                if (authIds.isNotEmpty()) {
                    responseBuilder.setAuthentication(
                        authIds.toTypedArray(), 
                        getAuthIntent(), 
                        getRemoteViews("Unlock CipherMesh")
                    )
                }
                // Even if locked, we want to set SaveInfo so users can save new logins
            } else {
                Log.d("AutoFill", "Vault is unlocked, searching for matches")
                // 3. Handle Unlocked State: Find & Add Datasets
                val matches = vault.findEntriesByLocation(targetDomain)
                if (matches != null && matches.isNotEmpty()) {
                    Log.d("AutoFill", "Found ${matches.size} matches for $targetDomain")
                    for (matchStr in matches) {
                        try {
                            val parts = matchStr.split("|")
                            if (parts.size < 4) continue

                            val title = parts[1]
                            val username = parts[2]
                            val password = parts[3]

                            val datasetBuilder = Dataset.Builder()
                            
                            val displayTitle = if (title.isNotEmpty()) title else "CipherMesh"
                            val subTitle = if (username.isNotEmpty()) username else "Tap to fill"
                            val presentation = getRemoteViews("$displayTitle\n$subTitle")

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
                        } catch (e: Exception) {
                            Log.e("AutoFill", "Error adding dataset: ${e.message}")
                        }
                    }
                } else {
                    Log.d("AutoFill", "No matches found for $targetDomain, but offering Save support")
                }
            }
            
            // 4. Always Add SaveInfo if we detected fields
            // This ensures onSaveRequest is called even if we didn't fill anything
            if (parser.usernameFields.isNotEmpty() || parser.passwordFields.isNotEmpty()) {
                Log.d("AutoFill", "Adding SaveInfo")
                val saveInfoBuilder = SaveInfo.Builder(
                    SaveInfo.SAVE_DATA_TYPE_USERNAME or SaveInfo.SAVE_DATA_TYPE_PASSWORD,
                    (parser.usernameFields + parser.passwordFields).toTypedArray()
                )
                // Aggressive save flags to ensure prompt appears
                saveInfoBuilder.setFlags(SaveInfo.FLAG_SAVE_ON_ALL_VIEWS_INVISIBLE)
                
                responseBuilder.setSaveInfo(saveInfoBuilder.build())
                
                // Allow system to show "Save" prompt even if we didn't autofill
                callback.onSuccess(responseBuilder.build())
            } else {
                // No fields relevant to us
                Log.d("AutoFill", "No relevant fields found, returning null")
                callback.onSuccess(null)
            }
        } catch (e: Exception) {
            Log.e("AutoFill", "CRASH in onFillRequest: ${e.message}")
            e.printStackTrace()
            callback.onFailure(e.message)
        }
    }

    override fun onSaveRequest(request: SaveRequest, callback: SaveCallback) {
        Log.d("AutoFill", "onSaveRequest triggered")
        
        try {
            val structure = request.fillContexts.last().structure
            val parser = StructureParser(structure)
            parser.parse()
            
            val username = parser.usernameValues.firstOrNull() ?: ""
            val password = parser.passwordValues.firstOrNull() ?: ""
            
            if (username.isEmpty() && password.isEmpty()) {
                Log.d("AutoFill", "No credentials to save")
                callback.onSuccess()
                return
            }
            
            val intent = Intent(this, AutoSaveActivity::class.java).apply {
                addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
                putExtra(AutoSaveActivity.EXTRA_USERNAME, username)
                putExtra(AutoSaveActivity.EXTRA_PASSWORD, password)
                putExtra(AutoSaveActivity.EXTRA_WEB_DOMAIN, parser.webDomain)
                putExtra(AutoSaveActivity.EXTRA_PACKAGE, parser.packageName)
            }
            startActivity(intent)
            callback.onSuccess()
            
        } catch (e: Exception) {
            Log.e("AutoFill", "Error in onSaveRequest: ${e.message}")
            callback.onFailure(e.message)
        }
    }

    private fun getRemoteViews(text: String): RemoteViews {
        val pkg = packageName ?: "com.ciphermesh.mobile"
        val presentation = RemoteViews(pkg, android.R.layout.simple_list_item_1)
        presentation.setTextViewText(android.R.id.text1, text)
        return presentation
    }

    private fun getAuthIntent(): android.content.IntentSender {
        // [FIX] Use specialized AutofillAuthActivity for seamless unlocking
        val intent = Intent(this, AutofillAuthActivity::class.java)
        return PendingIntent.getActivity(this, 1001, intent, PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_IMMUTABLE).intentSender
    }
}