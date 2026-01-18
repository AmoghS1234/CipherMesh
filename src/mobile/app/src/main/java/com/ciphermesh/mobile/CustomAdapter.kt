package com.ciphermesh.mobile

import android.content.Context
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.ImageView
import android.widget.TextView
import java.util.ArrayList

class CustomAdapter(context: Context, private var dataList: ArrayList<EntryModel>) : ArrayAdapter<EntryModel>(context, R.layout.item_list_row, dataList) {

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        // 1. Inflate the new custom layout (item_list_row)
        val view = convertView ?: LayoutInflater.from(context).inflate(R.layout.item_list_row, parent, false)

        // 2. Get the data item for this position
        val item = getItem(position) ?: return view

        // 3. Find views in the new layout
        val titleText = view.findViewById<TextView>(R.id.rowTitle)
        val subtitleText = view.findViewById<TextView>(R.id.rowSubtitle)
        val iconImg = view.findViewById<ImageView>(R.id.rowIcon)

        // 4. Set Text Data
        titleText.text = item.title
        subtitleText.text = item.subtitle

        // 5. Set Logic for Icon based on ID
        // - ID < -1000: Pending Invite
        // - ID == -1:   Syncing Status
        // - ID == 0:    Group (Folder)
        // - ID > 0:     Vault Entry (Lock)
        
        when {
            item.id < -1000 -> {
                // Pending Invite
                iconImg.setImageResource(android.R.drawable.ic_dialog_email)
                iconImg.alpha = 0.7f // Dim it slightly
            }
            item.id == -1 -> {
                // Syncing / Loading
                iconImg.setImageResource(android.R.drawable.stat_notify_sync)
                iconImg.alpha = 0.5f
            }
            item.id > 0 -> {
                // Actual Password Entry
                iconImg.setImageResource(android.R.drawable.ic_lock_lock)
                iconImg.alpha = 1.0f
            }
            else -> {
                // Group (ID is usually 0 for groups in this app logic)
                iconImg.setImageResource(android.R.drawable.ic_menu_myplaces) 
                iconImg.alpha = 1.0f
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