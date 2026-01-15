#include "database.hpp"
#include <vector>
#include <iostream>

// =========================================================
//  ANDROID IMPLEMENTATION (Raw SQLite3)
// =========================================================
#if defined(__ANDROID__) || defined(ANDROID)

#include <sqlite3.h>
#include <stdexcept>
#include <cstring>
#include <chrono>

namespace CipherMesh {
namespace Core {

// Helper: Cast void* handle to sqlite3*
static sqlite3* getHandle(void* h) { return static_cast<sqlite3*>(h); }

// Helper class to mimic QSqlQuery behavior for easier porting
class SqlStatement {
    sqlite3_stmt* stmt = nullptr;
    sqlite3* db = nullptr;
public:
    SqlStatement(void* dbHandle, const std::string& sql) {
        db = getHandle(dbHandle);
        // Prepare statement safely
        if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
            stmt = nullptr;
        }
    }
    ~SqlStatement() { if (stmt) sqlite3_finalize(stmt); }
    
    bool isValid() const { return stmt != nullptr; }

    void bind(int idx, int value) { if(stmt) sqlite3_bind_int(stmt, idx, value); }
    void bind(int idx, long long value) { if(stmt) sqlite3_bind_int64(stmt, idx, value); }
    void bind(int idx, const std::string& value) { 
        if(stmt) sqlite3_bind_text(stmt, idx, value.c_str(), -1, SQLITE_TRANSIENT); 
    }
    void bind(int idx, const std::vector<unsigned char>& blob) {
        if (!stmt) return;
        if (blob.empty()) sqlite3_bind_null(stmt, idx);
        else sqlite3_bind_blob(stmt, idx, blob.data(), blob.size(), SQLITE_TRANSIENT);
    }

    bool step() { return stmt && sqlite3_step(stmt) == SQLITE_ROW; }
    
    bool exec() { 
        if (!stmt) return false;
        int rc = sqlite3_step(stmt);
        return rc == SQLITE_DONE || rc == SQLITE_ROW; 
    }

    int getInt(int idx) { return stmt ? sqlite3_column_int(stmt, idx) : 0; }
    long long getInt64(int idx) { return stmt ? sqlite3_column_int64(stmt, idx) : 0; }
    std::string getString(int idx) {
        if (!stmt) return "";
        const char* txt = (const char*)sqlite3_column_text(stmt, idx);
        return txt ? std::string(txt) : "";
    }
    std::vector<unsigned char> getBlob(int idx) {
        if (!stmt) return {};
        const void* data = sqlite3_column_blob(stmt, idx);
        int bytes = sqlite3_column_bytes(stmt, idx);
        if (!data || bytes <= 0) return {};
        return std::vector<unsigned char>((const unsigned char*)data, (const unsigned char*)data + bytes);
    }
};

Database::Database() : m_db_handle(nullptr) {}

Database::~Database() {
    close();
}

void Database::open(const std::string& path) {
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK) {
        throw DBException("Failed to open database");
    }
    m_db_handle = db;
    // Enable Foreign Keys
    exec("PRAGMA foreign_keys = ON;");
}

void Database::close() {
    if (m_db_handle) {
        sqlite3_close(getHandle(m_db_handle));
        m_db_handle = nullptr;
    }
}

bool Database::isOpen() const {
    return m_db_handle != nullptr;
}

void Database::exec(const std::string& sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(getHandle(m_db_handle), sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "Unknown error";
        sqlite3_free(errMsg);
        throw DBException("Exec failed: " + err);
    }
}

void Database::createTables() {
    // Create all necessary tables
    exec("CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value BLOB)");
    exec("CREATE TABLE IF NOT EXISTS groups (id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT UNIQUE NOT NULL, encrypted_key BLOB NOT NULL, owner_id TEXT, admins_only_write INTEGER DEFAULT 0, admins_only_invite INTEGER DEFAULT 0)");
    exec("CREATE TABLE IF NOT EXISTS group_members (group_id INTEGER, user_id TEXT, role TEXT, status TEXT, PRIMARY KEY(group_id, user_id), FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE)");
    exec("CREATE TABLE IF NOT EXISTS entries (id INTEGER PRIMARY KEY AUTOINCREMENT, group_id INTEGER, title TEXT, username TEXT, encrypted_password BLOB, url TEXT, notes TEXT, totp_secret TEXT, entry_type TEXT, created_at INTEGER, updated_at INTEGER, access_count INTEGER DEFAULT 0, last_accessed INTEGER DEFAULT 0, password_expiry INTEGER DEFAULT 0, FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE)");
    exec("CREATE TABLE IF NOT EXISTS locations (id INTEGER PRIMARY KEY AUTOINCREMENT, entry_id INTEGER, type TEXT, value TEXT, FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)");
    exec("CREATE TABLE IF NOT EXISTS password_history (id INTEGER PRIMARY KEY AUTOINCREMENT, entry_id INTEGER, encrypted_password BLOB, timestamp INTEGER, FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)");
    exec("CREATE TABLE IF NOT EXISTS pending_invites (id INTEGER PRIMARY KEY AUTOINCREMENT, sender_id TEXT, group_name TEXT, payload TEXT, status TEXT)");
}

// -- Groups --
void Database::storeEncryptedGroup(const std::string& name, const std::vector<unsigned char>& encryptedKey, const std::string& ownerId) {
    SqlStatement q(m_db_handle, "INSERT INTO groups (name, encrypted_key, owner_id) VALUES (?, ?, ?)");
    q.bind(1, name); q.bind(2, encryptedKey); q.bind(3, ownerId);
    if (!q.exec()) throw DBException("Failed to add group");
}

