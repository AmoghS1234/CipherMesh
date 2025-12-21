#include "database.hpp"
#include <sodium.h>

// Qt SQL Includes
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QSqlRecord>
#include <QVariant>
#include <QDateTime>
#include <QDebug>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>

#include <stdexcept>
#include <ctime>
#include <iostream>

namespace CipherMesh {
namespace Core {

// Helper to throw DB exceptions from Qt errors
inline void check_qt_error(const QSqlQuery& query) {
    if (query.lastError().isValid()) {
        throw DBException("SQL Error: " + query.lastError().text().toStdString());
    }
}

// Helper: Convert std::vector<unsigned char> to QByteArray
inline QByteArray toQByteArray(const std::vector<unsigned char>& vec) {
    return QByteArray(reinterpret_cast<const char*>(vec.data()), static_cast<int>(vec.size()));
}

// Helper: Convert QByteArray to std::vector<unsigned char>
inline std::vector<unsigned char> toVector(const QByteArray& ba) {
    return std::vector<unsigned char>(ba.begin(), ba.end());
}

Database::Database() {
    // Check if the connection already exists to avoid warnings
    if (QSqlDatabase::contains("CipherMeshConnection")) {
        m_db = QSqlDatabase::database("CipherMeshConnection");
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", "CipherMeshConnection");
    }
}

Database::~Database() {
    close();
}

void Database::open(const std::string& path) {
    if (m_db.isOpen()) {
        m_db.close();
    }

    // Ensure the directory exists
    QString dbPath = QString::fromStdString(path);
    QFileInfo fileInfo(dbPath);
    QDir dir = fileInfo.absoluteDir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            throw DBException("Cannot create database directory: " + dir.path().toStdString());
        }
    }

    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        throw DBException("Cannot open database: " + m_db.lastError().text().toStdString());
    }

    // Enforce Foreign Keys support in SQLite
    QSqlQuery query(m_db);
    if (!query.exec("PRAGMA foreign_keys = ON;")) {
        throw DBException("Failed to enable foreign keys");
    }
}

void Database::close() {
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void Database::exec(const std::string& sql) {
    QSqlQuery query(m_db);
    if (!query.exec(QString::fromStdString(sql))) {
        throw DBException("SQL Exec Error: " + query.lastError().text().toStdString());
    }
}

void Database::createTables() {
    exec(R"( CREATE TABLE IF NOT EXISTS vault_metadata ( key TEXT PRIMARY KEY, value BLOB NOT NULL ); )");
    exec(R"( CREATE TABLE IF NOT EXISTS groups ( id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL, owner_id TEXT DEFAULT 'me' ); )");
    exec(R"( CREATE TABLE IF NOT EXISTS group_keys ( group_id INTEGER PRIMARY KEY, encrypted_group_key BLOB NOT NULL, FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE ); )");
    
    // Updated entries table with timestamps
    exec(R"( 
        CREATE TABLE IF NOT EXISTS entries ( 
            id INTEGER PRIMARY KEY AUTOINCREMENT, 
            group_id INTEGER NOT NULL, 
            title TEXT NOT NULL, 
            username TEXT, 
            notes TEXT, 
            encrypted_password BLOB NOT NULL, 
            created_at INTEGER DEFAULT 0,
            last_modified INTEGER DEFAULT 0,
            last_accessed INTEGER DEFAULT 0,
            password_expiry INTEGER DEFAULT 0,
            totp_secret TEXT DEFAULT '',
            entry_type TEXT DEFAULT 'password',
            FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE 
        ); 
    )");
    
    // Migrations: Add columns if they don't exist
    // QtSql doesn't throw on simple exec failures unless checked, but we check specifically.
    QSqlQuery query(m_db);
    
    // Migration 1: totp_secret
    if (!query.exec("ALTER TABLE entries ADD COLUMN totp_secret TEXT DEFAULT '';")) {
        // Ignore "duplicate column name" error, throw others if critical, mostly safe to ignore for migrations
    }
    
    // Migration 2: entry_type
    if (!query.exec("ALTER TABLE entries ADD COLUMN entry_type TEXT DEFAULT 'password';")) {
        // Ignore error if column exists
    }
    
    exec(R"( CREATE TABLE IF NOT EXISTS locations ( id INTEGER PRIMARY KEY AUTOINCREMENT, entry_id INTEGER NOT NULL, type TEXT NOT NULL, value TEXT NOT NULL, FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE ); )");
    
    // Password history table
    exec(R"(
        CREATE TABLE IF NOT EXISTS password_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            entry_id INTEGER NOT NULL,
            encrypted_password BLOB NOT NULL,
            changed_at INTEGER NOT NULL,
            FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE
        );
    )");
    
    // Pending invites
    exec(R"( 
        CREATE TABLE IF NOT EXISTS pending_invites ( 
            id INTEGER PRIMARY KEY AUTOINCREMENT, 
            sender_id TEXT NOT NULL, 
            group_name TEXT NOT NULL, 
            payload_json TEXT NOT NULL, 
            timestamp INTEGER,
            status TEXT DEFAULT 'pending'
        ); 
    )");

    exec(R"( CREATE TABLE IF NOT EXISTS group_members ( id INTEGER PRIMARY KEY AUTOINCREMENT, group_id INTEGER NOT NULL, user_id TEXT NOT NULL, role TEXT DEFAULT 'member', status TEXT DEFAULT 'accepted', FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE, UNIQUE(group_id, user_id) ); )");
    exec(R"( CREATE TABLE IF NOT EXISTS group_settings ( group_id INTEGER PRIMARY KEY, admins_only_write INTEGER DEFAULT 0, FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE ); )");
}

