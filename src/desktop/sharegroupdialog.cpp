#include "sharegroupdialog.hpp"
#include "vault.hpp" 
#include "crypto.hpp" 
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QInputDialog>
#include <QMessageBox>
#include <QListWidgetItem>
#include <QMenu>
#include <QCheckBox>
#include <vector>
#include <string>

static int s_uiGroupMemberId = qRegisterMetaType<UIGroupMember>("UIGroupMember");

ShareGroupDialog::ShareGroupDialog(const QString& groupName, 
                                   CipherMesh::P2P::IP2PService* p2pService, 
                                   CipherMesh::Core::Vault* vault,
                                   QWidget* parent)
    : QDialog(parent), m_groupName(groupName), m_p2pService(p2pService), m_vault(vault)
{
    setupUi();
    setWindowTitle("Manage Group: " + m_groupName);
    
    connect(this, &ShareGroupDialog::internalGroupMembersUpdated, this, &ShareGroupDialog::updateMemberList);
    connect(this, &ShareGroupDialog::internalInviteStatus, this, &ShareGroupDialog::showStatusMessage);
    connect(this, &ShareGroupDialog::internalRemoveStatus, this, &ShareGroupDialog::showStatusMessage);

    setupServiceCallbacks();
    loadMembers(); 
    loadPermissions();
}

ShareGroupDialog::~ShareGroupDialog() {
    if (m_p2pService) {
        m_p2pService->onGroupMembersUpdated = nullptr;
        m_p2pService->onInviteStatus = nullptr;
        m_p2pService->onRemoveStatus = nullptr;
    }
}

void ShareGroupDialog::loadMembers() {
    if (!m_vault) return;
    try {
        std::vector<CipherMesh::Core::GroupMember> members = m_vault->getGroupMembers(m_groupName.toStdString());
        QList<UIGroupMember> qtMembers;
        for (const auto& m : members) {
            qtMembers.append({
                QString::fromStdString(m.userId),
                QString::fromStdString(m.userId), 
                QString::fromStdString(m.status),
                QString::fromStdString(m.role)
            });
        }
        updateMemberList(qtMembers);
    } catch (const std::exception& e) {
        qWarning() << "Failed to load members:" << e.what();
    }
}

void ShareGroupDialog::loadPermissions() {
    if (!m_vault) return;
    try {
        int groupId = m_vault->getGroupId(m_groupName.toStdString());
        CipherMesh::Core::GroupPermissions perms = m_vault->getGroupPermissions(groupId);
        
        // Block signals to prevent triggering onToggleAdminOnly while loading
        m_adminOnlyCheckbox->blockSignals(true);
        m_adminOnlyCheckbox->setChecked(perms.adminsOnlyWrite);
        m_adminOnlyCheckbox->blockSignals(false);
        
        // Only Owner can change this setting
        std::string myId = m_vault->getUserId();
        std::string ownerId = m_vault->getGroupOwner(groupId);
        
        if (myId != ownerId) {
            m_adminOnlyCheckbox->setEnabled(false);
            m_adminOnlyCheckbox->setToolTip("Only the Owner can change permissions.");
        } else {
            m_adminOnlyCheckbox->setEnabled(true);
            m_adminOnlyCheckbox->setToolTip("When enabled, only Admins and Owner can edit entries in this group.");
        }
    } catch (const std::exception& e) {
        qWarning() << "Failed to load group permissions:" << e.what();
    }
}

void ShareGroupDialog::setupServiceCallbacks() {
    m_p2pService->onInviteStatus = [this](bool success, const std::string& message) {
        emit internalInviteStatus(success, QString::fromStdString(message));
        if (success) loadMembers(); 
    };
    m_p2pService->onRemoveStatus = [this](bool success, const std::string& message) {
        emit internalRemoveStatus(success, QString::fromStdString(message));
        if (success) loadMembers(); 
    };
}

