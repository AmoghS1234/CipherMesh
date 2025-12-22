#include "vaultqmlwrapper.hpp"
#include "../utils/passwordstrength.hpp"
#include "../utils/totp.hpp"
#include "../p2p_webrtc/webrtcservice.hpp"
#include <QDebug>
#include <QGuiApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QDateTime>
#include <random>
#include <algorithm>

VaultQmlWrapper::VaultQmlWrapper(CipherMesh::Core::Vault* vault, QObject* parent)
    : QObject(parent), 
      m_vault(vault),
      m_p2pService(nullptr),
      m_autoLockTimer(nullptr),
      m_totpTimer(nullptr),
      m_autoLockMinutes(5),
      m_currentGroupName("Personal")
{
    setupTimers();
}

VaultQmlWrapper::~VaultQmlWrapper() {
    if (m_p2pService) {
        delete m_p2pService;
    }
}

void VaultQmlWrapper::setupTimers() {
    // Auto-lock timer
    m_autoLockTimer = new QTimer(this);
    connect(m_autoLockTimer, &QTimer::timeout, this, &VaultQmlWrapper::onAutoLockTimeout);
    
    // TOTP refresh timer (updates every second)
    m_totpTimer = new QTimer(this);
    m_totpTimer->setInterval(1000);
    connect(m_totpTimer, &QTimer::timeout, this, &VaultQmlWrapper::onTOTPTimerTick);
    m_totpTimer->start();
}

void VaultQmlWrapper::resetAutoLockTimer() {
    if (m_autoLockMinutes > 0 && !isLocked()) {
        m_autoLockTimer->start(m_autoLockMinutes * 60 * 1000);
    }
}

void VaultQmlWrapper::onAutoLockTimeout() {
    lockVault();
}

void VaultQmlWrapper::onTOTPTimerTick() {
    emit totpTimerTick();
}

// -- Vault Operations --

bool VaultQmlWrapper::unlockVault(const QString& password) {
    if (!m_vault) return false;
    
    // Get the database path from the property set in main
    QString dbPath = property("dbPath").toString();
    bool vaultExists = property("vaultExists").toBool();
    
    if (dbPath.isEmpty()) {
        emit errorOccurred("Database path not set");
        return false;
    }
    
    bool success = false;
    
    // If vault doesn't exist, create it
    if (!vaultExists) {
        success = m_vault->createNewVault(dbPath.toStdString(), password.toStdString());
        if (success) {
            // Update the property so we know vault exists now
            setProperty("vaultExists", true);
            // Create default Personal group
            m_vault->addGroup("Personal");
            m_vault->setActiveGroup("Personal");
            m_currentGroupName = "Personal";
        }
    } else {
        // Vault exists, try to load it
        success = m_vault->loadVault(dbPath.toStdString(), password.toStdString());
    }
    
    if (success) {
        emit lockStatusChanged();
        resetAutoLockTimer();
    }
    
    return success;
}

void VaultQmlWrapper::lockVault() {
    if (m_vault) {
        m_vault->lock();
        m_autoLockTimer->stop();
        emit lockStatusChanged();
    }
}

bool VaultQmlWrapper::changeMasterPassword(const QString& oldPassword, const QString& newPassword) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        // First verify the old password
        if (!m_vault->verifyMasterPassword(oldPassword.toStdString())) {
            emit errorOccurred("Current password is incorrect");
            return false;
        }
        
        // Change to new password
        bool success = m_vault->changeMasterPassword(newPassword.toStdString());
        return success;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Failed to change password: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::isLocked() const {
    return m_vault ? m_vault->isLocked() : true;
}

// -- Group Management --

QVariantList VaultQmlWrapper::getGroups() {
    QVariantList list;
    if (!m_vault || m_vault->isLocked()) return list;
    
    try {
        auto groups = m_vault->getGroupNames();
        for (const auto& group : groups) {
            QVariantMap map;
            map["name"] = QString::fromStdString(group);
            list.append(map);
        }
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error fetching groups: %1").arg(e.what()));
    }
    return list;
}

