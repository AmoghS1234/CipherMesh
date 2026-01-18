package com.ciphermesh.mobile.autofill

import android.content.Context
import android.os.Parcelable
import com.ciphermesh.mobile.core.Vault
import kotlinx.parcelize.Parcelize
import org.json.JSONObject
import java.net.URL

@Parcelize
data class MatchedEntry(
    val id: Int,
    val title: String,
    val username: String
) : Parcelable

data class LocationData(
    val type: String,
    val value: String
)

class EntryMatcher(private val context: Context) {
    
    private val vault = Vault()
    
    fun findMatches(packageName: String, webDomain: String?): List<MatchedEntry> {
        if (vault.isLocked()) {
            return emptyList()
        }
        
        val allGroups = vault.getGroupNames() ?: return emptyList()
        val matches = mutableListOf<MatchedEntry>()
        
        // Search through all groups
        for (groupName in allGroups) {
            try {
                vault.setActiveGroup(groupName)
                val entries = vault.getEntries() ?: continue
                
                for (entry in entries) {
                    if (entryMatches(entry, packageName, webDomain)) {
                        val parts = entry.split("|")
                        if (parts.size >= 3) {
                            matches.add(MatchedEntry(
                                id = parts[0].toIntOrNull() ?: continue,
                                title = parts[1],
                                username = parts[2]
                            ))
                        }
                    }
                }
            } catch (e: Exception) {
                // Skip this group if there's an error
                continue
            }
        }
        
        return matches
    }
    
    private fun entryMatches(
        entry: String, // Entry format: "id|title|username|locations"
        packageName: String,
        webDomain: String?
    ): Boolean {
        val parts = entry.split("|")
        if (parts.size < 4) return false
        
        val locations = parseLocations(parts[3])
        
        return locations.any { location ->
            when {
                // Check Android app package
                location.type.equals("Android App", ignoreCase = true) &&
                location.value == packageName -> true
                
                // Check URL/web domain
                location.type.equals("URL", ignoreCase = true) &&
                webDomain != null &&
                domainMatches(location.value, webDomain) -> true
                
                else -> false
            }
        }
    }
    
    private fun parseLocations(locationsJson: String): List<LocationData> {
        val locations = mutableListOf<LocationData>()
        
        try {
            val json = JSONObject(locationsJson)
            val locArray = json.optJSONArray("locations") ?: return locations
            
            for (i in 0 until locArray.length()) {
                val locObj = locArray.getJSONObject(i)
                locations.add(LocationData(
                    type = locObj.optString("type", ""),
                    value = locObj.optString("value", "")
                ))
            }
        } catch (e: Exception) {
            // Fallback to empty list
        }
        
        return locations
    }
    
    private fun domainMatches(urlString: String, targetDomain: String): Boolean {
        return try {
            val url = URL(if (urlString.startsWith("http")) urlString else "https://$urlString")
            val storedDomain = url.host.lowercase()
            val target = targetDomain.lowercase()
            
            // Exact match or subdomain match
            storedDomain == target || 
            storedDomain.endsWith(".$target") ||
            target.endsWith(".$storedDomain")
        } catch (e: Exception) {
            // Fallback: simple string comparison
            urlString.contains(targetDomain, ignoreCase = true)
        }
    }
}
