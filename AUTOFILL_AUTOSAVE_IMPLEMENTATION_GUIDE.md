# AutoFill and Auto-Save Implementation Guide

## Overview
This document outlines the implementation plan for AutoFill and Auto-Save credential features for the CipherMesh mobile app.

---

## 1. AutoFill Service

### What It Does
- Detects when user is on a login page in any app
- Checks if we have matching credentials based on app package/URL
- Prompts user to select which account to autofill
- Requires biometric/master password authentication
- Fills username and password fields automatically

### Android Architecture Required

#### A. AutofillService (Android 8.0+)
```kotlin
class CipherMeshAutofillService : AutofillService() {
    
    override fun onFillRequest(
        request: FillRequest,
        cancellationSignal: CancellationSignal,
        callback: FillCallback
    ) {
        // 1. Parse the ViewStructure to find login fields
        val structure = request.fillContexts.last().structure
        val loginFields = parseLoginFields(structure)
        
        // 2. Get package name or URL
        val appPackage = structure.activityComponent.packageName
        val webDomain = extractWebDomain(structure)
        
        // 3. Query vault for matching entries
        val matches = findMatchingEntries(appPackage, webDomain)
        
        // 4. Create fill response with authentication
        if (matches.isNotEmpty()) {
            val fillResponse = createAuthenticatedFillResponse(matches, loginFields)
            callback.onSuccess(fillResponse)
        } else {
            callback.onSuccess(null)
        }
    }
    
    override fun onSaveRequest(request: SaveRequest, callback: SaveCallback) {
        // Handle auto-save (see section 2)
    }
}
```

#### B. AndroidManifest.xml Configuration
```xml
<service
    android:name=".autofill.CipherMeshAutofillService"
    android:label="CipherMesh AutoFill"
    android:permission="android.permission.BIND_AUTOFILL_SERVICE"
    android:exported="true">
    <intent-filter>
        <action android:name="android.service.autofill.AutofillService" />
    </intent-filter>
    <meta-data
        android:name="android.autofill"
        android:resource="@xml/autofill_service_config" />
</service>
```

#### C. Matching Algorithm
```kotlin
fun findMatchingEntries(packageName: String, webDomain: String?): List<VaultEntry> {
    val allEntries = vault.getAllEntries()
    
    return allEntries.filter { entry ->
        entry.locations.any { location ->
            when (location.type) {
                "Android App" -> location.value == packageName
                "URL" -> webDomain != null && matchDomain(location.value, webDomain)
                else -> false
            }
        }
    }
}

fun matchDomain(storedUrl: String, webDomain: String): Boolean {
    val storedDomain = extractDomain(storedUrl)
    return storedDomain.equals(webDomain, ignoreCase = true)
}
```

#### D. Authentication Flow
```kotlin
fun createAuthenticatedFillResponse(
    matches: List<VaultEntry>,
    loginFields: LoginFields
): FillResponse {
    // Create intent for authentication activity
    val authIntent = Intent(this, AutofillAuthActivity::class.java).apply {
        putExtra("MATCHES", matches.toTypedArray())
        putExtra("LOGIN_FIELDS", loginFields)
    }
    
    val authPendingIntent = PendingIntent.getActivity(
        this,
        REQUEST_CODE_AUTOFILL,
        authIntent,
        PendingIntent.FLAG_CANCEL_CURRENT or PendingIntent.FLAG_IMMUTABLE
    )
    
    // Build response with authentication
    val presentation = RemoteViews(packageName, R.layout.autofill_item).apply {
        setTextViewText(R.id.text, "Unlock CipherMesh to autofill")
    }
    
    return FillResponse.Builder()
        .setAuthentication(
            loginFields.usernameAutofillId,
            authPendingIntent,
            presentation
        )
        .build()
}
```