bool VaultQmlWrapper::setActiveGroup(const QString& groupName) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        bool success = m_vault->setActiveGroup(groupName.toStdString());
        if (success) {
            m_currentGroupName = groupName;
            emit currentGroupChanged();
        }
        resetAutoLockTimer();
        return success;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error setting active group: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::createGroup(const QString& groupName) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        bool success = m_vault->addGroup(groupName.toStdString());
        if (success) {
            emit groupsChanged();
        }
        resetAutoLockTimer();
        return success;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error creating group: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::deleteGroup(const QString& groupName) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        bool success = m_vault->deleteGroup(groupName.toStdString());
        if (success) {
            emit groupsChanged();
        }
        resetAutoLockTimer();
        return success;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error deleting group: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::renameGroup(const QString& oldName, const QString& newName) {
    // Group renaming is not implemented in the core Vault API
    // This functionality would require extending the Vault class
    emit errorOccurred("Renaming groups is not currently supported");
    Q_UNUSED(oldName);
    Q_UNUSED(newName);
    return false;
}

QString VaultQmlWrapper::currentGroup() const {
    // Since the Vault class doesn't expose the active group name directly,
    // we'll track it ourselves in the wrapper
    if (!m_vault || m_vault->isLocked()) return QString();
    
    // Return the internally tracked group name
    return m_currentGroupName;
}

// -- Entry Operations --

QVariantList VaultQmlWrapper::getEntries() {
    QVariantList list;
    if (!m_vault || m_vault->isLocked()) return list;

    try {
        if (!m_vault->isGroupActive()) {
            // Default to Personal group if none is active
            m_vault->setActiveGroup("Personal");
        }

        auto entries = m_vault->getEntries();
        for (const auto& entry : entries) {
            QVariantMap map;
            map["id"] = entry.id;
            map["title"] = QString::fromStdString(entry.title);
            map["username"] = QString::fromStdString(entry.username);
            map["notes"] = QString::fromStdString(entry.notes);
            map["entryType"] = QString::fromStdString(entry.entry_type);
            map["hasTotp"] = !entry.totp_secret.empty();
            map["createdAt"] = static_cast<qint64>(entry.createdAt);
            map["lastModified"] = static_cast<qint64>(entry.lastModified);
            map["lastAccessed"] = static_cast<qint64>(entry.lastAccessed);
            list.append(map);
        }
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error fetching entries: %1").arg(e.what()));
    }
    return list;
}

QVariantMap VaultQmlWrapper::getEntry(int entryId) {
    QVariantMap map;
    if (!m_vault || m_vault->isLocked()) return map;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (const auto& entry : entries) {
            if (entry.id == entryId) {
                // Get the decrypted password
                std::string decryptedPassword = m_vault->getDecryptedPassword(entryId);
                
                map["id"] = entry.id;
                map["title"] = QString::fromStdString(entry.title);
                map["username"] = QString::fromStdString(entry.username);
                map["notes"] = QString::fromStdString(entry.notes);
                map["password"] = QString::fromStdString(decryptedPassword);
                map["entryType"] = QString::fromStdString(entry.entry_type);
                map["totpSecret"] = QString::fromStdString(entry.totp_secret);
                map["hasTotp"] = !entry.totp_secret.empty();
                map["createdAt"] = static_cast<qint64>(entry.createdAt);
                map["lastModified"] = static_cast<qint64>(entry.lastModified);
                map["lastAccessed"] = static_cast<qint64>(entry.lastAccessed);
                
                // Get locations
                QVariantList locations;
                for (const auto& loc : entry.locations) {
                    QVariantMap locMap;
                    locMap["id"] = loc.id;
                    locMap["type"] = QString::fromStdString(loc.type);
                    locMap["value"] = QString::fromStdString(loc.value);
                    locations.append(locMap);
                }
                map["locations"] = locations;
                break;
            }
        }
        
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error fetching entry: %1").arg(e.what()));
    }
    return map;
}

bool VaultQmlWrapper::addEntry(const QString& title, const QString& username, 
                               const QString& password, const QString& notes,
                               const QString& totpSecret, const QString& entryType) {
    if (!m_vault || m_vault->isLocked()) return false;

    CipherMesh::Core::VaultEntry entry;
    entry.title = title.toStdString();
    entry.username = username.toStdString();
    entry.notes = notes.toStdString();
    entry.totp_secret = totpSecret.toStdString();
    entry.entry_type = entryType.toStdString();
    entry.id = -1; // New entry
    entry.createdAt = QDateTime::currentSecsSinceEpoch();
    entry.lastModified = entry.createdAt;
    
    bool success = m_vault->addEntry(entry, password.toStdString());
    if (success) {
        emit entryAdded();
        resetAutoLockTimer();
    }
    return success;
}

