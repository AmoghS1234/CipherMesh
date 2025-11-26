#pragma once

#include <QMainWindow>
#include <QMap>
#include <QListWidgetItem>
#include "vault_entry.hpp"
#include "ip2pservice.hpp" 

class QListWidget;
class QStackedWidget;
class QLineEdit;
class QPushButton;
class QSplitter;
class QWidget;
class QColor;
class QByteArray;
class QPoint;
class QTextEdit; 
class QLabel; 
class QThread;
class QProgressBar;
class BreachChecker;

namespace CipherMesh { namespace Core { class Vault; } }
namespace CipherMesh { namespace P2P { class IP2PService; } } 

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const QString& userId, QWidget *parent = nullptr);
    ~MainWindow();

    void setVault(CipherMesh::Core::Vault* vault);

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void postUnlockInit();
    void loadGroups();
    void onGroupSelected(QListWidgetItem* current);
    void onEntrySelected(QListWidgetItem* current);
    void onCopyUsername();
    void onCopyPassword();
    void onCopyTOTPCode();
    void onToggleShowPassword(bool checked);
    void onNewGroupClicked();
    void onDeleteGroupClicked();
    void onShareGroupClicked(); 
    void onNewEntryClicked();
    void onEditEntryClicked();
    void onDuplicateEntryClicked();
    void onDeleteEntryClicked();
    void onViewPasswordHistoryClicked();
    void onCheckPasswordBreach();
    
    void onGroupContextMenuRequested(const QPoint &pos); 
    void onEntryContextMenuRequested(const QPoint &pos); 
    void onSearchTextChanged(const QString& text); 
    
    void onChangeMasterPasswordClicked();
    void onSettingsButtonClicked(); 
    void onLockVault(); 

    void handleIncomingInvite(const QString& senderId, const QString& groupName);
    void handleInviteCancelled(const QString& senderId);
    void handlePeerOnline(const QString& userId);
    void handleDataRequested(const QString& requesterId, const QString& groupName);
    void handleInviteResponse(const QString& userId, const QString& groupName, bool accepted);
    void handleConnectionStatusChanged(bool connected);
    void handleGroupData(const QString& senderId,
                         const QString& groupName, 
                         const std::vector<unsigned char>& key, 
                         const std::vector<CipherMesh::Core::VaultEntry>& entries);

    void onAcceptInviteClicked();
    void onRejectInviteClicked();
    void onLocationDoubleClicked(QListWidgetItem* item);
    void restoreOutgoingInvites();
    
    // Theme Slot
    void setTheme(const QString& themeId, const QString& styleSheet);

private:
    void setupUi();
    void setupKeyboardShortcuts(); // NEW: Setup keyboard shortcuts
    void updateIcons(); // <-- NEW: Refreshes icons with correct colors
    void updateWindowTitle(); // NEW: Update window title with lock status
    void updateRecentMenu(); // NEW: Update recently accessed entries menu

    void loadEntries(const std::vector<CipherMesh::Core::VaultEntry>& entries); 
    QIcon loadSvgIcon(const QByteArray& svgData, const QColor& color);
    int getSelectedEntryId();
    QString getSelectedGroupName();

    QSplitter* m_mainSplitter;
    QListWidget* m_groupListWidget;
    QPushButton* m_newGroupButton;
    QPushButton* m_settingsButton;
    QLabel* m_connectionStatusLabel;  // NEW: Connection status indicator
    
    QLineEdit* m_searchEdit; 
    QListWidget* m_entryListWidget;
    QPushButton* m_newEntryButton;
    
    QStackedWidget* m_detailsStack;
    QWidget* m_detailViewWidget;
    
    QWidget* m_inviteViewWidget;
    QLabel* m_inviteInfoLabel;
    QPushButton* m_acceptInviteButton;
    QPushButton* m_rejectInviteButton;
    int m_currentSelectedInviteId = -1;

    QLabel* m_usernameLabel; 
    QLineEdit* m_passwordEdit;
    QListWidget* m_locationsList; 
    QTextEdit* m_notesEdit;
    QLabel* m_timestampLabel; // NEW: Display entry timestamps
    
    QLabel* m_totpCodeLabel;
    QProgressBar* m_totpTimerBar;
    QLabel* m_totpTimerLabel;
    QPushButton* m_copyTOTPButton;
    QTimer* m_totpRefreshTimer;
    
    QPushButton* m_copyUsernameButton;
    QPushButton* m_copyPasswordButton;
    QPushButton* m_showPasswordButton;
    QPushButton* m_viewHistoryButton;
    QPushButton* m_checkBreachButton;
    QPushButton* m_editEntryButton;
    QPushButton* m_deleteEntryButton;
    
    BreachChecker* m_breachChecker;
    QLabel* m_breachStatusLabel;
    
    CipherMesh::Core::Vault* m_vault;
    CipherMesh::P2P::IP2PService* m_p2pService; 
    QThread* m_p2pThread; 

    QMap<QListWidgetItem*, CipherMesh::Core::VaultEntry> m_entryMap;
    QMap<QListWidgetItem*, int> m_pendingInviteMap;

    bool m_isPasswordVisible;
    QString m_currentUserId;
    
    // --- NEW: Icon Colors ---
    QColor m_actionIconColor; // For buttons like "New Entry" (might be dark or light)
    QColor m_uiIconColor;     // For UI elements like Settings/Folders (usually matches text)
    
    // --- NEW: Auto-lock timer ---
    QTimer* m_autoLockTimer;
    void resetAutoLockTimer();
    void setupAutoLockTimer();
    
    // --- NEW: Recently accessed menu ---
    QMenu* m_recentMenu;
    void onRecentEntrySelected();
    
private slots:
    void onAutoLockTimeout();
    void onAutoLockTimeoutChanged(int minutes);
    void refreshTOTPCode();
};