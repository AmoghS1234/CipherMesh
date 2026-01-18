package com.ciphermesh.mobile.util

import java.nio.ByteBuffer
import java.security.InvalidKeyException
import java.security.NoSuchAlgorithmException
import javax.crypto.Mac
import javax.crypto.spec.SecretKeySpec
import java.util.Base64 // Android O+ or use android.util.Base64

object TotpUtil {

    fun generateTOTP(secret: String): String {
        if (secret.isEmpty()) return "--- ---"
        
        try {
            // Clean secret string (remove spaces, uppercase)
            val cleanSecret = secret.replace(" ", "").uppercase()
            val keyBytes = decodeBase32(cleanSecret) ?: return "Invalid Key"
            
            // Time step (30 seconds)
            val time = (System.currentTimeMillis() / 1000) / 30
            
            // Create data
            val data = ByteBuffer.allocate(8).putLong(time).array()
            val signKey = SecretKeySpec(keyBytes, "HmacSHA1")
            
            val mac = Mac.getInstance("HmacSHA1")
            mac.init(signKey)
            val hash = mac.doFinal(data)

            // Truncate - HMAC-SHA1 always produces 20 bytes, but validate to be safe
            if (hash.size < 20) return "000000" // Safety check
            
            val offset = hash[hash.size - 1].toInt() and 0xF
            
            // Validate offset bounds: need offset + 3 < hash.size
            if (offset + 3 >= hash.size) {
                return "000000" // Invalid offset, return safe default
            }
            
            var binary = ((hash[offset].toInt() and 0x7f) shl 24) or
                    ((hash[offset + 1].toInt() and 0xff) shl 16) or
                    ((hash[offset + 2].toInt() and 0xff) shl 8) or
                    (hash[offset + 3].toInt() and 0xff)

            val otp = binary % 1000000
            return String.format("%06d", otp)
            
        } catch (e: Exception) {
            return "Error"
        }
    }

    private fun decodeBase32(secret: String): ByteArray? {
        // Simple Base32 decoder (or use a library if available, creating simple one here)
        val base32chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"
        var bits = ""
        for (c in secret) {
            val `val` = base32chars.indexOf(c)
            if (`val` < 0) return null
            bits += String.format("%5s", Integer.toBinaryString(`val`)).replace(' ', '0')
        }
        
        val bytes = ByteArray(bits.length / 8)
        for (i in bytes.indices) {
            bytes[i] = Integer.parseInt(bits.substring(i * 8, (i + 1) * 8), 2).toByte()
        }
        return bytes
    }
}