bool VaultQmlWrapper::updateEntry(int entryId, const QString& title, const QString& username,
                                  const QString& password, const QString& notes,
                                  const QString& totpSecret, const QString& entryType) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (auto& entry : entries) {
            if (entry.id == entryId) {
                entry.title = title.toStdString();
                entry.username = username.toStdString();
                entry.notes = notes.toStdString();
                entry.totp_secret = totpSecret.toStdString();
                entry.entry_type = entryType.toStdString();
                entry.lastModified = QDateTime::currentSecsSinceEpoch();
                
                bool success = m_vault->updateEntry(entry, password.toStdString());
                if (success) {
                    emit entryUpdated();
                    resetAutoLockTimer();
                }
                return success;
            }
        }
        emit errorOccurred("Entry not found");
        return false;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error updating entry: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::deleteEntry(int entryId) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        bool success = m_vault->deleteEntry(entryId);
        if (success) {
            emit entryDeleted();
            resetAutoLockTimer();
        }
        return success;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error deleting entry: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::duplicateEntry(int entryId) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (const auto& entry : entries) {
            if (entry.id == entryId) {
                // Get the decrypted password
                std::string decryptedPassword = m_vault->getDecryptedPassword(entryId);
                
                CipherMesh::Core::VaultEntry newEntry = entry;
                newEntry.title = entry.title + " (Copy)";
                newEntry.id = -1; // New entry
                newEntry.createdAt = QDateTime::currentSecsSinceEpoch();
                newEntry.lastModified = newEntry.createdAt;
                
                bool success = m_vault->addEntry(newEntry, decryptedPassword);
                if (success) {
                    emit entryAdded();
                    resetAutoLockTimer();
                }
                return success;
            }
        }
        emit errorOccurred("Entry not found");
        return false;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error duplicating entry: %1").arg(e.what()));
        return false;
    }
}

QVariantList VaultQmlWrapper::searchEntries(const QString& query) {
    QVariantList list;
    if (!m_vault || m_vault->isLocked() || query.isEmpty()) return list;
    
    try {
        auto entries = m_vault->searchEntries(query.toStdString());
        for (const auto& entry : entries) {
            QVariantMap map;
            map["id"] = entry.id;
            map["title"] = QString::fromStdString(entry.title);
            map["username"] = QString::fromStdString(entry.username);
            map["notes"] = QString::fromStdString(entry.notes);
            map["entryType"] = QString::fromStdString(entry.entry_type);
            map["hasTotp"] = !entry.totp_secret.empty();
            list.append(map);
        }
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error searching entries: %1").arg(e.what()));
    }
    return list;
}

// -- Location Management --

QVariantList VaultQmlWrapper::getLocations(int entryId) {
    QVariantList list;
    if (!m_vault || m_vault->isLocked()) return list;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (const auto& entry : entries) {
            if (entry.id == entryId) {
                for (const auto& loc : entry.locations) {
                    QVariantMap map;
                    map["id"] = loc.id;
                    map["type"] = QString::fromStdString(loc.type);
                    map["value"] = QString::fromStdString(loc.value);
                    list.append(map);
                }
                break;
            }
        }
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error fetching locations: %1").arg(e.what()));
    }
    return list;
}

bool VaultQmlWrapper::addLocation(int entryId, const QString& type, const QString& value) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (auto& entry : entries) {
            if (entry.id == entryId) {
                // Get the decrypted password to re-encrypt
                std::string decryptedPassword = m_vault->getDecryptedPassword(entryId);
                
                // Add new location
                CipherMesh::Core::Location loc(-1, type.toStdString(), value.toStdString());
                entry.locations.push_back(loc);
                entry.lastModified = QDateTime::currentSecsSinceEpoch();
                
                bool success = m_vault->updateEntry(entry, decryptedPassword);
                if (success) {
                    emit entryUpdated();
                    resetAutoLockTimer();
                }
                return success;
            }
        }
        emit errorOccurred("Entry not found");
        return false;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error adding location: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::updateLocation(int entryId, int locationId, const QString& type, const QString& value) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (auto& entry : entries) {
            if (entry.id == entryId) {
                // Get the decrypted password to re-encrypt
                std::string decryptedPassword = m_vault->getDecryptedPassword(entryId);
                
                // Find and update the location
                bool found = false;
                for (auto& loc : entry.locations) {
                    if (loc.id == locationId) {
                        loc.type = type.toStdString();
                        loc.value = value.toStdString();
                        found = true;
                        break;
                    }
                }
                
                if (!found) {
                    emit errorOccurred("Location not found");
                    return false;
                }
                
                entry.lastModified = QDateTime::currentSecsSinceEpoch();
                
                bool success = m_vault->updateEntry(entry, decryptedPassword);
                if (success) {
                    emit entryUpdated();
                    resetAutoLockTimer();
                }
                return success;
            }
        }
        emit errorOccurred("Entry not found");
        return false;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error updating location: %1").arg(e.what()));
        return false;
    }
}

