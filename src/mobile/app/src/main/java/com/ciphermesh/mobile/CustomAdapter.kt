package com.ciphermesh.mobile

import android.content.Context
import android.graphics.Color
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.ImageView
import android.widget.TextView
import java.util.ArrayList

class CustomAdapter(context: Context, private var dataList: ArrayList<EntryModel>) : 
    ArrayAdapter<EntryModel>(context, R.layout.item_list_row, dataList) {

    // View Holder Pattern to improve scrolling performance
    private class ViewHolder {
        lateinit var title: TextView
        lateinit var subtitle: TextView
        lateinit var icon: ImageView
    }

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val view: View
        val holder: ViewHolder

        // 1. Recycle View if possible (Performance Optimization)
        if (convertView == null) {
            view = LayoutInflater.from(context).inflate(R.layout.item_list_row, parent, false)
            holder = ViewHolder()
            holder.title = view.findViewById(R.id.rowTitle)
            holder.subtitle = view.findViewById(R.id.rowSubtitle)
            holder.icon = view.findViewById(R.id.rowIcon)
            view.tag = holder
        } else {
            view = convertView
            holder = view.tag as ViewHolder
        }

        // 2. Get Data
        val item = getItem(position) ?: return view

        // 3. Set Base Text
        holder.title.text = item.title
        holder.subtitle.text = item.subtitle

        // 4. Default Styling (Reset state for recycled views)
        holder.icon.alpha = 1.0f
        holder.title.setTextColor(Color.WHITE)
        holder.subtitle.setTextColor(Color.parseColor("#B0B0B0")) // Light Gray

        // 5. Apply Logic based on ID types
        when {
            // --- Case A: Pending Invite ---
            item.id < -1000 -> {
                holder.icon.setImageResource(android.R.drawable.ic_dialog_email)
                holder.icon.alpha = 0.8f
                
                // Red Alert Style
                holder.title.setTextColor(Color.parseColor("#FF5252")) 
                holder.subtitle.setTextColor(Color.parseColor("#FF8A80")) 
            }

            // --- Case B: Syncing / Status Message ---
            item.id == -1 -> {
                holder.icon.setImageResource(android.R.drawable.stat_notify_sync)
                holder.icon.alpha = 0.5f
                
                // Dimmed Gray Style
                holder.title.setTextColor(Color.parseColor("#757575"))
                holder.subtitle.setTextColor(Color.parseColor("#9E9E9E"))
            }

            // --- Case C: Password Entry ---
            item.id > 0 -> {
                // Check if it's a Note or a Password based on title/logic (optional)
                holder.icon.setImageResource(android.R.drawable.ic_lock_lock)
            }

            // --- Case D: Group (Folder) ---
            else -> {
                holder.icon.setImageResource(android.R.drawable.ic_menu_myplaces)
                
                // Special highlight for Personal group
                if (item.title == "Personal") {
                    holder.title.setTextColor(Color.parseColor("#64B5F6")) // Light Blue
                }
            }
        }

        return view
    }

    // Helper to refresh list from Activity
    fun updateData(newData: ArrayList<EntryModel>) {
        dataList.clear()
        dataList.addAll(newData)
        notifyDataSetChanged()
    }
}