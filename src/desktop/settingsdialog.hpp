#pragma once

#include <QDialog>
#include <QString>

class QLineEdit;
class QPushButton;
class QLabel;
class QComboBox;

namespace CipherMesh { namespace Core { class Vault; } }

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(const QString& userId, CipherMesh::Core::Vault* vault, QWidget *parent = nullptr);
    ~SettingsDialog() = default;

signals:
    void themeChanged(const QString& themeId);
    void autoLockTimeoutChanged(int minutes);

private slots:
    void onCopyUserId();
    void onShowQRCode();
    void onThemeChanged(int index);
    void onAutoLockChanged(int index);

private:
    void setupUi();
    
    QString m_userId;
    CipherMesh::Core::Vault* m_vault;
    
    QLineEdit* m_userIdEdit;
    QPushButton* m_copyUserIdButton;
    QPushButton* m_showQRButton;
    QComboBox* m_themeComboBox;
    QComboBox* m_autoLockComboBox;
};