// --- METADATA & GROUPS ---

void Database::storeMetadata(const std::string& key, const std::vector<unsigned char>& value) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO vault_metadata (key, value) VALUES (?, ?);");
    query.addBindValue(QString::fromStdString(key));
    query.addBindValue(toQByteArray(value));
    
    if (!query.exec()) check_qt_error(query);
}

std::vector<unsigned char> Database::getMetadata(const std::string& key) {
    QSqlQuery query(m_db);
    query.prepare("SELECT value FROM vault_metadata WHERE key = ?;");
    query.addBindValue(QString::fromStdString(key));
    
    if (!query.exec()) check_qt_error(query);
    
    if (query.next()) {
        return toVector(query.value(0).toByteArray());
    }
    throw DBException("Metadata key not found: " + key);
}

void Database::storeEncryptedGroup(const std::string& name, const std::vector<unsigned char>& encryptedKey, const std::string& ownerId) {
    if (!m_db.transaction()) throw DBException("Failed to start transaction");

    try {
        QSqlQuery q1(m_db);
        q1.prepare("INSERT INTO groups (name, owner_id) VALUES (?, ?);");
        q1.addBindValue(QString::fromStdString(name));
        q1.addBindValue(QString::fromStdString(ownerId));
        if (!q1.exec()) throw DBException("Failed to insert group name: " + q1.lastError().text().toStdString());
        
        QVariant groupId = q1.lastInsertId();

        QSqlQuery q2(m_db);
        q2.prepare("INSERT INTO group_keys (group_id, encrypted_group_key) VALUES (?, ?);");
        q2.addBindValue(groupId);
        q2.addBindValue(toQByteArray(encryptedKey));
        if (!q2.exec()) throw DBException("Failed to insert group key: " + q2.lastError().text().toStdString());

        QSqlQuery q3(m_db);
        q3.prepare("INSERT INTO group_settings (group_id, admins_only_write) VALUES (?, 0);");
        q3.addBindValue(groupId);
        if (!q3.exec()) throw DBException("Failed to insert group settings");

        m_db.commit();
    } catch (...) {
        m_db.rollback();
        throw;
    }
}

std::vector<unsigned char> Database::getEncryptedGroupKey(const std::string& name, int& groupId) {
    QSqlQuery query(m_db);
    query.prepare("SELECT g.id, gk.encrypted_group_key FROM groups g JOIN group_keys gk ON g.id = gk.group_id WHERE g.name = ?;");
    query.addBindValue(QString::fromStdString(name));
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        groupId = query.value(0).toInt();
        return toVector(query.value(1).toByteArray());
    }
    throw DBException("Group not found: " + name);
}

