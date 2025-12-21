#ifndef VAULTQMLWRAPPER_HPP
#define VAULTQMLWRAPPER_HPP

#include <QObject>
#include <QVariant>
#include <QList>
#include "../core/vault.hpp"

class VaultQmlWrapper : public QObject {
    Q_OBJECT
    // Properties accessible inside QML
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY lockStatusChanged)

public:
    explicit VaultQmlWrapper(CipherMesh::Core::Vault* vault, QObject* parent = nullptr);

    // -- Exposed Methods (Callable from JavaScript/QML) --
    
    Q_INVOKABLE bool unlockVault(const QString& password);
    Q_INVOKABLE void lockVault();
    
    // Returns a list of objects { "id": 1, "title": "Google", "username": "..." }
    Q_INVOKABLE QVariantList getEntries(); 
    
    Q_INVOKABLE bool addEntry(const QString& title, const QString& username, const QString& password, const QString& notes);

    bool isLocked() const;

signals:
    void lockStatusChanged();
    void entryAdded(); // Signal to tell UI to refresh list

private:
    CipherMesh::Core::Vault* m_vault;
};

#endif // VAULTQMLWRAPPER_HPP