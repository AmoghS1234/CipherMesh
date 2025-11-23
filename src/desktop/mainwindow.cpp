#include <sodium.h>
#include <sqlite3.h>
#include "mainwindow.hpp"
#include "newentrydialog.hpp"
#include "newgroupdialog.hpp"
#include "vault.hpp"
#include "crypto.hpp" 
#include "sharegroupdialog.hpp" 
#include "changepassworddialog.hpp" 
#include "settingsdialog.hpp"
#include "passwordhistorydialog.hpp"
#include "toast.hpp"
#include "webrtcservice.hpp" 
#include "themes.hpp"

#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QTimer>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QFormLayout>
#include <QByteArray> 
#include <QInputDialog> 
#include <QShortcut>
#include <QKeySequence>
#include <QDateTime>
#include <QStringList>
#include <stdexcept>
#include <string>
#include <vector>
#include <QMetaObject> 

#include <QPushButton>
#include <QLineEdit>
#include <QSplitter>
#include <QListWidget>
#include <QMenu> 
#include <QTextEdit> 
#include <QMenuBar> 
#include <QThread> 
#include <QObject> 
#include <QCloseEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDesktopServices> 
#include <QUrl>
#include <QFile>
#include <QDir>

// --- ICON DEFINITIONS ---
const QByteArray g_folderIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M10 4H4c-1.1 0-1.99.9-1.99 2L2 18c0 1.1.9 2 2 2h16c1.1 0 2-.9 2-2V8c0-1.1-.9-2-2-2h-8l-2-2z"/></svg>)";
const QByteArray g_keyIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M12.65 10C11.83 7.67 9.61 6 7 6c-3.31 0-6 2.69-6 6s2.69 6 6 6c2.61 0 4.83-1.67 5.65-4H17v4h4v-4h2v-4H12.65zM7 15c-1.66 0-3-1.34-3-3s1.34-3 3-3 3 1.34 3 3-1.34 3-3 3z"/></svg>)";
const QByteArray g_plusIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M19 13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z"/></svg>)";
const QByteArray g_trashIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M6 19c0 1.1.9 2 2 2h8c1.1 0 2-.9 2-2V7H6v12zM19 4h-3.5l-1-1h-5l-1 1H5v2h14V4z"/></svg>)";
const QByteArray g_pencilIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M3 17.25V21h3.75L17.81 9.94l-3.75-3.75L3 17.25zM20.71 7.04c.39-.39.39-1.02 0-1.41l-2.34-2.34c-.39-.39-1.02-.39-1.41 0l-1.83 1.83 3.75 3.75 1.83-1.83z"/></svg>)";
const QByteArray g_shareIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M18 16.08c-.76 0-1.44.3-1.96.77L8.91 12.7c.05-.23.09-.46.09-.7s-.04-.47-.09-.7l7.05-4.11c.54.5 1.25.81 2.04.81 1.66 0 3-1.34 3-3s-1.34-3-3-3-3 1.34-3 3c0 .24.04.47.09.7L8.04 8.81C7.5 8.31 6.79 8 6 8c-1.66 0-3 1.34-3 3s1.34 3 3 3c.79 0 1.5-.31 2.04-.81l7.12 4.16c-.05.23-.09.46-.09.7 0 1.66 1.34 3 3 3s3-1.34 3-3-1.34-3-3-3z"/></svg>)";
const QByteArray g_copyIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M16 1H4c-1.1 0-2 .9-2 2v14h2V3h12V1zm3 4H8c-1.1 0-2 .9-2 2v14c0 1.1.9 2 2 2h11c1.1 0 2-.9 2-2V7c0-1.1-.9-2-2-2zm0 16H8V7h11v14z"/></svg>)";
const QByteArray g_cogIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M19.43 12.98c.04-.32.07-.64.07-.98s-.03-.66-.07-.98l2.11-1.65c.19-.15.24-.42.12-.64l-2-3.46c-.12-.22-.39-.3-.61-.22l-2.49 1c-.52-.4-1.08-.73-1.69-.98l-.38-2.65C14.46 2.18 14.25 2 14 2h-4c-.25 0-.46.18-.49.42l-.38 2.65c-.61.25-1.17.59-1.69.98l-2.49-1c-.22-.08-.49 0-.61.22l-2 3.46c-.13.22-.07.49.12.64l2.11 1.65c-.04.32-.07.65-.07.98s.03.66.07.98l-2.11 1.65c-.19.15-.24.42-.12.64l2 3.46c.12.22.39.3.61.22l2.49-1c.52.4 1.08.73 1.69.98l.38 2.65c.03.24.24.42.49.42h4c.25 0 .46-.18.49-.42l.38-2.65c.61-.25 1.17-.59 1.69-.98l2.49 1c.22.08.49 0 .61-.22l2-3.46c.12-.22.07-.49-.12-.64l-2.11-1.65zM12 15.5c-1.93 0-3.5-1.57-3.5-3.5s1.57-3.5 3.5-3.5 3.5 1.57 3.5 3.5-1.57 3.5-3.5 3.5z"/></svg>)";
const QByteArray g_lockIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M18 8h-1V6c0-2.76-2.24-5-5-5S7 3.24 7 6v2H6c-1.1 0-2 .9-2 2v10c0 1.1.9 2 2 2h12c1.1 0 2-.9 2-2V10c0-1.1-.9-2-2-2zm-6 9c-1.1 0-2-.9-2-2s.9-2 2-2 2 .9 2 2-.9 2-2 2zm3.1-9H8.9V6c0-1.71 1.39-3.1 3.1-3.1 1.71 0 3.1 1.39 3.1 3.1v2z"/></svg>)";
const QByteArray g_eyeIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M12 4.5C7 4.5 2.73 7.61 1 12c1.73 4.39 6 7.5 11 7.5s9.27-3.11 11-7.5c-1.73-4.39-6-7.5-11-7.5zM12 17c-2.76 0-5-2.24-5-5s2.24-5 5-5 5 2.24 5 5-2.24 5-5 5zm0-8c-1.66 0-3 1.34-3 3s1.34 3 3 3 3-1.34 3-3-1.34-3-3-3z"/></svg>)";
const QByteArray g_eyeOffIconSvg = R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 24 24" fill="currentColor"><path d="M12 7c2.76 0 5 2.24 5 5 0 .65-.13 1.26-.36 1.83l2.92 2.92c1.51-1.26 2.7-2.89 3.43-4.75-1.73-4.39-6-7.5-11-7.5-1.4 0-2.74.25-3.98.7l2.16 2.16C10.74 7.13 11.35 7 12 7zM2 4.27l2.28 2.28.46.46C3.08 8.3 1.78 10.02 1 12c1.73 4.39 6 7.5 11 7.5 1.55 0 3.03-.3 4.38-.84l.42.42L19.73 22 21 20.73 3.27 3 2 4.27zM7.53 9.8l1.55 1.55c-.05.21-.08.43-.08.65 0 1.66 1.34 3 3 3 .22 0 .44-.03.65-.08l1.55 1.55c-.67.33-1.41.53-2.2.53-2.76 0-5-2.24-5-5 0-.79.2-1.53.53-2.2zm4.31-.78l3.15 3.15.02-.16c0-1.66-1.34-3-3-3l-.17.01z"/></svg>)";

// Constants for UI strings
const QString INVITE_SUFFIX = " (Invite)";
const QString COPY_SUFFIX = " (Copy)";