bool VaultQmlWrapper::deleteLocation(int entryId, int locationId) {
    if (!m_vault || m_vault->isLocked()) return false;
    
    try {
        // Get all entries and find the one with matching ID
        auto entries = m_vault->getEntries();
        for (auto& entry : entries) {
            if (entry.id == entryId) {
                // Get the decrypted password to re-encrypt
                std::string decryptedPassword = m_vault->getDecryptedPassword(entryId);
                
                // Find and delete the location
                auto it = std::remove_if(entry.locations.begin(), entry.locations.end(),
                    [locationId](const CipherMesh::Core::Location& loc) {
                        return loc.id == locationId;
                    });
                
                if (it == entry.locations.end()) {
                    emit errorOccurred("Location not found");
                    return false;
                }
                
                entry.locations.erase(it, entry.locations.end());
                entry.lastModified = QDateTime::currentSecsSinceEpoch();
                
                bool success = m_vault->updateEntry(entry, decryptedPassword);
                if (success) {
                    emit entryUpdated();
                    resetAutoLockTimer();
                }
                return success;
            }
        }
        emit errorOccurred("Entry not found");
        return false;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error deleting location: %1").arg(e.what()));
        return false;
    }
}

// -- Password Operations --

QString VaultQmlWrapper::generatePassword(int length, bool upper, bool lower, bool numbers, bool symbols, const QString& customSymbols) {
    std::string charset;
    if (upper) charset += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    if (lower) charset += "abcdefghijklmnopqrstuvwxyz";
    if (numbers) charset += "0123456789";
    if (symbols) {
        if (!customSymbols.isEmpty()) {
            charset += customSymbols.toStdString();
        } else {
            charset += "!@#$%^&*()_+-=[]{}|;:,.<>?";
        }
    }
    
    if (charset.empty()) {
        charset = "abcdefghijklmnopqrstuvwxyz"; // Default to lowercase
    }
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charset.size() - 1);
    
    std::string password;
    for (int i = 0; i < length; ++i) {
        password += charset[dis(gen)];
    }
    
    return QString::fromStdString(password);
}

int VaultQmlWrapper::calculatePasswordStrength(const QString& password) {
    auto info = CipherMesh::Utils::PasswordStrengthCalculator::calculate(password.toStdString());
    return info.score;
}

QString VaultQmlWrapper::getPasswordStrengthText(int strength) {
    if (strength < 20) return "Very Weak";
    if (strength < 40) return "Weak";
    if (strength < 60) return "Fair";
    if (strength < 80) return "Strong";
    return "Very Strong";
}

QVariantList VaultQmlWrapper::getPasswordHistory(int entryId) {
    QVariantList list;
    if (!m_vault || m_vault->isLocked()) return list;
    
    try {
        auto history = m_vault->getPasswordHistory(entryId);
        for (const auto& item : history) {
            QVariantMap map;
            map["id"] = item.id;
            map["changedAt"] = static_cast<qint64>(item.changedAt);
            map["password"] = QString::fromStdString(item.encryptedPassword); // Already decrypted by vault.getPasswordHistory()
            list.append(map);
        }
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error fetching password history: %1").arg(e.what()));
    }
    return list;
}

// -- TOTP Operations --

QString VaultQmlWrapper::generateTOTPCode(const QString& secret) {
    if (secret.isEmpty()) return QString();
    
    try {
        std::string code = CipherMesh::Utils::TOTP::generate(secret.toStdString());
        return QString::fromStdString(code);
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error generating TOTP code: %1").arg(e.what()));
        return QString();
    }
}

int VaultQmlWrapper::getTOTPTimeRemaining() {
    return CipherMesh::Utils::TOTP::getSecondsRemaining();
}

