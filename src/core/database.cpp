#include <sodium.h>
#include "database.hpp"
#include <sqlite3.h>
#include <stdexcept>
#include <ctime>
#include <iostream>

namespace CipherMesh {
namespace Core {

inline void check_sqlite(int rc, sqlite3* db) {
    if (rc != SQLITE_OK) {
        std::string errMsg = sqlite3_errmsg(db);
        throw DBException("SQLite error: " + errMsg);
    }
}

Database::Database() : m_db(nullptr) {}
Database::~Database() { close(); }

void Database::open(const std::string& path) {
    if (m_db) close();
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        std::string errMsg = sqlite3_errmsg(m_db);
        sqlite3_close(m_db);
        m_db = nullptr;
        throw DBException("Cannot open database: " + errMsg);
    }
    exec("PRAGMA foreign_keys = ON;");
}

void Database::close() {
    if (m_db) { sqlite3_close(m_db); m_db = nullptr; }
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
    
    // Add totp_secret column if it doesn't exist (migration for existing databases)
    // This will fail silently if column already exists
    try {
        exec(R"( 
            ALTER TABLE entries ADD COLUMN totp_secret TEXT DEFAULT '';
        )");
    } catch (const DBException&) {
        // Column already exists, ignore error
    }
    
    // Add entry_type column if it doesn't exist (migration for existing databases)
    try {
        exec(R"( 
            ALTER TABLE entries ADD COLUMN entry_type TEXT DEFAULT 'password';
        )");
    } catch (const DBException&) {
        // Column already exists, ignore error
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
    
    // UPDATED: Added 'status' column
    exec(R"( 
        CREATE TABLE IF NOT EXISTS pending_invites ( 
            id INTEGER PRIMARY KEY AUTOINCREMENT, 
            sender_id TEXT NOT NULL, 
            group_name TEXT NOT NULL, 
            payload_json TEXT NOT NULL, 
            timestamp INTEGER,
            status TEXT DEFAULT 'pending' -- 'pending', 'accepted'
        ); 
    )");

    exec(R"( CREATE TABLE IF NOT EXISTS group_members ( id INTEGER PRIMARY KEY AUTOINCREMENT, group_id INTEGER NOT NULL, user_id TEXT NOT NULL, role TEXT DEFAULT 'member', status TEXT DEFAULT 'accepted', FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE, UNIQUE(group_id, user_id) ); )");
    exec(R"( CREATE TABLE IF NOT EXISTS group_settings ( group_id INTEGER PRIMARY KEY, admins_only_write INTEGER DEFAULT 0, FOREIGN KEY(group_id) REFERENCES groups(id) ON DELETE CASCADE ); )");
}

void Database::exec(const std::string& sql) {
    char* zErrMsg = nullptr;
    int rc = sqlite3_exec(m_db, sql.c_str(), 0, 0, &zErrMsg);
    if (rc != SQLITE_OK) {
        std::string errMsg = zErrMsg;
        sqlite3_free(zErrMsg);
        throw DBException("SQL error: " + errMsg);
    }
}

// --- METADATA & GROUPS (Unchanged) ---
void Database::storeMetadata(const std::string& key, const std::vector<unsigned char>& value) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO vault_metadata (key, value) VALUES (?, ?);";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_blob(stmt, 2, value.data(), value.size(), SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<unsigned char> Database::getMetadata(const std::string& key) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT value FROM vault_metadata WHERE key = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int size = sqlite3_column_bytes(stmt, 0);
        std::vector<unsigned char> value(static_cast<const unsigned char*>(blob), static_cast<const unsigned char*>(blob) + size);
        sqlite3_finalize(stmt);
        return value;
    }
    sqlite3_finalize(stmt);
    throw DBException("Metadata key not found: " + key);
}

