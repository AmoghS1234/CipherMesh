#include "newentrydialog.hpp"
#include "passwordgeneratordialog.hpp"
#include "locationeditdialog.hpp"
#include "passwordstrength.hpp"
#include "breach_checker.hpp"
#include "crypto.hpp" // <-- FIXED PATH
#include "vault.hpp"  // <-- FIXED PATH
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTextEdit> 
#include <QListWidget> 
#include <QMessageBox> 
#include <QProgressBar> 

NewEntryDialog::NewEntryDialog(CipherMesh::Core::Vault* vault, QWidget *parent)
    : QDialog(parent), m_vault(vault), m_isEditMode(false), m_editingEntryId(-1) {
    m_breachChecker = new BreachChecker(this);
    setupUi();
    setWindowTitle("Create New Entry");
}

NewEntryDialog::NewEntryDialog(CipherMesh::Core::Vault* vault, const CipherMesh::Core::VaultEntry& entry, QWidget *parent)
    : QDialog(parent), m_vault(vault), m_isEditMode(true), m_editingEntryId(entry.id) {
    m_breachChecker = new BreachChecker(this);
    setupUi();
    setWindowTitle("Edit Entry");
    m_titleLabel->setText("Edit Entry");
    m_titleEdit->setText(QString::fromStdString(entry.title));
    m_usernameEdit->setText(QString::fromStdString(entry.username));
    m_notesEdit->setText(QString::fromStdString(entry.notes));
    m_totpSecretEdit->setText(QString::fromStdString(entry.totp_secret));
    
    m_locations = entry.locations; // Copy existing locations
    loadLocations(m_locations);
    
    m_passwordLabel->setText("New Password (leave blank to keep old):");
    m_confirmEdit->setPlaceholderText("Confirm new password");
    m_createButton->setText("Save");
}

NewEntryDialog::~NewEntryDialog() {}

