package com.ciphermesh.mobile

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.TextView
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
        // [FIX] Updated ID to match item_password_history.xml
        val textTimestamp: TextView = view.findViewById(R.id.textHistoryDate) 
        // [FIX] Updated ID to match item_password_history.xml
        val btnCopy: MaterialButton = view.findViewById(R.id.btnHistoryCopy) 
    }

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): ViewHolder {
        val view = LayoutInflater.from(parent.context)
            .inflate(R.layout.item_password_history, parent, false)
        return ViewHolder(view)
    }

    override fun onBindViewHolder(holder: ViewHolder, position: Int) {
        val item = items[position]
        
        val dateStr = try {
            synchronized(dateFormat) {
                val timestamp = item.timestamp
                if (timestamp in 0..32503680000) { 
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