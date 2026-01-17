package com.ciphermesh.mobile

import android.content.Context
import android.graphics.Typeface
import android.util.TypedValue
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.ImageView
import android.widget.TextView

class CustomAdapter(context: Context, private var dataList: ArrayList<EntryModel>) 
    : ArrayAdapter<EntryModel>(context, R.layout.item_vault_entry, dataList) {

    // Helper to get theme color safely
    private fun getThemeColor(attrId: Int): Int {
        val typedValue = TypedValue()
        context.theme.resolveAttribute(attrId, typedValue, true)
        return typedValue.data
    }

    private val defaultTextColor: Int by lazy { getThemeColor(android.R.attr.textColorPrimary) }
    private val primaryColor: Int by lazy { getThemeColor(com.google.android.material.R.attr.colorPrimary) } // FIX: Use Material R
    private val errorColor: Int by lazy { context.getColor(R.color.error_red) }

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val entry = getItem(position) ?: return convertView ?: View(context)
        val view = convertView ?: LayoutInflater.from(context).inflate(R.layout.item_vault_entry, parent, false)

        val title = view.findViewById<TextView>(R.id.itemTitle)
        val subtitle = view.findViewById<TextView>(R.id.itemSubtitle)
        val icon = view.findViewById<ImageView>(R.id.itemIcon)

        title.text = entry.title
        subtitle.text = entry.subtitle
        subtitle.visibility = View.VISIBLE

        // Reset Styles
        title.setTextColor(defaultTextColor)
        title.typeface = Typeface.DEFAULT_BOLD
        icon.alpha = 1.0f
        icon.colorFilter = null 

        // Conditional Styling
        if (entry.subtitle == "Pending" || entry.subtitle == "Syncing...") {
            title.setTextColor(errorColor)
            icon.setImageResource(android.R.drawable.ic_dialog_email)
            icon.setColorFilter(errorColor)
            if (entry.subtitle == "Syncing...") icon.alpha = 0.5f
        } 
        else if (entry.subtitle == "Owner" || entry.subtitle == "Member") {
            icon.setImageResource(android.R.drawable.ic_menu_my_calendar)
            icon.setColorFilter(primaryColor) // FIX: Use cached color
        } 
        else {
            icon.setImageResource(android.R.drawable.ic_lock_lock)
            icon.setColorFilter(primaryColor) // FIX: Use cached color
        }
        return view
    }

    fun updateData(newData: List<EntryModel>) {
        dataList.clear()
        dataList.addAll(newData)
        notifyDataSetChanged()
    }
}