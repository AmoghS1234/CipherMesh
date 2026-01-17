package com.ciphermesh.mobile

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
import android.widget.Toast
import androidx.recyclerview.widget.RecyclerView
import com.google.android.material.button.MaterialButton
import java.text.SimpleDateFormat
import java.util.*

data class PasswordHistoryItem(
    val encryptedPassword: String,
    val timestamp: Long
)

class PasswordHistoryAdapter(
    private val context: Context,
    private val items: List<PasswordHistoryItem>,
    private val onCopy: (String) -> Unit
) : RecyclerView.Adapter<PasswordHistoryAdapter.ViewHolder>() {

    class ViewHolder(view: View) : RecyclerView.ViewHolder(view) {
        val textPasswordMasked: TextView = view.findViewById(R.id.textPasswordMasked)
        val textTimestamp: TextView = view.findViewById(R.id.textTimestamp)
        val btnCopy: MaterialButton = view.findViewById(R.id.btnCopyHistoryPassword)
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_password_history, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val item = items[position]
        
        // Format timestamp
        val sdf = SimpleDateFormat("MMM dd, yyyy HH:mm", Locale.getDefault())
        val dateStr = sdf.format(Date(item.timestamp * 1000))
        holder.textTimestamp.text = context.getString(R.string.password_changed_at, dateStr)
        
        holder.btnCopy.setOnClickListener {
            onCopy(item.encryptedPassword)
        }
    }

    override fun getItemCount() = items.size
}