std::vector<unsigned char> Database::getEncryptedGroupKey(const std::string& name, int& groupId) {
    SqlStatement q(m_db_handle, "SELECT id, encrypted_key FROM groups WHERE name = ?");
    q.bind(1, name);
    if (q.step()) {
        groupId = q.getInt(0);
        return q.getBlob(1);
    }
    throw DBException("Group not found");
}

std::vector<unsigned char> Database::getEncryptedGroupKeyById(int groupId) {
    SqlStatement q(m_db_handle, "SELECT encrypted_key FROM groups WHERE id = ?");
    q.bind(1, groupId);
    if (q.step()) return q.getBlob(0);
    return {};
}

std::map<int, std::vector<unsigned char>> Database::getAllEncryptedGroupKeys() {
    std::map<int, std::vector<unsigned char>> keys;
    SqlStatement q(m_db_handle, "SELECT id, encrypted_key FROM groups");
    while (q.step()) {
        keys[q.getInt(0)] = q.getBlob(1);
    }
    return keys;
}

void Database::updateEncryptedGroupKey(int groupId, const std::vector<unsigned char>& newKey) {
    SqlStatement q(m_db_handle, "UPDATE groups SET encrypted_key = ? WHERE id = ?");
    q.bind(1, newKey); q.bind(2, groupId);
    q.exec();
}

std::vector<std::string> Database::getAllGroupNames() {
    std::vector<std::string> names;
    SqlStatement q(m_db_handle, "SELECT name FROM groups");
    while (q.step()) names.push_back(q.getString(0));
    return names;
}

int Database::getGroupId(const std::string& name) {
    SqlStatement q(m_db_handle, "SELECT id FROM groups WHERE name = ?");
    q.bind(1, name);
    if (q.step()) return q.getInt(0);
    return -1;
}

int Database::getGroupIdForEntry(int entryId) {
    SqlStatement q(m_db_handle, "SELECT group_id FROM entries WHERE id = ?");
    q.bind(1, entryId);
    if (q.step()) return q.getInt(0);
    return -1;
}

bool Database::deleteGroup(const std::string& name) {
    SqlStatement q(m_db_handle, "DELETE FROM groups WHERE name = ?");
    q.bind(1, name);
    return q.exec(); 
}

std::string Database::getGroupOwner(int groupId) {
    SqlStatement q(m_db_handle, "SELECT owner_id FROM groups WHERE id = ?");
    q.bind(1, groupId);
    if (q.step()) return q.getString(0);
    return "";
}

// -- Members --
void Database::addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status) {
    SqlStatement q(m_db_handle, "INSERT OR REPLACE INTO group_members (group_id, user_id, role, status) VALUES (?, ?, ?, ?)");
    q.bind(1, groupId); q.bind(2, userId); q.bind(3, role); q.bind(4, status);
    q.exec();
}

std::vector<GroupMember> Database::getGroupMembers(int groupId) {
    std::vector<GroupMember> list;
    SqlStatement q(m_db_handle, "SELECT user_id, role, status FROM group_members WHERE group_id = ?");
    q.bind(1, groupId);
    while (q.step()) {
        list.push_back({q.getString(0), q.getString(1), q.getString(2)});
    }
    return list;
}

void Database::removeGroupMember(int groupId, const std::string& userId) {
    SqlStatement q(m_db_handle, "DELETE FROM group_members WHERE group_id = ? AND user_id = ?");
    q.bind(1, groupId); q.bind(2, userId);
    q.exec();
}

void Database::setGroupPermissions(int groupId, bool adminsOnly) {
    SqlStatement q(m_db_handle, "UPDATE groups SET admins_only_write = ? WHERE id = ?");
    q.bind(1, adminsOnly ? 1 : 0); q.bind(2, groupId);
    q.exec();
}

GroupPermissions Database::getGroupPermissions(int groupId) {
    GroupPermissions gp;
    SqlStatement q(m_db_handle, "SELECT admins_only_write, admins_only_invite FROM groups WHERE id = ?");
    q.bind(1, groupId);
    if (q.step()) {
        gp.adminsOnlyWrite = (q.getInt(0) != 0);
        gp.adminsOnlyInvite = (q.getInt(1) != 0);
    }
    return gp;
}

void Database::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) {
    SqlStatement q(m_db_handle, "UPDATE group_members SET role = ? WHERE group_id = ? AND user_id = ?");
    q.bind(1, newRole); q.bind(2, groupId); q.bind(3, userId);
    q.exec();
}

void Database::updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus) {
    SqlStatement q(m_db_handle, "UPDATE group_members SET status = ? WHERE group_id = ? AND user_id = ?");
    q.bind(1, newStatus); q.bind(2, groupId); q.bind(3, userId);
    q.exec();
}

