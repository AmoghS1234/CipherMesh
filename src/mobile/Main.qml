import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 360
    height: 640
    title: "CipherMesh Mobile"
    
    // Dark Theme Background
    color: "#121212"

    StackView {
        id: stackView
        anchors.fill: parent
        initialItem: loginPage
    }

    Component {
        id: loginPage
        Page {
            background: Rectangle { color: "#121212" }
            
            ColumnLayout {
                anchors.centerIn: parent
                spacing: 20
                
                Label {
                    text: "CipherMesh"
                    font.pixelSize: 32
                    color: "white"
                    Layout.alignment: Qt.AlignHCenter
                }
                
                TextField {
                    id: masterPassField
                    placeholderText: "Master Password"
                    echoMode: TextInput.Password
                    Layout.preferredWidth: 300
                }
                
                Button {
                    text: "Unlock Vault"
                    Layout.fillWidth: true
                    onClicked: {
                        // Call C++ function here
                        // if (vaultBackend.verify(masterPassField.text)) ...
                        stackView.push(homePage)
                    }
                }
            }
        }
    }

    Component {
        id: homePage
        Page {
            header: ToolBar {
                Label { text: "My Passwords"; anchors.centerIn: parent }
            }
            
            ListView {
                anchors.fill: parent
                model: 5 // Placeholder: Replace with C++ model
                delegate: ItemDelegate {
                    text: "Entry " + index
                    width: parent.width
                    onClicked: console.log("Clicked entry")
                }
            }
            
            // Floating Action Button for Scan/Share
            RoundButton {
                text: "+"
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.margins: 20
                onClicked: console.log("Add new or Scan QR")
            }
        }
    }
}