#pragma once

#include <QDialog>
#include <QList> 
#include <QString>
#include <QMap>
#include <QListWidgetItem>
#include "ip2pservice.hpp" 

class QListWidget;
class QPushButton;
class QLabel;
class QCheckBox; // <-- NEW

namespace CipherMesh { namespace Core { class Vault; } }

struct UIGroupMember {
    QString id;
    QString displayName;
    QString status;
    QString role; // "owner", "admin", "member"
};
Q_DECLARE_METATYPE(UIGroupMember)

class ShareGroupDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShareGroupDialog(const QString& groupName, 
                              CipherMesh::P2P::IP2PService* p2pService, 
                              CipherMesh::Core::Vault* vault,
                              QWidget* parent = nullptr);
    ~ShareGroupDialog();

signals:
    void internalGroupMembersUpdated(const QList<UIGroupMember>& members);
    void internalInviteStatus(bool success, const QString& message);
    void internalRemoveStatus(bool success, const QString& message);

private slots:
    void onInviteClicked();
    void onRemoveClicked();
    void onUserSelectionChanged();
    void updateMemberList(const QList<UIGroupMember>& members);
    void showStatusMessage(bool success, const QString& message);

    // --- NEW SLOTS ---
    void onContextMenuRequested(const QPoint &pos);
    void onToggleAdminOnly(bool checked);
    void promoteUser();
    void demoteUser();

private:
    void setupUi();
    void setupServiceCallbacks();
    void loadMembers(); 
    void loadPermissions(); // Load checkbox state

    QString m_groupName;
    CipherMesh::P2P::IP2PService* m_p2pService; 
    CipherMesh::Core::Vault* m_vault;
    
    QMap<QListWidgetItem*, QString> m_itemToUserIdMap;

    QLabel* m_titleLabel;
    QListWidget* m_memberListWidget;
    QPushButton* m_inviteButton;
    QPushButton* m_removeButton;
    QLabel* m_statusLabel;
    QCheckBox* m_adminOnlyCheckbox; // <-- NEW
};