// -- Entries --
void Database::storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword) {
    SqlStatement q(m_db_handle, "INSERT INTO entries (group_id, title, username, encrypted_password, url, notes, totp_secret, entry_type, created_at, updated_at, password_expiry) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    q.bind(1, groupId);
    q.bind(2, entry.title);
    q.bind(3, entry.username);
    q.bind(4, encryptedPassword);
    q.bind(5, entry.url);
    q.bind(6, entry.notes);
    q.bind(7, entry.totpSecret);
    q.bind(8, entry.entryType);
    q.bind(9, now);
    q.bind(10, now);
    q.bind(11, entry.passwordExpiry);
    
    if (q.exec()) {
        entry.id = (int)sqlite3_last_insert_rowid(getHandle(m_db_handle));
        // Locations
        for (const auto& loc : entry.locations) {
            SqlStatement lq(m_db_handle, "INSERT INTO locations (entry_id, type, value) VALUES (?, ?, ?)");
            lq.bind(1, entry.id); lq.bind(2, loc.type); lq.bind(3, loc.value);
            lq.exec();
        }
    } else {
        throw DBException("Failed to add entry");
    }
}

std::vector<VaultEntry> Database::getEntriesForGroup(int groupId) {
    std::vector<VaultEntry> list;
    SqlStatement q(m_db_handle, "SELECT id, title, username, url, notes, totp_secret, entry_type, created_at, updated_at, last_accessed, password_expiry FROM entries WHERE group_id = ?");
    q.bind(1, groupId);
    while (q.step()) {
        VaultEntry e;
        e.id = q.getInt(0);
        e.groupId = groupId;
        e.title = q.getString(1);
        e.username = q.getString(2);
        e.url = q.getString(3);
        e.notes = q.getString(4);
        e.totpSecret = q.getString(5);
        e.entryType = q.getString(6);
        e.createdAt = q.getInt64(7);
        e.updatedAt = q.getInt64(8);
        e.lastAccessed = q.getInt64(9);
        e.passwordExpiry = q.getInt64(10);
        e.locations = getLocationsForEntry(e.id);
        list.push_back(e);
    }
    return list;
}

std::vector<Location> Database::getLocationsForEntry(int entryId) {
    std::vector<Location> locs;
    SqlStatement q(m_db_handle, "SELECT id, type, value FROM locations WHERE entry_id = ?");
    q.bind(1, entryId);
    while (q.step()) {
        locs.emplace_back(
            q.getInt(0),
            q.getString(1),
            q.getString(2)
        );
    }
    return locs;
}

std::vector<unsigned char> Database::getEncryptedPassword(int entryId) {
    SqlStatement q(m_db_handle, "SELECT encrypted_password FROM entries WHERE id = ?");
    q.bind(1, entryId);
    if (q.step()) return q.getBlob(0);
    return {};
}

bool Database::deleteEntry(int entryId) {
    SqlStatement q(m_db_handle, "DELETE FROM entries WHERE id = ?");
    q.bind(1, entryId);
    return q.exec();
}

void Database::updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword) {
    std::string sql = "UPDATE entries SET title=?, username=?, url=?, notes=?, totp_secret=?, entry_type=?, updated_at=?, password_expiry=?";
    if (newEncryptedPassword) sql += ", encrypted_password=?";
    sql += " WHERE id=?";
    
    SqlStatement q(m_db_handle, sql);
    q.bind(1, entry.title);
    q.bind(2, entry.username);
    q.bind(3, entry.url);
    q.bind(4, entry.notes);
    q.bind(5, entry.totpSecret);
    q.bind(6, entry.entryType);
    q.bind(7, (long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    q.bind(8, entry.passwordExpiry);
    
    int nextIdx = 9;
    if (newEncryptedPassword) {
        q.bind(nextIdx++, *newEncryptedPassword);
    }
    q.bind(nextIdx, entry.id);
    
    if (q.exec()) {
        // Update locations
        SqlStatement d(m_db_handle, "DELETE FROM locations WHERE entry_id = ?");
        d.bind(1, entry.id);
        d.exec();
        
        for (const auto& loc : entry.locations) {
            SqlStatement lq(m_db_handle, "INSERT INTO locations (entry_id, type, value) VALUES (?, ?, ?)");
            lq.bind(1, entry.id); lq.bind(2, loc.type); lq.bind(3, loc.value);
            lq.exec();
        }
    }
}

bool Database::entryExists(const std::string& username, const std::string& locationValue) {
    SqlStatement q(m_db_handle, "SELECT count(*) FROM entries e JOIN locations l ON e.id = l.entry_id WHERE e.username = ? AND l.value = ?");
    q.bind(1, username); q.bind(2, locationValue);
    if (q.step()) return q.getInt(0) > 0;
    return false;
}

std::vector<VaultEntry> Database::findEntriesByLocation(const std::string& locationValue) {
    std::vector<VaultEntry> results;
    SqlStatement q(m_db_handle, "SELECT DISTINCT e.id, e.group_id, e.title, e.username, e.url, e.notes FROM entries e JOIN locations l ON e.id = l.entry_id WHERE l.value LIKE ?");
    q.bind(1, "%" + locationValue + "%");
    while (q.step()) {
        VaultEntry e;
        e.id = q.getInt(0);
        e.groupId = q.getInt(1);
        e.title = q.getString(2);
        e.username = q.getString(3);
        e.url = q.getString(4);
        e.notes = q.getString(5);
        e.locations = getLocationsForEntry(e.id);
        results.push_back(e);
    }
    return results;
}

std::vector<VaultEntry> Database::searchEntries(const std::string& searchTerm) {
    std::vector<VaultEntry> list;
    SqlStatement q(m_db_handle, "SELECT id, group_id, title, username, url, notes FROM entries WHERE title LIKE ? OR username LIKE ? OR url LIKE ? OR notes LIKE ?");
    std::string term = "%" + searchTerm + "%";
    q.bind(1, term); q.bind(2, term); q.bind(3, term); q.bind(4, term);
    while (q.step()) {
        VaultEntry e;
        e.id = q.getInt(0);
        e.groupId = q.getInt(1);
        e.title = q.getString(2);
        e.username = q.getString(3);
        e.url = q.getString(4);
        e.notes = q.getString(5);
        list.push_back(e);
    }
    return list;
}

void Database::updateEntryAccessTime(int entryId) {
    SqlStatement q(m_db_handle, "UPDATE entries SET access_count = access_count + 1, last_accessed = ? WHERE id = ?");
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    q.bind(1, now);
    q.bind(2, entryId);
    q.exec();
}

std::vector<VaultEntry> Database::getRecentlyAccessedEntries(int groupId, int limit) {
    std::vector<VaultEntry> list;
    SqlStatement q(m_db_handle, "SELECT id, title, username, url, notes FROM entries WHERE group_id = ? ORDER BY last_accessed DESC LIMIT ?");
    q.bind(1, groupId); q.bind(2, limit);
    while (q.step()) {
        VaultEntry e;
        e.id = q.getInt(0);
        e.title = q.getString(1);
        e.username = q.getString(2);
        e.url = q.getString(3);
        e.notes = q.getString(4);
        list.push_back(e);
    }
    return list;
}

// -- Metadata --
void Database::storeMetadata(const std::string& key, const std::vector<unsigned char>& value) {
    SqlStatement q(m_db_handle, "INSERT OR REPLACE INTO metadata (key, value) VALUES (?, ?)");
    q.bind(1, key); q.bind(2, value);
    q.exec();
}

std::vector<unsigned char> Database::getMetadata(const std::string& key) {
    SqlStatement q(m_db_handle, "SELECT value FROM metadata WHERE key = ?");
    q.bind(1, key);
    if (q.step()) return q.getBlob(0);
    return {}; 
}

// -- History --
void Database::storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword) {
    SqlStatement q(m_db_handle, "INSERT INTO password_history (entry_id, encrypted_password, timestamp) VALUES (?, ?, ?)");
    long long now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    q.bind(1, entryId); q.bind(2, oldEncryptedPassword); q.bind(3, now);
    q.exec();
}

std::vector<PasswordHistoryEntry> Database::getPasswordHistory(int entryId) {
    std::vector<PasswordHistoryEntry> list;
    SqlStatement q(m_db_handle, "SELECT id, encrypted_password, timestamp FROM password_history WHERE entry_id = ? ORDER BY timestamp DESC");
    q.bind(1, entryId);
    while (q.step()) {
        PasswordHistoryEntry h;
        h.id = q.getInt(0);
        h.entryId = entryId;
        std::vector<unsigned char> blob = q.getBlob(1);
        h.encryptedPassword = std::string(blob.begin(), blob.end());
        h.changedAt = q.getInt64(2);
        list.push_back(h);
    }
    return list;
}

void Database::deleteOldPasswordHistory(int entryId, int keepCount) {
    SqlStatement q(m_db_handle, "DELETE FROM password_history WHERE entry_id = ? AND id NOT IN (SELECT id FROM password_history WHERE entry_id = ? ORDER BY timestamp DESC LIMIT ?)");
    q.bind(1, entryId); q.bind(2, entryId); q.bind(3, keepCount);
    q.exec();
}

// -- Invites --
void Database::storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson) {
    SqlStatement q(m_db_handle, "INSERT INTO pending_invites (sender_id, group_name, payload, status) VALUES (?, ?, ?, 'pending')");
    q.bind(1, senderId); q.bind(2, groupName); q.bind(3, payloadJson);
    q.exec();
}