#### E. Selection and Fill Activity
```kotlin
class AutofillAuthActivity : AppCompatActivity() {
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        
        // 1. Authenticate user (biometric or master password)
        authenticateUser { success ->
            if (success) {
                showAccountSelection()
            } else {
                setResult(RESULT_CANCELED)
                finish()
            }
        }
    }
    
    private fun showAccountSelection() {
        val matches = intent.getParcelableArrayExtra("MATCHES")
        
        // Show dialog to select which account
        MaterialAlertDialogBuilder(this)
            .setTitle("Select Account")
            .setItems(matches.map { it.title }.toTypedArray()) { _, which ->
                fillCredentials(matches[which])
            }
            .show()
    }
    
    private fun fillCredentials(entry: VaultEntry) {
        val loginFields = intent.getParcelableExtra<LoginFields>("LOGIN_FIELDS")
        
        // Decrypt password
        val password = vault.getDecryptedPassword(entry.id)
        
        // Create dataset with credentials
        val dataset = Dataset.Builder()
            .setValue(
                loginFields.usernameAutofillId,
                AutofillValue.forText(entry.username)
            )
            .setValue(
                loginFields.passwordAutofillId,
                AutofillValue.forText(password)
            )
            .build()
        
        // Return to AutofillService
        val replyIntent = Intent().apply {
            putExtra(AutofillManager.EXTRA_AUTHENTICATION_RESULT, dataset)
        }
        
        setResult(RESULT_OK, replyIntent)
        finish()
    }
}
```

### Implementation Steps
1. Create `AutofillService` subclass
2. Implement view structure parsing
3. Add location matching logic
4. Create authentication activity
5. Add account selection UI
6. Test with common apps (Chrome, Gmail, etc.)
7. Add to Settings for user to enable

### Security Considerations
- ✅ Require authentication before every autofill
- ✅ Don't cache decrypted passwords
- ✅ Use FLAG_SECURE on authentication screens
- ✅ Log autofill events for audit
- ✅ Allow users to disable per-app

---

## 2. Auto-Save Credentials

### What It Does
- Detects when user successfully logs into an app
- Prompts to save the credentials they just entered
- Asks which group to save in
- Requires authentication before saving
- Automatically adds app package/URL as location

### Android Architecture Required

#### A. Save Request Handler
```kotlin
override fun onSaveRequest(request: SaveRequest, callback: SaveCallback) {
    val structure = request.fillContexts.last().structure
    
    // Extract entered credentials
    val username = extractValueForId(structure, usernameAutofillId)
    val password = extractValueForId(structure, passwordAutofillId)
    
    // Get app context
    val packageName = structure.activityComponent.packageName
    val webDomain = extractWebDomain(structure)
    
    // Check if we already have this entry
    val existing = findMatchingEntry(username, packageName, webDomain)
    
    if (existing != null) {
        // Ask to update existing
        promptUpdateEntry(existing, password)
    } else {
        // Ask to save new entry
        promptSaveNewEntry(username, password, packageName, webDomain)
    }
    
    callback.onSuccess()
}
```

#### B. Save Prompt Activity
```kotlin
class AutoSaveActivity : AppCompatActivity() {
    
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_auto_save)
        
        val username = intent.getStringExtra("USERNAME")
        val password = intent.getStringExtra("PASSWORD")
        val packageName = intent.getStringExtra("PACKAGE")
        val webDomain = intent.getStringExtra("WEB_DOMAIN")
        
        // Get app name
        val appName = getAppName(packageName)
        
        // Show save prompt
        MaterialAlertDialogBuilder(this)
            .setTitle("Save Password?")
            .setMessage("Save credentials for $appName?")
            .setPositiveButton("Save") { _, _ ->
                authenticateAndSave(username, password, packageName, webDomain)
            }
            .setNegativeButton("Not Now", null)
            .setNeutralButton("Never for this app") { _, _ ->
                addToNeverSaveList(packageName)
            }
            .show()
    }
    
    private fun authenticateAndSave(
        username: String,
        password: String,
        packageName: String,
        webDomain: String?
    ) {
        // 1. Authenticate user
        BiometricHelper.authenticate(this) { success ->
            if (success) {
                showGroupSelection(username, password, packageName, webDomain)
            }
        }
    }
    
    private fun showGroupSelection(
        username: String,
        password: String,
        packageName: String,
        webDomain: String?
    ) {
        val groups = vault.getGroupNames()
        
        MaterialAlertDialogBuilder(this)
            .setTitle("Select Group")
            .setItems(groups) { _, which ->
                saveToGroup(groups[which], username, password, packageName, webDomain)
            }
            .show()
    }
    
    private fun saveToGroup(
        groupName: String,
        username: String,
        password: String,
        packageName: String,
        webDomain: String?
    ) {
        vault.setActiveGroup(groupName)
        
        // Create location
        val location = if (webDomain != null) {
            LocationData("URL", "https://$webDomain")
        } else {
            LocationData("Android App", packageName)
        }
        
        // Get app name as title
        val appName = getAppName(packageName) ?: webDomain ?: packageName
        
        // Save entry
        vault.addEntry(
            title = appName,
            username = username,
            password = password,
            type = "Login",
            locations = listOf(location),
            notes = "Auto-saved on ${Date()}",
            totpSecret = ""
        )
        
        Toast.makeText(this, "Saved to $groupName", Toast.LENGTH_SHORT).show()
        finish()
    }
}
```