void NewEntryDialog::setupUi() {
    setModal(true);
    setMinimumWidth(550); // Increased from 500 to accommodate all buttons
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    m_titleLabel = new QLabel("Create New Entry", this);
    m_titleLabel->setObjectName("DialogTitle");
    
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText("e.g., Google");
    
    m_usernameEdit = new QLineEdit(this);
    m_usernameEdit->setPlaceholderText("e.g., user@gmail.com");
    
    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setPlaceholderText("Add notes here (e.g., security questions)...");
    m_notesEdit->setAcceptRichText(false);
    m_notesEdit->setMinimumHeight(80);
    m_notesEdit->setMaximumHeight(120);

    // --- Locations UI ---
    m_locationsList = new QListWidget(this);
    m_locationsList->setMinimumHeight(80);
    m_locationsList->setMaximumHeight(120);
    
    QHBoxLayout* locationButtonLayout = new QHBoxLayout();
    locationButtonLayout->setSpacing(8);
    QPushButton* addLocationButton = new QPushButton("Add...");
    m_editLocationButton = new QPushButton("Edit...");
    m_removeLocationButton = new QPushButton("Remove");
    m_editLocationButton->setEnabled(false);
    m_removeLocationButton->setEnabled(false);
    locationButtonLayout->addWidget(addLocationButton);
    locationButtonLayout->addWidget(m_editLocationButton);
    locationButtonLayout->addWidget(m_removeLocationButton);
    locationButtonLayout->addStretch(1);
    
    m_passwordLabel = new QLabel("Password:", this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    
    QHBoxLayout* passLayout = new QHBoxLayout();
    passLayout->setSpacing(8);
    passLayout->setContentsMargins(0, 0, 0, 0);
    passLayout->addWidget(m_passwordEdit, 1);
    QPushButton* generateButton = new QPushButton("Generate");
    generateButton->setFixedWidth(100);
    passLayout->addWidget(generateButton);
    m_checkBreachButton = new QPushButton("Breach?");
    m_checkBreachButton->setFixedWidth(80);
    m_checkBreachButton->setToolTip("Check if password has been compromised in data breaches");
    passLayout->addWidget(m_checkBreachButton);
    
    m_confirmEdit = new QLineEdit(this);
    m_confirmEdit->setEchoMode(QLineEdit::Password);
    m_confirmEdit->setPlaceholderText("Confirm password");
    
    // --- Breach Status Label ---
    m_breachStatusLabel = new QLabel("", this);
    m_breachStatusLabel->setWordWrap(true);
    m_breachStatusLabel->setTextFormat(Qt::RichText);
    m_breachStatusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_breachStatusLabel->setMaximumWidth(500); // Ensure it doesn't exceed reasonable width
    m_breachStatusLabel->hide();
    
    // --- Strength Meter ---
    m_strengthBar = new QProgressBar(this);
    m_strengthBar->setMaximum(100);
    m_strengthBar->setTextVisible(false);
    m_strengthBar->setMaximumHeight(12);
    m_strengthLabel = new QLabel("", this);
    m_strengthLabel->setMinimumWidth(120);
    QHBoxLayout* strengthLayout = new QHBoxLayout();
    strengthLayout->setSpacing(8);
    strengthLayout->addWidget(m_strengthBar, 1);
    strengthLayout->addWidget(m_strengthLabel);
    
    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet("color: #d32f2f;");
    m_messageLabel->hide();
    
    m_createButton = new QPushButton("Create", this);
    m_createButton->setObjectName("NewButton");
    m_createButton->setDefault(true);
    m_createButton->setEnabled(false);
    m_createButton->setMinimumWidth(100);
    
    m_cancelButton = new QPushButton("Cancel", this);
    m_cancelButton->setMinimumWidth(100);

    mainLayout->addWidget(m_titleLabel);
    
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setSpacing(12);
    formLayout->setContentsMargins(0, 8, 0, 8);
    formLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    formLayout->addRow("Title:", m_titleEdit);
    formLayout->addRow("Username:", m_usernameEdit);
    formLayout->addRow("Locations:", m_locationsList);
    formLayout->addRow("", locationButtonLayout);
    formLayout->addRow("Notes:", m_notesEdit);
    formLayout->addRow(m_passwordLabel, passLayout);
    formLayout->addRow("", m_breachStatusLabel);
    formLayout->addRow("Confirm:", m_confirmEdit);
    formLayout->addRow("Strength:", strengthLayout);
    
    // TOTP Secret field
    m_totpSecretEdit = new QLineEdit(this);
    m_totpSecretEdit->setPlaceholderText("e.g., JBSWY3DPEHPK3PXP (Base32 encoded)");
    QLabel* totpHelpLabel = new QLabel("<small>Optional: Add 2FA secret key for TOTP codes</small>", this);
    totpHelpLabel->setStyleSheet("color: #666;");
    formLayout->addRow("2FA Secret:", m_totpSecretEdit);
    formLayout->addRow("", totpHelpLabel);
    
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_messageLabel);
    
    QHBoxLayout *buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_createButton);
    mainLayout->addLayout(buttonLayout);
    setLayout(mainLayout);

    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_createButton, &QPushButton::clicked, this, &NewEntryDialog::onCreateOrSaveClicked);
    connect(m_passwordEdit, &QLineEdit::textChanged, this, &NewEntryDialog::onPasswordChanged);
    connect(m_confirmEdit, &QLineEdit::textChanged, this, &NewEntryDialog::onConfirmPasswordChanged);
    connect(generateButton, &QPushButton::clicked, this, &NewEntryDialog::onGeneratePassword);
    connect(m_checkBreachButton, &QPushButton::clicked, this, &NewEntryDialog::onCheckBreach);
    
    // --- NEW CONNECTIONS ---
    connect(addLocationButton, &QPushButton::clicked, this, &NewEntryDialog::onAddLocation);
    connect(m_editLocationButton, &QPushButton::clicked, this, &NewEntryDialog::onEditLocation);
    connect(m_removeLocationButton, &QPushButton::clicked, this, &NewEntryDialog::onRemoveLocation);
    connect(m_locationsList, &QListWidget::currentItemChanged, [this](QListWidgetItem* current) {
        m_editLocationButton->setEnabled(current != nullptr);
        m_removeLocationButton->setEnabled(current != nullptr);
    });
    
    onConfirmPasswordChanged("");
}

void NewEntryDialog::loadLocations(const std::vector<CipherMesh::Core::Location>& locations) {
    m_locationsList->clear();
    m_locationItemMap.clear();
    for(const auto& loc : locations) {
        QString text = QString("[%1] %2").arg(QString::fromStdString(loc.type), QString::fromStdString(loc.value));
        QListWidgetItem* item = new QListWidgetItem(text, m_locationsList);
        m_locationItemMap[item] = loc;
    }
}

void NewEntryDialog::onAddLocation() {
    LocationEditDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        m_locations.push_back(dialog.getLocationData());
        loadLocations(m_locations);
    }
}

void NewEntryDialog::onEditLocation() {
    QListWidgetItem* item = m_locationsList->currentItem();
    if (!item) return;

    CipherMesh::Core::Location& locToEdit = m_locationItemMap[item];
    LocationEditDialog dialog(locToEdit, this);
    
    if (dialog.exec() == QDialog::Accepted) {
        locToEdit = dialog.getLocationData(); // Update the location
        loadLocations(m_locations); // Reload the list
    }
}

void NewEntryDialog::onRemoveLocation() {
    QListWidgetItem* item = m_locationsList->currentItem();
    if (!item) return;
    
    int indexToRemove = -1;
    for (int i = 0; i < m_locations.size(); ++i) {
        if (m_locations[i].id == m_locationItemMap[item].id) {
            indexToRemove = i;
            break;
        }
    }
    if (indexToRemove != -1) {
        m_locations.erase(m_locations.begin() + indexToRemove);
    }
    
    loadLocations(m_locations);
}