void Database::updatePendingInviteStatus(int inviteId, const std::string& status) {
    SqlStatement q(m_db_handle, "UPDATE pending_invites SET status = ? WHERE id = ?");
    q.bind(1, status); q.bind(2, inviteId);
    q.exec();
}

std::vector<PendingInvite> Database::getPendingInvites() {
    std::vector<PendingInvite> list;
    SqlStatement q(m_db_handle, "SELECT id, sender_id, group_name, payload, status FROM pending_invites");
    while (q.step()) {
        PendingInvite p;
        p.id = q.getInt(0);
        p.senderId = q.getString(1);
        p.groupName = q.getString(2);
        p.payloadJson = q.getString(3);
        p.status = q.getString(4);
        list.push_back(p);
    }
    return list;
}

void Database::deletePendingInvite(int inviteId) {
    SqlStatement q(m_db_handle, "DELETE FROM pending_invites WHERE id = ?");
    q.bind(1, inviteId);
    q.exec();
}

} // namespace Core
} // namespace CipherMesh

// =========================================================
//  DESKTOP IMPLEMENTATION (Qt SQL)
// =========================================================
#else 

#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QVariant>
#include <QDateTime>
#include <QUuid>
#include <QDebug>

namespace CipherMesh {
namespace Core {

// Helper: Convert vector<uchar> to QByteArray
static QByteArray toQByteArray(const std::vector<unsigned char>& vec) {
    return QByteArray(reinterpret_cast<const char*>(vec.data()), static_cast<int>(vec.size()));
}

// Helper: Convert QByteArray to vector<uchar>
static std::vector<unsigned char> toVector(const QByteArray& ba) {
    return std::vector<unsigned char>(ba.begin(), ba.end());
}

Database::Database() {
    // Generate a unique connection name to avoid conflicts if multiple instances exist
    m_connectionName = "CipherMesh_DB_" + QUuid::createUuid().toString().toStdString();
}

Database::~Database() {
    close();
}

void Database::open(const std::string& path) {
    if (QSqlDatabase::contains(QString::fromStdString(m_connectionName))) {
        m_db = QSqlDatabase::database(QString::fromStdString(m_connectionName));
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", QString::fromStdString(m_connectionName));
    }

    m_db.setDatabaseName(QString::fromStdString(path));

    if (!m_db.open()) {
        throw DBException("Failed to open database: " + m_db.lastError().text().toStdString());
    }

    // Enable Foreign Keys for SQLite
    QSqlQuery query(m_db);
    query.exec("PRAGMA foreign_keys = ON;");
}

void Database::close() {
    if (m_db.isOpen()) {
        m_db.close();
    }
    // Release object
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(QString::fromStdString(m_connectionName));
}

bool Database::isOpen() const {
    return m_db.isOpen();
}

void Database::exec(const std::string& sql) {
    QSqlQuery query(m_db);
    if (!query.exec(QString::fromStdString(sql))) {
        throw DBException("Exec failed: " + query.lastError().text().toStdString());
    }
}

void Database::createTables() {
    QSqlQuery q(m_db);
    
    // 1. Metadata
    if (!q.exec("CREATE TABLE IF NOT EXISTS metadata (key TEXT PRIMARY KEY, value BLOB)")) 
        throw DBException(q.lastError().text().toStdString());

    // 2. Groups
    if (!q.exec("CREATE TABLE IF NOT EXISTS groups ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "name TEXT UNIQUE NOT NULL, "
                "encrypted_key BLOB NOT NULL, "
                "owner_id TEXT, "
                "admins_only_write INTEGER DEFAULT 0, "
                "admins_only_invite INTEGER DEFAULT 0)"))
        throw DBException(q.lastError().text().toStdString());

    // 3. Group Members
    if (!q.exec("CREATE TABLE IF NOT EXISTS group_members ("
                "group_id INTEGER, "
                "user_id TEXT, "
                "role TEXT, "
                "status TEXT, "
                "PRIMARY KEY(group_id, user_id), "
                "FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE)"))
        throw DBException(q.lastError().text().toStdString());

    // 4. Entries
    if (!q.exec("CREATE TABLE IF NOT EXISTS entries ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "group_id INTEGER, "
                "title TEXT, "
                "username TEXT, "
                "encrypted_password BLOB, "
                "url TEXT, "
                "notes TEXT, "
                "totp_secret TEXT, "
                "entry_type TEXT, "
                "created_at INTEGER, "
                "updated_at INTEGER, "
                "access_count INTEGER DEFAULT 0, "
                "last_accessed INTEGER DEFAULT 0, "
                "password_expiry INTEGER DEFAULT 0, "
                "FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE)"))
        throw DBException(q.lastError().text().toStdString());

    // 5. Locations (Geo-fencing)
    if (!q.exec("CREATE TABLE IF NOT EXISTS locations ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "entry_id INTEGER, "
                "type TEXT, "
                "value TEXT, "
                "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)"))
        throw DBException(q.lastError().text().toStdString());

    // 6. Password History
    if (!q.exec("CREATE TABLE IF NOT EXISTS password_history ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "entry_id INTEGER, "
                "encrypted_password BLOB, "
                "timestamp INTEGER, "
                "FOREIGN KEY(entry_id) REFERENCES entries(id) ON DELETE CASCADE)"))
        throw DBException(q.lastError().text().toStdString());

    // 7. Pending Invites
    if (!q.exec("CREATE TABLE IF NOT EXISTS pending_invites ("
                "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                "sender_id TEXT, "
                "group_name TEXT, "
                "payload TEXT, "
                "status TEXT)"))
        throw DBException(q.lastError().text().toStdString());
}

// Group Management
void Database::storeEncryptedGroup(const std::string& name, const std::vector<unsigned char>& encryptedKey, const std::string& ownerId) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO groups (name, encrypted_key, owner_id) VALUES (:name, :key, :owner)");
    q.bindValue(":name", QString::fromStdString(name));
    q.bindValue(":key", toQByteArray(encryptedKey));
    q.bindValue(":owner", QString::fromStdString(ownerId));
    if (!q.exec()) throw DBException("Failed to add group: " + q.lastError().text().toStdString());
}

std::vector<unsigned char> Database::getEncryptedGroupKey(const std::string& name, int& groupId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id, encrypted_key FROM groups WHERE name = :name");
    q.bindValue(":name", QString::fromStdString(name));
    if (q.exec() && q.next()) {
        groupId = q.value("id").toInt();
        return toVector(q.value("encrypted_key").toByteArray());
    }
    throw DBException("Group not found");
}

std::vector<unsigned char> Database::getEncryptedGroupKeyById(int groupId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT encrypted_key FROM groups WHERE id = :id");
    q.bindValue(":id", groupId);
    if (q.exec() && q.next()) {
        return toVector(q.value(0).toByteArray());
    }
    return {};
}

std::map<int, std::vector<unsigned char>> Database::getAllEncryptedGroupKeys() {
    std::map<int, std::vector<unsigned char>> keys;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, encrypted_key FROM groups");
    if (q.exec()) {
        while (q.next()) {
            keys[q.value(0).toInt()] = toVector(q.value(1).toByteArray());
        }
    }
    return keys;
}

void Database::updateEncryptedGroupKey(int groupId, const std::vector<unsigned char>& newKey) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE groups SET encrypted_key = :key WHERE id = :id");
    q.bindValue(":key", toQByteArray(newKey));
    q.bindValue(":id", groupId);
    q.exec();
}