#### C. Detection Logic
The AutofillService automatically calls `onSaveRequest` when:
1. User fills in login fields
2. User submits the form (clicks login button)
3. Fields have FLAG_SAVE set in ViewStructure

```kotlin
fun parseLoginFields(structure: ViewNode): LoginFields? {
    var usernameId: AutofillId? = null
    var passwordId: AutofillId? = null
    
    structure.traverse { node ->
        when {
            node.autofillHints?.contains(View.AUTOFILL_HINT_USERNAME) == true ||
            node.autofillHints?.contains(View.AUTOFILL_HINT_EMAIL_ADDRESS) == true -> {
                usernameId = node.autofillId
            }
            node.autofillHints?.contains(View.AUTOFILL_HINT_PASSWORD) == true -> {
                passwordId = node.autofillId
            }
            // Fallback: check input types
            node.inputType and InputType.TYPE_TEXT_VARIATION_EMAIL_ADDRESS != 0 -> {
                usernameId = node.autofillId
            }
            node.inputType and InputType.TYPE_TEXT_VARIATION_PASSWORD != 0 -> {
                passwordId = node.autofillId
            }
        }
    }
    
    return if (usernameId != null && passwordId != null) {
        LoginFields(usernameId!!, passwordId!!)
    } else {
        null
    }
}
```

### Implementation Steps
1. Implement `onSaveRequest` in AutofillService
2. Create auto-save prompt activity
3. Add group selection dialog
4. Implement never-save list
5. Add app name resolution
6. Test with common apps
7. Add settings to enable/disable

### User Experience Flow
```
User logs into Instagram
    ↓
AutofillService detects credentials
    ↓
"Save password for Instagram?" prompt appears
    ↓
User clicks "Save"
    ↓
Biometric authentication
    ↓
"Select group to save in" dialog
    ↓
User selects "Social Media"
    ↓
Entry saved with:
  - Title: "Instagram"
  - Username: "user@example.com"
  - Password: "***"
  - Location: "Android App: com.instagram.android"
    ↓
Toast: "Saved to Social Media"
```

---

## 3. Privacy & Security Features

### Never Save List
```kotlin
class NeverSaveManager(context: Context) {
    private val prefs = context.getSharedPreferences("never_save", Context.MODE_PRIVATE)
    
    fun addToNeverSave(packageName: String) {
        prefs.edit().putBoolean(packageName, true).apply()
    }
    
    fun shouldNeverSave(packageName: String): Boolean {
        return prefs.getBoolean(packageName, false)
    }
    
    fun removeFromNeverSave(packageName: String) {
        prefs.edit().remove(packageName).apply()
    }
    
    fun getAllNeverSave(): List<String> {
        return prefs.all.keys.toList()
    }
}
```

### Audit Logging
```kotlin
fun logAutofillEvent(action: String, appPackage: String, success: Boolean) {
    val log = AutofillLog(
        timestamp = System.currentTimeMillis(),
        action = action, // "FILL" or "SAVE"
        appPackage = appPackage,
        success = success
    )
    
    vault.addAuditLog(log)
}
```