void NewEntryDialog::onGeneratePassword() {
    PasswordGeneratorDialog genDialog(this);
    if (genDialog.exec() == QDialog::Accepted) {
        std::string password = genDialog.getPassword();
        QString qPassword = QString::fromStdString(password);
        m_passwordEdit->setText(qPassword);
        m_confirmEdit->setText(qPassword);
        CipherMesh::Core::Crypto::secureWipe(password);
    }
}

void NewEntryDialog::onConfirmPasswordChanged(const QString&) {
    std::string pass = m_passwordEdit->text().toStdString();
    std::string confirm = m_confirmEdit->text().toStdString();
    if (m_isEditMode && pass.empty() && confirm.empty()) {
        m_messageLabel->hide();
        m_createButton->setEnabled(true);
    } else if (pass.empty() && !m_isEditMode) {
        m_createButton->setEnabled(false);
    } else if (pass != confirm) {
        m_messageLabel->setText("Passwords do not match.");
        m_messageLabel->show();
        m_createButton->setEnabled(false);
    } else {
        m_messageLabel->hide();
        m_createButton->setEnabled(true);
    }
}

void NewEntryDialog::onPasswordChanged(const QString& text) {
    updateStrengthMeter();
    onConfirmPasswordChanged(text);
}

void NewEntryDialog::updateStrengthMeter() {
    using namespace CipherMesh::GUI;
    
    QString password = m_passwordEdit->text();
    auto info = PasswordStrengthCalculatorQt::calculate(password);
    
    m_strengthBar->setValue(info.score);
    m_strengthLabel->setText(info.text);
    m_strengthLabel->setStyleSheet(QString("color: %1; font-weight: bold;").arg(info.color.name()));
    
    // Dynamically color the progress bar chunk based on strength
    QString chunkColor = info.color.name();
    m_strengthBar->setStyleSheet(QString(
        "QProgressBar::chunk { background-color: %1; }"
    ).arg(chunkColor));
}

void NewEntryDialog::onCreateOrSaveClicked() {
    if (m_titleEdit->text().isEmpty()) {
        m_messageLabel->setText("Title cannot be empty.");
        m_messageLabel->show();
        return;
    }
    
    // --- NEW DUPLICATE CHECK ---
    if (!m_isEditMode && !m_locations.empty()) {
        std::string username = m_usernameEdit->text().toStdString();
        // Check first location value for a potential duplicate
        std::string firstLocationValue = m_locations[0].value;
        
        if (m_vault->entryExists(username, firstLocationValue)) {
            QMessageBox::StandardButton reply;
            reply = QMessageBox::question(this, "Duplicate Entry",
                                          "An entry with this username and location already exists. Are you sure you want to save a new one?",
                                          QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::No) {
                return; // Stop the save
            }
        }
    }
    
    accept();
}

CipherMesh::Core::VaultEntry NewEntryDialog::getEntryData() const {
    CipherMesh::Core::VaultEntry entry;
    if (m_isEditMode) entry.id = m_editingEntryId;
    entry.title = m_titleEdit->text().toStdString();
    entry.username = m_usernameEdit->text().toStdString();
    entry.notes = m_notesEdit->toPlainText().toStdString();
    entry.totp_secret = m_totpSecretEdit->text().toStdString();
    entry.locations = m_locations;
    return entry;
}

std::string NewEntryDialog::getPassword() const {
    return m_passwordEdit->text().toStdString();
}

void NewEntryDialog::onCheckBreach() {
    QString password = m_passwordEdit->text();
    if (password.isEmpty()) {
        m_breachStatusLabel->setText("⚠️ Please enter a password first");
        m_breachStatusLabel->setStyleSheet("color: #f57c00;");
        m_breachStatusLabel->show();
        return;
    }
    
    m_checkBreachButton->setEnabled(false);
    m_checkBreachButton->setText("...");
    m_breachStatusLabel->setText("🔍 Checking password against breach database...");
    m_breachStatusLabel->setStyleSheet("color: #666;");
    m_breachStatusLabel->show();
    
    m_breachChecker->checkPassword(password.toStdString(), [this](bool isCompromised, int count) {
        m_checkBreachButton->setEnabled(true);
        m_checkBreachButton->setText("Breach?");
        
        if (count < 0) {
            // API error occurred
            m_breachStatusLabel->setText("<b>⚠️ Unable to check.</b> Try again.");
            m_breachStatusLabel->setStyleSheet("color: #f57c00;");
        } else if (isCompromised) {
            m_breachStatusLabel->setText(QString("<b>⚠️ BREACHED:</b> Seen %1 times in data breaches!").arg(QString::number(count)));
            m_breachStatusLabel->setStyleSheet("color: #d32f2f; font-weight: bold;");
        } else {
            m_breachStatusLabel->setText("<b>✓ Safe:</b> Not found in known breaches.");
            m_breachStatusLabel->setStyleSheet("color: #388e3c;");
        }
        m_breachStatusLabel->show();
    });
}
