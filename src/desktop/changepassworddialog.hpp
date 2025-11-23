#pragma once

#include <QDialog>

namespace CipherMesh { namespace Core { class Vault; } }

class QLineEdit;
class QLabel;

class ChangePasswordDialog : public QDialog {
    Q_OBJECT

public:
    explicit ChangePasswordDialog(CipherMesh::Core::Vault* vault, QWidget *parent = nullptr);

private slots:
    void onOkClicked();

private:
    CipherMesh::Core::Vault* m_vault;
    QLineEdit* m_currentPasswordEdit;
    QLineEdit* m_newPasswordEdit;
    QLineEdit* m_confirmPasswordEdit;
    QLabel* m_messageLabel;
};