MainWindow::MainWindow(const QString& userId, QWidget *parent)
    : QMainWindow(parent),
      m_vault(nullptr),
      m_p2pService(nullptr), 
      m_isPasswordVisible(false),
      m_currentUserId(userId),
      m_actionIconColor("#ffffff"),
      m_uiIconColor("#e0e0e0"),
      m_autoLockTimer(nullptr),
      m_recentMenu(nullptr)
{
    setWindowTitle("CipherMesh - (Locked) - " + m_currentUserId);
    
    // Set a more reasonable default size and make it resizable
    resize(1000, 650);
    setMinimumSize(800, 500);
    
    // Setup auto-lock timer
    setupAutoLockTimer();
    
    m_p2pThread = new QThread(this);
    m_p2pThread->setObjectName("P2PWorkerThread");
    
    // Render.com URL
    WebRTCService* p2pWorker = new WebRTCService("wss://ciphermesh-signal-server.onrender.com", m_currentUserId.toStdString(), nullptr); 

    p2pWorker->onIncomingInvite = [this](const std::string& senderId, const std::string& groupName) {
        QMetaObject::invokeMethod(this, [this, senderId, groupName]() {
            handleIncomingInvite(QString::fromStdString(senderId), QString::fromStdString(groupName));
        }, Qt::QueuedConnection);
    };
    
    p2pWorker->onInviteCancelled = [this](const std::string& senderId) {
        QMetaObject::invokeMethod(this, [this, senderId]() {
            handleInviteCancelled(QString::fromStdString(senderId));
        }, Qt::QueuedConnection);
    };
    
    p2pWorker->onGroupDataReceived = [this](const std::string& senderId,
                                            const std::string& groupName, 
                                            const std::vector<unsigned char>& key, 
                                            const std::vector<CipherMesh::Core::VaultEntry>& entries) 
    {
        QMetaObject::invokeMethod(this, [this, senderId, groupName, key, entries]() {
            handleGroupData(QString::fromStdString(senderId), QString::fromStdString(groupName), key, entries);
        }, Qt::QueuedConnection);
    };
    
    // --- NEW: Handle Peer Coming Online ---
    p2pWorker->onPeerOnline = [this](const std::string& userId) {
        QMetaObject::invokeMethod(this, [this, userId]() {
            handlePeerOnline(QString::fromStdString(userId));
        }, Qt::QueuedConnection);
    };

    p2pWorker->onDataRequested = [this](const std::string& requesterId, const std::string& groupName) {
        QMetaObject::invokeMethod(this, [this, requesterId, groupName]() {
            handleDataRequested(QString::fromStdString(requesterId), QString::fromStdString(groupName));
        }, Qt::QueuedConnection);
    };
    
    p2pWorker->onInviteResponse = [this](const std::string& userId, const std::string& groupName, bool accepted) {
        QMetaObject::invokeMethod(this, [this, userId, groupName, accepted]() {
            handleInviteResponse(QString::fromStdString(userId), QString::fromStdString(groupName), accepted);
        }, Qt::QueuedConnection);
    };
    
    p2pWorker->onConnectionStatusChanged = [this](bool connected) {
        QMetaObject::invokeMethod(this, [this, connected]() {
            handleConnectionStatusChanged(connected);
        }, Qt::QueuedConnection);
    };
    
    p2pWorker->moveToThread(m_p2pThread);
    connect(m_p2pThread, &QThread::started, p2pWorker, &WebRTCService::startSignaling);
    
    m_p2pThread->start();
    m_p2pService = p2pWorker;

    connect(m_p2pThread, &QThread::finished, p2pWorker, &QObject::deleteLater);
    connect(this, &QObject::destroyed, m_p2pThread, &QThread::quit);
    connect(m_p2pThread, &QThread::finished, m_p2pThread, &QObject::deleteLater);

    setupUi();
    setupKeyboardShortcuts();
    updateIcons();
    updateWindowTitle();
    m_detailsStack->setCurrentIndex(0);
    
    // Install event filter to detect user activity
    qApp->installEventFilter(this);
}

