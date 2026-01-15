#include "mainwindow.hpp"
#include "unlockdialog.hpp"
#include "vault.hpp"
#include "themes.hpp"
#include "crypto.hpp"
#include <QApplication>
#include <QStyleFactory>
#include <QStandardPaths>
#include <QDir>
#include <cstdio>
#include <vector>
#include <string>

int main(int argc, char *argv[])
{
    // 1. Initialize Application FIRST
    QApplication app(argc, argv);
    app.setStyle(QStyleFactory::create("Fusion"));
    
    // Load default theme
    app.setStyleSheet(CipherMesh::Themes::getDefault().styleSheet);
    
    // Keep app running even if a dialog closes, until we explicitly quit
    app.setQuitOnLastWindowClosed(true);

    // 2. Determine Database Path
    // We use AppDataLocation (e.g., ~/.local/share/CipherMesh or %APPDATA%/CipherMesh)
    QString dataPath = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir dir(dataPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    QString dbFilePath = dir.filePath("ciphermesh.db");

    // 3. Create Vault and Connect
    CipherMesh::Core::Vault vault;

    // [CRITICAL] Connect to the DB file immediately.
    // This allows vault.hasUsers() to return true if the file exists and has data.
    vault.connect(dbFilePath.toStdString());

    MainWindow* w = nullptr;
    bool initialUnlock = true;

    while (true) {
        // UnlockDialog now gets the path automatically from the vault
        UnlockDialog dialog(&vault);
        
        // Show the dialog (Login or Setup depending on vault state)
        if (dialog.exec() == QDialog::Accepted) {
            
            // Vault is now unlocked. Ensure a User ID exists.
            std::string userId = vault.getUserId();
            
            // If user ID doesn't exist (e.g., legacy DB or fresh setup glitch), generate one
            if (userId.empty()) {
                std::vector<unsigned char> randomBytes = CipherMesh::Core::Crypto::randomBytes(8);
                std::string hexSuffix;
                for (unsigned char byte : randomBytes) {
                    char hex[3];
                    snprintf(hex, sizeof(hex), "%02x", byte);
                    hexSuffix += hex;
                }
                userId = "user_" + hexSuffix;
                vault.setUserId(userId);
            }
            
            // Create MainWindow only once (or recreate if we want a fresh state)
            if (initialUnlock) {
                // We pass &vault. The MainWindow can get the userId via vault->getUserId() if needed.
                w = new MainWindow(&vault);
                initialUnlock = false;
            }
            
            w->show();
            
            // Enter the main event loop (this blocks until the main window closes)
            QApplication::exec(); 
            
            // 4. Handle Lock vs Exit
            // If the loop finished but vault is Locked (user clicked 'Lock'), show UnlockDialog again.
            if (vault.isLocked()) {
                if (w) { 
                    delete w; 
                    w = nullptr; 
                }
                initialUnlock = true; // Reset flag to recreate MainWindow next time
                continue;
            } else {
                // User closed the window without locking (Exit)
                delete w;
                return 0; 
            }
        } else {
            // User cancelled the unlock dialog (clicked 'X')
            if (w) delete w;
            return 0;
        }
    }
    return 0;
}