std::vector<std::string> Database::getAllGroupNames() {
    std::vector<std::string> names;
    QSqlQuery q(m_db);
    if (q.exec("SELECT name FROM groups")) {
        while (q.next()) {
            names.push_back(q.value(0).toString().toStdString());
        }
    }
    return names;
}

int Database::getGroupId(const std::string& name) {
    QSqlQuery q(m_db);
    q.prepare("SELECT id FROM groups WHERE name = :name");
    q.bindValue(":name", QString::fromStdString(name));
    if (q.exec() && q.next()) return q.value(0).toInt();
    throw DBException("Group not found: " + name);
}

int Database::getGroupIdForEntry(int entryId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT group_id FROM entries WHERE id = :id");
    q.bindValue(":id", entryId);
    if (q.exec() && q.next()) return q.value(0).toInt();
    return -1;
}

bool Database::deleteGroup(const std::string& name) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM groups WHERE name = :name");
    q.bindValue(":name", QString::fromStdString(name));
    return q.exec();
}

std::string Database::getGroupOwner(int groupId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT owner_id FROM groups WHERE id = :id");
    q.bindValue(":id", groupId);
    if (q.exec() && q.next()) return q.value(0).toString().toStdString();
    return "";
}

// Group Members
void Database::addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO group_members (group_id, user_id, role, status) VALUES (:gid, :uid, :role, :status)");
    q.bindValue(":gid", groupId);
    q.bindValue(":uid", QString::fromStdString(userId));
    q.bindValue(":role", QString::fromStdString(role));
    q.bindValue(":status", QString::fromStdString(status));
    q.exec();
}