MainWindow::~MainWindow()
{
    if (m_p2pThread && m_p2pThread->isRunning()) {
        m_p2pThread->quit();
        m_p2pThread->wait(500);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    // Reset auto-lock timer on user activity (mouse or keyboard)
    if (event->type() == QEvent::MouseButtonPress ||
        event->type() == QEvent::KeyPress ||
        event->type() == QEvent::MouseMove) {
        resetAutoLockTimer();
    }
    return QMainWindow::eventFilter(obj, event);
}

void MainWindow::setVault(CipherMesh::Core::Vault* vault)
{
    m_vault = vault;
    if (m_vault && !m_vault->isLocked()) {
        postUnlockInit(); 
    } else {
        QMessageBox::critical(this, "Error", "Failed to receive unlocked vault.");
    }
}

void MainWindow::onSettingsButtonClicked()
{
    SettingsDialog dialog(m_currentUserId, m_vault, this);
    
    connect(&dialog, &SettingsDialog::themeChanged, this, [this](const QString& themeId) {
        setTheme(themeId, CipherMesh::Themes::getById(themeId).styleSheet);
    });
    
    connect(&dialog, &SettingsDialog::autoLockTimeoutChanged, this, &MainWindow::onAutoLockTimeoutChanged);
    
    dialog.exec();
    
    // Reset timer after settings dialog closes in case timeout changed
    resetAutoLockTimer();
}

void MainWindow::setTheme(const QString& themeId, const QString& styleSheet) {
    qApp->setStyleSheet(styleSheet);
    
    auto themeDef = CipherMesh::Themes::getById(themeId);
    m_actionIconColor = themeDef.actionIconColor;
    m_uiIconColor = themeDef.uiIconColor;
    
    updateIcons();

    if (m_vault) {
        m_vault->setThemeId(themeId.toStdString());
    }
}

void MainWindow::updateIcons() {
    m_newGroupButton->setIcon(loadSvgIcon(g_plusIconSvg, m_actionIconColor));
    m_newEntryButton->setIcon(loadSvgIcon(g_plusIconSvg, m_actionIconColor));
    m_editEntryButton->setIcon(loadSvgIcon(g_pencilIconSvg, m_actionIconColor));
    m_deleteEntryButton->setIcon(loadSvgIcon(g_trashIconSvg, m_actionIconColor));
    m_settingsButton->setIcon(loadSvgIcon(g_cogIconSvg, m_uiIconColor));
    
    // Update show/hide password button icon
    if (m_showPasswordButton->isChecked()) {
        m_showPasswordButton->setIcon(loadSvgIcon(g_eyeOffIconSvg, m_actionIconColor));
    } else {
        m_showPasswordButton->setIcon(loadSvgIcon(g_eyeIconSvg, m_actionIconColor));
    }
    
    if(m_vault && !m_vault->isLocked()) {
        loadGroups(); 
    }
}

void MainWindow::handleIncomingInvite(const QString& senderId, const QString& groupName)
{
    if (!m_vault || !m_p2pService) return;
    auto existing = m_vault->getPendingInvites();
    for(const auto& inv : existing) {
        if(inv.senderId == senderId.toStdString() && inv.groupName == groupName.toStdString()) {
            // If we already accepted this invite, request the data now that sender is online
            if (inv.status == "accepted") {
                qDebug() << "DEBUG: Received invite-request for already-accepted invite. Requesting data...";
                m_p2pService->requestData(senderId.toStdString(), groupName.toStdString());
            }
            return;
        }
    }

    QJsonObject placeholder;
    placeholder["status"] = "waiting_for_consent";
    placeholder["group"] = groupName;
    placeholder["sender"] = senderId;
    std::string payload = QJsonDocument(placeholder).toJson(QJsonDocument::Compact).toStdString();

    m_vault->storePendingInvite(senderId.toStdString(), groupName.toStdString(), payload);
    loadGroups();
    QMessageBox::information(this, "Group Invite", 
        QString("User <b>%1</b> wants to share group <b>'%2'</b>.<br>It has been added to your inbox.")
        .arg(senderId, groupName));
}

void MainWindow::handleInviteCancelled(const QString& senderId) {
    if (!m_vault) return;
    auto invites = m_vault->getPendingInvites();
    for (const auto& inv : invites) {
        if (inv.senderId == senderId.toStdString()) {
            m_vault->deletePendingInvite(inv.id);
            break; 
        }
    }
    loadGroups();
    if (m_currentSelectedInviteId != -1) {
        m_detailsStack->setCurrentIndex(0);
    }
    QMessageBox::information(this, "Invite Cancelled", QString("The user <b>%1</b> cancelled their group invitation.").arg(senderId));
}

void MainWindow::handlePeerOnline(const QString& userId) {
    if (!m_vault || !m_p2pService) return;
    
    qDebug() << "DEBUG: Peer came online:" << userId;
    
    // Check if we have any accepted invites from this user
    auto invites = m_vault->getPendingInvites();
    for (const auto& invite : invites) {
        if (invite.senderId == userId.toStdString() && invite.status == "accepted") {
            qDebug() << "DEBUG: Found accepted invite from" << userId << "- requesting data";
            
            // Request the data since sender is now online
            m_p2pService->requestData(invite.senderId, invite.groupName);
            
            // Update UI if we're viewing this invite
            if (m_currentSelectedInviteId == invite.id) {
                m_inviteInfoLabel->setText("<b>Sender is online!</b><br>Requesting data transfer...");
            }
            
            break; // Handle one invite at a time
        }
    }
}

void MainWindow::handleGroupData(const QString& senderId,
                                 const QString& groupName, 
                                 const std::vector<unsigned char>& key, 
                                 const std::vector<CipherMesh::Core::VaultEntry>& entries)
{
    if (!m_vault) return;

    int inviteIdToDelete = -1;
    auto invites = m_vault->getPendingInvites();
    for(const auto& inv : invites) {
        if (inv.senderId == senderId.toStdString() && inv.groupName == groupName.toStdString()) {
            inviteIdToDelete = inv.id;
            break;
        }
    }

    std::string finalName = groupName.toStdString();
    int counter = 1;
    while (m_vault->groupExists(finalName)) {
        finalName = groupName.toStdString() + " (Shared " + std::to_string(counter++) + ")";
    }

    if (m_vault->addGroup(finalName, key)) {
        m_vault->importGroupEntries(finalName, entries);
        
        if (inviteIdToDelete != -1) {
            m_vault->deletePendingInvite(inviteIdToDelete);
            if (m_currentSelectedInviteId == inviteIdToDelete) {
                m_currentSelectedInviteId = -1;
                m_detailsStack->setCurrentIndex(0); 
            }
        }
        
        loadGroups();
        
        for (int i=0; i<m_groupListWidget->count(); i++) {
            if (m_groupListWidget->item(i)->text() == QString::fromStdString(finalName)) {
                m_groupListWidget->setCurrentRow(i);
                break;
            }
        }

        QMessageBox::information(this, "Transfer Complete", 
            QString("Group <b>%1</b> has been imported successfully with %2 entries.")
            .arg(QString::fromStdString(finalName))
            .arg(entries.size()));
    } else {
        QMessageBox::critical(this, "Import Failed", "Could not create group. Data transfer failed.");
    }
}

void MainWindow::onAcceptInviteClicked() {
    if (m_currentSelectedInviteId == -1 || !m_vault || !m_p2pService) return;

    auto invites = m_vault->getPendingInvites(); 
    CipherMesh::Core::PendingInvite target;
    bool found = false;
    for(const auto& inv : invites) { if(inv.id == m_currentSelectedInviteId) { target = inv; found = true; break; } }
    
    if (!found) return;

    // Update the invite status in the database so we can request data when sender comes online
    m_vault->updatePendingInviteStatus(m_currentSelectedInviteId, "accepted");
    
    m_p2pService->respondToInvite(target.senderId, true);
    
    m_inviteInfoLabel->setText("<b>Acceptance Sent!</b><br>Waiting for sender to transfer data...<br>(They must be online)");
    m_acceptInviteButton->setEnabled(false);
    m_acceptInviteButton->setText("Waiting...");
}

void MainWindow::onRejectInviteClicked() {
    if (m_currentSelectedInviteId != -1 && m_vault && m_p2pService) {
        // Get the invite details before deleting it
        auto invites = m_vault->getPendingInvites();
        for (const auto& inv : invites) {
            if (inv.id == m_currentSelectedInviteId) {
                // Send rejection to the sender
                m_p2pService->respondToInvite(inv.senderId, false);
                break;
            }
        }
        
        m_vault->deletePendingInvite(m_currentSelectedInviteId);
        m_currentSelectedInviteId = -1;
        loadGroups();
        m_detailsStack->setCurrentIndex(0); 
    }
}

void MainWindow::onLocationDoubleClicked(QListWidgetItem* item) {
    QString text = item->text();
    int splitIndex = text.indexOf("] ");
    if (splitIndex == -1) return;
    
    QString type = text.mid(1, splitIndex - 1);
    QString value = text.mid(splitIndex + 2);
    
    if (type == "URL") {
        if (!value.startsWith("http")) value = "https://" + value;
        QDesktopServices::openUrl(QUrl(value));
    } else {
        QApplication::clipboard()->setText(value);
        QMessageBox::information(this, "Copied", type + " copied to clipboard.");
    }
}

void MainWindow::setupUi()
{
    // --- Menu Bar (create first for proper layout) ---
    QMenuBar* menuBar = new QMenuBar(this);
    setMenuBar(menuBar);
    
    QMenu* fileMenu = menuBar->addMenu("&File");
    
    QAction* lockAction = fileMenu->addAction(loadSvgIcon(g_lockIconSvg, m_uiIconColor), "Lock Vault");
    lockAction->setShortcut(QKeySequence("Ctrl+L"));
    connect(lockAction, &QAction::triggered, this, &MainWindow::onLockVault);
    
    fileMenu->addSeparator();
    QAction* quitAction = fileMenu->addAction("Quit");
    quitAction->setShortcut(QKeySequence("Ctrl+Q"));
    connect(quitAction, &QAction::triggered, qApp, &QApplication::quit);
    
    QMenu* vaultMenu = menuBar->addMenu("&Vault");
    QAction* changePwAction = vaultMenu->addAction("Change Master Password...");
    connect(changePwAction, &QAction::triggered, this, &MainWindow::onChangeMasterPasswordClicked);
    
    QAction* newGroupAction = vaultMenu->addAction("New Group");
    newGroupAction->setShortcut(QKeySequence("Ctrl+G"));
    connect(newGroupAction, &QAction::triggered, this, &MainWindow::onNewGroupClicked);
    
    QAction* newEntryAction = vaultMenu->addAction("New Entry");
    newEntryAction->setShortcut(QKeySequence("Ctrl+N"));
    connect(newEntryAction, &QAction::triggered, this, &MainWindow::onNewEntryClicked);
    
    // --- Recently Accessed Menu ---
    m_recentMenu = menuBar->addMenu("&Recent");
    updateRecentMenu();
    
    // --- Group Pane ---
    QWidget* groupPaneWidget = new QWidget(this);
    QVBoxLayout* groupLayout = new QVBoxLayout(groupPaneWidget);
    groupLayout->setContentsMargins(8, 8, 8, 8);
    groupLayout->setSpacing(8);

    m_groupListWidget = new QListWidget(this);
    m_groupListWidget->setObjectName("GroupList"); 
    m_groupListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_groupListWidget, &QListWidget::customContextMenuRequested, this, &MainWindow::onGroupContextMenuRequested);
    
    m_newGroupButton = new QPushButton("New Group");
    m_newGroupButton->setObjectName("NewButton");
    m_newGroupButton->setToolTip("Create a new group (Ctrl+G)");

    m_settingsButton = new QPushButton("");
    m_settingsButton->setObjectName("IconButton"); 
    m_settingsButton->setToolTip("Settings");
    m_settingsButton->setFixedSize(32, 32);
    
    // Connection status indicator
    m_connectionStatusLabel = new QLabel("● Connecting...");
    m_connectionStatusLabel->setObjectName("StatusLabel");
    m_connectionStatusLabel->setToolTip("Connection status to signaling server");

    QHBoxLayout* groupButtonLayout = new QHBoxLayout();
    groupButtonLayout->setContentsMargins(0, 0, 0, 0); 
    groupButtonLayout->setSpacing(8);
    groupButtonLayout->addWidget(m_newGroupButton);
    groupButtonLayout->addStretch(1);
    groupButtonLayout->addWidget(m_connectionStatusLabel);
    groupButtonLayout->addWidget(m_settingsButton);

    groupLayout->addWidget(m_groupListWidget, 1); 
    groupLayout->addLayout(groupButtonLayout); 

    // --- Entry Pane ---
    QWidget* entryPaneWidget = new QWidget(this);
    QVBoxLayout* entryLayout = new QVBoxLayout(entryPaneWidget);
    entryLayout->setContentsMargins(8, 8, 8, 8);
    entryLayout->setSpacing(8);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Search entries... (Ctrl+F)");
    m_searchEdit->setClearButtonEnabled(true);
    
    m_newEntryButton = new QPushButton("New Entry");
    m_newEntryButton->setObjectName("NewButton"); 
    m_newEntryButton->setEnabled(false);
    m_newEntryButton->setToolTip("Add a new password entry (Ctrl+N)");

    QHBoxLayout* entryButtonLayout = new QHBoxLayout();
    entryButtonLayout->setContentsMargins(0, 0, 0, 0); 
    entryButtonLayout->setSpacing(8);
    entryButtonLayout->addWidget(m_newEntryButton);
    entryButtonLayout->addStretch(1);
    
    m_entryListWidget = new QListWidget(this);
    m_entryListWidget->setObjectName("EntryList"); 
    m_entryListWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    m_entryListWidget->setDragEnabled(true);
    m_entryListWidget->setAcceptDrops(true);
    m_entryListWidget->setDropIndicatorShown(true);
    m_entryListWidget->setDefaultDropAction(Qt::MoveAction);
    m_entryListWidget->setDragDropMode(QAbstractItemView::InternalMove);
    connect(m_entryListWidget, &QListWidget::customContextMenuRequested, this, &MainWindow::onEntryContextMenuRequested);
    
    entryLayout->addWidget(m_searchEdit); 
    entryLayout->addWidget(m_entryListWidget, 1); 
    entryLayout->addLayout(entryButtonLayout); 

    // --- Details Pane ---
    QWidget* placeholderWidget = new QWidget(this);
    QVBoxLayout* placeholderLayout = new QVBoxLayout(placeholderWidget);
    QLabel* placeholderLabel = new QLabel("Select an entry to view details.", this);
    placeholderLabel->setAlignment(Qt::AlignCenter);
    placeholderLabel->setObjectName("PlaceholderLabel"); 
    placeholderLayout->addWidget(placeholderLabel);

    m_detailViewWidget = new QWidget(this);
    QVBoxLayout* detailPageLayout = new QVBoxLayout(m_detailViewWidget);
    detailPageLayout->setContentsMargins(20, 20, 20, 20); 
    detailPageLayout->setSpacing(12);

    m_usernameLabel = new QLabel("Username", this); 
    m_usernameLabel->setObjectName("DetailUsername");
    m_usernameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse); 
    
    m_copyUsernameButton = new QPushButton("Copy");
    m_copyUsernameButton->setFixedWidth(80); 
    
    QHBoxLayout* userLayout = new QHBoxLayout();
    userLayout->setContentsMargins(0, 0, 0, 8); 
    userLayout->addWidget(m_usernameLabel, 1); 
    userLayout->addWidget(m_copyUsernameButton);
    
    detailPageLayout->addLayout(userLayout); 
    
    QFormLayout* detailsLayout = new QFormLayout(); 
    detailsLayout->setContentsMargins(0, 0, 0, 0); 
    detailsLayout->setSpacing(12);
    detailsLayout->setRowWrapPolicy(QFormLayout::WrapAllRows);
    detailsLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setReadOnly(true);
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    
    m_copyPasswordButton = new QPushButton("Copy");
    m_copyPasswordButton->setFixedWidth(80); 
    
    m_showPasswordButton = new QPushButton();
    m_showPasswordButton->setIcon(loadSvgIcon(g_eyeIconSvg, m_actionIconColor));
    m_showPasswordButton->setToolTip("Show/Hide Password");
    m_showPasswordButton->setFixedWidth(40); 
    m_showPasswordButton->setCheckable(true); 
    
    QHBoxLayout* passLayout = new QHBoxLayout();
    passLayout->setSpacing(8);
    passLayout->addWidget(m_passwordEdit, 1); 
    passLayout->addWidget(m_copyPasswordButton);
    passLayout->addWidget(m_showPasswordButton);
    detailsLayout->addRow("Password:", passLayout);

    m_locationsList = new QListWidget(this);
    m_locationsList->setMinimumHeight(80);
    m_locationsList->setMaximumHeight(150);
    m_locationsList->setObjectName("DetailLocationsList"); 
    detailsLayout->addRow("Locations:", m_locationsList);
    
    connect(m_locationsList, &QListWidget::itemDoubleClicked, this, &MainWindow::onLocationDoubleClicked);

    m_notesEdit = new QTextEdit(this);
    m_notesEdit->setReadOnly(true);
    m_notesEdit->setMaximumHeight(120);
    detailsLayout->addRow("Notes:", m_notesEdit);
    
    // Timestamp label
    m_timestampLabel = new QLabel(this);
    m_timestampLabel->setObjectName("PlaceholderLabel");
    m_timestampLabel->setWordWrap(true);
    detailsLayout->addRow("", m_timestampLabel);
    
    detailPageLayout->addLayout(detailsLayout, 1); 

    m_editEntryButton = new QPushButton("Edit Entry");
    m_editEntryButton->setObjectName("NewButton"); 
    m_editEntryButton->setEnabled(false); 
    m_editEntryButton->setMinimumWidth(100);
    
    m_viewHistoryButton = new QPushButton("View History");
    m_viewHistoryButton->setEnabled(false); 
    m_viewHistoryButton->setMinimumWidth(110);
    m_viewHistoryButton->setToolTip("View password history for this entry");
    
    m_deleteEntryButton = new QPushButton("Delete Entry");
    m_deleteEntryButton->setObjectName("DeleteButton"); 
    m_deleteEntryButton->setEnabled(false); 
    m_deleteEntryButton->setMinimumWidth(100);

    QHBoxLayout* detailButtonLayout = new QHBoxLayout();
    detailButtonLayout->setContentsMargins(0, 12, 0, 0); 
    detailButtonLayout->addStretch(1); 
    detailButtonLayout->addWidget(m_viewHistoryButton);
    detailButtonLayout->addSpacing(8);
    detailButtonLayout->addWidget(m_editEntryButton);
    detailButtonLayout->addSpacing(8);
    detailButtonLayout->addWidget(m_deleteEntryButton);

    detailPageLayout->addLayout(detailButtonLayout); 
    
    // --- Invite View Widget ---
    m_inviteViewWidget = new QWidget(this);
    QVBoxLayout* inviteLayout = new QVBoxLayout(m_inviteViewWidget);
    inviteLayout->setContentsMargins(20, 20, 20, 20);
    
    m_inviteInfoLabel = new QLabel("Select an invite to view details.", m_inviteViewWidget);
    m_inviteInfoLabel->setAlignment(Qt::AlignCenter);
    m_inviteInfoLabel->setWordWrap(true);
    m_inviteInfoLabel->setObjectName("InviteInfoLabel");
    
    m_acceptInviteButton = new QPushButton("Accept Invite", m_inviteViewWidget);
    m_acceptInviteButton->setObjectName("NewButton");
    m_acceptInviteButton->setMinimumWidth(150);
    
    m_rejectInviteButton = new QPushButton("Reject Invite", m_inviteViewWidget);
    m_rejectInviteButton->setObjectName("DeleteButton");
    m_rejectInviteButton->setMinimumWidth(150);
    
    inviteLayout->addStretch();
    inviteLayout->addWidget(m_inviteInfoLabel);
    inviteLayout->addSpacing(20);
    
    QHBoxLayout* inviteButtonLayout = new QHBoxLayout();
    inviteButtonLayout->addStretch();
    inviteButtonLayout->addWidget(m_acceptInviteButton);
    inviteButtonLayout->addSpacing(12);
    inviteButtonLayout->addWidget(m_rejectInviteButton);
    inviteButtonLayout->addStretch();
    
    inviteLayout->addLayout(inviteButtonLayout);
    inviteLayout->addStretch();
    
    connect(m_acceptInviteButton, &QPushButton::clicked, this, &MainWindow::onAcceptInviteClicked);
    connect(m_rejectInviteButton, &QPushButton::clicked, this, &MainWindow::onRejectInviteClicked);

    m_detailsStack = new QStackedWidget(this);
    m_detailsStack->addWidget(placeholderWidget); 
    m_detailsStack->addWidget(m_detailViewWidget); 
    m_detailsStack->addWidget(m_inviteViewWidget); 

    // --- Main Splitter with flexible sizing ---
    m_mainSplitter = new QSplitter(Qt::Horizontal, this);
    m_mainSplitter->addWidget(groupPaneWidget);
    m_mainSplitter->addWidget(entryPaneWidget);
    m_mainSplitter->addWidget(m_detailsStack);
    
    // Set flexible size constraints
    m_mainSplitter->setStretchFactor(0, 1);  // Groups pane
    m_mainSplitter->setStretchFactor(1, 1);  // Entries pane
    m_mainSplitter->setStretchFactor(2, 2);  // Details pane (larger)
    
    // Set reasonable initial sizes
    m_mainSplitter->setSizes({250, 250, 400});
    
    // Allow collapsing but prevent too small sizes
    m_mainSplitter->setCollapsible(0, false);
    m_mainSplitter->setCollapsible(1, false);
    m_mainSplitter->setCollapsible(2, false);
    
    setCentralWidget(m_mainSplitter);

    connect(m_groupListWidget, &QListWidget::currentItemChanged, this, &MainWindow::onGroupSelected);
    connect(m_entryListWidget, &QListWidget::currentItemChanged, this, &MainWindow::onEntrySelected);
    connect(m_copyUsernameButton, &QPushButton::clicked, this, &MainWindow::onCopyUsername);
    connect(m_copyPasswordButton, &QPushButton::clicked, this, &MainWindow::onCopyPassword);
    connect(m_showPasswordButton, &QPushButton::toggled, this, &MainWindow::onToggleShowPassword);
    
    connect(m_newGroupButton, &QPushButton::clicked, this, &MainWindow::onNewGroupClicked);
    connect(m_newEntryButton, &QPushButton::clicked, this, &MainWindow::onNewEntryClicked);
    
    connect(m_editEntryButton, &QPushButton::clicked, this, &MainWindow::onEditEntryClicked);
    connect(m_deleteEntryButton, &QPushButton::clicked, this, &MainWindow::onDeleteEntryClicked);
    connect(m_viewHistoryButton, &QPushButton::clicked, this, &MainWindow::onViewPasswordHistoryClicked);
    
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainWindow::onSearchTextChanged); 
    connect(m_settingsButton, &QPushButton::clicked, this, &MainWindow::onSettingsButtonClicked); 
}

