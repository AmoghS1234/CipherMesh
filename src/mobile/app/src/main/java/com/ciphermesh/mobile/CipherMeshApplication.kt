package com.ciphermesh.mobile

import android.app.Activity
import android.app.Application
import android.content.Context
import android.content.Intent
import android.os.Bundle
import com.ciphermesh.mobile.core.Vault

class CipherMeshApplication : Application(), Application.ActivityLifecycleCallbacks {

    companion object {
        const val PREF_LOCK_TIMEOUT = "auto_lock_timeout"
        // Default to 10 minutes (600000ms)
        const val DEFAULT_TIMEOUT = 600000L 
    }

    private var activityReferences = 0
    private var isActivityChangingConfigurations = false
    private var lastBackgroundTimeStamp: Long = 0
    private val vault = Vault()

    override fun onCreate() {
        super.onCreate()
        registerActivityLifecycleCallbacks(this)
    }

    override fun onActivityCreated(activity: Activity, savedInstanceState: Bundle?) {}
    
    override fun onActivityStarted(activity: Activity) {
        if (++activityReferences == 1 && !isActivityChangingConfigurations) {
            // App enters foreground
            val prefs = getSharedPreferences("app_prefs", Context.MODE_PRIVATE)
            val timeoutStr = prefs.getString(PREF_LOCK_TIMEOUT, DEFAULT_TIMEOUT.toString())
            val timeout = timeoutStr?.toLongOrNull() ?: DEFAULT_TIMEOUT

            // If timeout is "Never" (-1), skip lock
            if (timeout != -1L) {
                val now = System.currentTimeMillis()
                if (lastBackgroundTimeStamp > 0 && (now - lastBackgroundTimeStamp) > timeout) {
                    // Lock the vault
                    vault.lock()
                    
                    // If we are not already on MainActivity, go there
                    if (activity !is MainActivity) {
                        val intent = Intent(activity, MainActivity::class.java)
                        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK)
                        startActivity(intent)
                    }
                }
            }
        }
    }

    override fun onActivityResumed(activity: Activity) {}

    override fun onActivityPaused(activity: Activity) {}

    override fun onActivityStopped(activity: Activity) {
        isActivityChangingConfigurations = activity.isChangingConfigurations
        if (--activityReferences == 0 && !isActivityChangingConfigurations) {
            // App enters background
            lastBackgroundTimeStamp = System.currentTimeMillis()
        }
    }

    override fun onActivitySaveInstanceState(activity: Activity, outState: Bundle) {}

    override fun onActivityDestroyed(activity: Activity) {}
}
