#include "changepassworddialog.hpp"
#include "vault.hpp"         // <-- FIXED PATH
#include "crypto.hpp"        // <-- FIXED PATH
#include <QVBoxLayout>
#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QLabel>
#include <QMessageBox>

ChangePasswordDialog::ChangePasswordDialog(CipherMesh::Core::Vault* vault, QWidget *parent)
    : QDialog(parent), m_vault(vault)
{
    setWindowTitle("Change Master Password");
    setModal(true);
    setMinimumWidth(500);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    QLabel* titleLabel = new QLabel("Change Master Password", this);
    titleLabel->setObjectName("DialogTitle");
    mainLayout->addWidget(titleLabel);
    
    QFormLayout *formLayout = new QFormLayout();
    formLayout->setSpacing(12);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    
    m_currentPasswordEdit = new QLineEdit(this);
    m_currentPasswordEdit->setEchoMode(QLineEdit::Password);
    m_currentPasswordEdit->setMinimumHeight(40);
    
    m_newPasswordEdit = new QLineEdit(this);
    m_newPasswordEdit->setEchoMode(QLineEdit::Password);
    m_newPasswordEdit->setMinimumHeight(40);
    
    m_confirmPasswordEdit = new QLineEdit(this);
    m_confirmPasswordEdit->setEchoMode(QLineEdit::Password);
    m_confirmPasswordEdit->setMinimumHeight(40);
    
    m_messageLabel = new QLabel(this);
    m_messageLabel->setStyleSheet("color: #d32f2f;");
    
    formLayout->addRow("Current Password:", m_currentPasswordEdit);
    formLayout->addRow("New Password:", m_newPasswordEdit);
    formLayout->addRow("Confirm New Password:", m_confirmPasswordEdit);

    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_messageLabel);

    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setMinimumWidth(100);
    buttonBox->button(QDialogButtonBox::Ok)->setMinimumHeight(40);
    buttonBox->button(QDialogButtonBox::Cancel)->setMinimumWidth(100);
    buttonBox->button(QDialogButtonBox::Cancel)->setMinimumHeight(40);
    mainLayout->addWidget(buttonBox);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ChangePasswordDialog::onOkClicked);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    
    // We connect to the OK button's click, not the dialog's "accepted" signal
    // This allows us to stop the dialog from closing if validation fails.
    buttonBox->button(QDialogButtonBox::Ok)->disconnect();
    connect(buttonBox->button(QDialogButtonBox::Ok), &QPushButton::clicked, this, &ChangePasswordDialog::onOkClicked);
}

void ChangePasswordDialog::onOkClicked() {
    std::string currentPass = m_currentPasswordEdit->text().toStdString();
    std::string newPass = m_newPasswordEdit->text().toStdString();
    std::string confirmPass = m_confirmPasswordEdit->text().toStdString();
    
    // 1. Client-side validation
    if (newPass.empty()) {
        m_messageLabel->setText("New password cannot be empty.");
        return;
    }
    if (newPass != confirmPass) {
        m_messageLabel->setText("New passwords do not match.");
        return;
    }
    
    // 2. Verify current password with the core
    if (!m_vault->verifyMasterPassword(currentPass)) {
        m_messageLabel->setText("The current password you entered is incorrect.");
        m_currentPasswordEdit->clear();
        m_currentPasswordEdit->setFocus();
        CipherMesh::Core::Crypto::secureWipe(currentPass);
        CipherMesh::Core::Crypto::secureWipe(newPass);
        CipherMesh::Core::Crypto::secureWipe(confirmPass);
        return;
    }
    
    // 3. If valid, attempt to change
    if (m_vault->changeMasterPassword(newPass)) {
        QMessageBox::information(this, "Success", "Your master password has been changed.");
        CipherMesh::Core::Crypto::secureWipe(currentPass);
        CipherMesh::Core::Crypto::secureWipe(newPass);
        CipherMesh::Core::Crypto::secureWipe(confirmPass);
        accept();
    } else {
        QMessageBox::critical(this, "Error", "An unknown error occurred while changing the password.");
        CipherMesh::Core::Crypto::secureWipe(currentPass);
        CipherMesh::Core::Crypto::secureWipe(newPass);
        CipherMesh::Core::Crypto::secureWipe(confirmPass);
    }
}