### Settings UI
```xml
<!-- Settings for AutoFill -->
<PreferenceScreen>
    <SwitchPreference
        android:key="autofill_enabled"
        android:title="Enable AutoFill"
        android:summary="Automatically fill passwords in apps" />
        
    <SwitchPreference
        android:key="autosave_enabled"
        android:title="Enable Auto-Save"
        android:summary="Prompt to save passwords after login" />
        
    <Preference
        android:key="never_save_list"
        android:title="Never Save List"
        android:summary="Manage apps to never save credentials" />
        
    <SwitchPreference
        android:key="require_auth_autofill"
        android:title="Require Authentication"
        android:summary="Always require biometric/password before autofill"
        android:defaultValue="true" />
</PreferenceScreen>
```

---

## 4. Testing Strategy

### Unit Tests
- [ ] Location matching algorithm
- [ ] Domain extraction from URLs
- [ ] App package name resolution
- [ ] Never-save list management

### Integration Tests
- [ ] AutofillService lifecycle
- [ ] Save request handling
- [ ] Authentication flow
- [ ] Vault integration

### Manual Testing Apps
1. **Web browsers**: Chrome, Firefox
2. **Social media**: Instagram, Twitter/X, Facebook
3. **Email**: Gmail, Outlook
4. **Banking**: (use test accounts only)
5. **Shopping**: Amazon, eBay
6. **Custom apps**: Check various input types

### Edge Cases to Test
- [ ] Multiple accounts for same app
- [ ] Apps with custom login flows
- [ ] Web views within apps
- [ ] Biometric failure fallback
- [ ] Vault locked during autofill
- [ ] Screen rotation during flow
- [ ] App package name changes (updates)

---

## 5. Implementation Timeline

### Phase 1: Foundation (Week 1)
- [ ] Create AutofillService skeleton
- [ ] Implement ViewStructure parsing
- [ ] Add basic authentication flow

### Phase 2: AutoFill (Week 2)
- [ ] Implement location matching
- [ ] Create selection UI
- [ ] Add credential filling logic
- [ ] Test with common apps

### Phase 3: Auto-Save (Week 3)
- [ ] Implement save request handling
- [ ] Create save prompt UI
- [ ] Add group selection
- [ ] Implement never-save list

### Phase 4: Polish (Week 4)
- [ ] Add settings UI
- [ ] Implement audit logging
- [ ] Performance optimization
- [ ] Security review
- [ ] Documentation

---

## 6. Known Limitations

### Android Version Support
- AutofillService requires Android 8.0+ (API 26)
- Biometric auth requires Android 6.0+ (API 23)
- Some features may need different implementations for older versions

### App Compatibility
- Some apps block autofill for security
- Custom input fields may not be detected
- Web views may have different structure
- Banking apps often disable autofill

### Privacy Considerations
- Autofill events are visible to system
- Package names are stored in vault
- Consider user consent for telemetry

---

## 7. Alternative Approach: Accessibility Service

If AutofillService proves too restrictive, can use AccessibilityService (works on Android 4.1+):

```kotlin
class CipherMeshAccessibilityService : AccessibilityService() {
    
    override fun onAccessibilityEvent(event: AccessibilityEvent) {
        if (event.eventType == AccessibilityEvent.TYPE_VIEW_FOCUSED) {
            val node = event.source ?: return
            
            if (isPasswordField(node)) {
                // Show floating button to autofill
                showAutofillButton(node)
            }
        }
    }
    
    private fun isPasswordField(node: AccessibilityNodeInfo): Boolean {
        return node.inputType and InputType.TYPE_TEXT_VARIATION_PASSWORD != 0
    }
}
```

**Pros:**
- Works on older Android versions
- More flexible field detection
- Can handle custom UI

**Cons:**
- Less secure (can read all screen content)
- Users hesitant to grant accessibility permission
- May violate Play Store policies
- Requires constant background service

---

## 8. Conclusion

AutoFill and Auto-Save are advanced features requiring:
- Deep Android framework integration
- Careful security considerations
- Extensive testing across apps
- User education and setup

**Recommendation**: Implement as separate feature release after core password manager is stable and thoroughly tested. Consider starting with AutofillService on Android 8+ devices only, then expand based on user feedback.

**Estimated Effort**: 
- Full implementation: 3-4 weeks
- Testing & refinement: 2-3 weeks
- Total: ~6-7 weeks for production-ready feature

**Priority**: Medium-High (nice-to-have, not critical for MVP)
