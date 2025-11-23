#include "mainwindow.hpp"
#include "unlockdialog.hpp"
#include "vault.hpp"
#include "themes.hpp" // Modular header
#include "crypto.hpp"
#include <QApplication>
#include <QStyleFactory>
#include <cstdio>

CipherMesh::Core::Vault g_vault;

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Load default theme initially
    app.setStyleSheet(CipherMesh::Themes::getDefault().styleSheet);
    
    app.setQuitOnLastWindowClosed(false);

    // Initial window with temporary user ID
    MainWindow* w = nullptr;
    bool initialUnlock = true;

    while (true) {
        UnlockDialog dialog(&g_vault);
        if (dialog.exec() == QDialog::Accepted) {
            // Vault is now unlocked, get the user ID from vault
            std::string userId = g_vault.getUserId();
            
            // If user ID doesn't exist, generate one
            if (userId.empty()) {
                // Generate user ID with default username "user"
                std::vector<unsigned char> randomBytes = CipherMesh::Core::Crypto::randomBytes(8);
                std::string hexSuffix;
                for (unsigned char byte : randomBytes) {
                    char hex[3];
                    snprintf(hex, sizeof(hex), "%02x", byte);
                    hexSuffix += hex;
                }
                userId = "user_" + hexSuffix;
                g_vault.setUserId(userId);
            }
            
            if (initialUnlock) {
                // Create MainWindow with the loaded user ID
                w = new MainWindow(QString::fromStdString(userId));
                w->setVault(&g_vault);
                initialUnlock = false;
            }
            w->show();
            QApplication::exec(); 
            if (g_vault.isLocked()) continue;
            else {
                delete w;
                return 0;
            }
        } else {
            if (w) delete w;
            return 0;
        }
    }
    return 0;
}