std::vector<unsigned char> Database::getEncryptedGroupKeyById(int groupId) {
    QSqlQuery query(m_db);
    query.prepare("SELECT encrypted_group_key FROM group_keys WHERE group_id = ?;");
    query.addBindValue(groupId);
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        return toVector(query.value(0).toByteArray());
    }
    throw DBException("Group key not found for ID");
}

std::map<int, std::vector<unsigned char>> Database::getAllEncryptedGroupKeys() {
    std::map<int, std::vector<unsigned char>> keys;
    QSqlQuery query(m_db);
    
    if (!query.exec("SELECT group_id, encrypted_group_key FROM group_keys;")) {
        check_qt_error(query);
    }

    while (query.next()) {
        int gid = query.value(0).toInt();
        keys[gid] = toVector(query.value(1).toByteArray());
    }
    return keys;
}

void Database::updateEncryptedGroupKey(int groupId, const std::vector<unsigned char>& newKey) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE group_keys SET encrypted_group_key = ? WHERE group_id = ?;");
    query.addBindValue(toQByteArray(newKey));
    query.addBindValue(groupId);
    
    if (!query.exec()) check_qt_error(query);
}

std::vector<std::string> Database::getAllGroupNames() {
    std::vector<std::string> names;
    QSqlQuery query(m_db);
    
    if (!query.exec("SELECT name FROM groups ORDER BY name;")) {
        check_qt_error(query);
    }

    while (query.next()) {
        names.push_back(query.value(0).toString().toStdString());
    }
    return names;
}

int Database::getGroupId(const std::string& name) {
    QSqlQuery query(m_db);
    query.prepare("SELECT id FROM groups WHERE name = ?;");
    query.addBindValue(QString::fromStdString(name));
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        return query.value(0).toInt();
    }
    throw DBException("Group not found: " + name);
}

int Database::getGroupIdForEntry(int entryId) {
    QSqlQuery query(m_db);
    query.prepare("SELECT group_id FROM entries WHERE id = ?;");
    query.addBindValue(entryId);
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        return query.value(0).toInt();
    }
    throw DBException("Entry not found");
}

bool Database::deleteGroup(const std::string& name) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM groups WHERE name = ?;");
    query.addBindValue(QString::fromStdString(name));
    
    if (!query.exec()) check_qt_error(query);
    
    return query.numRowsAffected() > 0;
}

// --- ENTRIES ---

