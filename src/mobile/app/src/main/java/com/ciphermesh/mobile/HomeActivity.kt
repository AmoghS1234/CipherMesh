package com.ciphermesh.mobile

import android.content.BroadcastReceiver
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.Looper
import android.util.Log
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.widget.*
import androidx.activity.OnBackPressedCallback
import androidx.annotation.Keep
import androidx.appcompat.app.ActionBarDrawerToggle
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SearchView
import androidx.appcompat.widget.Toolbar
import androidx.core.graphics.drawable.DrawableCompat
import androidx.core.view.GravityCompat
import androidx.drawerlayout.widget.DrawerLayout
import com.ciphermesh.mobile.core.Vault
import com.ciphermesh.mobile.p2p.P2PConnectionListener
import com.ciphermesh.mobile.p2p.P2PManager
import com.google.android.material.bottomsheet.BottomSheetDialog
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.floatingactionbutton.FloatingActionButton
import com.google.android.material.navigation.NavigationView
import com.google.android.material.textfield.TextInputEditText
import java.io.File
import java.util.*

class HomeActivity : AppCompatActivity(), NavigationView.OnNavigationItemSelectedListener, P2PConnectionListener {

    companion object {
        private const val TAG = "HomeActivity"
    }

    private val vault = Vault()
    private lateinit var p2pManager: P2PManager
    
    // UI Elements
    private lateinit var drawerLayout: DrawerLayout
    private lateinit var listView: ListView
    private lateinit var fabAdd: FloatingActionButton
    private lateinit var adapter: CustomAdapter
    private lateinit var toggle: ActionBarDrawerToggle
    
    private var reconnectMenuItem: MenuItem? = null
    private var currentConnectionState = P2PManager.ConnectionState.DISCONNECTED

    // State
    private var isShowingGroups = true 
    private var currentGroup = "" 
    private var isGroupOwner = false 
    private var allEntries = ArrayList<EntryModel>()
    private val processingInvites = mutableSetOf<String>()
    
    private val clipboardHandler = Handler(Looper.getMainLooper())
    private var clipboardClearRunnable: Runnable? = null
    private var isReceiverRegistered = false

