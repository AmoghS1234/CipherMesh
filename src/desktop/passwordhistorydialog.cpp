#include "passwordhistorydialog.hpp"
#include "../core/vault.hpp"
#include "../core/vault_entry.hpp"
#include "toast.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QClipboard>
#include <QApplication>
#include <QDateTime>
#include <QMessageBox>

namespace CipherMesh {
namespace GUI {

PasswordHistoryDialog::PasswordHistoryDialog(Core::Vault* vault, int entryId, const std::string& entryName, QWidget* parent)
    : QDialog(parent), m_vault(vault), m_entryId(entryId), m_entryName(entryName) {
    
    setWindowTitle("Password History");
    setMinimumSize(500, 400);
    
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    // Title
    auto* titleLabel = new QLabel(QString::fromStdString("Password History: " + m_entryName));
    titleLabel->setObjectName("DialogTitle");
    mainLayout->addWidget(titleLabel);
    
    // Info label
    auto* infoLabel = new QLabel("Previous passwords for this entry (most recent first):");
    mainLayout->addWidget(infoLabel);
    
    // History list
    m_historyList = new QListWidget();
    m_historyList->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_historyList);
    
    // Buttons
    auto* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    
    m_copyButton = new QPushButton("Copy Password");
    m_copyButton->setObjectName("NewButton");
    m_copyButton->setFixedWidth(150);
    m_copyButton->setEnabled(false);
    connect(m_copyButton, &QPushButton::clicked, this, &PasswordHistoryDialog::onCopyPassword);
    
    m_showButton = new QPushButton("Show/Hide");
    m_showButton->setFixedWidth(120);
    m_showButton->setEnabled(false);
    connect(m_showButton, &QPushButton::clicked, this, &PasswordHistoryDialog::onShowPassword);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_copyButton);
    buttonLayout->addWidget(m_showButton);
    
    m_closeButton = new QPushButton("Close");
    m_closeButton->setObjectName("CancelButton");
    m_closeButton->setFixedWidth(80);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_closeButton);
    
    mainLayout->addLayout(buttonLayout);
    
    // Enable buttons when selection changes
    connect(m_historyList, &QListWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = m_historyList->currentRow() >= 0;
        m_copyButton->setEnabled(hasSelection);
        m_showButton->setEnabled(hasSelection);
    });
    
    loadHistory();
}

void PasswordHistoryDialog::loadHistory() {
    m_historyList->clear();
    m_historyItems.clear();
    
    if (!m_vault || m_vault->isLocked()) {
        return;
    }
    
    try {
        // Get password history from database
        auto historyEntries = m_vault->getPasswordHistory(m_entryId);
        
        if (historyEntries.empty()) {
            auto* item = new QListWidgetItem("No password history available");
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
            m_historyList->addItem(item);
            return;
        }
        
        // Decrypt and display each entry
        for (const auto& entry : historyEntries) {
            std::string decryptedPwd = m_vault->decryptPasswordFromHistory(entry.encryptedPassword);
            
            PasswordHistoryItem item;
            item.id = entry.id;
            item.changedAt = entry.changedAt;
            item.password = decryptedPwd;
            m_historyItems.push_back(item);
            
            // Display as "Changed: [Date] - ••••••••"
            QString displayText = QString("Changed: %1 - ••••••••").arg(formatTimestamp(entry.changedAt));
            m_historyList->addItem(displayText);
        }
        
    } catch (const std::exception& e) {
        QMessageBox::warning(this, "Error", QString("Failed to load password history: %1").arg(e.what()));
    }
}

QString PasswordHistoryDialog::formatTimestamp(long long timestamp) {
    QDateTime dt = QDateTime::fromSecsSinceEpoch(timestamp);
    return dt.toString("MMM dd, yyyy hh:mm AP");
}

void PasswordHistoryDialog::onCopyPassword() {
    int row = m_historyList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_historyItems.size())) {
        return;
    }
    
    const auto& item = m_historyItems[row];
    QApplication::clipboard()->setText(QString::fromStdString(item.password));
    
    // Show toast notification
    Toast* toast = new Toast("Password copied to clipboard", ToastType::Success, this);
    toast->show();
}

void PasswordHistoryDialog::onShowPassword() {
    int row = m_historyList->currentRow();
    if (row < 0 || row >= static_cast<int>(m_historyItems.size())) {
        return;
    }
    
    const auto& item = m_historyItems[row];
    QString currentText = m_historyList->item(row)->text();
    
    if (currentText.contains("••••")) {
        // Show password
        QString displayText = QString("Changed: %1 - %2")
            .arg(formatTimestamp(item.changedAt))
            .arg(QString::fromStdString(item.password));
        m_historyList->item(row)->setText(displayText);
    } else {
        // Hide password
        QString displayText = QString("Changed: %1 - ••••••••").arg(formatTimestamp(item.changedAt));
        m_historyList->item(row)->setText(displayText);
    }
}

} // namespace GUI
} // namespace CipherMesh