void Database::storeEncryptedGroup(const std::string& name, const std::vector<unsigned char>& encryptedKey, const std::string& ownerId) {
    exec("BEGIN TRANSACTION;");
    try {
        sqlite3_stmt* stmt1;
        const char* sql1 = "INSERT INTO groups (name, owner_id) VALUES (?, ?);";
        int rc = sqlite3_prepare_v2(m_db, sql1, -1, &stmt1, 0);
        check_sqlite(rc, m_db);
        sqlite3_bind_text(stmt1, 1, name.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt1, 2, ownerId.c_str(), -1, SQLITE_STATIC);
        if (sqlite3_step(stmt1) != SQLITE_DONE) { sqlite3_finalize(stmt1); throw DBException("Failed to insert group name"); }
        sqlite3_finalize(stmt1);
        sqlite3_int64 groupId = sqlite3_last_insert_rowid(m_db);
        sqlite3_stmt* stmt2;
        const char* sql2 = "INSERT INTO group_keys (group_id, encrypted_group_key) VALUES (?, ?);";
        rc = sqlite3_prepare_v2(m_db, sql2, -1, &stmt2, 0);
        check_sqlite(rc, m_db);
        sqlite3_bind_int64(stmt2, 1, groupId);
        sqlite3_bind_blob(stmt2, 2, encryptedKey.data(), encryptedKey.size(), SQLITE_STATIC);
        if (sqlite3_step(stmt2) != SQLITE_DONE) { sqlite3_finalize(stmt2); throw DBException("Failed to insert group key"); }
        sqlite3_finalize(stmt2);
        exec("INSERT INTO group_settings (group_id, admins_only_write) VALUES (" + std::to_string(groupId) + ", 0);");
        exec("COMMIT;");
    } catch (...) { exec("ROLLBACK;"); throw; }
}

std::vector<unsigned char> Database::getEncryptedGroupKey(const std::string& name, int& groupId) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT g.id, gk.encrypted_group_key FROM groups g JOIN group_keys gk ON g.id = gk.group_id WHERE g.name = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        groupId = sqlite3_column_int(stmt, 0);
        const void* blob = sqlite3_column_blob(stmt, 1);
        int size = sqlite3_column_bytes(stmt, 1);
        std::vector<unsigned char> value(static_cast<const unsigned char*>(blob), static_cast<const unsigned char*>(blob) + size);
        sqlite3_finalize(stmt);
        return value;
    }
    sqlite3_finalize(stmt);
    throw DBException("Group not found: " + name);
}

std::vector<unsigned char> Database::getEncryptedGroupKeyById(int groupId) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT encrypted_group_key FROM group_keys WHERE group_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int size = sqlite3_column_bytes(stmt, 0);
        std::vector<unsigned char> value(static_cast<const unsigned char*>(blob), static_cast<const unsigned char*>(blob) + size);
        sqlite3_finalize(stmt);
        return value;
    }
    sqlite3_finalize(stmt);
    throw DBException("Group key not found for ID");
}

std::map<int, std::vector<unsigned char>> Database::getAllEncryptedGroupKeys() {
    std::map<int, std::vector<unsigned char>> keys;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT group_id, encrypted_group_key FROM group_keys;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int groupId = sqlite3_column_int(stmt, 0);
        const void* blob = sqlite3_column_blob(stmt, 1);
        int size = sqlite3_column_bytes(stmt, 1);
        keys[groupId] = std::vector<unsigned char>(static_cast<const unsigned char*>(blob), static_cast<const unsigned char*>(blob) + size);
    }
    sqlite3_finalize(stmt);
    return keys;
}

void Database::updateEncryptedGroupKey(int groupId, const std::vector<unsigned char>& newKey) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE group_keys SET encrypted_group_key = ? WHERE group_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_blob(stmt, 1, newKey.data(), newKey.size(), SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, groupId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<std::string> Database::getAllGroupNames() {
    std::vector<std::string> names;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT name FROM groups ORDER BY name;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* name = sqlite3_column_text(stmt, 0);
        names.push_back(std::string(reinterpret_cast<const char*>(name)));
    }
    sqlite3_finalize(stmt);
    return names;
}

int Database::getGroupId(const std::string& name) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id FROM groups WHERE name = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return id;
    }
    sqlite3_finalize(stmt);
    throw DBException("Group not found: " + name);
}

int Database::getGroupIdForEntry(int entryId) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT group_id FROM entries WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
        return id;
    }
    sqlite3_finalize(stmt);
    throw DBException("Entry not found");
}

bool Database::deleteGroup(const std::string& name) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM groups WHERE name = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_STATIC);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_changes(m_db) > 0;
}