void MainWindow::setupKeyboardShortcuts()
{
    // Ctrl+F for search
    QShortcut* searchShortcut = new QShortcut(QKeySequence("Ctrl+F"), this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        m_searchEdit->setFocus();
        m_searchEdit->selectAll();
    });
}

void MainWindow::updateWindowTitle()
{
    QString lockStatus = (m_vault && !m_vault->isLocked()) ? "🔓 Unlocked" : "🔒 Locked";
    setWindowTitle(QString("CipherMesh - %1 - %2").arg(lockStatus, m_currentUserId));
}

void MainWindow::postUnlockInit()
{
    updateWindowTitle();
    // ... (Theme logic) ...
    
    // Notify P2P service that user is authenticated and can go online
    if (m_p2pService) {
        WebRTCService* webrtcService = dynamic_cast<WebRTCService*>(m_p2pService);
        if (webrtcService) {
            QMetaObject::invokeMethod(webrtcService, "setAuthenticated", Qt::QueuedConnection, Q_ARG(bool, true));
        }
    }
    
    loadGroups();
    restoreOutgoingInvites(); // <-- NEW: Restore the retry loop!
    
    if (m_groupListWidget->count() > 0) m_groupListWidget->setCurrentRow(0);
    
    // Start auto-lock timer
    resetAutoLockTimer();
}

