#include <sodium.h>
#include "unlockdialog.hpp"
#include "vault.hpp"
#include "crypto.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QStackedWidget>
#include <QFile>
#include <sqlite3.h>
#include <string>
#include <vector>
#include <cctype>
#include <cstdio>

UnlockDialog::UnlockDialog(CipherMesh::Core::Vault* vault, QWidget *parent)
    : QDialog(parent),
      m_vault(vault),
      m_vaultPath("ciphermesh.db") {
    setWindowTitle("CipherMesh - Unlock");
    setModal(true);
    setMinimumWidth(500);
    
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    buttonBox->button(QDialogButtonBox::Close)->setDefault(false);
    buttonBox->button(QDialogButtonBox::Close)->setAutoDefault(false);
    
    m_stack = new QStackedWidget(this);
    mainLayout->addWidget(m_stack, 1);
    mainLayout->addWidget(buttonBox);
    
    createUnlockView();
    createCreateView();
    
    if (isVaultInitialized()) {
        m_stack->setCurrentIndex(0);
        m_unlockPasswordEdit->setFocus();
    } else {
        m_stack->setCurrentIndex(1);
        m_createPasswordEdit->setFocus();
    }
}

UnlockDialog::~UnlockDialog() {}

bool UnlockDialog::isVaultInitialized() {
    if (!QFile::exists(QString::fromStdString(m_vaultPath))) return false;
    sqlite3* db = nullptr;
    if (sqlite3_open(m_vaultPath.c_str(), &db) != SQLITE_OK) return false;
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT value FROM vault_metadata WHERE key = 'key_canary';";
    bool success = false;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, 0) == SQLITE_OK)
        if (sqlite3_step(stmt) == SQLITE_ROW) success = true;
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return success;
}

void UnlockDialog::createUnlockView() {
    QWidget* view = new QWidget(this);
    QVBoxLayout* layout = new QVBoxLayout(view);
    layout->setContentsMargins(40, 40, 40, 40);
    layout->setSpacing(20);
    
    layout->addStretch();
    
    // Icon/Title section
    QLabel* iconLabel = new QLabel("🔒", this);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 48px;");
    layout->addWidget(iconLabel);
    
    m_unlockMessageLabel = new QLabel("Enter Master Password", this);
    m_unlockMessageLabel->setObjectName("DialogTitle");
    m_unlockMessageLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_unlockMessageLabel);
    
    QLabel* subtitleLabel = new QLabel("Unlock your vault to access your passwords", this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setObjectName("PlaceholderLabel");
    layout->addWidget(subtitleLabel);
    
    layout->addSpacing(20);
    
    m_unlockPasswordEdit = new QLineEdit(this);
    m_unlockPasswordEdit->setPlaceholderText("Enter your master password...");
    m_unlockPasswordEdit->setEchoMode(QLineEdit::Password);
    m_unlockPasswordEdit->setMinimumHeight(40);
    layout->addWidget(m_unlockPasswordEdit);
    
    QPushButton* unlockButton = new QPushButton("Unlock Vault", this);
    unlockButton->setObjectName("NewButton");
    unlockButton->setDefault(true);
    unlockButton->setMinimumHeight(40);
    layout->addWidget(unlockButton);
    
    layout->addStretch();
    m_stack->addWidget(view);
    
    connect(unlockButton, &QPushButton::clicked, this, &UnlockDialog::onUnlockClicked);
    connect(m_unlockPasswordEdit, &QLineEdit::returnPressed, this, &UnlockDialog::onUnlockClicked);
}

