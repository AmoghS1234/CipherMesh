package com.ciphermesh.util

import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.Scanner
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/**
 * Utility to check password against HaveIBeenPwned API (HIBP).
 * Uses k-Anonymity model: sends only first 5 chars of SHA-1 hash to API.
 */
object BreachChecker {

    /**
     * Checks if the password has been exposed in a breach.
     * @param password The password to check
     * @return Number of times seen in breaches (0 if safe, -1 if network error)
     */
    suspend fun checkPassword(password: String): Int = withContext(Dispatchers.IO) {
        if (password.isEmpty()) return@withContext 0

        try {
            val hash = sha1(password).uppercase()
            val prefix = hash.substring(0, 5)
            val suffix = hash.substring(5)

            val url = URL("https://api.pwnedpasswords.com/range/$prefix")
            val conn = url.openConnection() as HttpURLConnection
            conn.requestMethod = "GET"
            conn.connectTimeout = 5000
            conn.readTimeout = 5000
            
            // Add User-Agent as requested by HIBP API
            conn.setRequestProperty("User-Agent", "CipherMesh-Mobile")

            if (conn.responseCode != 200) {
                return@withContext -1
            }

            val scanner = Scanner(conn.inputStream)
            while (scanner.hasNextLine()) {
                val line = scanner.nextLine()
                val parts = line.split(":")
                if (parts.size >= 2) {
                    if (parts[0] == suffix) {
                        return@withContext parts[1].toInt()
                    }
                }
            }
            return@withContext 0

        } catch (e: Exception) {
            e.printStackTrace()
            return@withContext -1
        }
    }

    private fun sha1(input: String): String {
        val bytes = MessageDigest.getInstance("SHA-1").digest(input.toByteArray())
        return bytes.joinToString("") { "%02x".format(it) }
    }
}