void ShareGroupDialog::setupUi() {
    setModal(true);
    setMinimumWidth(500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(16);
    
    m_titleLabel = new QLabel("Manage Group: " + m_groupName, this);
    m_titleLabel->setObjectName("DialogTitle");
    mainLayout->addWidget(m_titleLabel);
    
    std::string myId = m_vault ? m_vault->getUserId() : "Unknown";
    QLabel* idLabel = new QLabel("Your ID: <b>" + QString::fromStdString(myId) + "</b>", this);
    idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mainLayout->addWidget(idLabel);
    
    // --- Permission Checkbox ---
    m_adminOnlyCheckbox = new QCheckBox("Restrict editing to Admins only", this);
    connect(m_adminOnlyCheckbox, &QCheckBox::toggled, this, &ShareGroupDialog::onToggleAdminOnly);
    mainLayout->addWidget(m_adminOnlyCheckbox);

    QLabel* listLabel = new QLabel("Members:", this);
    listLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    mainLayout->addWidget(listLabel);
    
    m_memberListWidget = new QListWidget(this);
    m_memberListWidget->setAlternatingRowColors(true);
    
    // Enable Right-Click
    m_memberListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_memberListWidget, &QListWidget::customContextMenuRequested, this, &ShareGroupDialog::onContextMenuRequested);

    mainLayout->addWidget(m_memberListWidget);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    
    m_inviteButton = new QPushButton("Invite User...", this);
    m_inviteButton->setObjectName("NewButton");
    m_inviteButton->setMinimumWidth(120);
    
    m_removeButton = new QPushButton("Remove User", this);
    m_removeButton->setObjectName("DeleteButton");
    m_removeButton->setMinimumWidth(120);
    m_removeButton->setEnabled(false); 
    
    buttonLayout->addWidget(m_inviteButton);
    buttonLayout->addStretch(1);
    buttonLayout->addWidget(m_removeButton);
    mainLayout->addLayout(buttonLayout);
    
    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_statusLabel);

    connect(m_inviteButton, &QPushButton::clicked, this, &ShareGroupDialog::onInviteClicked);
    connect(m_removeButton, &QPushButton::clicked, this, &ShareGroupDialog::onRemoveClicked);
    connect(m_memberListWidget, &QListWidget::itemSelectionChanged, this, &ShareGroupDialog::onUserSelectionChanged);
}

// --- FIX: Better Member List Display ---
void ShareGroupDialog::updateMemberList(const QList<UIGroupMember>& members) {
    m_memberListWidget->clear();
    m_itemToUserIdMap.clear();
    
    QString myId = QString::fromStdString(m_vault->getUserId());

    for (const auto& member : members) {
        QString suffix = "";
        QString idToDisplay = member.id;
        
        // Add "(You)" for clarity, but keep the ID
        if (member.id == myId) {
            idToDisplay += " (You)";
        }

        // Determine Role/Status Text
        QColor textColor = QColor("#FFFFFF"); // Default White
        
        if (member.status == "pending") {
            suffix = " (Invite Sent)";
            textColor = QColor("#FFA000"); // Orange
        } 
        else if (member.role == "owner") {
            suffix = " ★ Owner";
            textColor = QColor("#4CAF50"); // Green
        }
        else if (member.role == "admin") {
            suffix = " 🛡️ Admin";
            textColor = QColor("#2196F3"); // Blue
        }
        // No text for "member" status="accepted"

        QString displayText = idToDisplay + suffix;
        QListWidgetItem* item = new QListWidgetItem(displayText, m_memberListWidget);
        
        // Store metadata
        item->setData(Qt::UserRole, member.status);
        item->setData(Qt::UserRole + 1, member.role); 
        m_itemToUserIdMap[item] = member.id;
        
        item->setForeground(textColor);
        if (member.role == "owner" || member.role == "admin") {
            QFont font = item->font();
            font.setBold(true);
            item->setFont(font);
        }
    }
    onUserSelectionChanged(); 
}

// --- NEW: Context Menu for Roles ---
void ShareGroupDialog::onContextMenuRequested(const QPoint &pos) {
    QListWidgetItem* item = m_memberListWidget->itemAt(pos);
    if (!item) return;
    
    QString targetUserId = m_itemToUserIdMap[item];
    QString myId = QString::fromStdString(m_vault->getUserId());
    if (targetUserId == myId) return; // Can't edit self
    
    // Check MY permissions (Must be Owner to promote)
    int groupId = m_vault->getGroupId(m_groupName.toStdString());
    std::string ownerId = m_vault->getGroupOwner(groupId);
    
    if (myId.toStdString() != ownerId) return; // Only owner can change roles
    
    QString currentRole = item->data(Qt::UserRole + 1).toString();
    QString status = item->data(Qt::UserRole).toString();
    
    if (status == "pending") return; // Can't promote pending users

    QMenu menu(this);
    if (currentRole != "admin") {
        menu.addAction("Promote to Admin", this, &ShareGroupDialog::promoteUser);
    } else {
        menu.addAction("Demote to Member", this, &ShareGroupDialog::demoteUser);
    }
    menu.exec(m_memberListWidget->mapToGlobal(pos));
}

void ShareGroupDialog::promoteUser() {
    QListWidgetItem* item = m_memberListWidget->currentItem();
    if (!item) return;
    QString userId = m_itemToUserIdMap[item];
    int groupId = m_vault->getGroupId(m_groupName.toStdString());
    m_vault->updateGroupMemberRole(groupId, userId.toStdString(), "admin");
    loadMembers();
}

void ShareGroupDialog::demoteUser() {
    QListWidgetItem* item = m_memberListWidget->currentItem();
    if (!item) return;
    QString userId = m_itemToUserIdMap[item];
    int groupId = m_vault->getGroupId(m_groupName.toStdString());
    m_vault->updateGroupMemberRole(groupId, userId.toStdString(), "member");
    loadMembers();
}

void ShareGroupDialog::onToggleAdminOnly(bool checked) {
    int groupId = m_vault->getGroupId(m_groupName.toStdString());
    m_vault->setGroupPermissions(groupId, checked);
}

