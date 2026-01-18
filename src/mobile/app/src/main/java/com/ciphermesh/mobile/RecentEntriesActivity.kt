package com.ciphermesh.mobile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.widget.ListView
import android.widget.Toast
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.Toolbar
import com.ciphermesh.mobile.core.Vault
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import java.io.File

class RecentEntriesActivity : AppCompatActivity() {

    companion object {
        private const val ENTRY_DETAILS_PARTS_COUNT = 8
    }

    private val vault = Vault()
    private lateinit var listView: ListView
    private lateinit var adapter: CustomAdapter

    // [FIX] Helper to load the correct theme
    private fun applySavedTheme() {
        val idx = getSharedPreferences("app_prefs", Context.MODE_PRIVATE).getInt("theme_index", 0)
        val themes = listOf(
            R.style.Theme_CipherMesh_Professional, 
            R.style.Theme_CipherMesh_ModernLight, 
            R.style.Theme_CipherMesh_Ocean, 
            R.style.Theme_CipherMesh_Warm, 
            R.style.Theme_CipherMesh_Vibrant
        )
        if(idx in themes.indices) setTheme(themes[idx])
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        // [FIX] Apply Theme BEFORE super.onCreate
        applySavedTheme()
        super.onCreate(savedInstanceState)
        
        // Security: Prevent screenshots
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE
        )

        setContentView(R.layout.activity_recent_entries)

        val toolbar = findViewById<com.google.android.material.appbar.MaterialToolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        supportActionBar?.title = getString(R.string.recently_accessed_title)
        
        // [FIX] Ensure back button works
        toolbar.setNavigationOnClickListener { finish() }

        val dbPath = File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)

        if (vault.isLocked()) {
            Toast.makeText(this, "Session Expired", Toast.LENGTH_SHORT).show()
            startActivity(Intent(this, MainActivity::class.java))
            finish()
            return
        }

        listView = findViewById(R.id.listViewRecentEntries)
        listView.divider = ColorDrawable(Color.TRANSPARENT)
        listView.dividerHeight = 16

        adapter = CustomAdapter(this, arrayListOf())
        listView.adapter = adapter

        loadRecentEntries()

        listView.setOnItemClickListener { _, _, position, _ ->
            val entryId = adapter.getItem(position)?.id ?: return@setOnItemClickListener
            showEntryDetails(entryId)
        }
    }

    private fun loadRecentEntries() {
        val entriesRaw = vault.getRecentlyAccessedEntries(10)
        val entryList = ArrayList<EntryModel>()
        
        if (entriesRaw.isNotEmpty()) {
            for (raw in entriesRaw) {
                val parts = raw.split(":")
                if (parts.size >= 3) {
                    entryList.add(EntryModel(parts[0].toInt(), parts[1], parts[2]))
                }
            }
        }
        
        adapter.updateData(entryList)
    }

    private fun showEntryDetails(id: Int) {
        val raw = vault.getEntryFullDetails(id)
        val parts = raw.split("|")
        if (parts.size < ENTRY_DETAILS_PARTS_COUNT) return

        MaterialAlertDialogBuilder(this)
            .setTitle(parts[0])
            .setMessage("User: ${parts[1]}\nPass: ••••••")
            .setPositiveButton("Copy") { _, _ ->
                copyToClipboard(parts[2])
            }
            .setNegativeButton("Close", null)
            .show()
    }

    private fun copyToClipboard(text: String) {
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText("Password", text)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, "Password copied", Toast.LENGTH_SHORT).show()
    }
}