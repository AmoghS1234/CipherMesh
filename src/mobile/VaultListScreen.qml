import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: vaultListPage
    
    signal entrySelected(int entryId)
    signal settingsRequested()
    signal lockRequested()
    
    Material.theme: Material.Dark
    Material.primary: Material.Teal
    Material.accent: Material.Cyan
    
    header: ToolBar {
        Material.primary: "#1a1a2e"
        Material.elevation: 4
        
        RowLayout {
            anchors.fill: parent
            spacing: 10
            
            // Menu Button (Groups/Settings)
            ToolButton {
                icon.source: "qrc:/icons/menu.svg"
                onClicked: drawer.open()
                
                Material.foreground: "white"
            }
            
            // Current Group Label
            Label {
                Layout.fillWidth: true
                text: vaultBackend.currentGroup || "Personal"
                font.pixelSize: 20
                font.bold: true
                color: "white"
                elide: Text.ElideRight
            }
            
            // Search Button
            ToolButton {
                icon.source: "qrc:/icons/search.svg"
                onClicked: searchBar.visible = !searchBar.visible
                
                Material.foreground: "white"
            }
            
            // Lock Button
            ToolButton {
                icon.source: "qrc:/icons/lock.svg"
                onClicked: lockRequested()
                
                Material.foreground: "white"
            }
        }
    }
    
    // Search Bar
    Rectangle {
        id: searchBar
        anchors.top: parent.top
        width: parent.width
        height: visible ? 60 : 0
        color: "#2a2a3e"
        visible: false
        z: 10
        
        Behavior on height {
            NumberAnimation { duration: 200 }
        }
        
        RowLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 10
            
            TextField {
                id: searchField
                Layout.fillWidth: true
                placeholderText: "Search entries..."
                font.pixelSize: 16
                Material.containerStyle: Material.Filled
                
                onTextChanged: {
                    if (text.length > 0) {
                        entryListModel.clear()
                        var results = vaultBackend.searchEntries(text)
                        for (var i = 0; i < results.length; i++) {
                            entryListModel.append(results[i])
                        }
                    } else {
                        refreshEntries()
                    }
                }
            }
            
            ToolButton {
                icon.source: "qrc:/icons/close.svg"
                onClicked: {
                    searchField.text = ""
                    searchBar.visible = false
                    refreshEntries()
                }
            }
        }
    }
    
    // Main Content
    ColumnLayout {
        anchors.fill: parent
        anchors.topMargin: searchBar.visible ? searchBar.height : 0
        spacing: 0
        
        // Entry List
        ListView {
            id: entryListView
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            
            model: ListModel {
                id: entryListModel
            }
            
            delegate: ItemDelegate {
                width: ListView.view.width
                height: 80
                
                background: Rectangle {
                    color: index % 2 === 0 ? "#1e1e2e" : "#24243e"
                    
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width
                        height: 1
                        color: "#3a3a4e"
                    }
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    // Icon
                    Rectangle {
                        Layout.preferredWidth: 50
                        Layout.preferredHeight: 50
                        radius: 25
                        color: Material.primary
                        
                        Text {
                            anchors.centerIn: parent
                            text: model.entryType === "secure_note" ? "📝" : "🔑"
                            font.pixelSize: 28
                        }
                    }
                    
                    // Entry Info
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 5
                        
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            
                            Label {
                                Layout.fillWidth: true
                                text: model.title
                                font.pixelSize: 16
                                font.bold: true
                                color: "white"
                                elide: Text.ElideRight
                            }
                            
                            // TOTP Indicator
                            Rectangle {
                                visible: model.hasTotp
                                Layout.preferredWidth: 24
                                Layout.preferredHeight: 24
                                radius: 12
                                color: Material.accent
                                
                                Text {
                                    anchors.centerIn: parent
                                    text: "⏱"
                                    font.pixelSize: 14
                                }
                            }
                        }
                        
                        Label {
                            Layout.fillWidth: true
                            text: model.username || "No username"
                            font.pixelSize: 14
                            color: "#b0b0b0"
                            elide: Text.ElideRight
                        }
                        
                        Label {
                            Layout.fillWidth: true
                            text: formatTimestamp(model.lastModified)
                            font.pixelSize: 12
                            color: "#808080"
                        }
                    }
                    
                    // Chevron
                    Text {
                        text: "›"
                        font.pixelSize: 24
                        color: "#606060"
                    }
                }
                
                onClicked: {
                    entrySelected(model.id)
                }
            }
            
            // Empty State
            Label {
                anchors.centerIn: parent
                visible: entryListView.count === 0
                text: searchField.text.length > 0 ? "No entries found" : "No entries yet\nTap + to add one"
                font.pixelSize: 16
                color: "#808080"
                horizontalAlignment: Text.AlignHCenter
            }
            
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
            }
        }
    }
    
    // Floating Action Button
    RoundButton {
        id: fab
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        width: 60
        height: 60
        text: "+"
        font.pixelSize: 32
        Material.background: Material.Teal
        Material.elevation: 6
        
        onClicked: {
            entrySelected(-1) // -1 indicates new entry
        }
    }
    
    // Side Drawer for Groups
    Drawer {
        id: drawer
        width: Math.min(parent.width * 0.75, 300)
        height: parent.height
        
        Material.background: "#1a1a2e"
        
        ColumnLayout {
            anchors.fill: parent
            spacing: 0
            
            // Drawer Header
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 150
                color: Material.primary
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    
                    Text {
                        text: "🔐"
                        font.pixelSize: 48
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: "CipherMesh"
                        font.pixelSize: 24
                        font.bold: true
                        color: "white"
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: vaultBackend.currentGroup || "Personal"
                        font.pixelSize: 14
                        color: "#e0e0e0"
                    }
                }
            }
            
            // Groups Section
            Label {
                Layout.fillWidth: true
                Layout.margins: 15
                text: "GROUPS"
                font.pixelSize: 12
                font.bold: true
                color: "#808080"
            }
            
            ListView {
                id: groupListView
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                
                model: ListModel {
                    id: groupListModel
                }
                
                delegate: ItemDelegate {
                    width: ListView.view.width
                    height: 50
                    
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 15
                        spacing: 15
                        
                        Text {
                            text: "📁"
                            font.pixelSize: 20
                        }
                        
                        Label {
                            Layout.fillWidth: true
                            text: model.name
                            font.pixelSize: 16
                            color: "white"
                        }
                    }
                    
                    onClicked: {
                        vaultBackend.setActiveGroup(model.name)
                        refreshEntries()
                        drawer.close()
                    }
                }
            }
            
            // Drawer Actions
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
            
            ItemDelegate {
                Layout.fillWidth: true
                height: 50
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "➕"
                        font.pixelSize: 20
                    }
                    
                    Label {
                        text: "New Group"
                        font.pixelSize: 16
                        color: Material.accent
                    }
                }
                
                onClicked: {
                    drawer.close()
                    newGroupDialog.open()
                }
            }
            
            ItemDelegate {
                Layout.fillWidth: true
                height: 50
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    Text {
                        text: "⚙️"
                        font.pixelSize: 20
                    }
                    
                    Label {
                        text: "Settings"
                        font.pixelSize: 16
                        color: "white"
                    }
                }
                
                onClicked: {
                    drawer.close()
                    settingsRequested()
                }
            }
        }
    }
    
    // New Group Dialog
    Dialog {
        id: newGroupDialog
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "New Group"
        modal: true
        
        Material.background: "#2a2a3e"
        
        ColumnLayout {
            width: parent.width
            spacing: 15
            
            TextField {
                id: groupNameField
                Layout.fillWidth: true
                placeholderText: "Group name"
                font.pixelSize: 16
            }
        }
        
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        onAccepted: {
            if (groupNameField.text.length > 0) {
                if (vaultBackend.createGroup(groupNameField.text)) {
                    refreshGroups()
                    groupNameField.text = ""
                }
            }
        }
        
        onRejected: {
            groupNameField.text = ""
        }
    }
    
    // Helper Functions
    function formatTimestamp(timestamp) {
        if (!timestamp) return ""
        var date = new Date(timestamp * 1000)
        var now = new Date()
        var diff = now - date
        var days = Math.floor(diff / (1000 * 60 * 60 * 24))
        
        if (days === 0) return "Today"
        if (days === 1) return "Yesterday"
        if (days < 7) return days + " days ago"
        if (days < 30) return Math.floor(days / 7) + " weeks ago"
        if (days < 365) return Math.floor(days / 30) + " months ago"
        return Math.floor(days / 365) + " years ago"
    }
    
    function refreshEntries() {
        entryListModel.clear()
        var entries = vaultBackend.getEntries()
        for (var i = 0; i < entries.length; i++) {
            entryListModel.append(entries[i])
        }
    }
    
    function refreshGroups() {
        groupListModel.clear()
        var groups = vaultBackend.getGroups()
        for (var i = 0; i < groups.length; i++) {
            groupListModel.append(groups[i])
        }
    }
    
    // Connections to backend signals
    Connections {
        target: vaultBackend
        
        function onEntryAdded() {
            refreshEntries()
        }
        
        function onEntryUpdated() {
            refreshEntries()
        }
        
        function onEntryDeleted() {
            refreshEntries()
        }
        
        function onGroupsChanged() {
            refreshGroups()
        }
    }
    
    Component.onCompleted: {
        refreshEntries()
        refreshGroups()
    }
}
