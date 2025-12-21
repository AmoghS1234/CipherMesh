import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: settingsPage
    
    signal back()
    
    Material.theme: Material.Dark
    Material.primary: Material.Teal
    Material.accent: Material.Cyan
    
    header: ToolBar {
        Material.primary: "#1a1a2e"
        Material.elevation: 4
        
        RowLayout {
            anchors.fill: parent
            spacing: 10
            
            ToolButton {
                text: "‹"
                font.pixelSize: 32
                onClicked: back()
                
                Material.foreground: "white"
            }
            
            Label {
                Layout.fillWidth: true
                text: "Settings"
                font.pixelSize: 20
                font.bold: true
                color: "white"
            }
        }
    }
    
    Flickable {
        anchors.fill: parent
        contentHeight: settingsColumn.height + 40
        clip: true
        
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        
        ColumnLayout {
            id: settingsColumn
            width: parent.width
            spacing: 0
            
            // Security Section
            Label {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.topMargin: 30
                text: "SECURITY"
                font.pixelSize: 12
                font.bold: true
                color: "#808080"
            }
            
            // Change Master Password
            ItemDelegate {
                Layout.fillWidth: true
                height: 60
                
                background: Rectangle {
                    color: "#1e1e2e"
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15
                    
                    Text {
                        text: "🔒"
                        font.pixelSize: 24
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: "Change Master Password"
                        font.pixelSize: 16
                        color: "white"
                    }
                    
                    Text {
                        text: "›"
                        font.pixelSize: 24
                        color: "#606060"
                    }
                }
                
                onClicked: changePasswordDialog.open()
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
            
            // Auto-lock Timeout
            ItemDelegate {
                Layout.fillWidth: true
                height: 80
                
                background: Rectangle {
                    color: "#1e1e2e"
                }
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Text {
                            text: "⏱"
                            font.pixelSize: 24
                        }
                        
                        Label {
                            Layout.fillWidth: true
                            text: "Auto-lock Timeout"
                            font.pixelSize: 16
                            color: "white"
                        }
                    }
                    
                    ComboBox {
                        id: autoLockCombo
                        Layout.fillWidth: true
                        model: ["Immediately", "1 minute", "5 minutes", "15 minutes", "30 minutes", "Never"]
                        currentIndex: 2
                        
                        Material.background: "#2a2a3e"
                        
                        onCurrentIndexChanged: {
                            var minutes = [0, 1, 5, 15, 30, -1][currentIndex]
                            vaultBackend.setAutoLockTimeout(minutes)
                        }
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
            
            // Theme Section
            Label {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.topMargin: 30
                text: "APPEARANCE"
                font.pixelSize: 12
                font.bold: true
                color: "#808080"
            }
            
            // Theme Selection
            ItemDelegate {
                Layout.fillWidth: true
                height: 80
                
                background: Rectangle {
                    color: "#1e1e2e"
                }
                
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 10
                    
                    RowLayout {
                        Layout.fillWidth: true
                        
                        Text {
                            text: "🎨"
                            font.pixelSize: 24
                        }
                        
                        Label {
                            Layout.fillWidth: true
                            text: "Theme"
                            font.pixelSize: 16
                            color: "white"
                        }
                    }
                    
                    ComboBox {
                        id: themeCombo
                        Layout.fillWidth: true
                        model: ["Dark", "Light", "System"]
                        currentIndex: 0
                        
                        Material.background: "#2a2a3e"
                        
                        onCurrentIndexChanged: {
                            // Apply theme (currently only Dark is implemented)
                            if (currentIndex === 1) {
                                Material.theme = Material.Light
                            } else {
                                Material.theme = Material.Dark
                            }
                        }
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
            
            // About Section
            Label {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.topMargin: 30
                text: "ABOUT"
                font.pixelSize: 12
                font.bold: true
                color: "#808080"
            }
            
            // Version
            ItemDelegate {
                Layout.fillWidth: true
                height: 60
                
                background: Rectangle {
                    color: "#1e1e2e"
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15
                    
                    Text {
                        text: "ℹ️"
                        font.pixelSize: 24
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: "Version"
                        font.pixelSize: 16
                        color: "white"
                    }
                    
                    Label {
                        text: "1.0.0"
                        font.pixelSize: 14
                        color: "#808080"
                    }
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
            
            // Export Data
            ItemDelegate {
                Layout.fillWidth: true
                height: 60
                
                background: Rectangle {
                    color: "#1e1e2e"
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15
                    
                    Text {
                        text: "📤"
                        font.pixelSize: 24
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: "Export Vault Data"
                        font.pixelSize: 16
                        color: "white"
                    }
                    
                    Text {
                        text: "›"
                        font.pixelSize: 24
                        color: "#606060"
                    }
                }
                
                onClicked: {
                    // TODO: Implement export
                    showToast("Export functionality coming soon")
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
            
            // Dangerous Zone
            Label {
                Layout.fillWidth: true
                Layout.margins: 20
                Layout.topMargin: 30
                text: "DANGER ZONE"
                font.pixelSize: 12
                font.bold: true
                color: "#ff6b6b"
            }
            
            // Clear Clipboard
            ItemDelegate {
                Layout.fillWidth: true
                height: 60
                
                background: Rectangle {
                    color: "#1e1e2e"
                }
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 15
                    
                    Text {
                        text: "🗑️"
                        font.pixelSize: 24
                    }
                    
                    Label {
                        Layout.fillWidth: true
                        text: "Clear Clipboard"
                        font.pixelSize: 16
                        color: "white"
                    }
                }
                
                onClicked: {
                    vaultBackend.copyToClipboard("")
                    showToast("Clipboard cleared")
                }
            }
            
            Rectangle {
                Layout.fillWidth: true
                height: 1
                color: "#3a3a4e"
            }
        }
    }
    
    // Change Password Dialog
    Dialog {
        id: changePasswordDialog
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "Change Master Password"
        modal: true
        
        Material.background: "#2a2a3e"
        
        ColumnLayout {
            width: parent.width
            spacing: 15
            
            TextField {
                id: oldPasswordField
                Layout.fillWidth: true
                placeholderText: "Current Password"
                echoMode: TextInput.Password
                font.pixelSize: 16
            }
            
            TextField {
                id: newPasswordField
                Layout.fillWidth: true
                placeholderText: "New Password"
                echoMode: TextInput.Password
                font.pixelSize: 16
            }
            
            TextField {
                id: confirmPasswordField
                Layout.fillWidth: true
                placeholderText: "Confirm New Password"
                echoMode: TextInput.Password
                font.pixelSize: 16
            }
            
            Label {
                id: passwordErrorLabel
                Layout.fillWidth: true
                color: "#ff6b6b"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }
        }
        
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        onAccepted: {
            passwordErrorLabel.text = ""
            
            if (newPasswordField.text.length === 0) {
                passwordErrorLabel.text = "New password cannot be empty"
                changePasswordDialog.open()
                return
            }
            
            if (newPasswordField.text !== confirmPasswordField.text) {
                passwordErrorLabel.text = "New passwords do not match"
                changePasswordDialog.open()
                return
            }
            
            if (vaultBackend.changeMasterPassword(oldPasswordField.text, newPasswordField.text)) {
                showToast("Master password changed successfully")
                oldPasswordField.text = ""
                newPasswordField.text = ""
                confirmPasswordField.text = ""
            } else {
                passwordErrorLabel.text = "Incorrect current password"
                changePasswordDialog.open()
            }
        }
        
        onRejected: {
            oldPasswordField.text = ""
            newPasswordField.text = ""
            confirmPasswordField.text = ""
            passwordErrorLabel.text = ""
        }
    }
    
    // Toast Notification
    Popup {
        id: toast
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.8, 300)
        height: 60
        modal: false
        closePolicy: Popup.NoAutoClose
        
        Material.background: "#4CAF50"
        
        Label {
            id: toastLabel
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 14
        }
        
        Timer {
            id: toastTimer
            interval: 2000
            onTriggered: toast.close()
        }
    }
    
    function showToast(message) {
        toastLabel.text = message
        toast.open()
        toastTimer.start()
    }
    
    Component.onCompleted: {
        var timeout = vaultBackend.getAutoLockTimeout()
        var timeouts = [0, 1, 5, 15, 30, -1]
        var index = timeouts.indexOf(timeout)
        if (index !== -1) {
            autoLockCombo.currentIndex = index
        }
    }
}
