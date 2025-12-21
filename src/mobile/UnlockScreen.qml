import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: unlockPage
    
    signal unlocked()
    
    Material.theme: Material.Dark
    Material.primary: Material.Teal
    Material.accent: Material.Cyan
    
    background: Rectangle {
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1a1a2e" }
            GradientStop { position: 1.0; color: "#16213e" }
        }
    }
    
    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.85, 400)
        spacing: 30
        
        // Logo and Title
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 120
            
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 15
                
                Rectangle {
                    Layout.alignment: Qt.AlignHCenter
                    width: 80
                    height: 80
                    radius: 40
                    color: Material.primary
                    
                    Text {
                        anchors.centerIn: parent
                        text: "🔐"
                        font.pixelSize: 48
                    }
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "CipherMesh"
                    font.pixelSize: 32
                    font.bold: true
                    color: "white"
                }
                
                Text {
                    Layout.alignment: Qt.AlignHCenter
                    text: "Secure Password Manager"
                    font.pixelSize: 14
                    color: "#b0b0b0"
                }
            }
        }
        
        // Password Field
        TextField {
            id: passwordField
            Layout.fillWidth: true
            placeholderText: "Master Password"
            echoMode: TextInput.Password
            font.pixelSize: 16
            Material.containerStyle: Material.Filled
            
            Keys.onReturnPressed: unlockButton.clicked()
            
            background: Rectangle {
                color: "#2a2a3e"
                radius: 8
                border.color: passwordField.activeFocus ? Material.accent : "#3a3a4e"
                border.width: 2
            }
        }
        
        // Show/Hide Password Button
        CheckBox {
            id: showPasswordCheck
            Layout.alignment: Qt.AlignRight
            text: "Show password"
            font.pixelSize: 12
            Material.accent: Material.Cyan
            
            onCheckedChanged: {
                passwordField.echoMode = checked ? TextInput.Normal : TextInput.Password
            }
        }
        
        // Error Message
        Label {
            id: errorLabel
            Layout.fillWidth: true
            Layout.topMargin: -15
            horizontalAlignment: Text.AlignHCenter
            color: "#ff6b6b"
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }
        
        // Unlock Button
        Button {
            id: unlockButton
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            text: "Unlock Vault"
            font.pixelSize: 16
            font.bold: true
            Material.background: Material.Teal
            Material.foreground: "white"
            
            background: Rectangle {
                radius: 25
                color: unlockButton.pressed ? Qt.darker(Material.Teal, 1.2) : Material.Teal
                
                Behavior on color {
                    ColorAnimation { duration: 100 }
                }
            }
            
            onClicked: {
                errorLabel.text = ""
                
                if (passwordField.text.length === 0) {
                    errorLabel.text = "Please enter your master password"
                    return
                }
                
                if (vaultBackend.unlockVault(passwordField.text)) {
                    unlocked()
                } else {
                    errorLabel.text = "Incorrect password. Please try again."
                    passwordField.text = ""
                    passwordField.forceActiveFocus()
                    
                    // Shake animation
                    shakeAnimation.start()
                }
            }
            
            SequentialAnimation {
                id: shakeAnimation
                loops: 3
                NumberAnimation {
                    target: passwordField
                    property: "anchors.horizontalCenterOffset"
                    to: -10
                    duration: 50
                }
                NumberAnimation {
                    target: passwordField
                    property: "anchors.horizontalCenterOffset"
                    to: 10
                    duration: 50
                }
                NumberAnimation {
                    target: passwordField
                    property: "anchors.horizontalCenterOffset"
                    to: 0
                    duration: 50
                }
            }
        }
        
        // Biometric Option (placeholder for future)
        Label {
            Layout.fillWidth: true
            Layout.topMargin: 10
            horizontalAlignment: Text.AlignHCenter
            text: "Tap to use biometric authentication"
            font.pixelSize: 12
            color: Material.accent
            visible: false // Enable when biometric is implemented
        }
    }
    
    // Focus password field on page load
    Component.onCompleted: {
        passwordField.forceActiveFocus()
    }
}