void MainWindow::loadGroups()
{
    m_groupListWidget->clear();
    m_entryListWidget->clear(); 
    m_entryMap.clear();
    m_pendingInviteMap.clear(); 
    m_detailsStack->setCurrentIndex(0); 
    
    QIcon groupIcon = loadSvgIcon(g_folderIconSvg, m_uiIconColor); 
    QIcon inviteIcon = loadSvgIcon(g_folderIconSvg, QColor("#ff5555")); 

    try {
        if (!m_vault || m_vault->isLocked()) return;

        auto invites = m_vault->getPendingInvites();
        for (const auto& invite : invites) {
            QString text = QString("%1 (Invite)").arg(QString::fromStdString(invite.groupName));
            QListWidgetItem* item = new QListWidgetItem(inviteIcon, text);
            item->setForeground(QColor("#ff5555")); 
            m_groupListWidget->addItem(item);
            m_pendingInviteMap[item] = invite.id; 
        }

        std::vector<std::string> groups = m_vault->getGroupNames();
        for (const std::string& groupName : groups) {
            QString displayName = QString::fromStdString(groupName);
            QListWidgetItem* item = new QListWidgetItem(groupIcon, displayName);
            m_groupListWidget->addItem(item);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error Loading Groups", e.what());
    }
}

void MainWindow::onGroupSelected(QListWidgetItem* current)
{
    m_entryListWidget->clear();
    m_entryMap.clear();
    
    if (!current || !m_vault) {
        m_newEntryButton->setEnabled(false); 
        m_detailsStack->setCurrentIndex(0);
        return;
    }

    if (m_pendingInviteMap.contains(current)) {
        m_currentSelectedInviteId = m_pendingInviteMap[current];
        m_newEntryButton->setEnabled(false);
        
        auto invites = m_vault->getPendingInvites();
        for(const auto& inv : invites) {
            if(inv.id == m_currentSelectedInviteId) {
                m_inviteInfoLabel->setText(QString("<b>Incoming Group Invite</b><br><br>"
                                                   "User <b>%1</b> wants to share the group<br>"
                                                   "<b>'%2'</b> with you.<br><br>"
                                                   "Click Accept to add this group to your vault.")
                                                   .arg(QString::fromStdString(inv.senderId))
                                                   .arg(QString::fromStdString(inv.groupName)));
                break;
            }
        }
        
        m_detailsStack->setCurrentWidget(m_inviteViewWidget);
        return;
    }

    // Get the actual group name from text, removing invite suffix if present
    QString groupNameQStr = current->text();
    if (groupNameQStr.endsWith(INVITE_SUFFIX)) {
        groupNameQStr = groupNameQStr.left(groupNameQStr.length() - INVITE_SUFFIX.length());
    }
    std::string groupName = groupNameQStr.toStdString();
    bool canEdit = m_vault->canUserEdit(groupName);
    m_newEntryButton->setEnabled(canEdit);

    m_detailsStack->setCurrentIndex(0); 
    
    try {
        if (m_vault->setActiveGroup(groupName)) {
            QIcon entryIcon = loadSvgIcon(g_keyIconSvg, m_uiIconColor); 
            std::vector<CipherMesh::Core::VaultEntry> entries = m_vault->getEntries();
            for (const auto& entry : entries) {
                QListWidgetItem* item = new QListWidgetItem(entryIcon, QString::fromStdString(entry.title));
                m_entryListWidget->addItem(item);
                m_entryMap[item] = entry;
            }
            
            // Update recent menu when group changes
            updateRecentMenu();
        }
    } catch (const std::exception& e) {
        qWarning() << "Failed to load group entries:" << e.what();
    }
}

void MainWindow::onGroupContextMenuRequested(const QPoint &pos)
{
    QListWidgetItem* item = m_groupListWidget->itemAt(pos);
    if (!item) return; 
    
    if (m_pendingInviteMap.contains(item)) return;

    m_groupListWidget->setCurrentItem(item);
    
    QMenu contextMenu(this);
    QAction* shareAction = contextMenu.addAction(loadSvgIcon(g_shareIconSvg, m_uiIconColor), "Share Group...");
    QAction* deleteAction = contextMenu.addAction(loadSvgIcon(g_trashIconSvg, m_uiIconColor), "Delete Group...");

    connect(shareAction, &QAction::triggered, this, &MainWindow::onShareGroupClicked);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteGroupClicked);

    if (m_groupListWidget->count() <= 1 || item->text() == "Personal") {
        deleteAction->setEnabled(false);
    }

    contextMenu.exec(m_groupListWidget->mapToGlobal(pos));
}