void Database::storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword) {
    if (!m_db.transaction()) throw DBException("Failed to start transaction");

    try {
        qint64 now = QDateTime::currentSecsSinceEpoch();
        
        QSqlQuery q(m_db);
        q.prepare("INSERT INTO entries (group_id, title, username, notes, encrypted_password, created_at, last_modified, last_accessed, password_expiry, totp_secret, entry_type) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
        
        q.addBindValue(groupId);
        q.addBindValue(QString::fromStdString(entry.title));
        q.addBindValue(QString::fromStdString(entry.username));
        q.addBindValue(QString::fromStdString(entry.notes));
        q.addBindValue(toQByteArray(encryptedPassword));
        q.addBindValue(now); // created_at
        q.addBindValue(now); // last_modified
        q.addBindValue(now); // last_accessed
        q.addBindValue(static_cast<qint64>(entry.passwordExpiry));
        q.addBindValue(QString::fromStdString(entry.totp_secret));
        q.addBindValue(QString::fromStdString(entry.entry_type));

        if (!q.exec()) throw DBException("Insert entry failed: " + q.lastError().text().toStdString());

        entry.id = q.lastInsertId().toInt();
        entry.createdAt = now;
        entry.lastModified = now;
        entry.lastAccessed = now;

        QSqlQuery loc_stmt(m_db);
        loc_stmt.prepare("INSERT INTO locations (entry_id, type, value) VALUES (?, ?, ?);");
        
        for (Location& loc : entry.locations) {
            loc_stmt.bindValue(0, entry.id);
            loc_stmt.bindValue(1, QString::fromStdString(loc.type));
            loc_stmt.bindValue(2, QString::fromStdString(loc.value));
            if (!loc_stmt.exec()) throw DBException("Insert location failed");
            loc.id = loc_stmt.lastInsertId().toInt();
        }

        m_db.commit();
    } catch (...) {
        m_db.rollback();
        throw;
    }
}

std::vector<VaultEntry> Database::getEntriesForGroup(int groupId) {
    std::vector<VaultEntry> entries;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, username, notes, created_at, last_modified, last_accessed, password_expiry, totp_secret, entry_type FROM entries WHERE group_id = ? ORDER BY title;");
    query.addBindValue(groupId);
    
    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        int id = query.value(0).toInt();
        std::string title = query.value(1).toString().toStdString();
        std::string username = query.value(2).toString().toStdString();
        std::string notes = query.value(3).toString().toStdString();

        VaultEntry entry(id, title, username, notes);
        entry.createdAt = query.value(4).toLongLong();
        entry.lastModified = query.value(5).toLongLong();
        entry.lastAccessed = query.value(6).toLongLong();
        entry.passwordExpiry = query.value(7).toLongLong();
        entry.totp_secret = query.value(8).toString().toStdString();
        entry.entry_type = query.value(9).toString().toStdString();
        if (entry.entry_type.empty()) entry.entry_type = "password";

        entry.locations = getLocationsForEntry(id);
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<Location> Database::getLocationsForEntry(int entryId) {
    std::vector<Location> locations;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, type, value FROM locations WHERE entry_id = ?;");
    query.addBindValue(entryId);
    
    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        int id = query.value(0).toInt();
        std::string type = query.value(1).toString().toStdString();
        std::string value = query.value(2).toString().toStdString();
        locations.emplace_back(id, type, value);
    }
    return locations;
}

std::vector<VaultEntry> Database::findEntriesByLocation(const std::string& locationValue) {
    std::vector<VaultEntry> entries;
    QSqlQuery query(m_db);
    QString qs = QString::fromStdString(locationValue);

    query.prepare(R"(
        SELECT DISTINCT e.id, e.title, e.username, e.notes 
        FROM entries e 
        JOIN locations l ON e.id = l.entry_id 
        WHERE l.value = ?
        UNION
        SELECT DISTINCT e.id, e.title, e.username, e.notes 
        FROM entries e 
        JOIN locations l ON e.id = l.entry_id 
        WHERE ? LIKE '%' || l.value || '%' OR l.value LIKE '%' || ? || '%';
    )");
    
    query.addBindValue(qs);
    query.addBindValue(qs);
    query.addBindValue(qs);

    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        int id = query.value(0).toInt();
        std::string title = query.value(1).toString().toStdString();
        std::string username = query.value(2).toString().toStdString();
        std::string notes = query.value(3).toString().toStdString();
        
        VaultEntry entry(id, title, username, notes);
        entry.locations = getLocationsForEntry(id); 
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<VaultEntry> Database::searchEntries(const std::string& searchTerm) {
    std::vector<VaultEntry> entries;
    QSqlQuery query(m_db);
    QString term = "%" + QString::fromStdString(searchTerm) + "%";

    QString sql = R"( 
        SELECT id, title, username, notes, totp_secret, entry_type 
        FROM entries 
        WHERE title LIKE ? OR username LIKE ? OR notes LIKE ? 
        UNION 
        SELECT DISTINCT e.id, e.title, e.username, e.notes, e.totp_secret, e.entry_type 
        FROM entries e 
        JOIN locations l ON e.id = l.entry_id 
        WHERE l.value LIKE ?; 
    )";

    query.prepare(sql);
    query.addBindValue(term);
    query.addBindValue(term);
    query.addBindValue(term);
    query.addBindValue(term);

    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        int id = query.value(0).toInt();
        std::string title = query.value(1).toString().toStdString();
        std::string username = query.value(2).toString().toStdString();
        std::string notes = query.value(3).toString().toStdString();
        
        VaultEntry entry(id, title, username, notes);
        entry.totp_secret = query.value(4).toString().toStdString();
        entry.entry_type = query.value(5).toString().toStdString();
        if (entry.entry_type.empty()) entry.entry_type = "password";
        
        entry.locations = getLocationsForEntry(id);
        entries.push_back(std::move(entry));
    }
    return entries;
}

