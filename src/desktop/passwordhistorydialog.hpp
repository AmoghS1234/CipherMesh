#pragma once

#include <QDialog>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <vector>
#include <string>

namespace CipherMesh {
namespace Core {
    class Vault;
}
}

namespace CipherMesh {
namespace GUI {

struct PasswordHistoryItem {
    int id;
    long long changedAt;
    std::string password;
};

class PasswordHistoryDialog : public QDialog {
    Q_OBJECT

public:
    explicit PasswordHistoryDialog(Core::Vault* vault, int entryId, const std::string& entryName, QWidget* parent = nullptr);

private slots:
    void onCopyPassword();
    void onShowPassword();

private:
    void loadHistory();
    QString formatTimestamp(long long timestamp);

    Core::Vault* m_vault;
    int m_entryId;
    std::string m_entryName;
    
    QListWidget* m_historyList;
    QPushButton* m_copyButton;
    QPushButton* m_showButton;
    QPushButton* m_closeButton;
    
    std::vector<PasswordHistoryItem> m_historyItems;
};

} // namespace GUI
} // namespace CipherMesh
