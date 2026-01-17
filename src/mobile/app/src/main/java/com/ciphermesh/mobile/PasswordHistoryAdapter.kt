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

    companion object {
        private val dateFormat = SimpleDateFormat("MMM dd, yyyy HH:mm", Locale.getDefault())
    }

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
        
        // Format timestamp with thread-safe date formatter and overflow prevention
        val dateStr = try {
            synchronized(dateFormat) {
                // Safely multiply timestamp, checking for reasonable range (after year 1970, before year 3000)
                val timestamp = item.timestamp
                if (timestamp in 0..32503680000) { // Unix epoch to year 3000
                    dateFormat.format(Date(timestamp * 1000))
                } else {
                    "Invalid date"
                }
            }
        } catch (e: Exception) {
            "Invalid date"
        }
        holder.textTimestamp.text = context.getString(R.string.password_changed_at, dateStr)
        
        holder.btnCopy.setOnClickListener {
            onCopy(item.encryptedPassword)
        }
    }

    override fun getItemCount() = items.size
}