std::vector<GroupMember> Database::getGroupMembers(int groupId) {
    std::vector<GroupMember> members;
    QSqlQuery q(m_db);
    q.prepare("SELECT user_id, role, status FROM group_members WHERE group_id = :gid");
    q.bindValue(":gid", groupId);
    if (q.exec()) {
        while (q.next()) {
            GroupMember m;
            m.userId = q.value(0).toString().toStdString();
            m.role = q.value(1).toString().toStdString();
            m.status = q.value(2).toString().toStdString();
            members.push_back(m);
        }
    }
    return members;
}

void Database::removeGroupMember(int groupId, const std::string& userId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM group_members WHERE group_id = :gid AND user_id = :uid");
    q.bindValue(":gid", groupId);
    q.bindValue(":uid", QString::fromStdString(userId));
    q.exec();
}

void Database::setGroupPermissions(int groupId, bool adminsOnly) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE groups SET admins_only_write = :val WHERE id = :id");
    q.bindValue(":val", adminsOnly ? 1 : 0);
    q.bindValue(":id", groupId);
    q.exec();
}

GroupPermissions Database::getGroupPermissions(int groupId) {
    GroupPermissions gp;
    QSqlQuery q(m_db);
    q.prepare("SELECT admins_only_write, admins_only_invite FROM groups WHERE id = :id");
    q.bindValue(":id", groupId);
    if (q.exec() && q.next()) {
        gp.adminsOnlyWrite = q.value(0).toBool();
        gp.adminsOnlyInvite = q.value(1).toBool();
    }
    return gp;
}

void Database::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE group_members SET role = :role WHERE group_id = :gid AND user_id = :uid");
    q.bindValue(":role", QString::fromStdString(newRole));
    q.bindValue(":gid", groupId);
    q.bindValue(":uid", QString::fromStdString(userId));
    q.exec();
}

void Database::updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE group_members SET status = :status WHERE group_id = :gid AND user_id = :uid");
    q.bindValue(":status", QString::fromStdString(newStatus));
    q.bindValue(":gid", groupId);
    q.bindValue(":uid", QString::fromStdString(userId));
    q.exec();
}

// Entries
void Database::storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword) {
    m_db.transaction();
    
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO entries (group_id, title, username, encrypted_password, url, notes, totp_secret, entry_type, created_at, updated_at, password_expiry) "
              "VALUES (:gid, :title, :user, :pass, :url, :notes, :totp, :type, :created, :updated, :expiry)");
    
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    
    q.bindValue(":gid", groupId);
    q.bindValue(":title", QString::fromStdString(entry.title));
    q.bindValue(":user", QString::fromStdString(entry.username));
    q.bindValue(":pass", toQByteArray(encryptedPassword));
    q.bindValue(":url", QString::fromStdString(entry.url));
    q.bindValue(":notes", QString::fromStdString(entry.notes));
    q.bindValue(":totp", QString::fromStdString(entry.totpSecret));
    q.bindValue(":type", QString::fromStdString(entry.entryType));
    q.bindValue(":created", now);
    q.bindValue(":updated", now);
    q.bindValue(":expiry", (qint64)entry.passwordExpiry);

    if (!q.exec()) {
        m_db.rollback();
        throw DBException("Failed to insert entry: " + q.lastError().text().toStdString());
    }

    int entryId = q.lastInsertId().toInt();
    entry.id = entryId;

    // Locations
    for (const auto& loc : entry.locations) {
        QSqlQuery lq(m_db);
        lq.prepare("INSERT INTO locations (entry_id, type, value) VALUES (:eid, :type, :val)");
        lq.bindValue(":eid", entryId);
        lq.bindValue(":type", QString::fromStdString(loc.type));
        lq.bindValue(":val", QString::fromStdString(loc.value));
        lq.exec();
    }

    m_db.commit();
}

