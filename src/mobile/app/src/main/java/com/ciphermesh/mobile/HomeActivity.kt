package com.ciphermesh.mobile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.content.Intent
import android.graphics.Color
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.util.TypedValue
import android.view.Gravity
import android.view.LayoutInflater
import android.view.Menu
import android.view.MenuItem
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.TextView
import android.widget.Toast
import androidx.appcompat.app.ActionBarDrawerToggle
import androidx.appcompat.app.AppCompatActivity
import androidx.appcompat.widget.Toolbar
import androidx.core.view.GravityCompat
import androidx.drawerlayout.widget.DrawerLayout
import com.ciphermesh.mobile.core.Vault
import com.ciphermesh.mobile.p2p.P2PManager
import com.google.android.material.dialog.MaterialAlertDialogBuilder
import com.google.android.material.floatingactionbutton.FloatingActionButton
import com.google.android.material.navigation.NavigationView
import com.google.android.material.textfield.TextInputEditText
import java.io.File

class HomeActivity : AppCompatActivity(), NavigationView.OnNavigationItemSelectedListener {

    private val vault = Vault()
    private lateinit var drawerLayout: DrawerLayout
    private lateinit var listView: ListView
    private lateinit var fabAdd: FloatingActionButton
    private lateinit var adapter: CustomAdapter
    private lateinit var p2pManager: P2PManager
    
    private var isShowingGroups = true 
    private var currentGroup = ""
    private var isGroupOwner = false 

    override fun onCreate(savedInstanceState: Bundle?) {
        applySavedTheme()
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_home)

        // 1. Init Core
        val dbPath = File(filesDir, "vault.db").absolutePath
        vault.init(dbPath)
        vault.setActivityContext(this)

        // 2. Auth Check
        if (vault.isLocked()) {
            Toast.makeText(this, "Session Expired. Please Login.", Toast.LENGTH_SHORT).show()
            startActivity(Intent(this, MainActivity::class.java))
            finish()
            return
        }

        // 3. UI Setup
        val toolbar = findViewById<Toolbar>(R.id.toolbar)
        setSupportActionBar(toolbar)
        
        drawerLayout = findViewById(R.id.drawer_layout)
        val navView = findViewById<NavigationView>(R.id.nav_view)
        
        val toggle = ActionBarDrawerToggle(
            this, drawerLayout, toolbar,
            R.string.navigation_drawer_open, R.string.navigation_drawer_close
        )
        
        val typedValue = TypedValue()
        theme.resolveAttribute(android.R.attr.textColorPrimary, typedValue, true)
        toggle.drawerArrowDrawable.color = typedValue.data
        
        drawerLayout.addDrawerListener(toggle)
        toggle.syncState()
        navView.setNavigationItemSelectedListener(this)

        val headerView = navView.getHeaderView(0)
        val userText = headerView.findViewById<TextView>(R.id.nav_user_name)
        val userId = vault.getUserId()
        userText.text = userId
        userText.setOnClickListener {
            val clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
            val clip = ClipData.newPlainText("User ID", userId)
            clipboard.setPrimaryClip(clip)
            Toast.makeText(this, "ID Copied", Toast.LENGTH_SHORT).show()
        }

        listView = findViewById(R.id.listViewEntries)
        fabAdd = findViewById(R.id.fabAdd)
        
        listView.divider = ColorDrawable(Color.TRANSPARENT)
        listView.dividerHeight = 32 
        
        // 4. Init P2P
        p2pManager = P2PManager(this, vault)
        p2pManager.connect()

        adapter = CustomAdapter(this, arrayListOf())
        listView.adapter = adapter

        // 5. List Logic
        listView.setOnItemClickListener { _, _, position, _ ->
            if (isShowingGroups) {
                val groupName = adapter.getItem(position)?.title ?: return@setOnItemClickListener
                
                if (vault.isLocked()) {
                    Toast.makeText(this, "Vault Locked", Toast.LENGTH_SHORT).show()
                    startActivity(Intent(this, MainActivity::class.java))
                    finish()
                    return@setOnItemClickListener
                }

                if (vault.setActiveGroup(groupName)) {
                    currentGroup = groupName
                    val owner = vault.getGroupOwner(groupName)
                    isGroupOwner = (owner == vault.getUserId() || groupName == "Personal")
                    loadEntries()
                } else {
                    Toast.makeText(this, "Failed to open group", Toast.LENGTH_SHORT).show()
                }
            } else {
                val entryId = adapter.getItem(position)?.id ?: return@setOnItemClickListener
                showEntryDetails(entryId)
            }
        }

