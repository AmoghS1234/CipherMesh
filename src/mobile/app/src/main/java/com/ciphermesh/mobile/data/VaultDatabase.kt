package com.ciphermesh.mobile.data

import android.content.Context
import androidx.room.*

// 1. The Entity (Matches your Desktop Table Structure)
@Entity(tableName = "entries")
data class VaultEntry(
    @PrimaryKey(autoGenerate = true) val id: Int = 0,
    val groupId: Int = 0,
    val title: String,
    val username: String,
    val encryptedPassword: ByteArray, // Stored as BLOB
    val url: String = "",
    val notes: String = "",
    val iconId: String = "default"
) {
    // Auto-generated equals/hashCode needed for ByteArray fields in Data Classes
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (javaClass != other?.javaClass) return false
        other as VaultEntry
        return id == other.id && encryptedPassword.contentEquals(other.encryptedPassword)
    }
    override fun hashCode(): Int = id
}

// 2. The DAO (Database Access Object)
@Dao
interface VaultDao {
    @Query("SELECT * FROM entries ORDER BY title ASC")
    fun getAllEntries(): kotlinx.coroutines.flow.Flow<List<VaultEntry>>

    @Insert(onConflict = OnConflictStrategy.REPLACE)
    suspend fun insert(entry: VaultEntry)

    @Delete
    suspend fun delete(entry: VaultEntry)
}

// 3. The Database Instance
@Database(entities = [VaultEntry::class], version = 1)
abstract class AppDatabase : RoomDatabase() {
    abstract fun vaultDao(): VaultDao

    companion object {
        @Volatile private var INSTANCE: AppDatabase? = null

        fun getDatabase(context: Context): AppDatabase {
            return INSTANCE ?: synchronized(this) {
                val instance = Room.databaseBuilder(
                    context.applicationContext,
                    AppDatabase::class.java,
                    "ciphermesh_mobile.db"
                ).build()
                INSTANCE = instance
                instance
            }
        }
    }
}