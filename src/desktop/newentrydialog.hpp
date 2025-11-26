#pragma once

#include <QDialog>
#include <QMap>
#include <QListWidgetItem>
#include "vault_entry.hpp" // <-- FIXED (removed "core/")

class QLineEdit;
class QLabel;
class QPushButton;
class QTextEdit; 
class QListWidget;
class QProgressBar;
class BreachChecker;
namespace CipherMesh { namespace Core { class Vault; } } 

class NewEntryDialog : public QDialog {
    Q_OBJECT

public:
    // --- UPDATED CONSTRUCTORS ---
    explicit NewEntryDialog(CipherMesh::Core::Vault* vault, QWidget *parent = nullptr);
    explicit NewEntryDialog(CipherMesh::Core::Vault* vault, const CipherMesh::Core::VaultEntry& entry, QWidget *parent = nullptr);
    
    ~NewEntryDialog(); 

    CipherMesh::Core::VaultEntry getEntryData() const;
    std::string getPassword() const;

private slots:
    void onConfirmPasswordChanged(const QString& text);
    void onPasswordChanged(const QString& text);
    void onCreateOrSaveClicked();
    void onGeneratePassword();
    void onAddLocation();    
    void onEditLocation();   
    void onRemoveLocation(); 
    void updateStrengthMeter();
    void onCheckBreach();

private:
    void setupUi();
    void loadLocations(const std::vector<CipherMesh::Core::Location>& locations);

    CipherMesh::Core::Vault* m_vault; 
    bool m_isEditMode;
    int m_editingEntryId;
    std::vector<CipherMesh::Core::Location> m_locations; 

    QLabel* m_titleLabel;
    QLineEdit* m_titleEdit;
    QLineEdit* m_usernameEdit;
    QTextEdit* m_notesEdit; 
    QListWidget* m_locationsList; 
    
    QLabel* m_passwordLabel;
    QLineEdit* m_passwordEdit;
    QLineEdit* m_confirmEdit;
    QLabel* m_messageLabel;
    QPushButton* m_createButton;
    QPushButton* m_cancelButton;
    
    QPushButton* m_editLocationButton; 
    QPushButton* m_removeLocationButton; 
    
    QProgressBar* m_strengthBar;
    QLabel* m_strengthLabel;
    QPushButton* m_checkBreachButton;
    QLabel* m_breachStatusLabel;
    BreachChecker* m_breachChecker;
    
    QLineEdit* m_totpSecretEdit;
    
    // Map to store temporary location data in the list
    QMap<QListWidgetItem*, CipherMesh::Core::Location> m_locationItemMap;
};