// -- Clipboard Operations --

void VaultQmlWrapper::copyToClipboard(const QString& text) {
    QGuiApplication::clipboard()->setText(text);
    resetAutoLockTimer();
}

// -- P2P Sharing --

bool VaultQmlWrapper::initializeP2P(const QString& userId) {
    if (m_p2pService) {
        return true; // Already initialized
    }
    
    try {
        m_currentUserId = userId;
        m_p2pService = new WebRTCService("wss://ciphermesh-signal-server.onrender.com", 
                                          userId.toStdString(), 
                                          this);
        
        // Connect signals here when P2P events occur
        // This would require extending the P2P service to emit Qt signals
        
        emit p2pConnectionChanged();
        return true;
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error initializing P2P: %1").arg(e.what()));
        return false;
    }
}

void VaultQmlWrapper::shareGroup(const QString& groupName, const QString& recipientUserId) {
    if (!m_p2pService || !m_vault || m_vault->isLocked()) {
        emit errorOccurred("P2P not initialized or vault is locked");
        return;
    }
    
    try {
        m_p2pService->sendInvite(recipientUserId.toStdString(), groupName.toStdString());
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error sharing group: %1").arg(e.what()));
    }
}

QVariantList VaultQmlWrapper::getPendingInvites() {
    QVariantList list;
    if (!m_vault || m_vault->isLocked()) return list;
    
    try {
        auto invites = m_vault->getPendingInvites();
        for (const auto& invite : invites) {
            QVariantMap map;
            map["id"] = invite.id;
            map["senderId"] = QString::fromStdString(invite.senderId);
            map["groupName"] = QString::fromStdString(invite.groupName);
            map["timestamp"] = static_cast<qint64>(invite.timestamp);
            map["status"] = QString::fromStdString(invite.status);
            list.append(map);
        }
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error fetching invites: %1").arg(e.what()));
    }
    return list;
}

void VaultQmlWrapper::acceptInvite(int inviteId) {
    if (!m_vault || m_vault->isLocked()) return;
    
    try {
        // Accept invite - P2P service will handle the handshake and data transfer
        // The received group data will trigger the appropriate P2P callbacks
        // This is a stub until the P2P service interface is fully integrated
        Q_UNUSED(inviteId);
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error accepting invite: %1").arg(e.what()));
    }
}

void VaultQmlWrapper::rejectInvite(int inviteId) {
    if (!m_vault || m_vault->isLocked()) return;
    
    try {
        m_vault->deletePendingInvite(inviteId);
        resetAutoLockTimer();
    } catch (const std::exception& e) {
        emit errorOccurred(QString("Error rejecting invite: %1").arg(e.what()));
    }
}

bool VaultQmlWrapper::p2pConnected() const {
    return m_p2pService != nullptr;
}

// -- Settings --

void VaultQmlWrapper::setAutoLockTimeout(int minutes) {
    m_autoLockMinutes = minutes;
    if (!isLocked()) {
        resetAutoLockTimer();
    }
}

int VaultQmlWrapper::getAutoLockTimeout() const {
    return m_autoLockMinutes;
}

// -- Breach Checking --

void VaultQmlWrapper::checkPasswordBreach(const QString& password) {
    // Hash the password with SHA-1
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(password.toUtf8());
    QString sha1Hash = hash.result().toHex().toUpper();
    
    // Take first 5 characters for k-anonymity
    QString prefix = sha1Hash.left(5);
    QString suffix = sha1Hash.mid(5);
    
    // Call HaveIBeenPwned API
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QUrl url(QString("https://api.pwnedpasswords.com/range/%1").arg(prefix));
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "CipherMesh-Mobile");
    
    QNetworkReply* reply = manager->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, suffix]() {
        if (reply->error() == QNetworkReply::NoError) {
            QString response = QString::fromUtf8(reply->readAll());
            QStringList lines = response.split('\n');
            
            bool found = false;
            int count = 0;
            
            for (const QString& line : lines) {
                QStringList parts = line.split(':');
                if (parts.size() == 2 && parts[0].trimmed() == suffix) {
                    found = true;
                    count = parts[1].trimmed().toInt();
                    break;
                }
            }
            
            emit breachCheckResult(found, count);
        } else {
            emit errorOccurred("Failed to check password breach");
        }
        reply->deleteLater();
    });
}
