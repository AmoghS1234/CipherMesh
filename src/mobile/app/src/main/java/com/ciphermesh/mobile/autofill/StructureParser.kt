package com.ciphermesh.mobile.autofill

import android.app.assist.AssistStructure
import android.os.Build
import android.view.View
import android.view.autofill.AutofillId
import android.util.Log
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
        Log.d("StructureParser", "Starting parse(). WindowNodeCount: ${structure.windowNodeCount}")
        val nodes = structure.windowNodeCount
        for (i in 0 until nodes) {
            try {
                val windowNode = structure.getWindowNodeAt(i)
                val rootNode = windowNode.rootViewNode
                
                // [FIX] ViewNode uses 'idPackage', not 'packageName'
                packageName = rootNode.idPackage?.toString()
                Log.d("StructureParser", "Processing window $i, package: $packageName")
                
                parseNode(rootNode)
            } catch (e: Exception) {
                Log.e("StructureParser", "Error parsing window node $i: ${e.message}")
            }
        }
        
        // [FIX] Last Resort Heuristic if standard strict parsing failed
        if (passwordFields.isEmpty()) {
             Log.d("StructureParser", "No password fields found. Trying last resort heuristic.")
             lastResortHeuristic()
        }
        Log.d("StructureParser", "Parse complete. Found ${usernameFields.size} users, ${passwordFields.size} passwords.")
    }

    private fun isInputClass(className: String?): Boolean {
        if (className == null) return false
        val cls = className.lowercase()
        return cls.contains("edittext") || 
               cls.contains("autocompletetextview") || 
               cls.contains("textinput") || 
               cls.contains("entry") // generic term often used in cross-platform frameworks
    }

    private val potentialInputs = mutableListOf<AssistStructure.ViewNode>()

    private fun parseNode(node: AssistStructure.ViewNode) {
        if (node.webDomain != null) {
            webDomain = node.webDomain
        }

        val hints = node.autofillHints
        val textValue = node.text?.toString() ?: ""
        val idEntry = node.idEntry
        val desc = node.contentDescription?.toString()?.lowercase() ?: ""
        val className = node.className
        
        // Collect potential inputs for fallback
        if (isInputClass(className)) {
            potentialInputs.add(node)
        }
        
        var isPassword = false
        var isUsername = false

        // 1. Check Explicit Autofill Hints
        if (hints != null) {
            for (hint in hints) {
                if (hint.contains(View.AUTOFILL_HINT_PASSWORD)) {
                    isPassword = true
                } else if (hint.contains(View.AUTOFILL_HINT_USERNAME) || 
                           hint.contains("email") || 
                           hint.contains("phone") || 
                           hint.contains("account")) {
                    isUsername = true
                }
            }
        }

        // 2. Check Input Type (Strong signal)
        if (isInputClass(className) && !isPassword && !isUsername) {
             val inputType = node.inputType
             // 129=textPassword, 145=textVisiblePassword, 225=textWebPassword
             if ((inputType and 0xFFF) == 129 || (inputType and 0xFFF) == 225 || (inputType and 0xFFF) == 145) {
                 isPassword = true
             } else if ((inputType and 0xFFF) == 33) { // textEmailAddress
                 isUsername = true
             }
        }
        
        // 3. Check Resource IDs (idEntry)
        if (idEntry != null && !isPassword && !isUsername) {
            val idLower = idEntry.lowercase()
            if (idLower.contains("pass") || idLower.contains("pwd")) {
                 if (isInputClass(className)) isPassword = true
            } else if (idLower.contains("user") || idLower.contains("email") || idLower.contains("login") || idLower.contains("account")) {
                 if (isInputClass(className)) isUsername = true
            }
        }

        // 4. Check Content Description
        if (desc.isNotEmpty() && isInputClass(className) && !isPassword && !isUsername) {
            if (desc.contains("pass") || desc.contains("pwd")) {
                isPassword = true
            } else if (desc.contains("user") || desc.contains("email") || desc.contains("login")) {
                isUsername = true
            }
        }

        // Final Assignment
        if (isPassword) {
            node.autofillId?.let { if (!passwordFields.contains(it)) passwordFields.add(it) }
            if (textValue.isNotEmpty()) passwordValues.add(textValue)
        } else if (isUsername) {
            node.autofillId?.let { if (!usernameFields.contains(it)) usernameFields.add(it) }
            if (textValue.isNotEmpty()) usernameValues.add(textValue)
        }

        for (i in 0 until node.childCount) {
            parseNode(node.getChildAt(i))
        }
    }
    
    private fun lastResortHeuristic() {
        // Fallback: If we have exactly 2 inputs, and 2nd is password-like or 1st is text/email.
        // Or if we found NO password fields, look for any input with type=password
        
        if (potentialInputs.isEmpty()) return
        
        // Scan for ANY password type input we might have missed (e.g. custom view with correct inputType but weird class)
        for (node in potentialInputs) {
            val inputType = node.inputType
            if ((inputType and 0xFFF) == 129 || (inputType and 0xFFF) == 225 || (inputType and 0xFFF) == 145) {
                // Found a password field we missed!
                node.autofillId?.let { if (!passwordFields.contains(it)) passwordFields.add(it) }
                val txt = node.text?.toString() ?: ""
                if (txt.isNotEmpty()) passwordValues.add(txt)
            }
        }
        
        // If we found a password now, let's assume the input immediately before it is the username
        if (passwordFields.isNotEmpty() && usernameFields.isEmpty()) {
            val pwdId = passwordFields[0]
            // Find index of password field in potentialInputs
            val paramIndex = potentialInputs.indexOfFirst { it.autofillId == pwdId }
            if (paramIndex > 0) {
                val userNode = potentialInputs[paramIndex - 1]
                // Safety: ensure it's not another password field
                val inputType = userNode.inputType
                if ((inputType and 0xFFF) != 129 && (inputType and 0xFFF) != 225 && (inputType and 0xFFF) != 145) {
                    userNode.autofillId?.let { usernameFields.add(it) }
                     val txt = userNode.text?.toString() ?: ""
                    if (txt.isNotEmpty()) usernameValues.add(txt)
                }
            }
        }
    }
}