        listView.setOnItemLongClickListener { _, _, position, _ ->
            if (isShowingGroups) {
                val groupName = adapter.getItem(position)?.title ?: return@setOnItemLongClickListener false
                val owner = vault.getGroupOwner(groupName)
                
                if (owner == vault.getUserId() || groupName == "Personal") {
                    currentGroup = groupName
                    showGroupOptionsDialog(groupName)
                    true
                } else {
                    false
                }
            } else {
                false
            }
        }

        fabAdd.setOnClickListener {
            if (isShowingGroups) showCreateGroupDialog() else showCreateEntryDialog()
        }

        if (!vault.groupExists("Personal")) vault.addGroup("Personal")
        loadGroups()
    }

    // --- Menus ---

    override fun onCreateOptionsMenu(menu: Menu?): Boolean {
        menuInflater.inflate(R.menu.menu_home, menu)
        menu?.add(0, 1001, 0, "🔄 Reconnect")
        menu?.add(0, 1002, 0, "📡 Test Ping")
        return true
    }

    override fun onPrepareOptionsMenu(menu: Menu?): Boolean {
        val shareItem = menu?.findItem(R.id.action_share_group)
        shareItem?.isVisible = (!isShowingGroups && isGroupOwner)
        return super.onPrepareOptionsMenu(menu)
    }

    override fun onOptionsItemSelected(item: MenuItem): Boolean {
        return when (item.itemId) {
            R.id.action_share_group -> {
                showManageGroupDialog()
                true
            }
            1001 -> { p2pManager.connect(); true }
            1002 -> { p2pManager.sendPing(); true }
            else -> super.onOptionsItemSelected(item)
        }
    }

    override fun onBackPressed() {
        if (drawerLayout.isDrawerOpen(GravityCompat.START)) {
            drawerLayout.closeDrawer(GravityCompat.START)
        } else if (!isShowingGroups) {
            loadGroups()
        } else {
            super.onBackPressed()
        }
    }

    // --- Helpers ---

    private fun loadEntries() {
        isShowingGroups = false
        supportActionBar?.title = currentGroup
        invalidateOptionsMenu() 
        fabAdd.setImageDrawable(resources.getDrawable(android.R.drawable.ic_input_add, theme))
        
        val entriesRaw = vault.getEntries()
        val entryList = ArrayList<EntryModel>()
        if (entriesRaw != null) {
            for (raw in entriesRaw) {
                val parts = raw.split(":")
                if (parts.size >= 3) {
                    entryList.add(EntryModel(parts[0].toInt(), parts[1], parts[2]))
                }
            }
        }
        adapter.updateData(entryList)
    }

    private fun loadGroups() {
        isShowingGroups = true
        supportActionBar?.title = "Your Groups"
        invalidateOptionsMenu()
        fabAdd.setImageDrawable(resources.getDrawable(android.R.drawable.ic_input_add, theme)) 

        val groupsRaw = vault.getGroupNames()
        val groupList = ArrayList<EntryModel>()
        if (groupsRaw != null) {
            for (g in groupsRaw) {
                val isMine = (vault.getGroupOwner(g) == vault.getUserId() || g == "Personal")
                groupList.add(EntryModel(-1, g, if (isMine) "Owner" else "Member")) 
            }
        }
        adapter.updateData(groupList)
    }

    // --- Dialogs ---

    private fun showGroupOptionsDialog(groupName: String) {
        val options = arrayOf("Manage / Share", "Delete Group")
        MaterialAlertDialogBuilder(this)
            .setTitle("Options for '$groupName'")
            .setItems(options) { _, which ->
                when (which) {
                    0 -> showManageGroupDialog()
                    1 -> showDeleteGroupConfirmation(groupName)
                }
            }
            .show()
    }

    private fun showDeleteGroupConfirmation(groupName: String) {
        if (groupName == "Personal") {
            Toast.makeText(this, "Cannot delete 'Personal' group", Toast.LENGTH_SHORT).show()
            return
        }
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_confirm_password, null)
        val inputPass = dialogView.findViewById<TextInputEditText>(R.id.inputConfirmPass)

        MaterialAlertDialogBuilder(this)
            .setTitle("Delete '$groupName'?")
            .setView(dialogView)
            .setPositiveButton("Delete") { _, _ ->
                val pass = inputPass.text.toString()
                if (pass.isNotEmpty() && vault.verifyMasterPassword(pass)) {
                    if (vault.deleteGroup(groupName)) {
                        Toast.makeText(this, "Group Deleted", Toast.LENGTH_SHORT).show()
                        loadGroups()
                    } else {
                        Toast.makeText(this, "Deletion Failed", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    Toast.makeText(this, "Incorrect Password", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showManageGroupDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_share_group, null)
        val inputId = dialogView.findViewById<EditText>(R.id.inputInviteId)
        val btnInvite = dialogView.findViewById<Button>(R.id.btnInvite)
        val membersLayout = dialogView.findViewById<LinearLayout>(R.id.layoutMembersList)

        // Load and display group members with Material Design 3 styling
        fun loadMembers() {
            membersLayout.removeAllViews()
            val members = vault.getGroupMembers(currentGroup)
            val owner = vault.getGroupOwner(currentGroup)
            val myId = vault.getUserId()
            
            if (members.isEmpty()) {
                val emptyText = TextView(this).apply {
                    text = "No members in this group yet.\nInvite users to collaborate!"
                    setPadding(40, 40, 40, 40)
                    textSize = 14f
                    setTextColor(getColor(com.google.android.material.R.color.material_on_surface_emphasis_medium))
                    gravity = android.view.Gravity.CENTER
                    textAlignment = TextView.TEXT_ALIGNMENT_CENTER
                }
                membersLayout.addView(emptyText)
            } else {
                // Get pending invites to show status
                val pendingInvites = vault.getPendingInvites()
                
                for ((index, memberId) in members.withIndex()) {
                    // Card container for each member (like groups list)
                    val cardView = com.google.android.material.card.MaterialCardView(this).apply {
                        layoutParams = LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            LinearLayout.LayoutParams.WRAP_CONTENT
                        ).apply {
                            setMargins(0, 0, 0, 12) // 12dp bottom margin between cards
                        }
                        cardElevation = 8f
                        radius = 12f
                        setCardBackgroundColor(getColor(com.google.android.material.R.color.material_grey_100))
                    }
                    
                    val memberView = LinearLayout(this).apply {
                        orientation = LinearLayout.HORIZONTAL
                        setPadding(16, 14, 16, 14)
                        layoutParams = LinearLayout.LayoutParams(
                            LinearLayout.LayoutParams.MATCH_PARENT,
                            LinearLayout.LayoutParams.WRAP_CONTENT
                        )
                        gravity = Gravity.CENTER_VERTICAL
                    }
                    
                    // Check if this user has a pending invite
                    val isPending = pendingInvites.any { it.targetUserId == memberId }
                    
                    val memberText = TextView(this).apply {
                        text = when {
                            memberId == owner && memberId == myId -> "★ $memberId (Me, Owner)"
                            memberId == owner && isPending -> "★ $memberId (Owner, Pending Invite)"
                            memberId == owner -> "★ $memberId (Owner)"
                            memberId == myId -> "$memberId (Me)"
                            isPending -> "$memberId (Pending Invite)"
                            else -> memberId
                        }
                        layoutParams = LinearLayout.LayoutParams(
                            0,
                            LinearLayout.LayoutParams.WRAP_CONTENT,
                            1f
                        )
                        textSize = 15f
                        setTextColor(when {
                            memberId == owner -> getColor(com.google.android.material.R.color.material_deep_teal_500)
                            memberId == myId -> getColor(com.google.android.material.R.color.material_blue_grey_800)
                            else -> getColor(com.google.android.material.R.color.material_on_surface_emphasis_high_type)
                        })
                        setPadding(4, 8, 12, 8)
                        typeface = android.graphics.Typeface.create(
                            if (memberId == owner || memberId == myId) android.graphics.Typeface.DEFAULT_BOLD 
                            else android.graphics.Typeface.DEFAULT, 
                            android.graphics.Typeface.NORMAL
                        )
                        // Enable text selection for copying IDs
                        setTextIsSelectable(true)
                    }
                    
                    memberView.addView(memberText)
                    
                    // Add remove button if not self and not owner, and current user is owner
                    if (memberId != myId && memberId != owner && myId == owner) {
                        val removeBtn = com.google.android.material.button.MaterialButton(
                            this,
                            null,
                            com.google.android.material.R.attr.materialButtonOutlinedStyle
                        ).apply {
                            text = "Remove"
                            setTextColor(getColor(com.google.android.material.R.color.design_default_color_error))
                            strokeColor = android.content.res.ColorStateList.valueOf(
                                getColor(com.google.android.material.R.color.design_default_color_error)
                            )
                            setPadding(20, 4, 20, 4)
                            textSize = 11f
                            insetTop = 0
                            insetBottom = 0
                            minimumHeight = 0
                            setOnClickListener {
                                vault.removeUser(currentGroup, memberId)
                                loadMembers()
                                Toast.makeText(this@HomeActivity, "Removed $memberId from group", Toast.LENGTH_SHORT).show()
                            }
                        }
                        memberView.addView(removeBtn)
                    }
                    
                    cardView.addView(memberView)
                    membersLayout.addView(cardView)
                }
            }
        }
        
        loadMembers()

        val dialog = MaterialAlertDialogBuilder(this)
            .setTitle("Manage '$currentGroup'")
            .setView(dialogView)
            .setPositiveButton("Done", null)
            .create()

        btnInvite.setOnClickListener {
            val targetId = inputId.text.toString().trim()
            if (targetId.isNotEmpty()) {
                // [FIX] Run heavy P2P logic in background thread to prevent FREEZING
                Toast.makeText(this, "Sending invite...", Toast.LENGTH_SHORT).show()
                Thread {
                    vault.sendP2PInvite(currentGroup, targetId)
                    runOnUiThread {
                        Toast.makeText(this, "Invite Sent!", Toast.LENGTH_SHORT).show()
                        inputId.setText("")
                        loadMembers() // Refresh members list
                    }
                }.start()
            } else {
                Toast.makeText(this, "Enter a User ID", Toast.LENGTH_SHORT).show()
            }
        }
        dialog.show()
    }

    private fun showCreateEntryDialog() {
        val dialogView = LayoutInflater.from(this).inflate(R.layout.dialog_create_entry, null)
        val titleEdit = dialogView.findViewById<TextInputEditText>(R.id.inputTitle)
        val userEdit = dialogView.findViewById<TextInputEditText>(R.id.inputUsername)
        val passEdit = dialogView.findViewById<TextInputEditText>(R.id.inputPassword)
        val urlEdit = dialogView.findViewById<TextInputEditText>(R.id.inputUrl)

        MaterialAlertDialogBuilder(this)
            .setTitle("Add New Entry")
            .setView(dialogView)
            .setPositiveButton("Save") { _, _ ->
                val title = titleEdit.text.toString()
                val user = userEdit.text.toString()
                val pass = passEdit.text.toString()
                val url = urlEdit.text.toString()

                if (title.isNotEmpty() && pass.isNotEmpty()) {
                    if (vault.addEntry(title, user, pass, "Login", url, "", "")) {
                        loadEntries()
                        Toast.makeText(this, "Entry Saved", Toast.LENGTH_SHORT).show()
                    } else {
                        Toast.makeText(this, "Save Failed", Toast.LENGTH_SHORT).show()
                    }
                } else {
                    Toast.makeText(this, "Title/Password required", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showCreateGroupDialog() {
        val input = EditText(this)
        input.hint = "Group Name"
        val container = android.widget.FrameLayout(this)
        container.setPadding(60, 40, 60, 20)
        container.addView(input)

        MaterialAlertDialogBuilder(this)
            .setTitle("Create Group")
            .setView(container)
            .setPositiveButton("Create") { _, _ ->
                val name = input.text.toString().trim()
                if (name.isNotEmpty()) {
                    if (vault.addGroup(name)) {
                        loadGroups()
                    } else {
                        Toast.makeText(this, "Create Failed", Toast.LENGTH_SHORT).show()
                    }
                }
            }
            .setNegativeButton("Cancel", null)
            .show()
    }

    private fun showEntryDetails(id: Int) {
        val detailsRaw = vault.getEntryDetails(id) 
        val parts = detailsRaw.split("|")
        if (parts.size < 3) return

        val pass = parts[2]

        MaterialAlertDialogBuilder(this)
            .setTitle(parts[0]) 
            .setMessage("Username: ${parts[1]}\nPassword: ••••••••")
            .setPositiveButton("Copy Password") { _, _ ->
                val clipboard = getSystemService(CLIPBOARD_SERVICE) as ClipboardManager
                val clip = ClipData.newPlainText("Password", pass)
                clipboard.setPrimaryClip(clip)
                Toast.makeText(this, "Copied", Toast.LENGTH_SHORT).show()
            }
            .setNeutralButton("Delete") { _, _ ->
                if (vault.deleteEntry(id)) {
                    loadEntries()
                } else {
                    Toast.makeText(this, "Delete Failed", Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton("Close", null)
            .show()
    }

    private fun showPendingInvitesDialog() {
        // Fetch invites
        val rawInvites = vault.getPendingInvites()
        
        if (rawInvites.isEmpty()) {
            Toast.makeText(this, "No Pending Invites", Toast.LENGTH_SHORT).show()
            return
        }

        // Create a custom dialog with better UX
        val dialogView = LayoutInflater.from(this).inflate(R.layout.activity_invites, null)
        val listView = dialogView.findViewById<ListView>(R.id.listInvites)
        val emptyText = dialogView.findViewById<TextView>(R.id.txtNoInvites)
        
        if (rawInvites.isEmpty()) {
            listView.visibility = View.GONE
            emptyText.visibility = View.VISIBLE
        } else {
            listView.visibility = View.VISIBLE
            emptyText.visibility = View.GONE
            
            // Custom adapter for better UI
            val inviteAdapter = object : ArrayAdapter<String>(this, R.layout.item_invite_row, rawInvites) {
                override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
                    val view = convertView ?: LayoutInflater.from(context).inflate(R.layout.item_invite_row, parent, false)
                    
                    val parts = getItem(position)?.split("|") ?: return view
                    if (parts.size < 3) return view
                    
                    val inviteId = parts[0].toIntOrNull() ?: return view
                    val fromText = view.findViewById<TextView>(R.id.inviteFrom)
                    val groupText = view.findViewById<TextView>(R.id.inviteGroup)
                    val btnAccept = view.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnAccept)
                    val btnReject = view.findViewById<com.google.android.material.button.MaterialButton>(R.id.btnReject)
                    
                    fromText.text = "From: ${parts[1]}"
                    groupText.text = parts[2]
                    
                    btnAccept.setOnClickListener {
                        vault.respondToInvite(inviteId, true)
                        Toast.makeText(context, "Invite Accepted! Receiving group data...", Toast.LENGTH_LONG).show()
                        
                        // Refresh the list
                        remove(getItem(position))
                        notifyDataSetChanged()
                        
                        // Reload groups to show the new one - use delayed refresh to allow group-data to arrive
                        android.os.Handler(android.os.Looper.getMainLooper()).postDelayed({
                            loadGroups()
                        }, 1500)
                        
                        // Close dialog if no more invites
                        if (count == 0) {
                            (view.context as? HomeActivity)?.let { activity ->
                                // Dialog will be closed by user
                            }
                        }
                    }
                    
                    btnReject.setOnClickListener {
                        vault.respondToInvite(inviteId, false)
                        Toast.makeText(context, "Invite Rejected", Toast.LENGTH_SHORT).show()
                        
                        // Refresh the list
                        remove(getItem(position))
                        notifyDataSetChanged()
                    }
                    
                    return view
                }
            }
            
            listView.adapter = inviteAdapter
        }
        
        // Create dialog with custom view
        MaterialAlertDialogBuilder(this)
            .setTitle("Pending Invites")
            .setView(dialogView)
            .setPositiveButton("Close", null)
            .show()
    }

    override fun onNavigationItemSelected(item: MenuItem): Boolean {
        when (item.itemId) {
            R.id.nav_groups -> loadGroups()
            R.id.nav_add_group -> showCreateGroupDialog()
            // This is the menu item you asked for
            R.id.nav_invites -> showPendingInvitesDialog() 
            R.id.nav_theme -> cycleTheme()
            R.id.nav_lock -> {
                finish()
                startActivity(Intent(this, MainActivity::class.java))
            }
        }
        drawerLayout.closeDrawer(GravityCompat.START)
        return true
    }

    private fun cycleTheme() {
        val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
        var index = prefs.getInt("theme_index", 0)
        index = (index + 1) % 5 
        prefs.edit().putInt("theme_index", index).apply()
        finish()
        startActivity(intent)
    }

    private fun applySavedTheme() {
        val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
        val themeIndex = prefs.getInt("theme_index", 0)
        val themes = listOf(
            R.style.Theme_CipherMesh_Professional,
            R.style.Theme_CipherMesh_ModernLight,
            R.style.Theme_CipherMesh_Ocean,
            R.style.Theme_CipherMesh_Warm,
            R.style.Theme_CipherMesh_Vibrant
        )
        if (themeIndex in themes.indices) {
            setTheme(themes[themeIndex])
        }
    }
    
    // [NEW] Helper method for native code to show toasts on UI thread
    fun showToast(message: String) {
        runOnUiThread {
            Toast.makeText(this, message, Toast.LENGTH_SHORT).show()
        }
    }
}