// --- ENTRIES ---
void Database::storeEntry(int groupId, VaultEntry& entry, const std::vector<unsigned char>& encryptedPassword) {
    exec("BEGIN TRANSACTION;");
    try {
        sqlite3_stmt* stmt;
        long long now = std::time(nullptr);
        const char* sql = "INSERT INTO entries (group_id, title, username, notes, encrypted_password, created_at, last_modified, last_accessed, password_expiry, totp_secret, entry_type) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
        int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
        check_sqlite(rc, m_db);
        sqlite3_bind_int(stmt, 1, groupId);
        sqlite3_bind_text(stmt, 2, entry.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, entry.username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 4, entry.notes.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_blob(stmt, 5, encryptedPassword.data(), encryptedPassword.size(), SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 6, now); // created_at
        sqlite3_bind_int64(stmt, 7, now); // last_modified
        sqlite3_bind_int64(stmt, 8, now); // last_accessed
        sqlite3_bind_int64(stmt, 9, entry.passwordExpiry); // password_expiry
        sqlite3_bind_text(stmt, 10, entry.totp_secret.c_str(), -1, SQLITE_STATIC); // totp_secret
        sqlite3_bind_text(stmt, 11, entry.entry_type.c_str(), -1, SQLITE_STATIC); // entry_type
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        entry.id = sqlite3_last_insert_rowid(m_db);
        entry.createdAt = now;
        entry.lastModified = now;
        entry.lastAccessed = now;
        
        sqlite3_stmt* loc_stmt;
        const char* loc_sql = "INSERT INTO locations (entry_id, type, value) VALUES (?, ?, ?);";
        rc = sqlite3_prepare_v2(m_db, loc_sql, -1, &loc_stmt, 0);
        check_sqlite(rc, m_db);
        for (Location& loc : entry.locations) {
            sqlite3_bind_int(loc_stmt, 1, entry.id);
            sqlite3_bind_text(loc_stmt, 2, loc.type.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(loc_stmt, 3, loc.value.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(loc_stmt);
            loc.id = sqlite3_last_insert_rowid(m_db); 
            sqlite3_reset(loc_stmt);
        }
        sqlite3_finalize(loc_stmt);
        exec("COMMIT;");
    } catch (...) { exec("ROLLBACK;"); throw; }
}

std::vector<VaultEntry> Database::getEntriesForGroup(int groupId) {
    std::vector<VaultEntry> entries;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, title, username, notes, created_at, last_modified, last_accessed, password_expiry, totp_secret, entry_type FROM entries WHERE group_id = ? ORDER BY title;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        VaultEntry entry(id, title, username, notes);
        entry.createdAt = sqlite3_column_int64(stmt, 4);
        entry.lastModified = sqlite3_column_int64(stmt, 5);
        entry.lastAccessed = sqlite3_column_int64(stmt, 6);
        entry.passwordExpiry = sqlite3_column_int64(stmt, 7);
        const unsigned char* totp_ptr = sqlite3_column_text(stmt, 8);
        entry.totp_secret = totp_ptr ? reinterpret_cast<const char*>(totp_ptr) : "";
        const unsigned char* type_ptr = sqlite3_column_text(stmt, 9);
        entry.entry_type = type_ptr ? reinterpret_cast<const char*>(type_ptr) : "password";
        entry.locations = getLocationsForEntry(id);
        entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return entries;
}

std::vector<Location> Database::getLocationsForEntry(int entryId) {
    std::vector<Location> locations;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, type, value FROM locations WHERE entry_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string type = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string value = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        locations.emplace_back(id, type, value);
    }
    sqlite3_finalize(stmt);
    return locations;
}

std::vector<VaultEntry> Database::findEntriesByLocation(const std::string& locationValue) {
    std::vector<VaultEntry> entries;
    sqlite3_stmt* stmt;
    
    // First try exact match
    const char* sql = R"(
        SELECT DISTINCT e.id, e.title, e.username, e.notes 
        FROM entries e 
        JOIN locations l ON e.id = l.entry_id 
        WHERE l.value = ?
        UNION
        SELECT DISTINCT e.id, e.title, e.username, e.notes 
        FROM entries e 
        JOIN locations l ON e.id = l.entry_id 
        WHERE ? LIKE '%' || l.value || '%' OR l.value LIKE '%' || ? || '%';
    )";
    
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, locationValue.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, locationValue.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, locationValue.c_str(), -1, SQLITE_STATIC);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        VaultEntry entry(id, title, username, notes);
        entry.locations = getLocationsForEntry(id); 
        entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return entries;
}

