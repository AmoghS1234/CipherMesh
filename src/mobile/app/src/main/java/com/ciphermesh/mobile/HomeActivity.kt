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
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.ActionBarDrawerToggle
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.SearchView
import androidx.appcompat.widget.Toolbar
import androidx.core.view.GravityCompat
import androidx.drawerlayout.widget.DrawerLayout
import androidx.recyclerview.widget.LinearLayoutManager
import androidx.recyclerview.widget.RecyclerView
import com.ciphermesh.mobile.core.Vault
import com.ciphermesh.mobile.p2p.P2PManager
import com.ciphermesh.util.PasswordGenerator
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.floatingactionbutton.FloatingActionButton
import com.google.android.material.navigation.NavigationView
import com.google.android.material.textfield.TextInputEditText
import com.google.android.material.textfield.TextInputLayout
import java.io.File
import java.text.SimpleDateFormat
import java.util.*

class HomeActivity : AppCompatActivity(), NavigationView.OnNavigationItemSelectedListener {

    companion object {
        private val dateFormat = SimpleDateFormat("MMM dd, yyyy HH:mm", Locale.getDefault())
        private const val ENTRY_DETAILS_PARTS_COUNT = 8
        private const val TAG = "HomeActivity"
    }

    private val vault = Vault()
    private lateinit var drawerLayout: DrawerLayout
    private lateinit var listView: ListView
    private lateinit var fabAdd: FloatingActionButton
    private lateinit var adapter: CustomAdapter
    private lateinit var p2pManager: P2PManager
    
    private lateinit var toggle: ActionBarDrawerToggle
    
    private var isShowingGroups = true 
    private var currentGroup = ""
    private var isGroupOwner = false 
    
    private var allEntries = ArrayList<EntryModel>()
    
    private val clipboardHandler = Handler(Looper.getMainLooper())
    private var clipboardClearRunnable: Runnable? = null
    
    private var isReceiverRegistered = false
    private val processingInvites = mutableSetOf<String>()

    data class LocationData(var type: String, var value: String) {
        override fun toString(): String = "[$type] $value"
    }