std::vector<VaultEntry> Database::getEntriesForGroup(int groupId) {
    std::vector<VaultEntry> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, title, username, url, notes, totp_secret, entry_type, created_at, updated_at, last_accessed, password_expiry FROM entries WHERE group_id = :gid");
    q.bindValue(":gid", groupId);
    
    if (q.exec()) {
        while (q.next()) {
            VaultEntry e;
            e.id = q.value("id").toInt();
            e.groupId = groupId;
            e.title = q.value("title").toString().toStdString();
            e.username = q.value("username").toString().toStdString();
            e.url = q.value("url").toString().toStdString();
            e.notes = q.value("notes").toString().toStdString();
            e.totpSecret = q.value("totp_secret").toString().toStdString();
            e.entryType = q.value("entry_type").toString().toStdString();
            e.createdAt = q.value("created_at").toLongLong();
            e.updatedAt = q.value("updated_at").toLongLong();
            e.lastAccessed = q.value("last_accessed").toLongLong();
            e.passwordExpiry = q.value("password_expiry").toLongLong();
            
            e.locations = getLocationsForEntry(e.id);
            results.push_back(e);
        }
    }
    return results;
}

std::vector<Location> Database::getLocationsForEntry(int entryId) {
    std::vector<Location> locs;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, type, value FROM locations WHERE entry_id = :eid");
    q.bindValue(":eid", entryId);
    if (q.exec()) {
        while (q.next()) {
            locs.emplace_back(
                q.value("id").toInt(),
                q.value("type").toString().toStdString(),
                q.value("value").toString().toStdString()
            );
        }
    }
    return locs;
}

std::vector<unsigned char> Database::getEncryptedPassword(int entryId) {
    QSqlQuery q(m_db);
    q.prepare("SELECT encrypted_password FROM entries WHERE id = :id");
    q.bindValue(":id", entryId);
    if (q.exec() && q.next()) {
        return toVector(q.value(0).toByteArray());
    }
    return {};
}

bool Database::deleteEntry(int entryId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM entries WHERE id = :id");
    q.bindValue(":id", entryId);
    return q.exec();
}

void Database::updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword) {
    m_db.transaction();
    QSqlQuery q(m_db);
    QString sql = "UPDATE entries SET title=:title, username=:user, url=:url, notes=:notes, totp_secret=:totp, entry_type=:type, updated_at=:updated, password_expiry=:expiry";
    if (newEncryptedPassword) {
        sql += ", encrypted_password=:pass";
    }
    sql += " WHERE id=:id";
    
    q.prepare(sql);
    q.bindValue(":title", QString::fromStdString(entry.title));
    q.bindValue(":user", QString::fromStdString(entry.username));
    q.bindValue(":url", QString::fromStdString(entry.url));
    q.bindValue(":notes", QString::fromStdString(entry.notes));
    q.bindValue(":totp", QString::fromStdString(entry.totpSecret));
    q.bindValue(":type", QString::fromStdString(entry.entryType));
    q.bindValue(":updated", QDateTime::currentMSecsSinceEpoch());
    q.bindValue(":expiry", (qint64)entry.passwordExpiry);
    q.bindValue(":id", entry.id);

    if (newEncryptedPassword) {
        q.bindValue(":pass", toQByteArray(*newEncryptedPassword));
    }
    
    if (!q.exec()) {
        m_db.rollback();
        throw DBException("Failed update entry");
    }

    // Update locations
    QSqlQuery delLoc(m_db);
    delLoc.prepare("DELETE FROM locations WHERE entry_id = :id");
    delLoc.bindValue(":id", entry.id);
    delLoc.exec();

    for (const auto& loc : entry.locations) {
        QSqlQuery insLoc(m_db);
        insLoc.prepare("INSERT INTO locations (entry_id, type, value) VALUES (:eid, :type, :val)");
        insLoc.bindValue(":eid", entry.id);
        insLoc.bindValue(":type", QString::fromStdString(loc.type));
        insLoc.bindValue(":val", QString::fromStdString(loc.value));
        insLoc.exec();
    }

    m_db.commit();
}

bool Database::entryExists(const std::string& username, const std::string& locationValue) {
    QSqlQuery q(m_db);
    q.prepare("SELECT count(*) FROM entries e JOIN locations l ON e.id = l.entry_id "
              "WHERE e.username = :user AND l.value = :loc");
    q.bindValue(":user", QString::fromStdString(username));
    q.bindValue(":loc", QString::fromStdString(locationValue));
    if (q.exec() && q.next()) {
        return q.value(0).toInt() > 0;
    }
    return false;
}

std::vector<VaultEntry> Database::findEntriesByLocation(const std::string& locationValue) {
    std::vector<VaultEntry> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT DISTINCT e.id, e.group_id, e.title, e.username, e.url, e.notes FROM entries e "
              "JOIN locations l ON e.id = l.entry_id WHERE l.value LIKE :loc");
    q.bindValue(":loc", "%" + QString::fromStdString(locationValue) + "%");
    if (q.exec()) {
        while (q.next()) {
            VaultEntry e;
            e.id = q.value("id").toInt();
            e.groupId = q.value("group_id").toInt();
            e.title = q.value("title").toString().toStdString();
            e.username = q.value("username").toString().toStdString();
            e.url = q.value("url").toString().toStdString();
            e.notes = q.value("notes").toString().toStdString();
            e.locations = getLocationsForEntry(e.id);
            results.push_back(e);
        }
    }
    return results;
}

