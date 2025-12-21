#ifndef VAULTQMLWRAPPER_HPP
#define VAULTQMLWRAPPER_HPP

#include <QObject>
#include <QVariant>
#include <QList>
#include <QTimer>
#include "../core/vault.hpp"

namespace CipherMesh { namespace P2P { class IP2PService; } }

class VaultQmlWrapper : public QObject {
    Q_OBJECT
    // Properties accessible inside QML
    Q_PROPERTY(bool isLocked READ isLocked NOTIFY lockStatusChanged)
    Q_PROPERTY(QString currentGroup READ currentGroup NOTIFY currentGroupChanged)
    Q_PROPERTY(bool p2pConnected READ p2pConnected NOTIFY p2pConnectionChanged)

public:
    explicit VaultQmlWrapper(CipherMesh::Core::Vault* vault, QObject* parent = nullptr);
    ~VaultQmlWrapper();

    // -- Vault Operations --
    Q_INVOKABLE bool unlockVault(const QString& password);
    Q_INVOKABLE void lockVault();
    Q_INVOKABLE bool changeMasterPassword(const QString& oldPassword, const QString& newPassword);
    
    // -- Group Management --
    Q_INVOKABLE QVariantList getGroups();
    Q_INVOKABLE bool setActiveGroup(const QString& groupName);
    Q_INVOKABLE bool createGroup(const QString& groupName);
    Q_INVOKABLE bool deleteGroup(const QString& groupName);
    Q_INVOKABLE bool renameGroup(const QString& oldName, const QString& newName);
    
    // -- Entry Operations --
    Q_INVOKABLE QVariantList getEntries();
    Q_INVOKABLE QVariantMap getEntry(int entryId);
    Q_INVOKABLE bool addEntry(const QString& title, const QString& username, 
                              const QString& password, const QString& notes,
                              const QString& totpSecret, const QString& entryType);
    Q_INVOKABLE bool updateEntry(int entryId, const QString& title, const QString& username,
                                 const QString& password, const QString& notes,
                                 const QString& totpSecret, const QString& entryType);
    Q_INVOKABLE bool deleteEntry(int entryId);
    Q_INVOKABLE bool duplicateEntry(int entryId);
    Q_INVOKABLE QVariantList searchEntries(const QString& query);
    
    // -- Location Management --
    Q_INVOKABLE QVariantList getLocations(int entryId);
    Q_INVOKABLE bool addLocation(int entryId, const QString& type, const QString& value);
    Q_INVOKABLE bool updateLocation(int entryId, int locationId, const QString& type, const QString& value);
    Q_INVOKABLE bool deleteLocation(int entryId, int locationId);
    
    // -- Password Operations --
    Q_INVOKABLE QString generatePassword(int length, bool upper, bool lower, bool numbers, bool symbols, const QString& customSymbols);
    Q_INVOKABLE int calculatePasswordStrength(const QString& password);
    Q_INVOKABLE QString getPasswordStrengthText(int strength);
    Q_INVOKABLE QVariantList getPasswordHistory(int entryId);
    
    // -- TOTP Operations --
    Q_INVOKABLE QString generateTOTPCode(const QString& secret);
    Q_INVOKABLE int getTOTPTimeRemaining();
    
    // -- Clipboard Operations --
    Q_INVOKABLE void copyToClipboard(const QString& text);
    
    // -- P2P Sharing --
    Q_INVOKABLE bool initializeP2P(const QString& userId);
    Q_INVOKABLE void shareGroup(const QString& groupName, const QString& recipientUserId);
    Q_INVOKABLE QVariantList getPendingInvites();
    Q_INVOKABLE void acceptInvite(int inviteId);
    Q_INVOKABLE void rejectInvite(int inviteId);
    
    // -- Settings --
    Q_INVOKABLE void setAutoLockTimeout(int minutes);
    Q_INVOKABLE int getAutoLockTimeout() const;
    
    // -- Breach Checking --
    Q_INVOKABLE void checkPasswordBreach(const QString& password);
    
    // -- Properties --
    bool isLocked() const;
    QString currentGroup() const;
    bool p2pConnected() const;

signals:
    void lockStatusChanged();
    void entryAdded();
    void entryUpdated();
    void entryDeleted();
    void currentGroupChanged();
    void groupsChanged();
    void p2pConnectionChanged();
    void inviteReceived(const QString& senderId, const QString& groupName);
    void breachCheckResult(bool isBreached, int count);
    void totpTimerTick();
    void errorOccurred(const QString& message);

private slots:
    void onAutoLockTimeout();
    void onTOTPTimerTick();

private:
    CipherMesh::Core::Vault* m_vault;
    CipherMesh::P2P::IP2PService* m_p2pService;
    QTimer* m_autoLockTimer;
    QTimer* m_totpTimer;
    int m_autoLockMinutes;
    QString m_currentUserId;
    
    void resetAutoLockTimer();
    void setupTimers();
};

#endif // VAULTQMLWRAPPER_HPP