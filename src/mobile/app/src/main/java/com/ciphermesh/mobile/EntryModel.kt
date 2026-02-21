package com.ciphermesh.mobile

data class EntryModel(
    val id: Int,
    val title: String,
    val subtitle: String,
    val totpSecret: String = ""
)