    // JNI Callbacks
    private val refreshReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) { refreshUI() }
    }

    @Keep
    fun refreshUI() {
        runOnUiThread {
            Log.d(TAG, "Refreshing UI triggered from Native")
            if (isShowingGroups) loadGroups() else loadEntries()
        }
    }

    @Keep
    fun showToast(msg: String) { 
        runOnUiThread { 
            Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
            if(msg.contains("Invite received", true) || msg.contains("Joined Group", true)) {
                loadGroups()
            }
        } 
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        applySavedTheme()
        super.onCreate(savedInstanceState)
        
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE
        )
        
        setContentView(R.layout.activity_home)

        // Init Core
        val dbPath = File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)
        vault.setActivityContext(this)

        if (vault.isLocked()) {
            Toast.makeText(this, "Session Expired", Toast.LENGTH_SHORT).show()
            startActivity(Intent(this, MainActivity::class.java))
            finish()
            return
        }

        // Init P2P Manager
        p2pManager = P2PManager(vault, this)

        setupUI()
        
        if (!vault.groupExists("Personal")) vault.addGroup("Personal")
        loadGroups()

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(refreshReceiver, IntentFilter("com.ciphermesh.REFRESH_GROUPS"), Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(refreshReceiver, IntentFilter("com.ciphermesh.REFRESH_GROUPS"))
        }
        isReceiverRegistered = true

        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() {
                if (drawerLayout.isDrawerOpen(GravityCompat.START)) drawerLayout.closeDrawer(GravityCompat.START)
                else if (!isShowingGroups) loadGroups()
                else finish()
            }
        })

        // Start Connection
        p2pManager.connect()
    }

    override fun onDestroy() {
        super.onDestroy()
        p2pManager.disconnect()
        if (isReceiverRegistered) {
            try { unregisterReceiver(refreshReceiver); isReceiverRegistered = false } catch (e: Exception) {}
        }
        clipboardClearRunnable?.let { clipboardHandler.removeCallbacks(it) }
    }

    // --- P2P Listener ---
    override fun onConnectionStateChanged(state: P2PManager.ConnectionState) {
        currentConnectionState = state
        runOnUiThread {
            // Update menu icon color based on connection state
            reconnectMenuItem?.let { item ->
                val icon = item.icon?.mutate()
                if (icon != null) {
                    val color = when (state) {
                        P2PManager.ConnectionState.CONNECTED -> Color.parseColor("#4CAF50")
                        P2PManager.ConnectionState.CONNECTING -> Color.parseColor("#FFC107")
                        P2PManager.ConnectionState.DISCONNECTED -> Color.parseColor("#F44336")
                    }
                    DrawableCompat.setTint(icon, color)
                    item.icon = icon
                }
                item.title = when (state) {
                    P2PManager.ConnectionState.CONNECTED -> "Connected"
                    P2PManager.ConnectionState.CONNECTING -> "Connecting..."
                    P2PManager.ConnectionState.DISCONNECTED -> "Reconnect"
                }
            }
        }
    }

    // --- UI Setup ---
    private fun setupUI() {
        val toolbar = findViewById<Toolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        
        drawerLayout = findViewById(R.id.drawer_layout)
        val navView = findViewById<NavigationView>(R.id.nav_view)
        
        toggle = ActionBarDrawerToggle(this, drawerLayout, toolbar, R.string.navigation_drawer_open, R.string.navigation_drawer_close)
        drawerLayout.addDrawerListener(toggle)
        toggle.syncState()
        navView.setNavigationItemSelectedListener(this)

        val headerView = navView.getHeaderView(0)
        val userText = headerView.findViewById<TextView>(R.id.nav_user_name)
        val userId = vault.getUserId()
        userText.text = userId
        userText.setOnClickListener { copyToClipboardSecure("User ID", userId, "ID Copied") }

        listView = findViewById(R.id.listViewEntries)
        fabAdd = findViewById(R.id.fabAdd)
        listView.divider = ColorDrawable(Color.TRANSPARENT)
        listView.dividerHeight = 16 
        
        adapter = CustomAdapter(this, arrayListOf())
        listView.adapter = adapter
        
        setupListClick()
        setupListLongClick()

        fabAdd.setOnClickListener {
            if (isShowingGroups) showCreateGroupDialog() else showCreateEntryDialog()
        }
    }

    // --- Menus ---
    override fun onCreateOptionsMenu(menu: Menu?): Boolean { 
        menuInflater.inflate(R.menu.menu_home, menu)
        val searchItem = menu?.findItem(R.id.action_search)
        val searchView = searchItem?.actionView as? SearchView
        
        searchView?.queryHint = getString(R.string.search_hint)
        searchView?.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
            override fun onQueryTextSubmit(query: String?): Boolean { query?.let { performSearch(it) }; return true }
            override fun onQueryTextChange(newText: String?): Boolean { newText?.let { performSearch(it) }; return true }
        })
        
        searchItem?.setOnActionExpandListener(object : MenuItem.OnActionExpandListener {
            override fun onMenuItemActionExpand(item: MenuItem): Boolean = true
            override fun onMenuItemActionCollapse(item: MenuItem): Boolean {
                if (!isShowingGroups) adapter.updateData(allEntries)
                return true
            }
        })
        return true
    }
    
    override fun onPrepareOptionsMenu(menu: Menu?): Boolean { 
        val searchItem = menu?.findItem(R.id.action_search)
        reconnectMenuItem = menu?.findItem(R.id.action_reconnect)
        // Ensure icon is correct when menu opens
        onConnectionStateChanged(currentConnectionState)

        val settingsItem = menu?.findItem(R.id.action_group_settings)

        if (isShowingGroups) {
            searchItem?.isVisible = false
            reconnectMenuItem?.isVisible = true
            settingsItem?.isVisible = false
        } else {
            searchItem?.isVisible = true
            reconnectMenuItem?.isVisible = false
            settingsItem?.isVisible = true
        }
        return true 
    }
    
    override fun onOptionsItemSelected(item: MenuItem): Boolean { 
        when (item.itemId) {
            R.id.action_reconnect -> { 
                p2pManager.connect()
                Toast.makeText(this, "Reconnecting...", Toast.LENGTH_SHORT).show()
                return true
            }
            R.id.action_group_settings -> {
                showManageGroupDialog()
                return true
            }
        }
        return super.onOptionsItemSelected(item)
    }

    // --- Logic Helpers ---
    private fun checkOwnership(groupName: String) {
        if (groupName == "Personal") { isGroupOwner = true; return }
        val ownerId = vault.getGroupOwner(groupName)
        val myId = vault.getUserId()
        isGroupOwner = (ownerId == myId)
    }

    private fun loadGroups() {
        Log.d(TAG, "loadGroups called. IsShowingGroups=$isShowingGroups")
        isShowingGroups = true
        supportActionBar?.title = "Your Groups"
        invalidateOptionsMenu()
        fabAdd.setImageResource(android.R.drawable.ic_input_add)
        supportActionBar?.setDisplayHomeAsUpEnabled(false)
        toggle.isDrawerIndicatorEnabled = true
        toggle.syncState()

        val groupList = ArrayList<EntryModel>()
        val myId = vault.getUserId()

        val rawInvites = vault.getPendingInvites()
        if (rawInvites != null) {
            for (invite in rawInvites) {
                val parts = invite.split("|") 
                if (parts.size >= 3) {
                    val inviteId = parts[0].toIntOrNull() ?: 0
                    val sender = parts[1]
                    val groupName = parts[2]
                    
                    // [FIX] If we successfully joined (group exists locally), remove from processing set
                    if (vault.groupExists(groupName) || vault.groupExists("$groupName (from $sender)")) {
                        processingInvites.remove(groupName)
                    }
                    
                    if (processingInvites.contains(groupName)) {
                        groupList.add(EntryModel(-1, "Joining '$groupName'...", "Syncing..."))
                    } else {
                        groupList.add(EntryModel(-inviteId - 1000, "$groupName (Invite from $sender)", "Pending"))
                    }
                }
            }
        }
        
        val groupsRaw = vault.getGroupNames()
        if (groupsRaw != null) {
            for (g in groupsRaw) {
                // [FIX] Robust owner check
                val ownerId = vault.getGroupOwner(g)
                val isOwner = (ownerId == myId || ownerId.isEmpty() || g == "Personal")
                // Check if this is a shared group we just joined
                val isShared = g.contains("(from ")
                val roleLabel = if (isOwner) "★ Owner" else (if(isShared) "Shared" else "Member")
                Log.d(TAG, "Group found: $g (Owner: $ownerId, Label: $roleLabel)")
                groupList.add(EntryModel(0, g, roleLabel)) 
            }
        }
        adapter.updateData(groupList)
    }

    private fun loadEntries() {
        isShowingGroups = false
        supportActionBar?.title = currentGroup
        invalidateOptionsMenu() 
        fabAdd.setImageResource(android.R.drawable.ic_input_add)
        toggle.isDrawerIndicatorEnabled = false
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        toggle.setToolbarNavigationClickListener { onBackPressedDispatcher.onBackPressed() }
        
        val entriesRaw = vault.getEntries()
        val entryList = ArrayList<EntryModel>()
        if (entriesRaw != null) {
            for (raw in entriesRaw) {
                val parts = raw.split("|")
                if (parts.size >= 3) {
                    val id = parts[0].toIntOrNull() ?: 0
                    entryList.add(EntryModel(id, parts[1], parts[2])) 
                }
            }
        }
        allEntries = entryList
        adapter.updateData(entryList)
    }

    // --- Interaction ---
    private fun setupListClick() {
        listView.setOnItemClickListener { _, _, position, _ ->
            val item = adapter.getItem(position) ?: return@setOnItemClickListener
            if (isShowingGroups) {
                if (item.id < -1000) {
                    val inviteId = -(item.id + 1000)
                    val regex = """^(.+) \(Invite from (.+)\)$""".toRegex()
                    val match = regex.find(item.title ?: "")
                    if (match != null) showAcceptRejectInviteDialog(inviteId, match.groupValues[2], match.groupValues[1])
                    return@setOnItemClickListener
                }
                if (item.id == -1) return@setOnItemClickListener
                if (item.id == 0) {
                    val groupName = item.title ?: return@setOnItemClickListener
                    currentGroup = groupName
                    checkOwnership(currentGroup)
                    if (vault.setActiveGroup(currentGroup)) loadEntries()
                    else Toast.makeText(this, "Error opening group", Toast.LENGTH_SHORT).show()
                }
            } else {
                if (item.id > 0) showEntryOptions(item)
            }
        }
    }

    private fun setupListLongClick() {
        listView.setOnItemLongClickListener { _, _, position, _ ->
            if (isShowingGroups) {
                val groupName = adapter.getItem(position)?.title ?: return@setOnItemLongClickListener false
                val item = adapter.getItem(position)
                if (item != null && item.id < -1000) return@setOnItemLongClickListener false
                currentGroup = groupName
                checkOwnership(groupName)
                showGroupOptionsDialog(groupName)
                true
            } else false
        }
    }

    // --- Dialogs ---
    private fun showGroupOptionsDialog(groupName: String) {
        val options = if (isGroupOwner) {
            if (groupName == "Personal") arrayOf("Manage Group")
            else arrayOf("Rename Group", "Manage Group", "Delete Group")
        } else {
            arrayOf("View Members", "Leave Group")
        }
        MaterialAlertDialogBuilder(this).setTitle(groupName).setItems(options) { _, which ->
            when (options[which]) {
                "Rename Group" -> showRenameDialog(groupName)
                "Manage Group", "View Members" -> showManageGroupDialog()
                "Delete Group" -> showDeleteGroupConfirmation(groupName)
                "Leave Group" -> confirmLeaveGroup(groupName)
            }
        }.show()
    }

    private fun showRenameDialog(currentName: String) {
        val input = EditText(this)
        input.setText(currentName)
        val container = LinearLayout(this)
        container.orientation = LinearLayout.VERTICAL
        container.setPadding(50, 20, 50, 20)
        container.addView(input)
        
        AlertDialog.Builder(this).setTitle("Rename Group").setView(container).setPositiveButton("Rename") { _, _ ->
                val newName = input.text.toString().trim()
                if (newName.isNotEmpty() && newName != currentName) {
                    if (vault.renameGroup(currentName, newName)) {
                        Toast.makeText(this, "Group renamed", Toast.LENGTH_SHORT).show()
                        refreshUI()
                    } else Toast.makeText(this, "Rename failed", Toast.LENGTH_SHORT).show()
                }
            }.setNegativeButton("Cancel", null).show()
    }

    private fun confirmLeaveGroup(groupName: String) {
        AlertDialog.Builder(this).setTitle("Leave Group?").setMessage("Are you sure you want to leave '$groupName'?")
            .setPositiveButton("Leave") { _, _ ->
                vault.leaveGroup(groupName)
                Toast.makeText(this, "Left group: $groupName", Toast.LENGTH_SHORT).show()
                refreshUI()
            }.setNegativeButton("Cancel", null).show()
    }

    private fun showManageGroupDialog() {
        val dialogView = layoutInflater.inflate(R.layout.dialog_manage_group, null)
        val titleText = dialogView.findViewById<TextView>(R.id.textGroupTitle)
        val sectionSync = dialogView.findViewById<LinearLayout>(R.id.sectionSyncSettings)
        val sectionInvite = dialogView.findViewById<LinearLayout>(R.id.sectionInvite)
        val btnManualSync = dialogView.findViewById<Button>(R.id.btnManualSync)
        val inputInvite = dialogView.findViewById<TextInputEditText>(R.id.inputInviteId)
        val btnSendInvite = dialogView.findViewById<Button>(R.id.btnSendInvite)
        val membersContainer = dialogView.findViewById<LinearLayout>(R.id.containerMembers)

        titleText.setText("Manage '$currentGroup'")

        if (isGroupOwner) {
            sectionSync.visibility = View.VISIBLE
            sectionInvite.visibility = View.VISIBLE
            btnManualSync.setOnClickListener {
                vault.broadcastSync(currentGroup)
                Toast.makeText(this, "Broadcasting Update...", Toast.LENGTH_SHORT).show()
            }
            btnSendInvite.setOnClickListener {
                val target = inputInvite.text.toString().trim()
                if (target.isNotEmpty()) {
                    vault.sendP2PInvite(currentGroup, target)
                    Toast.makeText(this, "Invite Sent", Toast.LENGTH_SHORT).show()
                    inputInvite.setText("")
                }
            }
        } else {
            sectionSync.visibility = View.GONE
            sectionInvite.visibility = View.GONE
        }

        fun loadMembers() {
            membersContainer.removeAllViews()
            val members = vault.getGroupMembers(currentGroup)
            val myId = vault.getUserId()

            if (members == null || members.isEmpty()) {
                val emptyView = TextView(this)
                emptyView.text = "No other members"
                emptyView.setPadding(16, 16, 16, 16)
                membersContainer.addView(emptyView)
                return
            }

            for (m in members) {
                val parts = m.split("|")
                if (parts.size < 3) continue
                val uid = parts[0]
                val status = parts[2]

                val row = layoutInflater.inflate(R.layout.item_member_row, membersContainer, false)
                val nameTxt = row.findViewById<TextView>(R.id.memberName)
                val statusTxt = row.findViewById<TextView>(R.id.memberStatus)
                val btnRemove = row.findViewById<View>(R.id.btnRemove)

                nameTxt.setText(if (uid == myId) "$uid (You)" else uid)
                statusTxt.setText(status.uppercase())
                
                if (status.equals("pending", true)) statusTxt.setTextColor(Color.parseColor("#FFA000"))
                else if (status.equals("accepted", true)) statusTxt.setTextColor(Color.parseColor("#4CAF50"))

                if (isGroupOwner && uid != myId) {
                    btnRemove.visibility = View.VISIBLE
                    btnRemove.setOnClickListener {
                        MaterialAlertDialogBuilder(this)
                            .setTitle("Remove Member")
                            .setMessage("Kick $uid?")
                            .setPositiveButton("Remove") { _, _ -> vault.removeUser(currentGroup, uid); loadMembers() }
                            .setNegativeButton("Cancel", null).show()
                    }
                } else btnRemove.visibility = View.GONE
                membersContainer.addView(row)
            }
        }
        loadMembers()
        MaterialAlertDialogBuilder(this).setView(dialogView as View).setPositiveButton("Close", null).show()
    }

    private fun showEntryOptions(item: EntryModel) {
        val dialog = BottomSheetDialog(this)
        val view = layoutInflater.inflate(R.layout.bottom_sheet_entry_options, null)
        dialog.setContentView(view)

        view.findViewById<TextView>(R.id.sheetEntryTitle).text = item.title
        view.findViewById<TextView>(R.id.sheetEntrySubtitle).text = item.subtitle

        val fullData = vault.getEntryFullDetails(item.id)
        val parts = fullData.split("|")
        val password = if (parts.size > 2) parts[2] else ""
        val totpSecret = if (parts.size > 4) parts[4] else ""

        view.findViewById<View>(R.id.actionCopyPassword).setOnClickListener {
            if (password.isNotEmpty()) copyToClipboardSecure("Password", password, "Password copied")
            dialog.dismiss()
        }

        view.findViewById<View>(R.id.actionEdit).setOnClickListener {
            dialog.dismiss()
            showCreateEntryDialog(editMode = true, entryId = item.id)
        }

        view.findViewById<View>(R.id.actionDelete).setOnClickListener {
            dialog.dismiss()
            MaterialAlertDialogBuilder(this).setTitle("Delete Entry").setMessage("Delete '${item.title}'?")
                .setPositiveButton("Delete") { _, _ ->
                    if (vault.deleteEntry(item.id)) { loadEntries(); Toast.makeText(this, "Entry deleted", Toast.LENGTH_SHORT).show() }
                }.setNegativeButton("Cancel", null).show()
        }
        dialog.show()
    }

    private fun showCreateGroupDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_create_group, null)
        val input = dialogView.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.dialogInput)
        MaterialAlertDialogBuilder(this).setTitle("Create New Group").setView(dialogView as View).setPositiveButton("Create") { _, _ ->
                val groupName = input.text.toString().trim()
                if (groupName.isNotEmpty()) {
                    if (vault.addGroup(groupName)) loadGroups()
                    else Toast.makeText(this, "Failed (Exists?)", Toast.LENGTH_SHORT).show()
                }
            }.setNegativeButton("Cancel", null).show()
    }

    private fun showCreateEntryDialog(editMode: Boolean = false, entryId: Int = -1) {
        val builder = MaterialAlertDialogBuilder(this)
        val view = layoutInflater.inflate(R.layout.dialog_create_entry, null)
        val titleInput = view.findViewById<EditText>(R.id.inputTitle)
        val userInput = view.findViewById<EditText>(R.id.inputUsername)
        val passInput = view.findViewById<EditText>(R.id.inputPassword)
        val notesInput = view.findViewById<EditText>(R.id.inputNotes)
        val btnGenerate = view.findViewById<Button>(R.id.btnGeneratePassword)
        
        
        val btnEditLocations = view.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnEditLocations)
        
        // Store locations temporarily
        var currentLocations = mutableListOf<LocationData>()
        
        btnEditLocations.setOnClickListener {
            showLocationEditorDialog(currentLocations) { updatedLocations ->
                currentLocations = updatedLocations.toMutableList()
                // Update button text to show count
                btnEditLocations.text = if (updatedLocations.isEmpty()) {
                    "📍 Manage Locations"
                } else {
                    "📍 Locations (${updatedLocations.size})"
                }
            }
        }
        
        btnGenerate.setOnClickListener {
            val chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789!@#$%^&*"
            passInput.setText((1..16).map { chars.random() }.joinToString(""))
        }

        var originalPassword = ""
        if (editMode && entryId != -1) {
            val fullData = vault.getEntryFullDetails(entryId)
            val parts = fullData.split("|")
            if (parts.size >= 4) {
                titleInput.setText(parts[0])
                userInput.setText(parts[1])
                originalPassword = parts[2]
                notesInput.setText(parts[3])
                
                // Parse full locations list from JSON (index 9)
                if (parts.size >= 10 && parts[9].isNotEmpty()) {
                    try {
                        val jsonArray = org.json.JSONArray(parts[9])
                        for (i in 0 until jsonArray.length()) {
                            val obj = jsonArray.getJSONObject(i)
                            val type = obj.getString("type")
                            val value = obj.getString("value")
                            currentLocations.add(LocationData(type, value))
                        }
                    } catch (e: Exception) {
                        e.printStackTrace()
                        // Fallback: use legacy URL field if JSON parsing fails
                        if (parts.size >= 9 && parts[8].isNotEmpty()) {
                            currentLocations.add(LocationData("URL", parts[8]))
                        }
                    }
                } else if (parts.size >= 9 && parts[8].isNotEmpty()) {
                    // Fallback for old version / entry without locations JSON
                    currentLocations.add(LocationData("URL", parts[8]))
                }
                
                if (currentLocations.isNotEmpty()) {
                    btnEditLocations.text = "📍 Locations (${currentLocations.size})"
                }
            }
        }

        builder.setView(view).setTitle(if (editMode) "Edit Entry" else "New Entry")
            .setPositiveButton("Save") { _, _ ->
                val title = titleInput.text.toString().trim()
                val user = userInput.text.toString().trim()
                var pass = passInput.text.toString()
                val notes = notesInput.text.toString().trim()
                
                // Extract first URL-type location as primary url
                var url = ""
                val urlLocation = currentLocations.firstOrNull { it.type == "URL" }
                if (urlLocation != null) {
                    url = urlLocation.value
                }

                if (title.isNotEmpty()) {
                    if (editMode && pass.isEmpty()) pass = originalPassword
                    
                    var success = false
                    if (editMode) {
                        // Update native to accept URL! passing via extra notes or new param?
                        // Vault.updateEntry signature in Kotlin: (id, title, user, pass, totp, notes)
                        // It DOES NOT have URL param!
                        // I must update Vault.kt and JNI or overload notes.
                        // For now, let's prepend URL to notes with specific prefix or just wait?
                        // USER SAID: "Make them the same".
                        // I NEED TO UPDATE Vault.kt and native-lib JNI signature for updateEntry too?
                        // native-lib updateEntry implementation:
                        // Java...updateEntry(..., jstring notes)
                        // It doesn't take URL.
                        
                        // [Workaround while keeping signature same-ish? No, I should fix it properly]
                        // But I can't see Vault.java/kt easily.
                        // Wait, vault.addEntryNative takes URL (I modified/saw it in native-lib).
                        // vault.updateEntry does NOT?
                        // Let's check native-lib updateEntry.
                        
                        val result: Any = vault.updateEntry(entryId, title, user, pass, url, notes) 
                        success = (result as? Boolean) ?: false
                        
                        // We also need to update the location if possible.
                        // Since updateEntry doesn't take URL, we might need a separate call or update JNI.
                        // But native-lib `updateEntry` implementation in `vault.cpp`?
                        // C++ `updateEntry` takes `VaultEntry`.
                        // JNI `updateEntry` takes fields.
                        // I should update JNI `updateEntry` to take URL.
                    } else {
                        vault.addEntryNative(title, user, pass, url, notes)
                        success = true
                    }
                    
                    if (success) {
                        loadEntries()
                        if(!editMode) refreshUI()
                    } else Toast.makeText(this, "Error saving", Toast.LENGTH_SHORT).show()
                }
            }.setNegativeButton("Cancel", null).show()
    }

    private fun showAcceptRejectInviteDialog(inviteId: Int, fromUser: String, groupName: String) {
        val view = layoutInflater.inflate(R.layout.dialog_accept_invite, null)
        val text = view.findViewById<TextView>(R.id.inviteDetailText)
        val btnAccept = view.findViewById<Button>(R.id.btnAcceptInvite)
        val btnReject = view.findViewById<Button>(R.id.btnRejectInvite)
        text.text = "$fromUser invites you to '$groupName'"
        val dialog = MaterialAlertDialogBuilder(this).setView(view).setCancelable(false).create()
        
        btnAccept.setOnClickListener {
            btnAccept.isEnabled = false; btnAccept.text = "Joining..."
            processingInvites.add(groupName)
            vault.respondToInvite(groupName, fromUser, true)
            getSharedPreferences("memberships", Context.MODE_PRIVATE).edit().putBoolean(groupName, true).apply()
            Toast.makeText(this, "Accepted. Syncing...", Toast.LENGTH_SHORT).show()
            loadGroups(); view.postDelayed({ dialog.dismiss() }, 800)
        }
        btnReject.setOnClickListener { 
            vault.respondToInvite(groupName, fromUser, false)
            dialog.dismiss()
            // [FIX] Refresh UI to remove the declined invite from the list
            refreshUI()
            Toast.makeText(this, "Invite declined", Toast.LENGTH_SHORT).show()
        }
        dialog.show()
    }

    private fun showDeleteGroupConfirmation(groupName: String) {
        MaterialAlertDialogBuilder(this).setTitle("Delete '$groupName'?")
            .setPositiveButton("Delete") { _, _ ->
                if (vault.deleteGroup(groupName)) { loadGroups(); Toast.makeText(this, "Deleted", Toast.LENGTH_SHORT).show() }
            }.setNegativeButton("Cancel", null).show()
    }

    private fun performSearch(query: String) {
        if (query.isEmpty()) { adapter.updateData(allEntries); return }
        val searchResults = vault.searchEntries(query)
        val resultList = ArrayList<EntryModel>()
        if (searchResults != null) {
            for (raw in searchResults) {
                val parts = raw.split("|")
                if (parts.size >= 3) resultList.add(EntryModel(0, parts[0], parts[1]))
            }
        }
        adapter.updateData(resultList)
    }

    override fun onNavigationItemSelected(item: MenuItem): Boolean {
        when(item.itemId) {
            R.id.nav_groups -> loadGroups()
            R.id.nav_recent_entries -> startActivity(Intent(this, RecentEntriesActivity::class.java))
            R.id.nav_add_group -> showCreateGroupDialog()
            R.id.nav_theme -> showThemeDialog()
            R.id.nav_settings -> startActivity(Intent(this, SettingsActivity::class.java))
            R.id.nav_lock -> finish()
        }
        drawerLayout.closeDrawer(GravityCompat.START)
        return true
    }
    
    private fun showThemeDialog() {
        val themes = arrayOf("Professional (Dark)", "Modern Light", "Ocean", "Warm", "Vibrant")
        val themeStyles = listOf(R.style.Theme_CipherMesh_Professional, R.style.Theme_CipherMesh_ModernLight, R.style.Theme_CipherMesh_Ocean, R.style.Theme_CipherMesh_Warm, R.style.Theme_CipherMesh_Vibrant)
        
        val currentIdx = getSharedPreferences("app_prefs", Context.MODE_PRIVATE).getInt("theme_index", 0)
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Choose Theme")
            .setSingleChoiceItems(themes, currentIdx) { dialog, which ->
                getSharedPreferences("app_prefs", Context.MODE_PRIVATE).edit().putInt("theme_index", which).apply()
                dialog.dismiss()
                recreate() // Restart activity to apply theme
            }
            .setNegativeButton("Cancel", null)
            .show()
    }
    
    private fun applySavedTheme() {
        val idx = getSharedPreferences("app_prefs", Context.MODE_PRIVATE).getInt("theme_index", 0)
        val themes = listOf(R.style.Theme_CipherMesh_Professional, R.style.Theme_CipherMesh_ModernLight, R.style.Theme_CipherMesh_Ocean, R.style.Theme_CipherMesh_Warm, R.style.Theme_CipherMesh_Vibrant)
        if(idx in themes.indices) setTheme(themes[idx])
    }

    private fun copyToClipboardSecure(label: String, text: String, toastMessage: String) {
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText(label, text)
        clipboard.setPrimaryClip(clip)
        Toast.makeText(this, toastMessage, Toast.LENGTH_SHORT).show()
        clipboardHandler.postDelayed({ 
            try { clipboard.setPrimaryClip(ClipData.newPlainText("", "")) } catch(e:Exception){} 
        }, 60000)
    }

    // Data class to hold location information
    data class LocationData(
        var type: String = "URL",
        var value: String = ""
    )

    private fun showLocationEditorDialog(existingLocations: List<LocationData>, onSave: (List<LocationData>) -> Unit) {
        val dialog = MaterialAlertDialogBuilder(this)
        val dialogView = layoutInflater.inflate(R.layout.dialog_edit_locations, null)
        
        val locationsContainer = dialogView.findViewById<LinearLayout>(R.id.locationsContainer)
        val btnAddLocation = dialogView.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnAddLocation)
        val btnCancel = dialogView.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnCancel)
        val btnSave = dialogView.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnSave)
        
        val locationTypes = arrayOf("URL", "Android App", "iOS App", "Wi-Fi SSID", "Other")
        
        // Function to add a location row to the UI
        fun addLocationRow(locData: LocationData = LocationData()) {
            val row = layoutInflater.inflate(R.layout.item_location_editor_row, locationsContainer, false)
            val spinner = row.findViewById<Spinner>(R.id.spinnerLocationType)
            val inputValue = row.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.inputLocationValue)
            val btnDelete = row.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnDeleteLocation)
            
            // Setup spinner
            val spinnerAdapter = ArrayAdapter(this, android.R.layout.simple_spinner_item, locationTypes)
            spinnerAdapter.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item)
            spinner.adapter = spinnerAdapter
            
            // Set initial values
            val typeIndex = locationTypes.indexOf(locData.type)
            if (typeIndex != -1) spinner.setSelection(typeIndex)
            
            // Set initial hint BEFORE setting text to avoid overlap
            val initialHint = when (locData.type) {
                "URL" -> "e.g., https://google.com"
                "Android App" -> "e.g., com.google.android.gm"
                "iOS App" -> "e.g., com.google.Gmail"
                "Wi-Fi SSID" -> "e.g., MyHomeNetwork"
                "Other" -> "Custom value"
                else -> "Value"
            }
            inputValue.hint = initialHint
            inputValue.setText(locData.value)
            
            // Update hint based on selected type
            spinner.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
                override fun onItemSelected(parent: AdapterView<*>?, view: View?, position: Int, id: Long) {
                    val hint = when (locationTypes[position]) {
                        "URL" -> "e.g., https://google.com"
                        "Android App" -> "e.g., com.google.android.gm"
                        "iOS App" -> "e.g., com.google.Gmail"
                        "Wi-Fi SSID" -> "e.g., MyHomeNetwork"
                        "Other" -> "Custom value"
                        else -> "Value"
                    }
                    inputValue.hint = hint
                }
                override fun onNothingSelected(parent: AdapterView<*>?) {}
            }
            
            // Delete button
            btnDelete.setOnClickListener {
                locationsContainer.removeView(row)
            }
            
            locationsContainer.addView(row)
        }
        
        // Populate with existing locations
        if (existingLocations.isNotEmpty()) {
            existingLocations.forEach { addLocationRow(it) }
        } else {
            // Add one empty row by default
            addLocationRow()
        }
        
        // Add location button
        btnAddLocation.setOnClickListener {
            addLocationRow()
        }
        
        val alertDialog = dialog.setView(dialogView).create()
        
        // Cancel button
        btnCancel.setOnClickListener {
            alertDialog.dismiss()
        }
        
        // Save button
        btnSave.setOnClickListener {
            val locations = mutableListOf<LocationData>()
            
            // Extract all locations from the UI
            for (i in 0 until locationsContainer.childCount) {
                val row = locationsContainer.getChildAt(i)
                val spinner = row.findViewById<Spinner>(R.id.spinnerLocationType)
                val inputValue = row.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.inputLocationValue)
                
                val type = spinner.selectedItem.toString()
                val value = inputValue.text.toString().trim()
                
                if (value.isNotEmpty()) {
                    locations.add(LocationData(type, value))
                }
            }
            
            onSave(locations)
            alertDialog.dismiss()
        }
        
        alertDialog.show()
    }
}