std::vector<unsigned char> Database::getEncryptedPassword(int entryId) {
    QSqlQuery query(m_db);
    query.prepare("SELECT encrypted_password FROM entries WHERE id = ?;");
    query.addBindValue(entryId);
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        return toVector(query.value(0).toByteArray());
    }
    throw DBException("Entry not found");
}

bool Database::deleteEntry(int entryId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM entries WHERE id = ?;");
    query.addBindValue(entryId);
    
    if (!query.exec()) check_qt_error(query);
    return query.numRowsAffected() > 0;
}

void Database::updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword) {
    if (!m_db.transaction()) throw DBException("Failed to start transaction");

    try {
        QSqlQuery q(m_db);
        q.prepare("UPDATE entries SET title = ?, username = ?, notes = ?, last_modified = ?, password_expiry = ?, totp_secret = ?, entry_type = ? WHERE id = ?;");
        q.addBindValue(QString::fromStdString(entry.title));
        q.addBindValue(QString::fromStdString(entry.username));
        q.addBindValue(QString::fromStdString(entry.notes));
        q.addBindValue(QDateTime::currentSecsSinceEpoch());
        q.addBindValue(static_cast<qint64>(entry.passwordExpiry));
        q.addBindValue(QString::fromStdString(entry.totp_secret));
        q.addBindValue(QString::fromStdString(entry.entry_type));
        q.addBindValue(entry.id);
        
        if (!q.exec()) throw DBException("Update entry failed");

        if (newEncryptedPassword) {
            try {
                // Save old password
                std::vector<unsigned char> oldPassword = getEncryptedPassword(entry.id);
                storePasswordHistory(entry.id, oldPassword);
                deleteOldPasswordHistory(entry.id, 10);
            } catch (...) {
                // Ignore if history fails
            }
            
            QSqlQuery pq(m_db);
            pq.prepare("UPDATE entries SET encrypted_password = ? WHERE id = ?;");
            pq.addBindValue(toQByteArray(*newEncryptedPassword));
            pq.addBindValue(entry.id);
            if (!pq.exec()) throw DBException("Update password failed");
        }

        // Update locations
        QSqlQuery del_loc(m_db);
        del_loc.prepare("DELETE FROM locations WHERE entry_id = ?;");
        del_loc.addBindValue(entry.id);
        if (!del_loc.exec()) throw DBException("Clear locations failed");

        QSqlQuery loc_stmt(m_db);
        loc_stmt.prepare("INSERT INTO locations (entry_id, type, value) VALUES (?, ?, ?);");
        for (const Location& loc : entry.locations) {
            loc_stmt.bindValue(0, entry.id);
            loc_stmt.bindValue(1, QString::fromStdString(loc.type));
            loc_stmt.bindValue(2, QString::fromStdString(loc.value));
            if (!loc_stmt.exec()) throw DBException("Insert location failed");
        }

        m_db.commit();
    } catch (...) {
        m_db.rollback();
        throw;
    }
}

bool Database::entryExists(const std::string& username, const std::string& locationValue) {
    QSqlQuery query(m_db);
    query.prepare("SELECT 1 FROM entries e JOIN locations l ON e.id = l.entry_id WHERE e.username = ? AND l.value = ? LIMIT 1;");
    query.addBindValue(QString::fromStdString(username));
    query.addBindValue(QString::fromStdString(locationValue));
    
    if (!query.exec()) check_qt_error(query);
    return query.next();
}

// --- PASSWORD HISTORY ---

void Database::storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO password_history (entry_id, encrypted_password, changed_at) VALUES (?, ?, ?);");
    query.addBindValue(entryId);
    query.addBindValue(toQByteArray(oldEncryptedPassword));
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    
    if (!query.exec()) check_qt_error(query);
}