void UnlockDialog::createCreateView() {
    QWidget* view = new QWidget(this);
    QVBoxLayout* mainLayout = new QVBoxLayout(view);
    mainLayout->setContentsMargins(40, 40, 40, 40);
    mainLayout->setSpacing(20);
    
    mainLayout->addStretch();
    
    // Icon/Welcome section
    QLabel* iconLabel = new QLabel("🔐", this);
    iconLabel->setAlignment(Qt::AlignCenter);
    iconLabel->setStyleSheet("font-size: 48px;");
    mainLayout->addWidget(iconLabel);
    
    m_createMessageLabel = new QLabel("Welcome to CipherMesh!", this);
    m_createMessageLabel->setObjectName("DialogTitle");
    m_createMessageLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_createMessageLabel);
    
    QLabel* subtitleLabel = new QLabel("Choose a username and create a strong master password to secure your vault", this);
    subtitleLabel->setAlignment(Qt::AlignCenter);
    subtitleLabel->setObjectName("PlaceholderLabel");
    subtitleLabel->setWordWrap(true);
    mainLayout->addWidget(subtitleLabel);
    
    mainLayout->addSpacing(20);
    
    QFormLayout* layout = new QFormLayout();
    layout->setSpacing(16);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    m_createUsernameEdit = new QLineEdit(this);
    m_createUsernameEdit->setPlaceholderText("Your username (e.g., Alice)");
    m_createUsernameEdit->setMinimumHeight(40);
    
    m_createPasswordEdit = new QLineEdit(this);
    m_createPasswordEdit->setPlaceholderText("New master password...");
    m_createPasswordEdit->setEchoMode(QLineEdit::Password);
    m_createPasswordEdit->setMinimumHeight(40);
    
    m_confirmPasswordEdit = new QLineEdit(this);
    m_confirmPasswordEdit->setPlaceholderText("Confirm password...");
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setMinimumHeight(40);
    
    layout->addRow("Username:", m_createUsernameEdit);
    layout->addRow("Password:", m_createPasswordEdit);
    layout->addRow("Confirm:", m_confirmPasswordEdit);
    
    mainLayout->addLayout(layout);
    mainLayout->addSpacing(10);
    
    QPushButton* createButton = new QPushButton("Create Vault", this);
    createButton->setObjectName("NewButton");
    createButton->setDefault(true);
    createButton->setMinimumHeight(40);
    mainLayout->addWidget(createButton);
    
    mainLayout->addStretch();
    
    m_stack->addWidget(view);
    
    connect(createButton, &QPushButton::clicked, this, &UnlockDialog::onCreateClicked);
    connect(m_createUsernameEdit, &QLineEdit::returnPressed, this, &UnlockDialog::onCreateClicked);
    connect(m_createPasswordEdit, &QLineEdit::returnPressed, this, &UnlockDialog::onCreateClicked);
    connect(m_confirmPasswordEdit, &QLineEdit::returnPressed, this, &UnlockDialog::onCreateClicked);
}

void UnlockDialog::onUnlockClicked() {
    std::string password = m_unlockPasswordEdit->text().toStdString();
    if (m_vault->loadVault(m_vaultPath, password)) {
        CipherMesh::Core::Crypto::secureWipe(password);
        m_unlockPasswordEdit->clear();
        accept();
    } else {
        m_unlockMessageLabel->setText("Invalid password. Try again.");
        m_unlockMessageLabel->setStyleSheet("color: #ff5555;");
        m_unlockPasswordEdit->clear();
        m_unlockPasswordEdit->setFocus();
    }
    CipherMesh::Core::Crypto::secureWipe(password);
}

void UnlockDialog::onCreateClicked() {
    std::string username = m_createUsernameEdit->text().trimmed().toStdString();
    std::string p1 = m_createPasswordEdit->text().toStdString();
    std::string p2 = m_confirmPasswordEdit->text().toStdString();
    
    if (username.empty()) {
        m_createMessageLabel->setText("Username cannot be empty.");
        m_createMessageLabel->setStyleSheet("color: #ff5555;");
        return;
    }
    if (p1.empty()) {
        m_createMessageLabel->setText("Password cannot be empty.");
        m_createMessageLabel->setStyleSheet("color: #ff5555;");
        return;
    }
    if (p1 != p2) {
        m_createMessageLabel->setText("Passwords do not match. Try again.");
        m_createMessageLabel->setStyleSheet("color: #ff5555;");
        m_confirmPasswordEdit->clear();
        return;
    }
    
    // Sanitize username: remove spaces, convert to lowercase, keep only alphanumeric
    std::string sanitized;
    for (char c : username) {
        if (std::isalnum(c)) {
            sanitized += std::tolower(c);
        }
    }
    if (sanitized.empty()) {
        sanitized = "user";
    }
    // Limit to 12 characters
    if (sanitized.length() > 12) {
        sanitized = sanitized.substr(0, 12);
    }
    
    try {
        if (m_vault->createNewVault(m_vaultPath, p1)) {
            // Generate user ID: username_<16 hex chars>
            std::vector<unsigned char> randomBytes = CipherMesh::Core::Crypto::randomBytes(8);
            std::string hexSuffix;
            for (unsigned char byte : randomBytes) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02x", byte);
                hexSuffix += hex;
            }
            std::string userId = sanitized + "_" + hexSuffix;
            
            // Store username and user ID in vault
            m_vault->setUserId(userId);
            
            CipherMesh::Core::Crypto::secureWipe(p1);
            CipherMesh::Core::Crypto::secureWipe(p2);
            m_createUsernameEdit->clear();
            m_createPasswordEdit->clear();
            m_confirmPasswordEdit->clear();
            accept();
        } else {
            m_createMessageLabel->setText("An error occurred. Could not create vault.");
            m_createMessageLabel->setStyleSheet("color: #ff5555;");
        }
    } catch (const std::exception& e) {
        m_createMessageLabel->setText(QString("Error: %1").arg(e.what()));
        m_createMessageLabel->setStyleSheet("color: #ff5555;");
    }
    CipherMesh::Core::Crypto::secureWipe(p1);
    CipherMesh::Core::Crypto::secureWipe(p2);
}