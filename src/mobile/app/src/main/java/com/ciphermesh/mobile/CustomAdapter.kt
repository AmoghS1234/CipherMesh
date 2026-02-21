package com.ciphermesh.mobile

import android.content.Context
import android.graphics.Color
import android.util.TypedValue
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.ImageView
import android.widget.TextView
import java.util.ArrayList

class CustomAdapter(context: Context, private var dataList: ArrayList<EntryModel>) : 
    ArrayAdapter<EntryModel>(context, R.layout.item_list_row, dataList) {

    // Theme colors cached for performance
    private val colorPrimary: Int
    private val colorOnSurface: Int
    private val colorSecondary: Int
    private val colorError: Int
    
    init {
        val typedValue = TypedValue()
        context.theme.resolveAttribute(android.R.attr.textColorPrimary, typedValue, true)
        colorOnSurface = typedValue.data
        
        context.theme.resolveAttribute(android.R.attr.textColorSecondary, typedValue, true)
        colorSecondary = typedValue.data
        
        context.theme.resolveAttribute(com.google.android.material.R.attr.colorPrimary, typedValue, true)
        colorPrimary = typedValue.data
        
        context.theme.resolveAttribute(com.google.android.material.R.attr.colorError, typedValue, true)
        colorError = if (typedValue.data != 0) typedValue.data else Color.parseColor("#CF6679")
    }

    private class ViewHolder {
        lateinit var title: TextView
        lateinit var subtitle: TextView
        lateinit var icon: ImageView
        lateinit var frameContainer: android.widget.FrameLayout
    }

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val view: View
        val holder: ViewHolder

        if (convertView == null) {
            view = LayoutInflater.from(context).inflate(R.layout.item_list_row, parent, false)
            holder = ViewHolder()
            holder.title = view.findViewById(R.id.rowTitle)
            holder.subtitle = view.findViewById(R.id.rowSubtitle)
            holder.icon = view.findViewById(R.id.rowIcon)
            holder.frameContainer = holder.icon.parent as android.widget.FrameLayout
            view.tag = holder
        } else {
            view = convertView
            holder = view.tag as ViewHolder
        }

        val item = getItem(position) ?: return view

        holder.title.text = item.title
        holder.subtitle.text = item.subtitle

        // Reset to theme defaults
        holder.icon.alpha = 1.0f
        holder.icon.clearColorFilter()
        holder.title.setTextColor(colorOnSurface)
        holder.subtitle.setTextColor(colorSecondary)

        when {
            // Pending Invite - use error color
            item.id < -1000 -> {
                holder.icon.setImageResource(android.R.drawable.ic_dialog_email)
                holder.icon.setColorFilter(colorError)
                holder.icon.alpha = 0.8f
                holder.title.setTextColor(colorError)
                holder.subtitle.setTextColor(colorError)
            }

            // Syncing / Status Message - dimmed
            item.id == -1 -> {
                holder.icon.setImageResource(android.R.drawable.stat_notify_sync)
                holder.icon.setColorFilter(colorSecondary)
                holder.icon.alpha = 0.5f
                holder.title.setTextColor(colorSecondary)
                holder.subtitle.setTextColor(colorSecondary)
            }

            // Password Entry
            item.id > 0 -> {
                holder.icon.setImageResource(android.R.drawable.ic_lock_lock)
                holder.icon.setColorFilter(colorPrimary)
                
                // Removed redundant OTP circular indicator
            }

            // Group (Folder)
            else -> {
                holder.icon.setImageResource(android.R.drawable.ic_menu_myplaces)
                holder.icon.setColorFilter(colorPrimary)
                
                // Special highlight for Personal group
                if (item.title == "Personal") {
                    holder.title.setTextColor(colorPrimary)
                    holder.icon.setColorFilter(colorPrimary)
                }
            }
        }

        return view
    }

    fun updateData(newData: ArrayList<EntryModel>) {
        dataList.clear()
        dataList.addAll(newData)
        notifyDataSetChanged()
    }
}