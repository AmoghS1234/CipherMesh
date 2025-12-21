import QtQuick
import QtQuick.Controls.Material

ApplicationWindow {
    visible: true
    width: 360
    height: 640
    title: "CipherMesh Mobile"
    
    Material.theme: Material.Dark
    Material.primary: Material.Teal
    Material.accent: Material.Cyan
    
    // Main Stack View for Navigation
    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: unlockScreen
        
        // Unlock Screen
        Component {
            id: unlockScreen
            
            UnlockScreen {
                onUnlocked: {
                    stackView.replace(vaultListScreen)
                }
            }
        }
        
        // Vault List Screen
        Component {
            id: vaultListScreen
            
            VaultListScreen {
                onEntrySelected: function(entryId) {
                    stackView.push(entryDetailScreen, {
                        "entryId": entryId
                    })
                }
                
                onSettingsRequested: {
                    stackView.push(settingsScreen)
                }
                
                onLockRequested: {
                    vaultBackend.lockVault()
                    stackView.replace(unlockScreen)
                }
            }
        }
        
        // Entry Detail Screen
        Component {
            id: entryDetailScreen
            
            EntryDetailScreen {
                onBack: {
                    stackView.pop()
                }
            }
        }
        
        // Settings Screen
        Component {
            id: settingsScreen
            
            SettingsScreen {
                onBack: {
                    stackView.pop()
                }
            }
        }
    }
    
    // Handle back button on Android
    onClosing: function(close) {
        if (stackView.depth > 1) {
            close.accepted = false
            stackView.pop()
        }
    }
    
    // Global error handler
    Connections {
        target: vaultBackend
        
        function onErrorOccurred(message) {
            errorDialog.text = message
            errorDialog.open()
        }
    }
    
    // Error Dialog
    Dialog {
        id: errorDialog
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "Error"
        modal: true
        
        property alias text: errorLabel.text
        
        Material.background: "#2a2a3e"
        
        Label {
            id: errorLabel
            width: parent.width
            wrapMode: Text.WordWrap
            color: "#ff6b6b"
        }
        
        standardButtons: Dialog.Ok
    }
}
