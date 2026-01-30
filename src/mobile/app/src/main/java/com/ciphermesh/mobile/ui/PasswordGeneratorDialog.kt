package com.ciphermesh.mobile.ui

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.SharedPreferences
import android.view.LayoutInflater
import android.widget.EditText
import android.widget.TextView
import android.widget.Toast
import com.ciphermesh.mobile.R
import com.google.android.material.button.MaterialButton
import com.google.android.material.chip.Chip
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.slider.Slider

class PasswordGeneratorDialog(private val context: Context, private val onPasswordSelected: (String) -> Unit) {

    private val prefs: SharedPreferences = context.getSharedPreferences("PasswordGeneratorPrefs", Context.MODE_PRIVATE)
    private val defaultSymbols = "!@#\$%^&*()_+-="

    fun show() {
        val view = LayoutInflater.from(context).inflate(R.layout.dialog_password_generator, null)
        val dialog = MaterialAlertDialogBuilder(context)
            .setTitle("🔐 Password Generator")
            .setView(view)
            .create()

        val textPassword = view.findViewById<TextView>(R.id.textGeneratedPassword)
        val textLengthLabel = view.findViewById<TextView>(R.id.textLengthLabel)
        val sliderLength = view.findViewById<Slider>(R.id.sliderLength)
        val chipUpper = view.findViewById<Chip>(R.id.chipUppercase)
        val chipLower = view.findViewById<Chip>(R.id.chipLowercase)
        val chipNumbers = view.findViewById<Chip>(R.id.chipNumbers)
        val chipSymbols = view.findViewById<Chip>(R.id.chipSymbols)
        val inputCustomSymbols = view.findViewById<EditText>(R.id.inputCustomSymbols)
        val btnCopy = view.findViewById<MaterialButton>(R.id.btnCopyPassword)
        val btnRegenerate = view.findViewById<MaterialButton>(R.id.btnRegenerate)
        val btnUse = view.findViewById<MaterialButton>(R.id.btnUsePassword)

        // Load Prefs
        sliderLength.value = prefs.getInt("length", 16).toFloat().coerceIn(4f, 64f)
        chipUpper.isChecked = prefs.getBoolean("upper", true)
        chipLower.isChecked = prefs.getBoolean("lower", true)
        chipNumbers.isChecked = prefs.getBoolean("numbers", true)
        chipSymbols.isChecked = prefs.getBoolean("symbols", true)
        inputCustomSymbols.setText(prefs.getString("customSymbols", ""))

        fun generate() {
            val length = sliderLength.value.toInt()
            textLengthLabel.text = "Length: $length"

            val useUpper = chipUpper.isChecked
            val useLower = chipLower.isChecked
            val useNumbers = chipNumbers.isChecked
            val useSymbols = chipSymbols.isChecked

            if (!useUpper && !useLower && !useNumbers && !useSymbols) {
                textPassword.text = "Select at least one"
                btnUse.isEnabled = false
                return
            }
            btnUse.isEnabled = true

            val upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            val lower = "abcdefghijklmnopqrstuvwxyz"
            val numbers = "0123456789"
            val customSyms = inputCustomSymbols.text.toString().ifEmpty { defaultSymbols }

            var chars = ""
            if (useUpper) chars += upper
            if (useLower) chars += lower
            if (useNumbers) chars += numbers
            if (useSymbols) chars += customSyms

            val password = (1..length).map { chars.random() }.joinToString("")
            textPassword.text = password
        }

        fun savePrefs() {
            with(prefs.edit()) {
                putInt("length", sliderLength.value.toInt())
                putBoolean("upper", chipUpper.isChecked)
                putBoolean("lower", chipLower.isChecked)
                putBoolean("numbers", chipNumbers.isChecked)
                putBoolean("symbols", chipSymbols.isChecked)
                putString("customSymbols", inputCustomSymbols.text.toString())
                apply()
            }
        }

        // Listeners
        sliderLength.addOnChangeListener { _, value, _ ->
            textLengthLabel.text = "Length: ${value.toInt()}"
            generate()
        }

        chipUpper.setOnCheckedChangeListener { _, _ -> generate() }
        chipLower.setOnCheckedChangeListener { _, _ -> generate() }
        chipNumbers.setOnCheckedChangeListener { _, _ -> generate() }
        chipSymbols.setOnCheckedChangeListener { _, _ -> generate() }
        
        btnRegenerate.setOnClickListener { generate() }
        
        btnCopy.setOnClickListener {
            val clipboard = context.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
            clipboard.setPrimaryClip(ClipData.newPlainText("password", textPassword.text))
            Toast.makeText(context, "Copied!", Toast.LENGTH_SHORT).show()
        }
        
        btnUse.setOnClickListener {
            savePrefs()
            onPasswordSelected(textPassword.text.toString())
            dialog.dismiss()
        }

        // Initial generation
        generate()
        dialog.show()
    }
}