std::vector<VaultEntry> Database::searchEntries(const std::string& searchTerm) {
    std::vector<VaultEntry> entries;
    sqlite3_stmt* stmt;
    const char* sql = R"( SELECT id, title, username, notes, totp_secret, entry_type FROM entries WHERE title LIKE ? OR username LIKE ? OR notes LIKE ? UNION SELECT DISTINCT e.id, e.title, e.username, e.notes, e.totp_secret, e.entry_type FROM entries e JOIN locations l ON e.id = l.entry_id WHERE l.value LIKE ?; )";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    std::string likeTerm = "%" + searchTerm + "%";
    sqlite3_bind_text(stmt, 1, likeTerm.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, likeTerm.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, likeTerm.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, likeTerm.c_str(), -1, SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        const unsigned char* totp_ptr = sqlite3_column_text(stmt, 4);
        const unsigned char* type_ptr = sqlite3_column_text(stmt, 5);
        VaultEntry entry(id, title, username, notes);
        entry.totp_secret = totp_ptr ? reinterpret_cast<const char*>(totp_ptr) : "";
        entry.entry_type = type_ptr ? reinterpret_cast<const char*>(type_ptr) : "password";
        entry.locations = getLocationsForEntry(id);
        entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return entries;
}

std::vector<unsigned char> Database::getEncryptedPassword(int entryId) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT encrypted_password FROM entries WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int size = sqlite3_column_bytes(stmt, 0);
        std::vector<unsigned char> value(static_cast<const unsigned char*>(blob), static_cast<const unsigned char*>(blob) + size);
        sqlite3_finalize(stmt);
        return value;
    }
    sqlite3_finalize(stmt);
    throw DBException("Entry not found");
}

bool Database::deleteEntry(int entryId) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM entries WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return sqlite3_changes(m_db) > 0;
}

