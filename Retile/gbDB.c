#include "gbDB.h"

void gbDB_CloseDBConnAndQueryStmt(sqlite3*      database,
                                  sqlite3_stmt* queryStmt)
{
    sqlite3_finalize(queryStmt);
    sqlite3_close(database);
}//gbDB_CloseDBConnAndQueryStmt


void gbDB_CreateIfNeededDB(const char* dbPath)
{
    sqlite3* db = NULL;
    
    if (sqlite3_open_v2(dbPath, &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK)
    {
        sqlite3_close(db);
    }//if
}//gbDB_CreateIfNeededDB


bool gbDB_PrepConn_DBPath_CString(const char*    dbPath,
                                  const char*    selectSQL,
                                  sqlite3**      db,
                                  sqlite3_stmt** stmt)
{
    bool          retVal = false;
    sqlite3*      _db;
    sqlite3_stmt* _stmt;
    
    if (sqlite3_open_v2(dbPath, &_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) == SQLITE_OK)
    {
        //printf("PrepConn_DBPath_CString: Opened DB: %s\n", dbPath);
        
        if (sqlite3_prepare_v2(_db, selectSQL , -1, &_stmt, NULL) == SQLITE_OK)
        {
            //printf("PrepConn_DBPath_CString: Prepared query: %s\n", selectSQL);
            retVal = true;
        }//if
        else
        {
            printf("PrepConn_DBPath_CString: Error preparing statement: %s\n", sqlite3_errmsg(_db));
            sqlite3_finalize(_stmt);
            sqlite3_close(_db);
        }//else
    }//if
    else
    {
        printf("PrepConn_DBPath_CString: Error opening DB: %s\n", sqlite3_errmsg(_db));
        sqlite3_close(_db);
    }//else
    
    *db   = _db;
    *stmt = _stmt;
    
    return retVal;
}//gbDB_PrepConn_DBPath_CString




bool gbDB_BeginOrCommitTransactionReusingDB(sqlite3*   database,
                                            const bool isBegin)
{
    bool          retVal    = true;
    char*         sqlTrans  = isBegin ? "BEGIN TRANSACTION;" : "COMMIT TRANSACTION;";
    sqlite3_stmt* transStmt;
    
    if (sqlite3_prepare_v2(database, sqlTrans, -1, &transStmt, NULL) == SQLITE_OK)
    {
        if (sqlite3_step(transStmt) != SQLITE_DONE)
        {
            printf("BeginOrCommitTransactionReusingDB: DB error executing %s: %s\n", sqlTrans, sqlite3_errmsg(database));
            retVal = false;
        }//if
        
        sqlite3_finalize(transStmt);
    }//if
    else
    {
        printf("BeginOrCommitTransactionReusingDB: DB error compiling %s: %s\n", sqlTrans, sqlite3_errmsg(database));
        retVal = false;
    }//else
    
    return retVal;
}//gbDB_BeginOrCommitTransactionReusingDB

bool gbDB_ExecSQL_Generic_ReusingDB(sqlite3*    database,
                                    const char* query)
{
    bool retVal = true;
    int  status;
    
    status = sqlite3_exec(database, query, NULL, NULL, NULL);
    
    if (status != SQLITE_OK)
    {
        printf("gbDB_ExecSQL_Generic_ReusingDB: [ERR] [FIRST] Exec SQL failed, status: %d, msg: %s (query: %s)\n", status, sqlite3_errmsg(database), query);
        
        uint32_t        currentSleepMS  = 0;
        const uint32_t kSleepIntervalMS = 100;                      // 100ms
        const uint32_t kSleepIntervalUS = kSleepIntervalMS * 1000;  // 100ms -> us
        const uint32_t kMaxSleepMS      = 60 * 1000;                // 30s   -> ms
        
        while (status != SQLITE_OK)
        {
            if (          (status != SQLITE_BUSY
                           && status != SQLITE_LOCKED)
                || currentSleepMS >= kMaxSleepMS)
            {
                break;
            }//if
            
            status = sqlite3_exec(database, query, NULL, NULL, NULL);
            
            usleep(kSleepIntervalUS);
            currentSleepMS += kSleepIntervalMS;
        }//while
        
        if (currentSleepMS >= kMaxSleepMS)
        {
            printf("gbDB_ExecSQL_Generic_ReusingDB: [ERR] Timeout while DB was busy.\n");
        }//if
        
        if (status != SQLITE_OK)
        {
            printf("gbDB_ExecSQL_Generic_ReusingDB: [ERR] Exec SQL failed, status: %d, msg: %s (query: %s)\n", status, sqlite3_errmsg(database), query);
            retVal = false;;
        }//if
    }//if
    
    /*
     if (sqlite3_exec(database, query, NULL, NULL, NULL) != SQLITE_OK)
     {
     printf("gbDB_ExecSQL_Generic_ReusingDB: exec sql failed, msg: %s\n", sqlite3_errmsg(database));
     retVal = false;;
     }//if
     */
    
    return retVal;
}//gbDB_ExecSQL_Generic_ReusingDB

bool gbDB_ExecSQL_Generic(const char* dbPath,
                          const char* query)
{
    bool retVal = true;
    
    sqlite3 *database;
    
    if (sqlite3_open(dbPath, &database) != SQLITE_OK)
    {
        printf("gbDB_ExecSQL_Generic: abort: db not OK!\n");
        retVal = false;
    }//if
    else
    {
        retVal = gbDB_ExecSQL_Generic_ReusingDB(database, query);
    }//else
    
    sqlite3_close(database);
    
    return retVal;
}//gbDB_ExecSQL_Generic

int gbDB_ExecSQL_Scalar_ReusingDB(sqlite3* database,
                                  const char* query)
{
    int           rowCount          = 0;
    sqlite3_stmt *compiledStatement;
    
    if (sqlite3_prepare_v2(database, query, -1, &compiledStatement, NULL) == SQLITE_OK)
    {
        while(sqlite3_step(compiledStatement) == SQLITE_ROW)
        {
            rowCount = sqlite3_column_int(compiledStatement, 0);
        }//while
    }//if
    
    sqlite3_finalize(compiledStatement);
    
    return rowCount;
}//gbDB_ExecSQL_Scalar_ReusingDB

int gbDB_ExecSQL_Scalar(const char* dbPath,
                        const char* query)
{
    int retVal = -1;
    
    sqlite3 *database;
    
    if (sqlite3_open(dbPath, &database) != SQLITE_OK)
    {
        printf("gbDB_ExecSQL_Scalar: abort: db not OK!\n");
    }//if
    else
    {
        retVal = gbDB_ExecSQL_Scalar_ReusingDB(database, query);
    }//else
    
    sqlite3_close(database);
    
    return retVal;
}//gbDB_ExecSQL_Scalar



bool gbDB_Meta_VACUUM_ReusingDB(sqlite3* database)
{
    return gbDB_ExecSQL_Generic_ReusingDB(database, "VACUUM;");
}//gbDB_Meta_VACUUM_ReusingDB

bool gbDB_Meta_VACUUM(const char* dbPath)
{
    return gbDB_ExecSQL_Generic(dbPath, "VACUUM;");
}//gbDB_Meta_VACUUM