std::vector<PasswordHistoryEntry> Database::getPasswordHistory(int entryId) {
    std::vector<PasswordHistoryEntry> history;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, encrypted_password, changed_at FROM password_history WHERE entry_id = ? ORDER BY changed_at DESC;");
    query.addBindValue(entryId);
    
    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        int id = query.value(0).toInt();
        std::vector<unsigned char> encPwd = toVector(query.value(1).toByteArray());
        
        // Convert vector back to string for the struct if needed, or change struct to use vector
        // Assuming PasswordHistoryEntry uses string for encryptedBlob based on original code
        std::string encPwdStr(encPwd.begin(), encPwd.end()); 
        
        long long changedAt = query.value(2).toLongLong();
        history.emplace_back(id, entryId, encPwdStr, changedAt);
    }
    return history;
}

void Database::deleteOldPasswordHistory(int entryId, int keepCount) {
    QSqlQuery query(m_db);
    QString sql = R"(
        DELETE FROM password_history 
        WHERE entry_id = ? AND id NOT IN (
            SELECT id FROM password_history 
            WHERE entry_id = ? 
            ORDER BY changed_at DESC 
            LIMIT ?
        );
    )";
    query.prepare(sql);
    query.addBindValue(entryId);
    query.addBindValue(entryId);
    query.addBindValue(keepCount);
    
    if (!query.exec()) check_qt_error(query);
}

// --- ENTRY ACCESS TRACKING ---

void Database::updateEntryAccessTime(int entryId) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE entries SET last_accessed = ? WHERE id = ?;");
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    query.addBindValue(entryId);
    query.exec(); // Ignore errors for access time updates
}

std::vector<VaultEntry> Database::getRecentlyAccessedEntries(int groupId, int limit) {
    std::vector<VaultEntry> entries;
    QSqlQuery query(m_db);
    query.prepare("SELECT id, title, username, notes, created_at, last_modified, last_accessed, password_expiry, totp_secret, entry_type FROM entries WHERE group_id = ? AND last_accessed > 0 ORDER BY last_accessed DESC LIMIT ?;");
    query.addBindValue(groupId);
    query.addBindValue(limit);
    
    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        int id = query.value(0).toInt();
        std::string title = query.value(1).toString().toStdString();
        std::string username = query.value(2).toString().toStdString();
        std::string notes = query.value(3).toString().toStdString();

        VaultEntry entry(id, title, username, notes);
        entry.createdAt = query.value(4).toLongLong();
        entry.lastModified = query.value(5).toLongLong();
        entry.lastAccessed = query.value(6).toLongLong();
        entry.passwordExpiry = query.value(7).toLongLong();
        entry.totp_secret = query.value(8).toString().toStdString();
        entry.entry_type = query.value(9).toString().toStdString();
        if (entry.entry_type.empty()) entry.entry_type = "password";

        entry.locations = getLocationsForEntry(id);
        entries.push_back(std::move(entry));
    }
    return entries;
}

// --- PENDING INVITES ---

void Database::storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson) {
    QSqlQuery query(m_db);
    query.prepare("INSERT INTO pending_invites (sender_id, group_name, payload_json, timestamp) VALUES (?, ?, ?, ?);");
    query.addBindValue(QString::fromStdString(senderId));
    query.addBindValue(QString::fromStdString(groupName));
    query.addBindValue(QString::fromStdString(payloadJson));
    query.addBindValue(QDateTime::currentSecsSinceEpoch());
    
    if (!query.exec()) check_qt_error(query);
}

void Database::updatePendingInviteStatus(int inviteId, const std::string& status) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE pending_invites SET status = ? WHERE id = ?;");
    query.addBindValue(QString::fromStdString(status));
    query.addBindValue(inviteId);
    
    if (!query.exec()) check_qt_error(query);
}

