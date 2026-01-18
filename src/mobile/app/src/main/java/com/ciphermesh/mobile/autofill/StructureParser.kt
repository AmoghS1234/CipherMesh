package com.ciphermesh.mobile.autofill

import android.app.assist.AssistStructure
import android.os.Parcelable
import android.text.InputType
import android.view.View
import android.view.autofill.AutofillId
import kotlinx.parcelize.Parcelize

@Parcelize
data class LoginFields(
    val usernameAutofillId: AutofillId,
    val passwordAutofillId: AutofillId,
    val usernameText: String? = null,
    val passwordText: String? = null
) : Parcelable

class StructureParser(private val structure: AssistStructure) {
    
    fun parseLoginFields(): LoginFields? {
        var usernameId: AutofillId? = null
        var passwordId: AutofillId? = null
        var usernameText: String? = null
        var passwordText: String? = null
        
        structure.traverse { node ->
            when {
                // Check autofill hints first (most reliable)
                node.autofillHints?.any { hint ->
                    hint == View.AUTOFILL_HINT_USERNAME ||
                    hint == View.AUTOFILL_HINT_EMAIL_ADDRESS
                } == true -> {
                    usernameId = node.autofillId
                    usernameText = node.autofillValue?.textValue?.toString()
                }
                
                node.autofillHints?.contains(View.AUTOFILL_HINT_PASSWORD) == true -> {
                    passwordId = node.autofillId
                    passwordText = node.autofillValue?.textValue?.toString()
                }
                
                // Fallback: check input types and IDs
                usernameId == null && isUsernameField(node) -> {
                    usernameId = node.autofillId
                    usernameText = node.autofillValue?.textValue?.toString()
                }
                
                passwordId == null && isPasswordField(node) -> {
                    passwordId = node.autofillId
                    passwordText = node.autofillValue?.textValue?.toString()
                }
            }
        }
        
        return if (usernameId != null && passwordId != null) {
            LoginFields(usernameId!!, passwordId!!, usernameText, passwordText)
        } else {
            null
        }
    }
    
    fun getPackageName(): String {
        return structure.activityComponent.packageName
    }
    
    fun getWebDomain(): String? {
        var domain: String? = null
        
        structure.traverse { node ->
            if (node.webDomain != null && domain == null) {
                domain = node.webDomain
            }
        }
        
        return domain
    }
    
    private fun isUsernameField(node: AssistStructure.ViewNode): Boolean {
        val inputType = node.inputType
        
        return (inputType and InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS != 0) ||
               (inputType and InputType.TYPE_TEXT_VARIATION_WEB_EMAIL_ADDRESS != 0) ||
               node.idEntry?.contains("username", ignoreCase = true) == true ||
               node.idEntry?.contains("email", ignoreCase = true) == true ||
               node.idEntry?.contains("user", ignoreCase = true) == true ||
               node.hint?.contains("username", ignoreCase = true) == true ||
               node.hint?.contains("email", ignoreCase = true) == true
    }
    
    private fun isPasswordField(node: AssistStructure.ViewNode): Boolean {
        val inputType = node.inputType
        
        return (inputType and InputType.TYPE_TEXT_VARIATION_PASSWORD != 0) ||
               (inputType and InputType.TYPE_TEXT_VARIATION_WEB_PASSWORD != 0) ||
               node.idEntry?.contains("password", ignoreCase = true) == true ||
               node.idEntry?.contains("pass", ignoreCase = true) == true ||
               node.hint?.contains("password", ignoreCase = true) == true
    }
}

// Extension functions to traverse AssistStructure tree
private inline fun AssistStructure.traverse(crossinline callback: (AssistStructure.ViewNode) -> Unit) {
    for (i in 0 until windowNodeCount) {
        val windowNode = getWindowNodeAt(i)
        windowNode.rootViewNode.traverse(callback)
    }
}

private inline fun AssistStructure.ViewNode.traverse(crossinline callback: (AssistStructure.ViewNode) -> Unit) {
    callback(this)
    for (i in 0 until childCount) {
        getChildAt(i).traverse(callback)
    }
}
