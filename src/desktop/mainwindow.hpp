#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QProgressBar>
#include <QSplitter>
#include <QTimer>
#include <QMap>
#include <QThread>
#include <QTextEdit> // [FIX] Added missing include
#include <vector>
#include <memory>

// Forward declarations to avoid circular includes
namespace CipherMesh { 
    namespace Core { 
        class Vault; 
        struct VaultEntry; 
    }
    // [FIX] Correct namespace for P2P service
    namespace P2P {
        class IP2PService;
    }
}

// Forward declare BreachChecker
class BreachChecker;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(CipherMesh::Core::Vault* vault, QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    // Groups
    void onNewGroupClicked();
    void onGroupSelected(QListWidgetItem* current);
    void onGroupContextMenuRequested(const QPoint &pos);
    void onDeleteGroupClicked();
    void onRenameGroupClicked();
    void onShareGroupClicked();

    // Entries
    void onNewEntryClicked();
    void onEntrySelected(QListWidgetItem* current);
    void onEntryContextMenuRequested(const QPoint &pos);
    void onEditEntryClicked();
    void onDuplicateEntryClicked();
    void onDeleteEntryClicked();
    void onSearchTextChanged(const QString& text);
    
    // Actions
    void onCopyUsername();
    void onCopyPassword();
    void onCopyTOTPCode();
    void onToggleShowPassword(bool checked);
    void onChangeMasterPasswordClicked();
    void onLockVault();
    void onSettingsButtonClicked();
    void onAutoLockTimeoutChanged(int minutes);
    void onViewPasswordHistoryClicked();
    void onRecentEntrySelected();

    // P2P / Invites
    void onAcceptInviteClicked();
    void onRejectInviteClicked();
    
    // Breach
    void onCheckPasswordBreach();

    // UI Misc
    void refreshTOTPCode();
    void onAutoLockTimeout();
    void onLocationDoubleClicked(QListWidgetItem* item);

private:
    void setupUi();
    void setupKeyboardShortcuts();
    void updateIcons();
    void updateWindowTitle();
    void loadGroups();
    void loadEntries(const std::vector<CipherMesh::Core::VaultEntry>& entries);
    void postUnlockInit();
    void restoreOutgoingInvites();

    // Helpers
    int getSelectedEntryId();
    QString getSelectedGroupName();
    void setTheme(const QString& themeId, const QString& styleSheet);
    void setupAutoLockTimer();
    void resetAutoLockTimer();
    void updateRecentMenu();
    QIcon loadSvgIcon(const QByteArray& svgData, const QColor& color);

    // P2P Handlers
    void handleIncomingInvite(const QString& senderId, const QString& groupName);
    void handleInviteCancelled(const QString& senderId);
    void handlePeerOnline(const QString& userId);
    void handleGroupData(const QString& senderId, const QString& groupName, const std::vector<unsigned char>& key, const std::vector<CipherMesh::Core::VaultEntry>& entries);
    void handleDataRequested(const QString& requesterId, const QString& groupName, const QString& requesterPubKey);
    void handleInviteResponse(const QString& userId, const QString& groupName, bool accepted);
    void handleConnectionStatusChanged(bool connected);

    // Member Variables
    CipherMesh::Core::Vault* m_vault;
    
    // [FIX] Use namespaced type
    CipherMesh::P2P::IP2PService* m_p2pService;
    
    BreachChecker* m_breachChecker;

    QString m_currentUserId;
    bool m_isPasswordVisible;
    QColor m_actionIconColor;
    QColor m_uiIconColor;

    // UI Pointers
    QSplitter* m_mainSplitter;
    QListWidget* m_groupListWidget;
    QListWidget* m_entryListWidget;
    QStackedWidget* m_detailsStack;
    QLineEdit* m_searchEdit;
    
    // Buttons
    QPushButton* m_newGroupButton;
    QPushButton* m_newEntryButton;
    QPushButton* m_settingsButton;
    QPushButton* m_editEntryButton;
    QPushButton* m_deleteEntryButton;
    QPushButton* m_checkBreachButton;
    QPushButton* m_viewHistoryButton;
    
    // Detail View
    QWidget* m_detailViewWidget;
    QLabel* m_usernameLabel;
    QLineEdit* m_passwordEdit;
    QTextEdit* m_notesEdit;
    QListWidget* m_locationsList;
    QPushButton* m_copyUsernameButton;
    QPushButton* m_copyPasswordButton;
    QPushButton* m_showPasswordButton;
    
    // TOTP UI
    QLabel* m_totpCodeLabel;
    QProgressBar* m_totpTimerBar;
    QLabel* m_totpTimerLabel;
    QPushButton* m_copyTOTPButton;

    // Invite View
    QWidget* m_inviteViewWidget;
    QLabel* m_inviteInfoLabel;
    QPushButton* m_acceptInviteButton;
    QPushButton* m_rejectInviteButton;

    // Status / Labels
    QLabel* m_timestampLabel;
    QLabel* m_connectionStatusLabel;
    QLabel* m_breachStatusLabel;
    
    // Timers & Logic
    QTimer* m_autoLockTimer;
    QTimer* m_totpRefreshTimer;
    QThread* m_p2pThread;
    QMenu* m_recentMenu;

    // State maps
    QMap<QListWidgetItem*, CipherMesh::Core::VaultEntry> m_entryMap;
    QMap<QListWidgetItem*, int> m_pendingInviteMap;
    int m_currentSelectedInviteId = -1;
};