void MainWindow::onEntryContextMenuRequested(const QPoint &pos)
{
    QListWidgetItem* item = m_entryListWidget->itemAt(pos);
    if (!item) return; 

    m_entryListWidget->setCurrentItem(item);

    QMenu contextMenu(this);
    
    QAction* copyUserAction = contextMenu.addAction(loadSvgIcon(g_copyIconSvg, m_uiIconColor), "Copy Username");
    QAction* copyPassAction = contextMenu.addAction(loadSvgIcon(g_keyIconSvg, m_uiIconColor), "Copy Password");
    contextMenu.addSeparator();
    
    std::string groupName = getSelectedGroupName().toStdString();
    bool canEdit = m_vault->canUserEdit(groupName);

    QAction* editAction = contextMenu.addAction(loadSvgIcon(g_pencilIconSvg, m_uiIconColor), "Edit Entry...");
    QAction* duplicateAction = contextMenu.addAction(loadSvgIcon(g_copyIconSvg, m_uiIconColor), "Duplicate Entry");
    QAction* deleteAction = contextMenu.addAction(loadSvgIcon(g_trashIconSvg, m_uiIconColor), "Delete Entry...");
    
    if (!canEdit) {
        editAction->setEnabled(false);
        duplicateAction->setEnabled(false);
        deleteAction->setEnabled(false);
    }

    connect(copyUserAction, &QAction::triggered, this, &MainWindow::onCopyUsername);
    connect(copyPassAction, &QAction::triggered, this, &MainWindow::onCopyPassword);
    connect(editAction, &QAction::triggered, this, &MainWindow::onEditEntryClicked);
    connect(duplicateAction, &QAction::triggered, this, &MainWindow::onDuplicateEntryClicked);
    connect(deleteAction, &QAction::triggered, this, &MainWindow::onDeleteEntryClicked);

    contextMenu.exec(m_entryListWidget->mapToGlobal(pos));
}

void MainWindow::onEntrySelected(QListWidgetItem* current)
{
    if (!current || !m_vault) {
        m_detailsStack->setCurrentIndex(0); 
        
        m_editEntryButton->setEnabled(false);
        m_deleteEntryButton->setEnabled(false);
        m_viewHistoryButton->setEnabled(false);
        return;
    }

    m_editEntryButton->setEnabled(true);
    m_deleteEntryButton->setEnabled(true);
    m_viewHistoryButton->setEnabled(true);

    auto it = m_entryMap.find(current);
    if (it == m_entryMap.end()) return; 

    const CipherMesh::Core::VaultEntry& entry = it.value();
    
    // Track entry access time
    try {
        m_vault->updateEntryAccessTime(entry.id);
        updateRecentMenu(); // Update recently accessed menu
    } catch (const std::exception& e) {
        qWarning() << "Failed to update entry access time:" << e.what();
    }

    m_usernameLabel->setText(QString::fromStdString(entry.username)); 
    m_notesEdit->setText(QString::fromStdString(entry.notes)); 

    m_locationsList->clear();
    for (const auto& loc : entry.locations) {
        QString text = QString("[%1] %2").arg(QString::fromStdString(loc.type), QString::fromStdString(loc.value));
        m_locationsList->addItem(text);
    }
    
    m_passwordEdit->setText("************");
    m_passwordEdit->setEchoMode(QLineEdit::Password);
    
    m_isPasswordVisible = false;
    {
        QSignalBlocker blocker(m_showPasswordButton);
        m_showPasswordButton->setChecked(false);
    }
    m_showPasswordButton->setIcon(loadSvgIcon(g_eyeIconSvg, m_actionIconColor));
    m_showPasswordButton->setToolTip("Show Password");
    
    // Display timestamps in a more compact format to avoid cutoff
    QStringList timestampLines;
    if (entry.createdAt > 0) {
        QDateTime created = QDateTime::fromSecsSinceEpoch(entry.createdAt);
        timestampLines << QString("Created: %1").arg(created.toString("MMM dd, yyyy hh:mm"));
    }
    if (entry.lastModified > 0) {
        QDateTime modified = QDateTime::fromSecsSinceEpoch(entry.lastModified);
        timestampLines << QString("Modified: %1").arg(modified.toString("MMM dd, yyyy hh:mm"));
    }
    if (entry.lastAccessed > 0) {
        QDateTime accessed = QDateTime::fromSecsSinceEpoch(entry.lastAccessed);
        timestampLines << QString("Accessed: %1").arg(accessed.toString("MMM dd, yyyy hh:mm"));
    }
    m_timestampLabel->setText(timestampLines.join("\n"));

    m_detailsStack->setCurrentIndex(1);
}

void MainWindow::loadEntries(const std::vector<CipherMesh::Core::VaultEntry>& entries)
{
    m_entryListWidget->clear();
    m_entryMap.clear();
    m_detailsStack->setCurrentIndex(0);
    
    QIcon entryIcon = loadSvgIcon(g_keyIconSvg, m_uiIconColor); 

    for (const auto& entry : entries) {
        QListWidgetItem* item = new QListWidgetItem(entryIcon, QString::fromStdString(entry.title));
        m_entryListWidget->addItem(item);
        m_entryMap[item] = entry;
    }
    
    // Add empty state message if no entries exist
    if (m_entryListWidget->count() == 0) {
        QListWidgetItem* emptyItem = new QListWidgetItem("No entries yet - click 'New Entry' or press Ctrl+N to add one");
        emptyItem->setFlags(Qt::NoItemFlags); // Make it non-selectable
        emptyItem->setForeground(QColor("#888888"));
        m_entryListWidget->addItem(emptyItem);
    }
}

void MainWindow::onSearchTextChanged(const QString& text)
{
    if (!m_vault) return;

    if (text.isEmpty()) {
        if (m_groupListWidget->currentItem()) {
            onGroupSelected(m_groupListWidget->currentItem());
        } else {
            m_entryListWidget->clear();
            m_entryMap.clear();
        }
    } else {
        try {
            std::vector<CipherMesh::Core::VaultEntry> entries = m_vault->searchEntries(text.toStdString());
            loadEntries(entries);
            m_newEntryButton->setEnabled(false); 
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error Searching", e.what());
        }
    }
}

int MainWindow::getSelectedEntryId()
{
    QListWidgetItem* currentItem = m_entryListWidget->currentItem();
    if (!currentItem) return -1;

    auto it = m_entryMap.find(currentItem);
    if (it == m_entryMap.end()) return -1;

    return it.value().id;
}

QString MainWindow::getSelectedGroupName()
{
    QListWidgetItem* currentItem = m_groupListWidget->currentItem();
    if (!currentItem) return "";
    return currentItem->text();
}

void MainWindow::onCopyUsername()
{
    QApplication::clipboard()->setText(m_usernameLabel->text());
    
    // Show toast notification
    using namespace CipherMesh::GUI;
    Toast* toast = new Toast("Username copied to clipboard", ToastType::Success, this);
    toast->show();
}

void MainWindow::onCopyPassword()
{
    if (!m_vault) return;
    int entryId = getSelectedEntryId();
    if (entryId == -1) return;

    try {
        std::string password = m_vault->getDecryptedPassword(entryId);
        QClipboard* clipboard = QApplication::clipboard();
        clipboard->setText(QString::fromStdString(password));
        CipherMesh::Core::Crypto::secureWipe(password);
        
        // Show toast notification
        using namespace CipherMesh::GUI;
        Toast* toast = new Toast("Password copied (will clear in 30s)", ToastType::Success, this);
        toast->show();
        
        QTimer::singleShot(30000, [clipboard]() {
            if (clipboard) clipboard->clear();
        });
    } catch (const std::exception& e) {
        using namespace CipherMesh::GUI;
        Toast* toast = new Toast("Could not decrypt password", ToastType::Error, this);
        toast->show();
    }
}

