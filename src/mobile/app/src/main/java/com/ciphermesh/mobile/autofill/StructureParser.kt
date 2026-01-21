package com.ciphermesh.mobile.autofill

import android.app.assist.AssistStructure
import android.os.Build
import android.view.View
import android.view.autofill.AutofillId
import androidx.annotation.RequiresApi

@RequiresApi(Build.VERSION_CODES.O)
class StructureParser(private val structure: AssistStructure) {

    val usernameFields = mutableListOf<AutofillId>()
    val passwordFields = mutableListOf<AutofillId>()
    var webDomain: String? = null
    var packageName: String? = null
    
    // Value extraction for save operations
    val usernameValues = mutableListOf<String>()
    val passwordValues = mutableListOf<String>()

    fun parse() {
        val nodes = structure.windowNodeCount
        for (i in 0 until nodes) {
            val windowNode = structure.getWindowNodeAt(i)
            val rootNode = windowNode.rootViewNode
            
            // [FIX] ViewNode uses 'idPackage', not 'packageName'
            packageName = rootNode.idPackage?.toString()
            
            parseNode(rootNode)
        }
    }

    private fun parseNode(node: AssistStructure.ViewNode) {
        if (node.webDomain != null) {
            webDomain = node.webDomain
        }

        val hints = node.autofillHints
        val textValue = node.text?.toString() ?: ""
        
        if (hints != null) {
            for (hint in hints) {
                if (hint.contains(View.AUTOFILL_HINT_USERNAME) || 
                    hint.contains("email") || 
                    hint.contains("phone")) {
                    node.autofillId?.let { usernameFields.add(it) }
                    if (textValue.isNotEmpty()) usernameValues.add(textValue)
                } 
                else if (hint.contains(View.AUTOFILL_HINT_PASSWORD)) {
                    node.autofillId?.let { passwordFields.add(it) }
                    if (textValue.isNotEmpty()) passwordValues.add(textValue)
                }
            }
        }
        
        if (node.className?.contains("EditText") == true) {
             val inputType = node.inputType
             // 129 = textPassword, 145 = textVisiblePassword, 225 = textWebPassword
             if ((inputType and 0xFFF) == 129 || (inputType and 0xFFF) == 225) {
                 if (node.autofillId != null && !passwordFields.contains(node.autofillId)) {
                     passwordFields.add(node.autofillId!!)
                     if (textValue.isNotEmpty()) passwordValues.add(textValue)
                 }
             }
        }

        for (i in 0 until node.childCount) {
            parseNode(node.getChildAt(i))
        }
    }
}