void Database::updateEntry(const VaultEntry& entry, const std::vector<unsigned char>* newEncryptedPassword) {
    exec("BEGIN TRANSACTION;");
    try {
        sqlite3_stmt* stmt;
        const char* sql = "UPDATE entries SET title = ?, username = ?, notes = ?, last_modified = ?, password_expiry = ?, totp_secret = ?, entry_type = ? WHERE id = ?;";
        int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
        check_sqlite(rc, m_db);
        sqlite3_bind_text(stmt, 1, entry.title.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, entry.username.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, entry.notes.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int64(stmt, 4, std::time(nullptr));
        sqlite3_bind_int64(stmt, 5, entry.passwordExpiry);
        sqlite3_bind_text(stmt, 6, entry.totp_secret.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 7, entry.entry_type.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 8, entry.id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if (newEncryptedPassword) {
            // Save old password to history first
            try {
                std::vector<unsigned char> oldPassword = getEncryptedPassword(entry.id);
                storePasswordHistory(entry.id, oldPassword);
                // Keep only last 10 passwords
                deleteOldPasswordHistory(entry.id, 10);
            } catch (...) {
                // If entry doesn't exist or error, continue with password update
            }
            
            sqlite3_stmt* pass_stmt;
            const char* pass_sql = "UPDATE entries SET encrypted_password = ? WHERE id = ?;";
            rc = sqlite3_prepare_v2(m_db, pass_sql, -1, &pass_stmt, 0);
            check_sqlite(rc, m_db);
            sqlite3_bind_blob(pass_stmt, 1, newEncryptedPassword->data(), newEncryptedPassword->size(), SQLITE_STATIC);
            sqlite3_bind_int(pass_stmt, 2, entry.id);
            sqlite3_step(pass_stmt);
            sqlite3_finalize(pass_stmt);
        }

        sqlite3_stmt* del_stmt;
        const char* del_sql = "DELETE FROM locations WHERE entry_id = ?;";
        rc = sqlite3_prepare_v2(m_db, del_sql, -1, &del_stmt, 0);
        check_sqlite(rc, m_db);
        sqlite3_bind_int(del_stmt, 1, entry.id);
        sqlite3_step(del_stmt);
        sqlite3_finalize(del_stmt);

        sqlite3_stmt* loc_stmt;
        const char* loc_sql = "INSERT INTO locations (entry_id, type, value) VALUES (?, ?, ?);";
        rc = sqlite3_prepare_v2(m_db, loc_sql, -1, &loc_stmt, 0);
        check_sqlite(rc, m_db);
        for (const Location& loc : entry.locations) {
            sqlite3_bind_int(loc_stmt, 1, entry.id);
            sqlite3_bind_text(loc_stmt, 2, loc.type.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(loc_stmt, 3, loc.value.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(loc_stmt);
            sqlite3_reset(loc_stmt);
        }
        sqlite3_finalize(loc_stmt);
        exec("COMMIT;");
    } catch (...) { exec("ROLLBACK;"); throw; }
}

bool Database::entryExists(const std::string& username, const std::string& locationValue) {
    sqlite3_stmt* stmt;
    const char* sql = R"( SELECT 1 FROM entries e JOIN locations l ON e.id = l.entry_id WHERE e.username = ? AND l.value = ? LIMIT 1; )";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, locationValue.c_str(), -1, SQLITE_STATIC);
    bool exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

// --- PASSWORD HISTORY ---
void Database::storePasswordHistory(int entryId, const std::vector<unsigned char>& oldEncryptedPassword) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO password_history (entry_id, encrypted_password, changed_at) VALUES (?, ?, ?);";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    sqlite3_bind_blob(stmt, 2, oldEncryptedPassword.data(), oldEncryptedPassword.size(), SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 3, std::time(nullptr));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<PasswordHistoryEntry> Database::getPasswordHistory(int entryId) {
    std::vector<PasswordHistoryEntry> history;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, encrypted_password, changed_at FROM password_history WHERE entry_id = ? ORDER BY changed_at DESC;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        const void* blob = sqlite3_column_blob(stmt, 1);
        int size = sqlite3_column_bytes(stmt, 1);
        std::string encPwd(static_cast<const char*>(blob), size);
        long long changedAt = sqlite3_column_int64(stmt, 2);
        history.emplace_back(id, entryId, encPwd, changedAt);
    }
    sqlite3_finalize(stmt);
    return history;
}

void Database::deleteOldPasswordHistory(int entryId, int keepCount) {
    sqlite3_stmt* stmt;
    const char* sql = R"(
        DELETE FROM password_history 
        WHERE entry_id = ? AND id NOT IN (
            SELECT id FROM password_history 
            WHERE entry_id = ? 
            ORDER BY changed_at DESC 
            LIMIT ?
        );
    )";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, entryId);
    sqlite3_bind_int(stmt, 2, entryId);
    sqlite3_bind_int(stmt, 3, keepCount);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- ENTRY ACCESS TRACKING ---
void Database::updateEntryAccessTime(int entryId) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE entries SET last_accessed = ? WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int64(stmt, 1, std::time(nullptr));
    sqlite3_bind_int(stmt, 2, entryId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<VaultEntry> Database::getRecentlyAccessedEntries(int groupId, int limit) {
    std::vector<VaultEntry> entries;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, title, username, notes, created_at, last_modified, last_accessed, password_expiry, totp_secret, entry_type FROM entries WHERE group_id = ? AND last_accessed > 0 ORDER BY last_accessed DESC LIMIT ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int id = sqlite3_column_int(stmt, 0);
        std::string title = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        std::string username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        std::string notes = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        VaultEntry entry(id, title, username, notes);
        entry.createdAt = sqlite3_column_int64(stmt, 4);
        entry.lastModified = sqlite3_column_int64(stmt, 5);
        entry.lastAccessed = sqlite3_column_int64(stmt, 6);
        entry.passwordExpiry = sqlite3_column_int64(stmt, 7);
        const unsigned char* totp_ptr = sqlite3_column_text(stmt, 8);
        entry.totp_secret = totp_ptr ? reinterpret_cast<const char*>(totp_ptr) : "";
        const unsigned char* type_ptr = sqlite3_column_text(stmt, 9);
        entry.entry_type = type_ptr ? reinterpret_cast<const char*>(type_ptr) : "password";
        entry.locations = getLocationsForEntry(id);
        entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
    return entries;
}

// --- PENDING INVITES ---
void Database::storePendingInvite(const std::string& senderId, const std::string& groupName, const std::string& payloadJson) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO pending_invites (sender_id, group_name, payload_json, timestamp) VALUES (?, ?, ?, ?);";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, senderId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, groupName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, payloadJson.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt, 4, std::time(nullptr));
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- NEW: Update status ---
void Database::updatePendingInviteStatus(int inviteId, const std::string& status) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE pending_invites SET status = ? WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, inviteId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<PendingInvite> Database::getPendingInvites() {
    std::vector<PendingInvite> invites;
    // UPDATED: Now fetching status
    const char* sql = "SELECT id, sender_id, group_name, payload_json, timestamp, status FROM pending_invites ORDER BY timestamp DESC;";
    sqlite3_stmt* stmt;
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PendingInvite invite;
        invite.id = sqlite3_column_int(stmt, 0);
        invite.senderId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        invite.groupName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        invite.payloadJson = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        invite.timestamp = sqlite3_column_int64(stmt, 4);
        // Status might be null if old DB, handle gracefully or ensure schema upgrade
        const unsigned char* statusText = sqlite3_column_text(stmt, 5);
        invite.status = statusText ? reinterpret_cast<const char*>(statusText) : "pending";
        invites.push_back(invite);
    }
    sqlite3_finalize(stmt);
    return invites;
}

void Database::deletePendingInvite(int inviteId) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM pending_invites WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, inviteId);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

// --- MEMBERS ---
void Database::addGroupMember(int groupId, const std::string& userId, const std::string& role, const std::string& status) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO group_members (group_id, user_id, role, status) VALUES (?, ?, ?, ?);";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, role.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, status.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::removeGroupMember(int groupId, const std::string& userId) {
    sqlite3_stmt* stmt;
    const char* sql = "DELETE FROM group_members WHERE group_id = ? AND user_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    sqlite3_bind_text(stmt, 2, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::updateGroupMemberRole(int groupId, const std::string& userId, const std::string& newRole) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE group_members SET role = ? WHERE group_id = ? AND user_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, newRole.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, groupId);
    sqlite3_bind_text(stmt, 3, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void Database::updateGroupMemberStatus(int groupId, const std::string& userId, const std::string& newStatus) {
    sqlite3_stmt* stmt;
    const char* sql = "UPDATE group_members SET status = ? WHERE group_id = ? AND user_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_text(stmt, 1, newStatus.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, groupId);
    sqlite3_bind_text(stmt, 3, userId.c_str(), -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

std::vector<GroupMember> Database::getGroupMembers(int groupId) {
    std::vector<GroupMember> members;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT user_id, role, status FROM group_members WHERE group_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GroupMember m;
        m.userId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        m.role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        m.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        members.push_back(m);
    }
    sqlite3_finalize(stmt);
    return members;
}

void Database::setGroupPermissions(int groupId, bool adminsOnly) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT OR REPLACE INTO group_settings (group_id, admins_only_write) VALUES (?, ?);";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    sqlite3_bind_int(stmt, 2, adminsOnly ? 1 : 0);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

GroupPermissions Database::getGroupPermissions(int groupId) {
    GroupPermissions p;
    p.adminsOnlyWrite = false;
    sqlite3_stmt* stmt;
    const char* sql = "SELECT admins_only_write FROM group_settings WHERE group_id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        p.adminsOnlyWrite = (sqlite3_column_int(stmt, 0) != 0);
    }
    sqlite3_finalize(stmt);
    return p;
}

std::string Database::getGroupOwner(int groupId) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT owner_id FROM groups WHERE id = ?;";
    int rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, 0);
    check_sqlite(rc, m_db);
    sqlite3_bind_int(stmt, 1, groupId);
    std::string owner = "me";
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        owner = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return owner;
}

}
}