void MainWindow::onToggleShowPassword(bool checked)
{
    m_isPasswordVisible = checked;
    if (!m_vault) return;
    int entryId = getSelectedEntryId();
    if (entryId == -1) return; 

    if (m_isPasswordVisible) {
        try {
            std::string password = m_vault->getDecryptedPassword(entryId);
            m_passwordEdit->setEchoMode(QLineEdit::Normal);
            m_passwordEdit->setText(QString::fromStdString(password));
            m_showPasswordButton->setIcon(loadSvgIcon(g_eyeOffIconSvg, m_actionIconColor));
            m_showPasswordButton->setToolTip("Hide Password");
            CipherMesh::Core::Crypto::secureWipe(password);
        } catch (const std::exception& e) {
            m_passwordEdit->setText("Error decrypting!");
        }
    } else {
        m_passwordEdit->setEchoMode(QLineEdit::Password);
        m_passwordEdit->setText("************");
        m_showPasswordButton->setIcon(loadSvgIcon(g_eyeIconSvg, m_actionIconColor));
        m_showPasswordButton->setToolTip("Show Password");
    }
}

void MainWindow::onNewGroupClicked()
{
    if (!m_vault) return;
    
    CipherMesh::GUI::NewGroupDialog dialog(this);
    if (dialog.exec() == QDialog::Accepted) {
        QString groupName = dialog.groupName();
        if (!groupName.isEmpty()) {
            try {
                if (m_vault->addGroup(groupName.toStdString())) {
                    loadGroups();
                    for (int i = 0; i < m_groupListWidget->count(); ++i) {
                        if (m_groupListWidget->item(i)->text() == groupName) {
                            m_groupListWidget->setCurrentRow(i);
                            break;
                        }
                    }
                } else {
                    QMessageBox::warning(this, "Error", "A group with this name already exists.");
                }
            } catch (const std::exception& e) {
                QMessageBox::critical(this, "Error", e.what());
            }
        }
    }
}

void MainWindow::onNewEntryClicked()
{
    if (!m_vault) return;
    QString currentGroupName = getSelectedGroupName();
    if (currentGroupName.isEmpty()) {
         QMessageBox::warning(this, "Error", "Please select a group first.");
         return;
    }
    // Updated constructor usage:
    NewEntryDialog dialog(m_vault, this); 
    if (dialog.exec() == QDialog::Accepted) {
        try {
            CipherMesh::Core::VaultEntry entry = dialog.getEntryData();
            std::string password = dialog.getPassword();
            if (m_vault->addEntry(entry, password)) {
                onGroupSelected(m_groupListWidget->currentItem());
            }
            CipherMesh::Core::Crypto::secureWipe(password);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error Adding Entry", e.what());
        }
    }
}

void MainWindow::onDeleteGroupClicked()
{
    if (!m_vault) return;
    QString groupName = getSelectedGroupName();
    if (groupName.isEmpty()) {
        QMessageBox::warning(this, "Error", "No group selected.");
        return;
    }
    if (m_groupListWidget->count() <= 1 || groupName == "Personal") {
        QMessageBox::warning(this, "Error", "Cannot delete the 'Personal' group or the last remaining group.");
        return;
    }
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Group",
                                  QString("Are you sure you want to permanently delete the group '%1' and all its entries?").arg(groupName),
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        try {
            if (m_vault->deleteGroup(groupName.toStdString())) {
                loadGroups(); 
            } else {
                QMessageBox::warning(this, "Error", "Could not delete the group.");
            }
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", e.what());
        }
    }
}

void MainWindow::onShareGroupClicked()
{
    QString groupName = getSelectedGroupName();
    if (groupName.isEmpty() || !m_p2pService) {
        QMessageBox::warning(this, "Error", "No group selected or P2P service is unavailable.");
        return;
    }
    ShareGroupDialog dialog(groupName, m_p2pService, m_vault, this); 
    dialog.exec();
}

void MainWindow::onDeleteEntryClicked()
{
    if (!m_vault) return;
    int entryId = getSelectedEntryId();
    if (entryId == -1) {
        QMessageBox::warning(this, "Error", "No entry selected.");
        return;
    }
    QString entryTitle = m_entryListWidget->currentItem()->text();
    QMessageBox::StandardButton reply = QMessageBox::question(this, "Delete Entry",
                                  QString("Are you sure you want to permanently delete the entry '%1'?").arg(entryTitle),
                                  QMessageBox::Yes | QMessageBox::No);
    if (reply == QMessageBox::Yes) {
        try {
            if (m_vault->deleteEntry(entryId)) {
                QString searchText = m_searchEdit->text();
                if (searchText.isEmpty()) {
                    onGroupSelected(m_groupListWidget->currentItem());
                } else {
                    onSearchTextChanged(searchText);
                }
            } else {
                QMessageBox::warning(this, "Error", "Could not delete the entry.");
            }
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error", e.what());
        }
    }
}

void MainWindow::onEditEntryClicked()
{
    if (!m_vault) return;
    int entryId = getSelectedEntryId();
    if (entryId == -1) {
        QMessageBox::warning(this, "Error", "No entry selected to edit.");
        return;
    }
    const CipherMesh::Core::VaultEntry& entry = m_entryMap[m_entryListWidget->currentItem()];
    // Updated constructor usage:
    NewEntryDialog dialog(m_vault, entry, this); 
    if (dialog.exec() == QDialog::Accepted) {
        try {
            CipherMesh::Core::VaultEntry updatedEntry = dialog.getEntryData();
            std::string newPassword = dialog.getPassword(); 
            if (m_vault->updateEntry(updatedEntry, newPassword)) {
                QString searchText = m_searchEdit->text();
                if (searchText.isEmpty()) {
                    onGroupSelected(m_groupListWidget->currentItem());
                } else {
                    onSearchTextChanged(searchText);
                }
            }
            CipherMesh::Core::Crypto::secureWipe(newPassword);
        } catch (const std::exception& e) {
            QMessageBox::critical(this, "Error Updating Entry", e.what());
        }
    }
}

void MainWindow::onDuplicateEntryClicked()
{
    if (!m_vault) return;
    int entryId = getSelectedEntryId();
    if (entryId == -1) {
        QMessageBox::warning(this, "Error", "No entry selected to duplicate.");
        return;
    }
    
    try {
        const CipherMesh::Core::VaultEntry& originalEntry = m_entryMap[m_entryListWidget->currentItem()];
        
        // Create a copy with modified title
        CipherMesh::Core::VaultEntry duplicateEntry = originalEntry;
        duplicateEntry.id = -1; // New entry
        duplicateEntry.title += COPY_SUFFIX.toStdString();
        
        // Get the password
        std::string password = m_vault->getDecryptedPassword(entryId);
        
        // Add the duplicate entry
        if (m_vault->addEntry(duplicateEntry, password)) {
            CipherMesh::Core::Crypto::secureWipe(password);
            
            // Refresh the list
            QString searchText = m_searchEdit->text();
            if (searchText.isEmpty()) {
                onGroupSelected(m_groupListWidget->currentItem());
            } else {
                onSearchTextChanged(searchText);
            }
            
            // Show success message
            auto* toast = new CipherMesh::GUI::Toast("Entry duplicated successfully!", CipherMesh::GUI::ToastType::Success, this);
            toast->show();
        } else {
            CipherMesh::Core::Crypto::secureWipe(password);
            QMessageBox::warning(this, "Error", "Could not duplicate the entry.");
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Error Duplicating Entry", e.what());
    }
}

void MainWindow::onChangeMasterPasswordClicked()
{
    if (!m_vault || m_vault->isLocked()) {
        QMessageBox::warning(this, "Error", "Vault is locked.");
        return;
    }
    ChangePasswordDialog dialog(m_vault, this);
    dialog.exec();
}

void MainWindow::onLockVault()
{
    if (m_vault && !m_vault->isLocked()) {
        // Set authenticated to false before locking
        if (m_p2pService) {
            WebRTCService* webrtcService = dynamic_cast<WebRTCService*>(m_p2pService);
            if (webrtcService) {
                QMetaObject::invokeMethod(webrtcService, "setAuthenticated", Qt::QueuedConnection, Q_ARG(bool, false));
            }
        }
        
        m_vault->lock();
        this->hide();
        QApplication::quit(); 
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    QApplication::quit();
    event->accept();
}

QIcon MainWindow::loadSvgIcon(const QByteArray& svgData, const QColor& color)
{
    QSvgRenderer renderer(svgData);
    if (!renderer.isValid()) {
        return QIcon();
    }
    QSize size = renderer.defaultSize();
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);
    {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing);
        renderer.render(&painter);
    } 
    QPainter maskPainter(&pixmap);
    maskPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
    maskPainter.fillRect(pixmap.rect(), color);
    return QIcon(pixmap);
}