void ShareGroupDialog::onInviteClicked() {
    if (!m_vault || !m_p2pService) return;

    // Create a custom dialog instead of using QInputDialog
    QDialog inviteDialog(this);
    inviteDialog.setWindowTitle("Invite User");
    inviteDialog.setMinimumWidth(400);
    
    QVBoxLayout* layout = new QVBoxLayout(&inviteDialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    
    QLabel* titleLabel = new QLabel("Invite User to Group", &inviteDialog);
    titleLabel->setObjectName("DialogTitle");
    layout->addWidget(titleLabel);
    
    QLabel* infoLabel = new QLabel(
        "Enter the User ID of the person you want to invite to this group.\n"
        "They will receive access to all entries in this group.",
        &inviteDialog
    );
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);
    
    QLabel* inputLabel = new QLabel("User ID:", &inviteDialog);
    layout->addWidget(inputLabel);
    
    QLineEdit* userIdInput = new QLineEdit(&inviteDialog);
    userIdInput->setPlaceholderText("e.g., user@example.com or user-id-123");
    layout->addWidget(userIdInput);
    
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(8);
    
    QPushButton* cancelButton = new QPushButton("Cancel", &inviteDialog);
    cancelButton->setMinimumWidth(100);
    
    QPushButton* inviteButton = new QPushButton("Send Invite", &inviteDialog);
    inviteButton->setObjectName("NewButton");
    inviteButton->setMinimumWidth(100);
    inviteButton->setDefault(true);
    
    buttonLayout->addStretch();
    buttonLayout->addWidget(cancelButton);
    buttonLayout->addWidget(inviteButton);
    layout->addLayout(buttonLayout);
    
    connect(cancelButton, &QPushButton::clicked, &inviteDialog, &QDialog::reject);
    connect(inviteButton, &QPushButton::clicked, &inviteDialog, &QDialog::accept);
    connect(userIdInput, &QLineEdit::returnPressed, &inviteDialog, &QDialog::accept);
    
    if (inviteDialog.exec() == QDialog::Accepted) {
        QString email = userIdInput->text().trimmed();
        if (!email.isEmpty()) {
            m_statusLabel->setText("Preparing data...");
            
            try {
                std::vector<unsigned char> key = m_vault->getGroupKey(m_groupName.toStdString());
                std::vector<CipherMesh::Core::VaultEntry> entries = m_vault->exportGroupEntries(m_groupName.toStdString());
                
                m_statusLabel->setText("Sending invite...");
                m_p2pService->inviteUser(m_groupName.toStdString(), email.toStdString(), key, entries);
                
                m_vault->addGroupMember(m_groupName.toStdString(), email.toStdString(), "member", "pending");
                loadMembers();
                CipherMesh::Core::Crypto::secureWipe(key);
                
            } catch (const std::exception& e) {
                QMessageBox::critical(this, "Error", QString("Failed to export group: %1").arg(e.what()));
                m_statusLabel->setText("Error.");
            }
        }
    }
}

void ShareGroupDialog::onRemoveClicked() {
    QListWidgetItem* selectedItem = m_memberListWidget->currentItem();
    if (!selectedItem) return;

    QString userId = m_itemToUserIdMap.value(selectedItem);
    QString status = selectedItem->data(Qt::UserRole).toString();
    
    if (status == "owner" || userId == QString::fromStdString(m_vault->getUserId())) {
        QMessageBox::warning(this, "Error", "Cannot remove the owner.");
        return;
    }
    
    if (status == "pending") {
        m_statusLabel->setText("Cancelling invite...");
        m_p2pService->cancelInvite(userId.toStdString());
    } else {
        m_statusLabel->setText("Removing user...");
        m_p2pService->removeUser(m_groupName.toStdString(), userId.toStdString());
    }
    
    m_vault->removeGroupMember(m_groupName.toStdString(), userId.toStdString());
    loadMembers();
    m_statusLabel->setText("User removed.");
}

void ShareGroupDialog::onUserSelectionChanged() {
    QListWidgetItem* selectedItem = m_memberListWidget->currentItem();
    if (!selectedItem) {
        m_removeButton->setEnabled(false);
        m_removeButton->setText("Remove User");
        return;
    }
    
    QString status = selectedItem->data(Qt::UserRole).toString();
    QString role = selectedItem->data(Qt::UserRole + 1).toString();
    QString userId = m_itemToUserIdMap[selectedItem];
    QString myId = QString::fromStdString(m_vault->getUserId());
    
    if (userId == myId) {
        m_removeButton->setEnabled(false); // Can't remove self
    } else if (role == "owner") {
        m_removeButton->setEnabled(false);
    } else {
        m_removeButton->setEnabled(true);
    }
    
    if (status == "pending") {
        m_removeButton->setText("Cancel Invite");
    } else {
        m_removeButton->setText("Remove User");
    }
}

void ShareGroupDialog::showStatusMessage(bool success, const QString& message) {
    m_statusLabel->setStyleSheet(success ? "color: #4CAF50;" : "color: #ff5555;");
    m_statusLabel->setText(message);
}