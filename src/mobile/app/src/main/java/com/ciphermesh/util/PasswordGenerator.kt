package com.ciphermesh.util

import kotlin.math.log2
import kotlin.math.pow
import kotlin.random.Random

/**
 * Password Generator Utility
 * Generates secure random passwords and calculates password strength
 */
object PasswordGenerator {
    
    /**
     * Generate a random password with specified options
     */
    fun generate(
        length: Int = 16,
        useUppercase: Boolean = true,
        useLowercase: Boolean = true,
        useNumbers: Boolean = true,
        useSymbols: Boolean = true,
        symbols: String = "!@#$%^&*()_+-=[]{}|;:,.<>?"
    ): String {
        val chars = buildString {
            if (useUppercase) append("ABCDEFGHIJKLMNOPQRSTUVWXYZ")
            if (useLowercase) append("abcdefghijklmnopqrstuvwxyz")
            if (useNumbers) append("0123456789")
            if (useSymbols) append(symbols)
        }
        
        if (chars.isEmpty() || length < 4) return ""
        
        // Use secure random
        return (1..length)
            .map { chars[Random.nextInt(chars.length)] }
            .joinToString("")
    }
    
    /**
     * Calculate password strength score (0-100)
     */
    fun calculateStrength(password: String): Int {
        if (password.isEmpty()) return 0
        
        val hasUpper = password.any { it.isUpperCase() }
        val hasLower = password.any { it.isLowerCase() }
        val hasDigit = password.any { it.isDigit() }
        val hasSymbol = password.any { !it.isLetterOrDigit() }
        
        // Variety score (0-4)
        val varietyScore = listOf(hasUpper, hasLower, hasDigit, hasSymbol).count { it }
        
        // Length score (0-3)
        val lengthScore = when {
            password.length < 8 -> 0
            password.length < 12 -> 1
            password.length < 16 -> 2
            else -> 3
        }
        
        // Entropy score (0-3)
        val entropy = calculateEntropy(password)
        val entropyScore = when {
            entropy < 30 -> 0
            entropy < 50 -> 1
            entropy < 70 -> 2
            else -> 3
        }
        
        // Calculate total (0-10) and scale to 0-100
        val totalScore = ((varietyScore + lengthScore + entropyScore) * 100) / 10
        return totalScore.coerceIn(0, 100)
    }
    
    /**
     * Calculate password entropy (bits)
     */
    private fun calculateEntropy(password: String): Double {
        val charsetSize = when {
            password.any { it.isUpperCase() } && 
            password.any { it.isLowerCase() } && 
            password.any { it.isDigit() } && 
            password.any { !it.isLetterOrDigit() } -> 94.0  // Full set
            
            password.any { it.isLetter() } && 
            password.any { it.isDigit() } -> 62.0  // Letters + numbers
            
            password.any { it.isLetter() } -> 52.0  // Just letters
            else -> 10.0  // Just numbers
        }
        
        return password.length * log2(charsetSize)
    }
    
    /**
     * Get text description of strength score
     */
    fun getStrengthText(score: Int): String = when {
        score < 20 -> "Very Weak"
        score < 40 -> "Weak"
        score < 60 -> "Fair"
        score < 80 -> "Good"
        else -> "Strong"
    }
    
    /**
     * Get color for strength score (Android color resource style)
     */
    fun getStrengthColor(score: Int): Int = when {
        score < 20 -> android.graphics.Color.parseColor("#D32F2F") // Red
        score < 40 -> android.graphics.Color.parseColor("#F57C00") // Orange
        score < 60 -> android.graphics.Color.parseColor("#FBC02D") // Yellow
        score < 80 -> android.graphics.Color.parseColor("#388E3C") // Green
        else -> android.graphics.Color.parseColor("#1976D2") // Blue
    }
    
    /**
     * Check if password meets minimum requirements
     */
    fun meetsRequirements(password: String): Boolean {
        return password.length >= 8 &&
               password.any { it.isUpperCase() } &&
               password.any { it.isLowerCase() } &&
               password.any { it.isDigit() }
    }
}
