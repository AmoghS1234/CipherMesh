package com.ciphermesh.mobile

import android.content.Context
import android.graphics.Color
import android.view.LayoutInflater
import android.view.View
import android.view.ViewGroup
import android.widget.ArrayAdapter
import android.widget.TextView

class CustomAdapter(context: Context, private var dataList: ArrayList<EntryModel>) 
    : ArrayAdapter<EntryModel>(context, R.layout.item_vault_entry, dataList) {

    override fun getView(position: Int, convertView: View?, parent: ViewGroup): View {
        val entry = getItem(position)
        var view = convertView

        if (view == null) {
            // [FIX] Correct layout name
            view = LayoutInflater.from(context).inflate(R.layout.item_vault_entry, parent, false)
        }

        val title = view!!.findViewById<TextView>(R.id.itemTitle)
        val subtitle = view.findViewById<TextView>(R.id.itemSubtitle)

        title.text = entry?.title
        subtitle.text = entry?.subtitle
        
        // Show pending invites in red (like desktop app)
        if (entry?.subtitle == "Pending") {
            title.setTextColor(Color.parseColor("#FF5555")) // Red color for pending invites
        } else {
            title.setTextColor(Color.parseColor("#FFFFFF")) // White color for normal entries
        }

        return view
    }

    fun updateData(newData: List<EntryModel>) {
        dataList.clear()
        dataList.addAll(newData)
        notifyDataSetChanged()
    }
}