std::vector<PendingInvite> Database::getPendingInvites() {
    std::vector<PendingInvite> invites;
    QSqlQuery query(m_db);
    
    if (!query.exec("SELECT id, sender_id, group_name, payload_json, timestamp, status FROM pending_invites ORDER BY timestamp DESC;")) {
        check_qt_error(query);
    }

    while (query.next()) {
        PendingInvite invite;
        invite.id = query.value(0).toInt();
        invite.senderId = query.value(1).toString().toStdString();
        invite.groupName = query.value(2).toString().toStdString();
        invite.payloadJson = query.value(3).toString().toStdString();
        invite.timestamp = query.value(4).toLongLong();
        invite.status = query.value(5).toString().toStdString();
        if (invite.status.empty()) invite.status = "pending";
        
        invites.push_back(invite);
    }
    return invites;
}

void Database::deletePendingInvite(int inviteId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM pending_invites WHERE id = ?;");
    query.addBindValue(inviteId);
    
    if (!query.exec()) check_qt_error(query);
}

// --- MEMBERS ---

void Database::addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO group_members (group_id, user_id, role, status) VALUES (?, ?, ?, ?);");
    query.addBindValue(groupId);
    query.addBindValue(QString::fromStdString(userId));
    query.addBindValue(QString::fromStdString(role));
    query.addBindValue(QString::fromStdString(status));
    
    if (!query.exec()) check_qt_error(query);
}

void Database::removeGroupMember(int groupId, const std::string& userId) {
    QSqlQuery query(m_db);
    query.prepare("DELETE FROM group_members WHERE group_id = ? AND user_id = ?;");
    query.addBindValue(groupId);
    query.addBindValue(QString::fromStdString(userId));
    
    if (!query.exec()) check_qt_error(query);
}

void Database::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE group_members SET role = ? WHERE group_id = ? AND user_id = ?;");
    query.addBindValue(QString::fromStdString(newRole));
    query.addBindValue(groupId);
    query.addBindValue(QString::fromStdString(userId));
    
    if (!query.exec()) check_qt_error(query);
}

void Database::updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus) {
    QSqlQuery query(m_db);
    query.prepare("UPDATE group_members SET status = ? WHERE group_id = ? AND user_id = ?;");
    query.addBindValue(QString::fromStdString(newStatus));
    query.addBindValue(groupId);
    query.addBindValue(QString::fromStdString(userId));
    
    if (!query.exec()) check_qt_error(query);
}

std::vector<GroupMember> Database::getGroupMembers(int groupId) {
    std::vector<GroupMember> members;
    QSqlQuery query(m_db);
    query.prepare("SELECT user_id, role, status FROM group_members WHERE group_id = ?;");
    query.addBindValue(groupId);
    
    if (!query.exec()) check_qt_error(query);

    while (query.next()) {
        GroupMember m;
        m.userId = query.value(0).toString().toStdString();
        m.role = query.value(1).toString().toStdString();
        m.status = query.value(2).toString().toStdString();
        members.push_back(m);
    }
    return members;
}

void Database::setGroupPermissions(int groupId, bool adminsOnly) {
    QSqlQuery query(m_db);
    query.prepare("INSERT OR REPLACE INTO group_settings (group_id, admins_only_write) VALUES (?, ?);");
    query.addBindValue(groupId);
    query.addBindValue(adminsOnly ? 1 : 0);
    
    if (!query.exec()) check_qt_error(query);
}

GroupPermissions Database::getGroupPermissions(int groupId) {
    GroupPermissions p;
    p.adminsOnlyWrite = false;
    
    QSqlQuery query(m_db);
    query.prepare("SELECT admins_only_write FROM group_settings WHERE group_id = ?;");
    query.addBindValue(groupId);
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        p.adminsOnlyWrite = query.value(0).toInt() != 0;
    }
    return p;
}

std::string Database::getGroupOwner(int groupId) {
    QSqlQuery query(m_db);
    query.prepare("SELECT owner_id FROM groups WHERE id = ?;");
    query.addBindValue(groupId);
    
    if (!query.exec()) check_qt_error(query);

    if (query.next()) {
        return query.value(0).toString().toStdString();
    }
    return "me";
}

} // namespace Core
} // namespace CipherMesh