// Handle data request from receiver (Alice's logic)
// This replaces the fragile RAM-based m_pendingKeys
void MainWindow::handleDataRequested(const QString& requesterId, const QString& groupName) {
    if (!m_vault || !m_p2pService) return;

    qDebug() << "Processing data request from" << requesterId << "for group" << groupName;

    // A. Verify we have the group
    if (!m_vault->groupExists(groupName.toStdString())) {
        qWarning() << "Requested group not found:" << groupName;
        return;
    }

    // B. Export FRESH data from Vault
    try {
        std::vector<unsigned char> key = m_vault->getGroupKey(groupName.toStdString());
        std::vector<CipherMesh::Core::VaultEntry> entries = m_vault->exportGroupEntries(groupName.toStdString());
        
        // C. Send the data directly via P2P (not through invite flow)
        m_p2pService->sendGroupData(requesterId.toStdString(), groupName.toStdString(), key, entries);
        
        CipherMesh::Core::Crypto::secureWipe(key);
        
        qDebug() << "Successfully sent group data to" << requesterId;
    } catch (const std::exception& e) {
        qWarning() << "Error exporting data:" << e.what();
    }
}

void MainWindow::restoreOutgoingInvites() {
    if (!m_vault || !m_p2pService) return;
    
    // Cast to WebRTCService to access specific queueInvite method
    // (Ideally add to IP2PService interface, but dynamic_cast is fine for now)
    auto* p2p = dynamic_cast<WebRTCService*>(m_p2pService);
    if (!p2p) return;

    std::vector<std::string> groups = m_vault->getGroupNames();
    
    for (const std::string& groupName : groups) {
        auto members = m_vault->getGroupMembers(groupName);
        for (const auto& member : members) {
            // If we sent an invite but they haven't accepted yet
            if (member.status == "pending") {
                try {
                    // Re-export the latest data to ensure they get the freshest version
                    std::vector<unsigned char> key = m_vault->getGroupKey(groupName);
                    std::vector<CipherMesh::Core::VaultEntry> entries = m_vault->exportGroupEntries(groupName);
                    
                    // Queue it up!
                    p2p->queueInvite(groupName, member.userId, key, entries);
                    
                    CipherMesh::Core::Crypto::secureWipe(key);
                } catch (...) {
                    qWarning() << "Failed to restore invite for" << QString::fromStdString(member.userId);
                }
            }
        }
    }
}

void MainWindow::handleInviteResponse(const QString& userId, const QString& groupName, bool accepted) {
    if (!m_vault) return;
    
    qDebug() << "DEBUG: Received invite response from" << userId << "for group" << groupName << "- Accepted:" << accepted;
    
    try {
        // Update the member status in the database
        if (accepted) {
            m_vault->updateGroupMemberStatus(groupName.toStdString(), userId.toStdString(), "accepted");
        } else {
            // Remove the member from the group since they rejected
            m_vault->removeGroupMember(groupName.toStdString(), userId.toStdString());
        }
        
        // Reload the groups view to reflect the status change
        loadGroups();
        
        qDebug() << "DEBUG: Updated member status for" << userId << "in group" << groupName;
    } catch (const std::exception& e) {
        qWarning() << "ERROR: Failed to update member status:" << e.what();
    }
}

void MainWindow::handleConnectionStatusChanged(bool connected) {
    if (connected) {
        m_connectionStatusLabel->setText("● Connected");
        m_connectionStatusLabel->setToolTip("Connected to signaling server");
    } else {
        m_connectionStatusLabel->setText("● Offline");
        m_connectionStatusLabel->setToolTip("Disconnected from signaling server - Reconnecting...");
    }
}

// --- AUTO-LOCK TIMER IMPLEMENTATION ---
void MainWindow::setupAutoLockTimer() {
    m_autoLockTimer = new QTimer(this);
    m_autoLockTimer->setSingleShot(true);
    connect(m_autoLockTimer, &QTimer::timeout, this, &MainWindow::onAutoLockTimeout);
}

void MainWindow::resetAutoLockTimer() {
    if (!m_vault || m_vault->isLocked()) {
        return; // Don't set timer if vault is locked
    }
    
    int timeout = m_vault->getAutoLockTimeout();
    if (timeout == 0) {
        // Auto-lock disabled
        m_autoLockTimer->stop();
    } else {
        // Set timer for timeout minutes
        m_autoLockTimer->start(timeout * 60 * 1000); // Convert minutes to milliseconds
    }
}

void MainWindow::onAutoLockTimeout() {
    if (m_vault && !m_vault->isLocked()) {
        onLockVault();
    }
}

void MainWindow::onAutoLockTimeoutChanged(int minutes) {
    resetAutoLockTimer();
}
void MainWindow::onViewPasswordHistoryClicked() {
    if (!m_vault || m_vault->isLocked()) {
        return;
    }
    
    QListWidgetItem* currentItem = m_entryListWidget->currentItem();
    if (!currentItem) {
        return;
    }
    
    auto it = m_entryMap.find(currentItem);
    if (it == m_entryMap.end()) {
        return;
    }
    
    const CipherMesh::Core::VaultEntry& entry = it.value();
    
    // Update access time
    if (m_vault) {
        m_vault->updateEntryAccessTime(entry.id);
        updateRecentMenu(); // Update the recent menu
    }
    
    // Open password history dialog
    CipherMesh::GUI::PasswordHistoryDialog dialog(m_vault, entry.id, entry.title, this);
    dialog.exec();
}

void MainWindow::updateRecentMenu() {
    if (!m_recentMenu || !m_vault || m_vault->isLocked()) {
        return;
    }
    
    m_recentMenu->clear();
    
    // Only show recent entries if a group is selected
    QString currentGroup = getSelectedGroupName();
    if (currentGroup.isEmpty()) {
        QAction* emptyAction = m_recentMenu->addAction("No group selected");
        emptyAction->setEnabled(false);
        return;
    }
    
    try {
        auto recentEntries = m_vault->getRecentlyAccessedEntries(10);
        
        if (recentEntries.empty()) {
            QAction* emptyAction = m_recentMenu->addAction("No recently accessed entries in this group");
            emptyAction->setEnabled(false);
            return;
        }
        
        for (const auto& entry : recentEntries) {
            QString displayText = QString::fromStdString(entry.title);
            QAction* action = m_recentMenu->addAction(loadSvgIcon(g_keyIconSvg, m_uiIconColor), displayText);
            action->setData(QVariant::fromValue(entry.id));
            connect(action, &QAction::triggered, this, &MainWindow::onRecentEntrySelected);
        }
    } catch (const std::exception& e) {
        qWarning() << "Failed to load recently accessed entries:" << e.what();
    }
}

void MainWindow::onRecentEntrySelected() {
    QAction* action = qobject_cast<QAction*>(sender());
    if (!action || !m_vault) {
        return;
    }
    
    int entryId = action->data().toInt();
    
    // Find and select the entry in the current list
    for (int j = 0; j < m_entryListWidget->count(); ++j) {
        QListWidgetItem* item = m_entryListWidget->item(j);
        auto it = m_entryMap.find(item);
        if (it != m_entryMap.end() && it.value().id == entryId) {
            m_entryListWidget->setCurrentRow(j);
            break;
        }
    }
}