std::vector<VaultEntry> Database::searchEntries(const std::string& searchTerm) {
    std::vector<VaultEntry> results;
    QString term = "%" + QString::fromStdString(searchTerm) + "%";
    QSqlQuery q(m_db);
    q.prepare("SELECT id, group_id, title, username, url, notes FROM entries WHERE "
              "title LIKE :term OR username LIKE :term OR url LIKE :term OR notes LIKE :term");
    q.bindValue(":term", term);
    if (q.exec()) {
        while (q.next()) {
            VaultEntry e;
            e.id = q.value("id").toInt();
            e.groupId = q.value("group_id").toInt();
            e.title = q.value("title").toString().toStdString();
            e.username = q.value("username").toString().toStdString();
            e.url = q.value("url").toString().toStdString();
            e.notes = q.value("notes").toString().toStdString();
            results.push_back(e);
        }
    }
    return results;
}

void Database::updateEntryAccessTime(int entryId) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE entries SET access_count = access_count + 1, last_accessed = :now WHERE id = :id");
    q.bindValue(":now", QDateTime::currentMSecsSinceEpoch());
    q.bindValue(":id", entryId);
    q.exec();
}

std::vector<VaultEntry> Database::getRecentlyAccessedEntries(int groupId, int limit) {
    std::vector<VaultEntry> results;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, title, username, url, notes FROM entries WHERE group_id = :gid ORDER BY last_accessed DESC LIMIT :limit");
    q.bindValue(":gid", groupId);
    q.bindValue(":limit", limit);
    if (q.exec()) {
        while (q.next()) {
            VaultEntry e;
            e.id = q.value("id").toInt();
            e.title = q.value("title").toString().toStdString();
            e.username = q.value("username").toString().toStdString();
            e.url = q.value("url").toString().toStdString();
            e.notes = q.value("notes").toString().toStdString();
            results.push_back(e);
        }
    }
    return results;
}

// Metadata
void Database::storeMetadata(const std::string& key, const std::vector<unsigned char>& value) {
    QSqlQuery q(m_db);
    q.prepare("INSERT OR REPLACE INTO metadata (key, value) VALUES (:key, :val)");
    q.bindValue(":key", QString::fromStdString(key));
    q.bindValue(":val", toQByteArray(value));
    q.exec();
}

std::vector<unsigned char> Database::getMetadata(const std::string& key) {
    QSqlQuery q(m_db);
    q.prepare("SELECT value FROM metadata WHERE key = :key");
    q.bindValue(":key", QString::fromStdString(key));
    if (q.exec() && q.next()) {
        return toVector(q.value(0).toByteArray());
    }
    return {}; 
}

// History
void Database::storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO password_history (entry_id, encrypted_password, timestamp) VALUES (:eid, :pass, :ts)");
    q.bindValue(":eid", entryId);
    q.bindValue(":pass", toQByteArray(oldEncryptedPassword));
    q.bindValue(":ts", QDateTime::currentMSecsSinceEpoch());
    q.exec();
}

std::vector<PasswordHistoryEntry> Database::getPasswordHistory(int entryId) {
    std::vector<PasswordHistoryEntry> history;
    QSqlQuery q(m_db);
    q.prepare("SELECT id, encrypted_password, timestamp FROM password_history WHERE entry_id = :eid ORDER BY timestamp DESC");
    q.bindValue(":eid", entryId);
    if (q.exec()) {
        while (q.next()) {
            PasswordHistoryEntry h;
            h.id = q.value("id").toInt();
            h.entryId = entryId;
            QByteArray ba = q.value("encrypted_password").toByteArray();
            h.encryptedPassword = std::string(ba.constData(), ba.length()); 
            h.changedAt = q.value("timestamp").toLongLong();
            history.push_back(h);
        }
    }
    return history;
}

void Database::deleteOldPasswordHistory(int entryId, int keepCount) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM password_history WHERE entry_id = :eid AND id NOT IN "
              "(SELECT id FROM password_history WHERE entry_id = :eid ORDER BY timestamp DESC LIMIT :limit)");
    q.bindValue(":eid", entryId);
    q.bindValue(":limit", keepCount);
    q.exec();
}

// Invites
void Database::storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO pending_invites (sender_id, group_name, payload, status) VALUES (:sender, :group, :payload, 'pending')");
    q.bindValue(":sender", QString::fromStdString(senderId));
    q.bindValue(":group", QString::fromStdString(groupName));
    q.bindValue(":payload", QString::fromStdString(payloadJson));
    q.exec();
}

void Database::updatePendingInviteStatus(int inviteId, const std::string& status) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE pending_invites SET status = :status WHERE id = :id");
    q.bindValue(":status", QString::fromStdString(status));
    q.bindValue(":id", inviteId);
    q.exec();
}

std::vector<PendingInvite> Database::getPendingInvites() {
    std::vector<PendingInvite> invites;
    QSqlQuery q(m_db);
    if (q.exec("SELECT id, sender_id, group_name, payload, status FROM pending_invites")) {
        while (q.next()) {
            PendingInvite pi;
            pi.id = q.value("id").toInt();
            pi.senderId = q.value("sender_id").toString().toStdString();
            pi.groupName = q.value("group_name").toString().toStdString();
            pi.payloadJson = q.value("payload").toString().toStdString();
            pi.status = q.value("status").toString().toStdString();
            invites.push_back(pi);
        }
    }
    return invites;
}

void Database::deletePendingInvite(int inviteId) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM pending_invites WHERE id = :id");
    q.bindValue(":id", inviteId);
    q.exec();
}

} // namespace Core
} // namespace CipherMesh

#endif // DESKTOP implementation block