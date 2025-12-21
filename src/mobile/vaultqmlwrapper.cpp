#include "vaultqmlwrapper.hpp"
#include <QDebug>

VaultQmlWrapper::VaultQmlWrapper(CipherMesh::Core::Vault* vault, QObject* parent)
    : QObject(parent), m_vault(vault)
{
}

bool VaultQmlWrapper::unlockVault(const QString& password) {
    if (!m_vault) return false;
    
    // In production, verify against DB. For now, assume simple verify or load
    // Using the verifyMasterPassword method from Vault
    bool success = m_vault->verifyMasterPassword(password.toStdString());
    
    // Note: If Vault::loadVault wasn't called yet in main, you might need logic here
    // to call loadVault if not initialized. Assuming main_mobile.cpp loaded it.
    
    if (success) {
        emit lockStatusChanged();
    }
    return success;
}

void VaultQmlWrapper::lockVault() {
    if (m_vault) m_vault->lock();
    emit lockStatusChanged();
}

bool VaultQmlWrapper::isLocked() const {
    return m_vault ? m_vault->isLocked() : true;
}

QVariantList VaultQmlWrapper::getEntries() {
    QVariantList list;
    if (!m_vault || m_vault->isLocked()) return list;

    try {
        // Assuming we are just showing the active group's entries or all if simplified
        // Mobile UI might need group selection logic later. 
        // For V1, let's grab the "Personal" group or active group.
        
        if (!m_vault->isGroupActive()) {
            m_vault->setActiveGroup("Personal"); 
        }

        auto entries = m_vault->getEntries();
        for (const auto& entry : entries) {
            QVariantMap map;
            map["id"] = entry.id;
            map["title"] = QString::fromStdString(entry.title);
            map["username"] = QString::fromStdString(entry.username);
            map["notes"] = QString::fromStdString(entry.notes);
            // Don't send password unless requested for security? 
            // Or send it if this is for the ViewDetails page.
            list.append(map);
        }
    } catch (const std::exception& e) {
        qWarning() << "Error fetching entries for QML:" << e.what();
    }
    return list;
}

bool VaultQmlWrapper::addEntry(const QString& title, const QString& username, const QString& password, const QString& notes) {
    if (!m_vault || m_vault->isLocked()) return false;

    CipherMesh::Core::VaultEntry entry;
    entry.title = title.toStdString();
    entry.username = username.toStdString();
    entry.notes = notes.toStdString();
    entry.id = -1; // New entry
    
    bool success = m_vault->addEntry(entry, password.toStdString());
    if (success) {
        emit entryAdded(); // Tells QML List to refresh
    }
    return success;
}