    private val refreshReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context?, intent: Intent?) {
            if (isShowingGroups) runOnUiThread { loadGroups() }
            else runOnUiThread { loadEntries() }
        }
    }

    private fun checkOwnership(groupName: String) {
        if (groupName == "Personal") {
            isGroupOwner = true
            return
        }
        val ownerId = vault.getGroupOwner(groupName)
        val myId = vault.getUserId()
        isGroupOwner = (ownerId == myId)
        Log.d(TAG, "Ownership Check: Group=$groupName, Owner=$ownerId, Me=$myId, IsOwner=$isGroupOwner")
    }

    private fun showGroupOptionsDialog(groupName: String) {
        val options = if (isGroupOwner) {
            arrayOf("Manage Group", "Delete Group")
        } else {
            arrayOf("Members", "Leave Group")
        }

        MaterialAlertDialogBuilder(this)
            .setTitle(groupName)
            .setItems(options) { _, which ->
                when (which) {
                    0 -> showManageGroupDialog() 
                    1 -> showDeleteGroupConfirmation(groupName)
                }
            }
            .show()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        applySavedTheme()
        super.onCreate(savedInstanceState)
        
        window.setFlags(
            android.view.WindowManager.LayoutParams.FLAG_SECURE,
            android.view.WindowManager.LayoutParams.FLAG_SECURE
        )
        
        setContentView(R.layout.activity_home)

        val dbPath = File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)
        vault.setActivityContext(this)

        if (vault.isLocked()) {
            Toast.makeText(this, "Session Expired", Toast.LENGTH_SHORT).show()
            val intent = Intent(this, MainActivity::class.java)
            startActivity(intent)
            finish()
            return
        }

        vault.initP2P("wss://ciphermesh-signal-server.onrender.com")

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
        userText.setOnClickListener {
            copyToClipboardSecure("User ID", userId, "ID Copied")
        }

        listView = findViewById(R.id.listViewEntries)
        fabAdd = findViewById(R.id.fabAdd)
        listView.divider = ColorDrawable(Color.TRANSPARENT)
        listView.dividerHeight = 16 
        
        p2pManager = P2PManager(this, vault)
        p2pManager.connect()

        adapter = CustomAdapter(this, arrayListOf())
        listView.adapter = adapter

        listView.setOnItemClickListener { _, _, position, _ ->
            val entry = adapter.getItem(position) ?: return@setOnItemClickListener
            
            if (isShowingGroups) {
                if (entry.subtitle == "Pending" && entry.id < -1000) {
                    val inviteId = -(entry.id + 1000)
                    val regex = """^(.+) \(Invite from (.+)\)$""".toRegex()
                    val match = regex.find(entry.title ?: "")
                    if (match != null) {
                        showAcceptRejectInviteDialog(inviteId, match.groupValues[2], match.groupValues[1])
                    }
                    return@setOnItemClickListener
                }
                
                if (entry.subtitle == "Syncing...") {
                    Toast.makeText(this, "Syncing...", Toast.LENGTH_SHORT).show()
                    return@setOnItemClickListener
                }
                
                val groupName = entry.title ?: return@setOnItemClickListener
                Log.d(TAG, "Opening Group: $groupName")
                
                if (vault.setActiveGroup(groupName)) {
                    currentGroup = groupName
                    checkOwnership(groupName)
                    loadEntries()
                } else {
                    Log.e(TAG, "Failed to set active group: $groupName")
                    Toast.makeText(this, "Error opening group", Toast.LENGTH_SHORT).show()
                }
            } else {
                val entryId = entry.id
                showEntryDetails(entryId)
            }
        }

        listView.setOnItemLongClickListener { _, _, position, _ ->
            if (isShowingGroups) {
                val groupName = adapter.getItem(position)?.title ?: return@setOnItemLongClickListener false
                currentGroup = groupName
                checkOwnership(groupName)
                showGroupOptionsDialog(groupName)
                true
            } else false
        }

        fabAdd.setOnClickListener {
            if (isShowingGroups) showCreateGroupDialog() else showCreateEntryDialog()
        }

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
                if (drawerLayout.isDrawerOpen(GravityCompat.START)) {
                    drawerLayout.closeDrawer(GravityCompat.START)
                } else if (!isShowingGroups) {
                    loadGroups()
                } else {
                    finish() 
                }
            }
        })
    }

    override fun onDestroy() {
        super.onDestroy()
        p2pManager.disconnect()
        if (isReceiverRegistered) {
            try {
                unregisterReceiver(refreshReceiver)
                isReceiverRegistered = false
            } catch (e: IllegalArgumentException) {}
        }
        clipboardClearRunnable?.let { clipboardHandler.removeCallbacks(it) }
    }
    
    private fun copyToClipboardSecure(label: String, text: String, toastMessage: String = "Copied") {
        val clipboard = getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager
        val clip = ClipData.newPlainText(label, text)
        clipboard.setPrimaryClip(clip)
        
        clipboardClearRunnable?.let { clipboardHandler.removeCallbacks(it) }
        
        clipboardClearRunnable = Runnable {
            try {
                val emptyClip = ClipData.newPlainText("", "")
                clipboard.setPrimaryClip(emptyClip)
            } catch (e: Exception) {}
        }
        clipboardClearRunnable?.let {
            clipboardHandler.postDelayed(it, 60000) 
        }
        Toast.makeText(this, "$toastMessage (clears in 60s)", Toast.LENGTH_SHORT).show()
    }

    private fun loadEntries() {
        isShowingGroups = false
        supportActionBar?.title = currentGroup
        invalidateOptionsMenu() 
        fabAdd.setImageResource(android.R.drawable.ic_input_add)
        
        toggle.isDrawerIndicatorEnabled = false
        supportActionBar?.setDisplayHomeAsUpEnabled(true)
        
        toggle.setToolbarNavigationClickListener {
            onBackPressedDispatcher.onBackPressed()
        }
        
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

    private fun loadGroups() {
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
        for (invite in rawInvites) {
            val parts = invite.split("|") 
            if (parts.size >= 3) {
                val inviteId = parts[0].toIntOrNull() ?: 0
                val sender = parts[1]
                val groupName = parts[2]
                
                if (processingInvites.contains(groupName)) {
                    groupList.add(EntryModel(-1, "Joining '$groupName'...", "Syncing..."))
                } else {
                    groupList.add(EntryModel(-inviteId - 1000, "$groupName (Invite from $sender)", "Pending"))
                }
            }
        }
        
        val groupsRaw = vault.getGroupNames()
        if (groupsRaw != null) {
            for (g in groupsRaw) {
                val ownerId = vault.getGroupOwner(g)
                val roleLabel = if (ownerId == myId || ownerId.isEmpty()) "Owner" else "Member"
                groupList.add(EntryModel(0, g, roleLabel)) 
            }
        }
        
        adapter.updateData(groupList)
    }

    private fun showManageGroupDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_manage_group, null)

        val contentLayout = dialogView.findViewById<LinearLayout>(R.id.manageGroupContentLayout)
        val titleText = dialogView.findViewById<TextView>(R.id.textGroupTitle)
        val sectionSync = dialogView.findViewById<LinearLayout>(R.id.sectionSyncSettings)
        val sectionInvite = dialogView.findViewById<LinearLayout>(R.id.sectionInvite)
        val switchMemberSync = dialogView.findViewById<com.google.android.material.switchmaterial.SwitchMaterial>(R.id.switchMemberSync)
        val btnManualSync = dialogView.findViewById<Button>(R.id.btnManualSync)
        val inputInvite = dialogView.findViewById<TextInputEditText>(R.id.inputInviteId)
        val btnSendInvite = dialogView.findViewById<Button>(R.id.btnSendInvite)
        val membersContainer = dialogView.findViewById<LinearLayout>(R.id.containerMembers)

        titleText.text = "Manage '$currentGroup'"

        if (isGroupOwner) {
            sectionSync.visibility = View.VISIBLE
            sectionInvite.visibility = View.VISIBLE

            val prefs = getSharedPreferences("group_settings", Context.MODE_PRIVATE)
            val isSyncEnabled = prefs.getBoolean("sync_$currentGroup", false)
            switchMemberSync.isChecked = isSyncEnabled

            switchMemberSync.setOnCheckedChangeListener { _, isChecked ->
                prefs.edit().putBoolean("sync_$currentGroup", isChecked).apply()
                Toast.makeText(this, "Sync Policy Updated", Toast.LENGTH_SHORT).show()
            }

            btnManualSync.setOnClickListener {
                vault.broadcastSync(currentGroup)
                Toast.makeText(this, "Broadcasting Update...", Toast.LENGTH_SHORT).show()
            }

            btnSendInvite.setOnClickListener {
                val target = inputInvite.text.toString().trim()
                if(target.isNotEmpty()) {
                    vault.sendP2PInvite(currentGroup, target)
                    Toast.makeText(this, "Invite Sent", Toast.LENGTH_SHORT).show()
                    inputInvite.setText("")
                }
            }
        } else {
            sectionSync.visibility = View.GONE
            sectionInvite.visibility = View.GONE

            val btnLeave = Button(this)
            btnLeave.text = "Leave Group"
            btnLeave.setTextColor(Color.RED)
            btnLeave.setBackgroundColor(Color.TRANSPARENT)
            btnLeave.setOnClickListener {
                showDeleteGroupConfirmation(currentGroup)
            }
            contentLayout?.addView(btnLeave)
        }

        fun loadMembers() {
            membersContainer.removeAllViews()
            val members = vault.getGroupMembers(currentGroup)
            val myId = vault.getUserId()

            if (members.isNullOrEmpty()) {
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
                val role = parts[1]
                val status = parts[2]

                val row = LayoutInflater.from(this).inflate(R.layout.item_member_row, membersContainer, false)
                val nameTxt = row.findViewById<TextView>(R.id.memberName)
                val statusTxt = row.findViewById<TextView>(R.id.memberStatus)
                val btnRemove = row.findViewById<View>(R.id.btnRemove)

                nameTxt?.apply {
                    var display = uid
                    if (uid == myId) display += " (You)"
                    if (role.equals("owner", ignoreCase = true)) display = "👑 $display"
                    text = display
                }

                statusTxt?.text = status.uppercase()

                if (isGroupOwner && uid != myId) {
                    btnRemove?.visibility = View.VISIBLE
                    btnRemove?.setOnClickListener {
                        MaterialAlertDialogBuilder(this)
                            .setTitle("Remove Member")
                            .setMessage("Are you sure you want to kick $uid?")
                            .setPositiveButton("Remove") { _, _ ->
                                vault.removeUser(currentGroup, uid)
                                loadMembers() 
                            }
                            .setNegativeButton("Cancel", null)
                            .show()
                    }
                } else {
                    btnRemove?.visibility = View.GONE
                }

                membersContainer.addView(row)
            }
        }

        loadMembers()

        MaterialAlertDialogBuilder(this)
            .setView(dialogView)
            .setPositiveButton("Close", null)
            .show()
    }

    private fun showDeleteGroupConfirmation(groupName: String) {
        if (groupName == "Personal") { Toast.makeText(this, "Cannot delete 'Personal'", Toast.LENGTH_SHORT).show(); return }
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_confirm_password, null)
        val inputPass = dialogView.findViewById<TextInputEditText>(R.id.inputConfirmPass)
        val action = if (isGroupOwner) "Delete" else "Leave"
        MaterialAlertDialogBuilder(this).setTitle("$action '$groupName'?")
            .setView(dialogView).setPositiveButton(action) { _, _ ->
                val pass = inputPass.text.toString()
                if (pass.isNotEmpty() && vault.verifyMasterPassword(pass)) {
                    if (vault.deleteGroup(groupName)) {
                        getSharedPreferences("memberships", Context.MODE_PRIVATE).edit().remove(groupName).apply()
                        Toast.makeText(this, "Group $action" + "d", Toast.LENGTH_SHORT).show()
                        loadGroups()
                    } else Toast.makeText(this, "Failed", Toast.LENGTH_SHORT).show()
                } else Toast.makeText(this, "Incorrect Password", Toast.LENGTH_SHORT).show()
            }.setNegativeButton("Cancel", null).show()
    }

    private fun showAcceptRejectInviteDialog(inviteId: Int, fromUser: String, groupName: String) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_accept_invite, null)
        val text = view.findViewById<TextView>(R.id.inviteDetailText)
        val btnAccept = view.findViewById<Button>(R.id.btnAcceptInvite)
        val btnReject = view.findViewById<Button>(R.id.btnRejectInvite)
        text.text = "$fromUser invites you to '$groupName'"
        val dialog = MaterialAlertDialogBuilder(this).setView(view).setCancelable(false).create()
        btnAccept.setOnClickListener {
            btnAccept.isEnabled = false; btnAccept.text = "Joining..."; btnReject.isEnabled = false; btnReject.alpha = 0.5f
            getSharedPreferences("memberships", Context.MODE_PRIVATE).edit().putBoolean(groupName, true).apply()
            processingInvites.add(groupName)
            vault.respondToInvite(groupName, fromUser, true)
            Toast.makeText(this, "Accepted. Syncing...", Toast.LENGTH_SHORT).show()
            loadGroups(); view.postDelayed({ dialog.dismiss() }, 800)
        }
        btnReject.setOnClickListener { vault.respondToInvite(groupName, fromUser, false); dialog.dismiss(); loadGroups() }
        dialog.show()
    }

    private fun showLocationEditDialog(existing: LocationData?, onResult: (LocationData) -> Unit) {
        val view = LayoutInflater.from(this).inflate(R.layout.dialog_edit_location, null)
        
        val spinner = view.findViewById<android.widget.Spinner>(R.id.spinnerLocationType)
        val inputVal = view.findViewById<TextInputEditText>(R.id.inputLocationValue)
        val layoutVal = view.findViewById<TextInputLayout>(R.id.layoutLocationValue)
        val layoutCustom = view.findViewById<TextInputLayout>(R.id.layoutCustomType)
        val inputCustom = view.findViewById<TextInputEditText>(R.id.inputCustomType)

        val types = arrayOf("URL", "Android App", "iOS App", "WiFi SSID", "Other")
        val adapter = android.widget.ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item, types)
        spinner.adapter = adapter

        if (existing != null) {
            inputVal.setText(existing.value)
            if (types.contains(existing.type)) {
                spinner.setSelection(types.indexOf(existing.type))
            } else {
                spinner.setSelection(types.indexOf("Other"))
                layoutCustom.visibility = View.VISIBLE
                inputCustom.setText(existing.type)
            }
        }

        spinner.onItemSelectedListener = object : android.widget.AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p0: android.widget.AdapterView<*>?, p1: View?, pos: Int, p3: Long) {
                val selected = types[pos]
                layoutCustom.visibility = if (selected == "Other") View.VISIBLE else View.GONE
                
                layoutVal.placeholderText = when(selected) {
                    "URL" -> "https://example.com"
                    "Android App" -> "com.example.app"
                    "iOS App" -> "com.example.ios"
                    "WiFi SSID" -> "Network Name"
                    else -> "Value"
                }
            }
            override fun onNothingSelected(p0: android.widget.AdapterView<*>?) {}
        }

        MaterialAlertDialogBuilder(this)
            .setView(view)
            .setPositiveButton(if (existing == null) "Add" else "Update") { _, _ ->
                var type = spinner.selectedItem.toString()
                if (type == "Other") type = inputCustom.text.toString().trim()
                if (type.isEmpty()) type = "Other"
                
                val value = inputVal.text.toString().trim()
                if (value.isNotEmpty()) {
                    onResult(LocationData(type, value))
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showCreateEntryDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_create_entry, null)
        
        val titleEdit = dialogView.findViewById<TextInputEditText>(R.id.inputTitle)
        val userEdit = dialogView.findViewById<TextInputEditText>(R.id.inputUsername)
        val passEdit = dialogView.findViewById<TextInputEditText>(R.id.inputPassword)
        val totpEdit = dialogView.findViewById<TextInputEditText>(R.id.inputTotp)
        val notesEdit = dialogView.findViewById<TextInputEditText>(R.id.inputNotes)
        
        val locationsContainer = dialogView.findViewById<LinearLayout>(R.id.locationsContainer)
        val btnAdd = dialogView.findViewById<Button>(R.id.btnAddLoc)
        
        // Password generator and strength meter
        val btnGeneratePassword = dialogView.findViewById<Button>(R.id.btnGeneratePassword)
        val strengthContainer = dialogView.findViewById<LinearLayout>(R.id.passwordStrengthContainer)
        val strengthText = dialogView.findViewById<TextView>(R.id.textPasswordStrength)
        val strengthScore = dialogView.findViewById<TextView>(R.id.textPasswordScore)
        val strengthProgress = dialogView.findViewById<android.widget.ProgressBar>(R.id.progressPasswordStrength)

        val locationsList = ArrayList<LocationData>()
        
        // Password strength update function
        fun updatePasswordStrength() {
            val password = passEdit.text.toString()
            if (password.isEmpty()) {
                strengthContainer.visibility = View.GONE
                return
            }
            
            strengthContainer.visibility = View.VISIBLE
            val score = PasswordGenerator.calculateStrength(password)
            val strengthLabel = PasswordGenerator.getStrengthText(score)
            val color = PasswordGenerator.getStrengthColor(score)
            
            strengthText.text = strengthLabel
            strengthText.setTextColor(color)
            strengthScore.text = "$score%"
            strengthProgress.progress = score
            strengthProgress.progressTintList = android.content.res.ColorStateList.valueOf(color)
        }
        
        // Generate password button
        btnGeneratePassword.setOnClickListener {
            val generatedPassword = PasswordGenerator.generate(
                length = 16,
                useUppercase = true,
                useLowercase = true,
                useNumbers = true,
                useSymbols = true
            )
            passEdit.setText(generatedPassword)
            updatePasswordStrength()
        }
        
        // Monitor password field changes for strength meter with debouncing
        val strengthUpdateHandler = Handler(Looper.getMainLooper())
        var strengthUpdateRunnable: Runnable? = null
        
        passEdit.addTextChangedListener(object : android.text.TextWatcher {
            override fun beforeTextChanged(s: CharSequence?, start: Int, count: Int, after: Int) {}
            override fun onTextChanged(s: CharSequence?, start: Int, before: Int, count: Int) {}
            override fun afterTextChanged(s: android.text.Editable?) {
                // Debounce: cancel pending update and schedule new one
                strengthUpdateRunnable?.let { strengthUpdateHandler.removeCallbacks(it) }
                val newRunnable = Runnable { updatePasswordStrength() }
                strengthUpdateRunnable = newRunnable
                strengthUpdateHandler.postDelayed(newRunnable, 300) // 300ms debounce
            }
        })

        fun refreshLocationsList() {
            locationsContainer.removeAllViews()
            
            for ((index, loc) in locationsList.withIndex()) {
                val rowView = LayoutInflater.from(this).inflate(R.layout.item_location_row, locationsContainer, false)
                val typeText = rowView.findViewById<TextView>(R.id.textType)
                val valueText = rowView.findViewById<TextView>(R.id.textValue)
                val btnRemove = rowView.findViewById<View>(R.id.btnRemove)
                val clickArea = rowView.findViewById<View>(R.id.rowClickArea)

                typeText.text = loc.type
                valueText.text = loc.value

                clickArea.setOnClickListener {
                    showLocationEditDialog(loc) { updatedLoc ->
                        locationsList[index] = updatedLoc
                        refreshLocationsList()
                    }
                }

                btnRemove.setOnClickListener {
                    locationsList.removeAt(index)
                    refreshLocationsList()
                }

                locationsContainer.addView(rowView)
            }
        }

        btnAdd.setOnClickListener {
            showLocationEditDialog(null) { newLoc ->
                locationsList.add(newLoc)
                refreshLocationsList()
            }
        }

        MaterialAlertDialogBuilder(this)
            .setTitle("Add Entry")
            .setView(dialogView)
            .setCancelable(false) 
            .setPositiveButton("Save") { _, _ ->
                val title = titleEdit.text.toString().trim()
                val user = userEdit.text.toString().trim()
                val pass = passEdit.text.toString()
                val totp = totpEdit.text.toString().trim()
                val notes = notesEdit.text.toString().trim()

                val locListJson = ArrayList<String>()
                for (loc in locationsList) {
                    val t = loc.type.replace("\"", "\\\"")
                    val v = loc.value.replace("\"", "\\\"")
                    locListJson.add("{\"type\":\"$t\", \"value\":\"$v\"}")
                }
                val locationsJson = "{\"locations\":[${locListJson.joinToString(",")}]}"

                if (title.isNotEmpty() && pass.isNotEmpty()) {
                    vault.addEntry(title, user, pass, "Login", locationsJson, notes, totp)
                    loadEntries()
                    Toast.makeText(this, "Saved & Synced", Toast.LENGTH_SHORT).show()
                } else {
                    Toast.makeText(this, "Title/Pass required", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showCreateGroupDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_create_group, null)
        val input = dialogView.findViewById<com.google.android.material.textfield.TextInputEditText>(R.id.dialogInput)

        MaterialAlertDialogBuilder(this)
            .setTitle("Create New Group")
            .setIcon(android.R.drawable.ic_menu_add) 
            .setView(dialogView)
            .setPositiveButton("Create") { _, _ ->
                val groupName = input.text.toString().trim()
                if (groupName.isNotEmpty()) {
                    if (vault.addGroup(groupName)) {
                        loadGroups()
                        Toast.makeText(this, "Group Created", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(this, "Failed to create group", Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showEntryDetails(id: Int) {
        val raw = vault.getEntryFullDetails(id)
        val parts = raw.split("|")
        if (parts.size < ENTRY_DETAILS_PARTS_COUNT) return
        
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_entry_details, null)
        
        val detailTitle = dialogView.findViewById<EditText>(R.id.detailTitle)
        val detailUser = dialogView.findViewById<EditText>(R.id.detailUser)
        val detailPass = dialogView.findViewById<TextInputEditText>(R.id.detailPass)
        val detailNotes = dialogView.findViewById<EditText>(R.id.detailNotes)
        val btnCopyPass = dialogView.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnCopyPass)
        val btnPasswordHistory = dialogView.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnPasswordHistory)
        val btnDuplicate = dialogView.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnDuplicate)
        
        detailTitle.setText(parts[0])
        detailUser.setText(parts[1])
        detailPass.setText(parts[2])
        detailNotes.setText(parts[3])
        
        btnCopyPass.setOnClickListener {
            copyToClipboardSecure("Pass", parts[2], "Password copied")
        }
        
        btnPasswordHistory.setOnClickListener {
            showPasswordHistoryDialog(id)
        }
        
        btnDuplicate.setOnClickListener {
            duplicateEntry(id, parts[0], parts[1], parts[2], parts[3], parts[4])
        }
        
        MaterialAlertDialogBuilder(this)
            .setView(dialogView)
            .setNegativeButton("Close", null)
            .setNeutralButton("Delete") { _, _ ->
                if (vault.deleteEntry(id)) {
                    loadEntries()
                    Toast.makeText(this, "Entry deleted", Toast.LENGTH_SHORT).show()
                }
            }
            .show()
    }
    
    private fun showPasswordHistoryDialog(entryId: Int) {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_password_history, null)
        val recyclerView = dialogView.findViewById<RecyclerView>(R.id.recyclerPasswordHistory)
        val textNoHistory = dialogView.findViewById<TextView>(R.id.textNoHistory)
        
        val historyRaw = vault.getPasswordHistory(entryId)
        val historyItems = ArrayList<PasswordHistoryItem>()
        
        for (raw in historyRaw) {
            val parts = raw.split("|")
            if (parts.size >= 3) {
                try {
                    historyItems.add(PasswordHistoryItem(parts[1], parts[2].toLong()))
                } catch (e: NumberFormatException) { continue }
            }
        }
        
        if (historyItems.isEmpty()) {
            recyclerView.visibility = View.GONE
            textNoHistory.visibility = View.VISIBLE
        } else {
            recyclerView.visibility = View.VISIBLE
            textNoHistory.visibility = View.GONE
            
            recyclerView.layoutManager = LinearLayoutManager(this)
            recyclerView.adapter = PasswordHistoryAdapter(this, historyItems) { encryptedPassword ->
                val decryptedPassword = vault.decryptPasswordFromHistory(encryptedPassword)
                copyToClipboardSecure("Password", decryptedPassword, "Password copied")
            }
        }
        
        MaterialAlertDialogBuilder(this)
            .setView(dialogView)
            .setPositiveButton("Close", null)
            .show()
    }
    
    private fun duplicateEntry(originalId: Int, title: String, username: String, password: String, notes: String, totpSecret: String) {
        val newTitle = "$title (Copy)"
        if (vault.addEntry(newTitle, username, password, "Login", "{\"locations\":[]}", notes, totpSecret)) {
            Toast.makeText(this, getString(R.string.entry_duplicated), Toast.LENGTH_SHORT).show()
            loadEntries()
        } else {
            Toast.makeText(this, "Failed to duplicate entry", Toast.LENGTH_SHORT).show()
        }
    }
    
    private fun performSearch(query: String) {
        if (query.isEmpty()) {
            adapter.updateData(allEntries)
            return
        }
        
        val searchResults = vault.searchEntries(query)
        val resultList = ArrayList<EntryModel>()
        
        for (raw in searchResults) {
            val parts = raw.split("|") 
            if (parts.size >= 3) {
                resultList.add(EntryModel(0, parts[0], parts[1]))
            }
        }
        
        adapter.updateData(resultList)
    }

    override fun onCreateOptionsMenu(menu: Menu?): Boolean { 
        menuInflater.inflate(R.menu.menu_home, menu)
        
        val searchItem = menu?.findItem(R.id.action_search)
        val searchView = searchItem?.actionView as? SearchView
        
        searchView?.queryHint = getString(R.string.search_hint)
        searchView?.setOnQueryTextListener(object : SearchView.OnQueryTextListener {
            override fun onQueryTextSubmit(query: String?): Boolean {
                query?.let { performSearch(it) }
                return true
            }
            
            override fun onQueryTextChange(newText: String?): Boolean {
                newText?.let { performSearch(it) }
                return true
            }
        })
        
        searchItem?.setOnActionExpandListener(object : MenuItem.OnActionExpandListener {
            override fun onMenuItemActionExpand(item: MenuItem): Boolean = true
            
            override fun onMenuItemActionCollapse(item: MenuItem): Boolean {
                if (!isShowingGroups) {
                    adapter.updateData(allEntries)
                }
                return true
            }
        })
        
        return true
    }
    
    override fun onPrepareOptionsMenu(menu: Menu?): Boolean { 
        val searchItem = menu?.findItem(R.id.action_search)
        val reconnectItem = menu?.findItem(R.id.action_reconnect)
        val settingsItem = menu?.findItem(R.id.action_group_settings)

        if (isShowingGroups) {
            searchItem?.isVisible = false
            reconnectItem?.isVisible = true
            settingsItem?.isVisible = false
        } else {
            searchItem?.isVisible = true
            reconnectItem?.isVisible = false
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
    
    override fun onNavigationItemSelected(item: MenuItem): Boolean {
        when(item.itemId) {
            R.id.nav_groups -> loadGroups()
            R.id.nav_recent_entries -> startActivity(Intent(this, RecentEntriesActivity::class.java))
            R.id.nav_add_group -> showCreateGroupDialog()
            R.id.nav_theme -> { 
                val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
                prefs.edit().putInt("theme_index", (prefs.getInt("theme_index", 0) + 1) % 5).apply()
                finish(); startActivity(intent)
            }
            R.id.nav_settings -> startActivity(Intent(this, SettingsActivity::class.java))
            R.id.nav_lock -> { finish(); } 
        }
        drawerLayout.closeDrawer(GravityCompat.START)
        return true
    }
    
    private fun applySavedTheme() {
        val idx = getSharedPreferences("app_prefs", Context.MODE_PRIVATE).getInt("theme_index", 0)
        val themes = listOf(R.style.Theme_CipherMesh_Professional, R.style.Theme_CipherMesh_ModernLight, R.style.Theme_CipherMesh_Ocean, R.style.Theme_CipherMesh_Warm, R.style.Theme_CipherMesh_Vibrant)
        if(idx in themes.indices) setTheme(themes[idx])
    }

    fun showToast(msg: String) { 
        runOnUiThread { 
            Toast.makeText(this, msg, Toast.LENGTH_SHORT).show()
            if(msg.contains("Invite received", true) || msg.contains("Joined Group", true)) {
                loadGroups()
            }
        } 
    }
}