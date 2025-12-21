import QtQuick
import QtQuick.Controls.Material
import QtQuick.Layouts

Page {
    id: entryDetailPage
    
    // Constants for consistent spacing
    readonly property int defaultMargin: 20
    readonly property int defaultSpacing: 10
    
    property int entryId: -1
    property var entryData: null
    property bool isEditMode: entryId !== -1
    
    signal back()
    signal saveRequested()
    
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
                icon.source: "qrc:/icons/back.svg"
                text: "‹"
                font.pixelSize: 32
                onClicked: back()
                
                Material.foreground: "white"
            }
            
            Label {
                Layout.fillWidth: true
                text: isEditMode ? "Edit Entry" : "New Entry"
                font.pixelSize: 20
                font.bold: true
                color: "white"
            }
            
            ToolButton {
                visible: isEditMode
                icon.source: "qrc:/icons/delete.svg"
                text: "🗑"
                font.pixelSize: 20
                onClicked: deleteDialog.open()
                
                Material.foreground: "#ff6b6b"
            }
            
            ToolButton {
                icon.source: "qrc:/icons/save.svg"
                text: "✓"
                font.pixelSize: 24
                onClicked: saveEntry()
                
                Material.foreground: Material.Teal
            }
        }
    }
    
    Flickable {
        anchors.fill: parent
        contentHeight: mainColumn.height + 40
        clip: true
        
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        
        ColumnLayout {
            id: mainColumn
            width: parent.width
            anchors.margins: 20
            spacing: 20
            
            // Entry Type
            GroupBox {
                Layout.fillWidth: true
                title: "Entry Type"
                
                Material.background: "#2a2a3e"
                Material.foreground: "white"
                
                RowLayout {
                    width: parent.width
                    
                    RadioButton {
                        id: passwordRadio
                        text: "Password"
                        checked: true
                        Material.accent: Material.Teal
                    }
                    
                    RadioButton {
                        id: noteRadio
                        text: "Secure Note"
                        Material.accent: Material.Teal
                    }
                }
            }
            
            // Title
            TextField {
                id: titleField
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                placeholderText: "Title *"
                font.pixelSize: 16
                Material.containerStyle: Material.Filled
                
                background: Rectangle {
                    color: "#2a2a3e"
                    radius: 8
                }
            }
            
            // Username (only for passwords)
            TextField {
                id: usernameField
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                visible: passwordRadio.checked
                placeholderText: "Username"
                font.pixelSize: 16
                Material.containerStyle: Material.Filled
                
                background: Rectangle {
                    color: "#2a2a3e"
                    radius: 8
                }
                
                RowLayout {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 10
                    spacing: 5
                    
                    ToolButton {
                        icon.source: "qrc:/icons/copy.svg"
                        text: "📋"
                        font.pixelSize: 16
                        visible: usernameField.text.length > 0
                        onClicked: {
                            vaultBackend.copyToClipboard(usernameField.text)
                            showToast("Username copied")
                        }
                    }
                }
            }
            
            // Password (only for passwords)
            ColumnLayout {
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                visible: passwordRadio.checked
                spacing: 10
                
                TextField {
                    id: passwordField
                    Layout.fillWidth: true
                    placeholderText: "Password *"
                    echoMode: showPasswordCheck.checked ? TextInput.Normal : TextInput.Password
                    font.pixelSize: 16
                    Material.containerStyle: Material.Filled
                    
                    background: Rectangle {
                        color: "#2a2a3e"
                        radius: 8
                    }
                    
                    onTextChanged: updatePasswordStrength()
                    
                    RowLayout {
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: 10
                        spacing: 5
                        
                        ToolButton {
                            icon.source: "qrc:/icons/refresh.svg"
                            text: "⚡"
                            font.pixelSize: 16
                            onClicked: passwordGeneratorSheet.open()
                        }
                        
                        ToolButton {
                            icon.source: showPasswordCheck.checked ? "qrc:/icons/eye-off.svg" : "qrc:/icons/eye.svg"
                            text: showPasswordCheck.checked ? "🙈" : "👁"
                            font.pixelSize: 16
                            onClicked: showPasswordCheck.checked = !showPasswordCheck.checked
                        }
                        
                        ToolButton {
                            icon.source: "qrc:/icons/copy.svg"
                            text: "📋"
                            font.pixelSize: 16
                            visible: passwordField.text.length > 0
                            onClicked: {
                                vaultBackend.copyToClipboard(passwordField.text)
                                showToast("Password copied")
                            }
                        }
                    }
                }
                
                CheckBox {
                    id: showPasswordCheck
                    text: "Show password"
                    font.pixelSize: 12
                }
                
                // Password Strength Indicator
                RowLayout {
                    Layout.fillWidth: true
                    visible: passwordField.text.length > 0
                    spacing: 10
                    
                    ProgressBar {
                        id: strengthBar
                        Layout.fillWidth: true
                        from: 0
                        to: 100
                        value: 0
                        
                        Material.accent: {
                            if (value < 20) return "#ff6b6b"
                            if (value < 40) return "#ffa500"
                            if (value < 60) return "#ffd700"
                            if (value < 80) return "#90ee90"
                            return "#4CAF50"
                        }
                    }
                    
                    Label {
                        id: strengthLabel
                        text: "Weak"
                        font.pixelSize: 12
                        color: strengthBar.Material.accent
                    }
                }
                
                // Breach Check Button
                Button {
                    Layout.fillWidth: true
                    text: "Check if Password is Breached"
                    flat: true
                    Material.foreground: Material.accent
                    visible: passwordField.text.length > 0
                    
                    onClicked: {
                        vaultBackend.checkPasswordBreach(passwordField.text)
                        breachCheckDialog.open()
                    }
                }
            }
            
            // TOTP Secret (only for passwords)
            TextField {
                id: totpField
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                visible: passwordRadio.checked
                placeholderText: "TOTP Secret (Optional)"
                font.pixelSize: 16
                Material.containerStyle: Material.Filled
                
                background: Rectangle {
                    color: "#2a2a3e"
                    radius: 8
                }
            }
            
            // TOTP Code Display
            Rectangle {
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                Layout.preferredHeight: 80
                visible: totpField.text.length > 0 && passwordRadio.checked
                color: "#2a2a3e"
                radius: 8
                
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 15
                    spacing: 15
                    
                    ColumnLayout {
                        Layout.fillWidth: true
                        
                        Label {
                            text: "TOTP Code"
                            font.pixelSize: 12
                            color: "#808080"
                        }
                        
                        Label {
                            id: totpCodeLabel
                            text: vaultBackend.generateTOTPCode(totpField.text)
                            font.pixelSize: 24
                            font.bold: true
                            color: Material.accent
                            font.family: "monospace"
                        }
                    }
                    
                    ColumnLayout {
                        spacing: 5
                        
                        ProgressBar {
                            id: totpTimer
                            from: 0
                            to: 30
                            value: 30 - (vaultBackend.getTOTPTimeRemaining() % 30)
                            Material.accent: Material.Teal
                        }
                        
                        ToolButton {
                            text: "📋"
                            font.pixelSize: 16
                            onClicked: {
                                vaultBackend.copyToClipboard(totpCodeLabel.text)
                                showToast("TOTP code copied")
                            }
                        }
                    }
                }
            }
            
            // Locations
            GroupBox {
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                title: "Locations (URLs, Apps)"
                visible: passwordRadio.checked
                
                Material.background: "#2a2a3e"
                Material.foreground: "white"
                
                ColumnLayout {
                    width: parent.width
                    spacing: 10
                    
                    ListView {
                        id: locationsList
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(count * 60, 200)
                        clip: true
                        
                        model: ListModel {
                            id: locationsModel
                        }
                        
                        delegate: ItemDelegate {
                            width: ListView.view.width
                            height: 50
                            
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 5
                                spacing: 10
                                
                                Label {
                                    Layout.fillWidth: true
                                    text: model.type + ": " + model.value
                                    font.pixelSize: 14
                                    elide: Text.ElideRight
                                }
                                
                                ToolButton {
                                    text: "✕"
                                    font.pixelSize: 16
                                    onClicked: locationsModel.remove(index)
                                }
                            }
                        }
                    }
                    
                    Button {
                        Layout.fillWidth: true
                        text: "+ Add Location"
                        flat: true
                        Material.foreground: Material.accent
                        onClicked: locationDialog.open()
                    }
                }
            }
            
            // Notes
            TextArea {
                id: notesField
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                Layout.preferredHeight: 150
                placeholderText: "Notes (Optional)"
                font.pixelSize: 16
                wrapMode: TextArea.Wrap
                
                background: Rectangle {
                    color: "#2a2a3e"
                    radius: 8
                }
            }
            
            // Metadata (for existing entries)
            Label {
                Layout.fillWidth: true
                Layout.margins: defaultMargin
                visible: isEditMode && entryData
                text: {
                    if (!entryData) return ""
                    var created = new Date(entryData.createdAt * 1000).toLocaleString()
                    var modified = new Date(entryData.lastModified * 1000).toLocaleString()
                    return "Created: " + created + "\nLast modified: " + modified
                }
                font.pixelSize: 12
                color: "#808080"
            }
        }
    }
    
    // Password Generator Sheet
    Dialog {
        id: passwordGeneratorSheet
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "Password Generator"
        modal: true
        
        Material.background: "#2a2a3e"
        
        ColumnLayout {
            width: parent.width
            spacing: 15
            
            TextField {
                id: generatedPasswordField
                Layout.fillWidth: true
                readOnly: true
                font.family: "monospace"
                font.pixelSize: 14
                
                RowLayout {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.rightMargin: 10
                    
                    ToolButton {
                        text: "📋"
                        onClicked: {
                            vaultBackend.copyToClipboard(generatedPasswordField.text)
                            showToast("Generated password copied")
                        }
                    }
                }
            }
            
            RowLayout {
                Layout.fillWidth: true
                
                Label {
                    text: "Length:"
                    font.pixelSize: 14
                }
                
                Slider {
                    id: lengthSlider
                    Layout.fillWidth: true
                    from: 8
                    to: 64
                    value: 16
                    stepSize: 1
                }
                
                Label {
                    text: lengthSlider.value
                    font.pixelSize: 14
                    Layout.preferredWidth: 30
                }
            }
            
            CheckBox {
                id: upperCheck
                text: "Uppercase (A-Z)"
                checked: true
            }
            
            CheckBox {
                id: lowerCheck
                text: "Lowercase (a-z)"
                checked: true
            }
            
            CheckBox {
                id: numbersCheck
                text: "Numbers (0-9)"
                checked: true
            }
            
            CheckBox {
                id: symbolsCheck
                text: "Symbols (!@#$...)"
                checked: true
            }
            
            Button {
                Layout.fillWidth: true
                text: "Generate"
                Material.background: Material.Teal
                onClicked: {
                    generatedPasswordField.text = vaultBackend.generatePassword(
                        lengthSlider.value,
                        upperCheck.checked,
                        lowerCheck.checked,
                        numbersCheck.checked,
                        symbolsCheck.checked,
                        ""
                    )
                }
            }
        }
        
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        onAccepted: {
            if (generatedPasswordField.text.length > 0) {
                passwordField.text = generatedPasswordField.text
                updatePasswordStrength()
            }
        }
        
        onOpened: {
            generatedPasswordField.text = vaultBackend.generatePassword(
                lengthSlider.value,
                upperCheck.checked,
                lowerCheck.checked,
                numbersCheck.checked,
                symbolsCheck.checked,
                ""
            )
        }
    }
    
    // Location Add Dialog
    Dialog {
        id: locationDialog
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "Add Location"
        modal: true
        
        Material.background: "#2a2a3e"
        
        ColumnLayout {
            width: parent.width
            spacing: 15
            
            ComboBox {
                id: locationTypeCombo
                Layout.fillWidth: true
                model: ["URL", "App", "Email", "Other"]
            }
            
            TextField {
                id: locationValueField
                Layout.fillWidth: true
                placeholderText: "Value"
            }
        }
        
        standardButtons: Dialog.Ok | Dialog.Cancel
        
        onAccepted: {
            if (locationValueField.text.length > 0) {
                locationsModel.append({
                    "id": -1,
                    "type": locationTypeCombo.currentText,
                    "value": locationValueField.text
                })
                locationValueField.text = ""
            }
        }
        
        onRejected: {
            locationValueField.text = ""
        }
    }
    
    // Delete Confirmation Dialog
    Dialog {
        id: deleteDialog
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "Delete Entry"
        modal: true
        
        Material.background: "#2a2a3e"
        
        Label {
            width: parent.width
            text: "Are you sure you want to delete this entry? This action cannot be undone."
            wrapMode: Text.WordWrap
        }
        
        standardButtons: Dialog.Yes | Dialog.No
        
        onAccepted: {
            if (vaultBackend.deleteEntry(entryId)) {
                showToast("Entry deleted")
                back()
            }
        }
    }
    
    // Breach Check Dialog
    Dialog {
        id: breachCheckDialog
        anchors.centerIn: parent
        width: Math.min(parent.width * 0.9, 400)
        title: "Password Breach Check"
        modal: true
        
        Material.background: "#2a2a3e"
        
        ColumnLayout {
            width: parent.width
            spacing: 15
            
            Label {
                id: breachResultLabel
                Layout.fillWidth: true
                text: "Checking..."
                wrapMode: Text.WordWrap
            }
            
            BusyIndicator {
                id: breachSpinner
                Layout.alignment: Qt.AlignHCenter
                running: true
            }
        }
        
        standardButtons: Dialog.Close
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
    
    // Connections
    Connections {
        target: vaultBackend
        
        function onBreachCheckResult(isBreached, count) {
            breachSpinner.running = false
            if (isBreached) {
                breachResultLabel.text = "⚠️ Warning: This password has been found in " + count + " data breaches. It is recommended to use a different password."
                breachResultLabel.color = "#ff6b6b"
            } else {
                breachResultLabel.text = "✓ Good news! This password has not been found in any known data breaches."
                breachResultLabel.color = "#4CAF50"
            }
        }
        
        function onTotpTimerTick() {
            if (totpField.text.length > 0) {
                totpCodeLabel.text = vaultBackend.generateTOTPCode(totpField.text)
                totpTimer.value = 30 - (vaultBackend.getTOTPTimeRemaining() % 30)
            }
        }
    }
    
    // Functions
    function showToast(message) {
        toastLabel.text = message
        toast.open()
        toastTimer.start()
    }
    
    function updatePasswordStrength() {
        var strength = vaultBackend.calculatePasswordStrength(passwordField.text)
        strengthBar.value = strength
        strengthLabel.text = vaultBackend.getPasswordStrengthText(strength)
    }
    
    function loadEntry() {
        if (entryId === -1) return
        
        entryData = vaultBackend.getEntry(entryId)
        if (entryData) {
            titleField.text = entryData.title
            usernameField.text = entryData.username
            passwordField.text = entryData.password
            notesField.text = entryData.notes
            totpField.text = entryData.totpSecret
            
            if (entryData.entryType === "secure_note") {
                noteRadio.checked = true
            } else {
                passwordRadio.checked = true
            }
            
            // Load locations
            locationsModel.clear()
            if (entryData.locations) {
                for (var i = 0; i < entryData.locations.length; i++) {
                    locationsModel.append(entryData.locations[i])
                }
            }
            
            updatePasswordStrength()
        }
    }
    
    function saveEntry() {
        if (titleField.text.length === 0) {
            showToast("Title is required")
            return
        }
        
        if (passwordRadio.checked && passwordField.text.length === 0) {
            showToast("Password is required")
            return
        }
        
        var entryType = passwordRadio.checked ? "password" : "secure_note"
        var success = false
        
        if (isEditMode) {
            success = vaultBackend.updateEntry(
                entryId,
                titleField.text,
                usernameField.text,
                passwordField.text,
                notesField.text,
                totpField.text,
                entryType
            )
        } else {
            success = vaultBackend.addEntry(
                titleField.text,
                usernameField.text,
                passwordField.text,
                notesField.text,
                totpField.text,
                entryType
            )
        }
        
        if (success) {
            showToast(isEditMode ? "Entry updated" : "Entry created")
            back()
        } else {
            showToast("Failed to save entry")
        }
    }
    
    Component.onCompleted: {
        if (isEditMode) {
            loadEntry()
        }
    }
}
