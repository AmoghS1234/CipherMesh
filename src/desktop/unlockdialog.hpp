#pragma once

#include <QDialog>
#include <string>

class QStackedWidget;
class QLineEdit;
class QLabel;

namespace CipherMesh { namespace Core { class Vault; } }

class UnlockDialog : public QDialog {
    Q_OBJECT

public:
    explicit UnlockDialog(CipherMesh::Core::Vault* vault, QWidget *parent = nullptr);
    ~UnlockDialog();

private slots:
    void onUnlockClicked();
    void onCreateClicked();

private:
    bool isVaultInitialized();
    void createUnlockView();
    void createCreateView();

    CipherMesh::Core::Vault* m_vault;
    std::string m_vaultPath;

    QStackedWidget* m_stack;
    QLineEdit* m_unlockPasswordEdit;
    QLabel* m_unlockMessageLabel;
    QLineEdit* m_createUsernameEdit;
    QLineEdit* m_createPasswordEdit;
    QLineEdit* m_confirmPasswordEdit;
    QLabel* m_createMessageLabel;
};