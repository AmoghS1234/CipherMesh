package com.ciphermesh.mobile

object CryptoBridge {
    init {
        System.loadLibrary("ciphermesh-native")
    }

    external fun encryptString(plaintext: String): ByteArray
    external fun decryptBlob(blob: ByteArray): String
}