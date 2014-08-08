#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "sqlite3.h"
#include "unistd.h"
#include "stdbool.h"

#ifndef gbDB_h
#define gbDB_h

#if defined (__cplusplus)
extern "C" {
#endif

void gbDB_CloseDBConnAndQueryStmt(sqlite3*      database,
                                  sqlite3_stmt* queryStmt);

void gbDB_CreateIfNeededDB(const char* dbPath);

bool gbDB_PrepConn_DBPath_CString(const char*    dbPath,
                                  const char*    selectSQL,
                                  sqlite3**      db,
                                  sqlite3_stmt** stmt);
                                  
bool gbDB_BeginOrCommitTransactionReusingDB(sqlite3*   database,
                                            const bool isBegin);
                                            
bool gbDB_ExecSQL_Generic_ReusingDB(sqlite3*    database,
                                    const char* query);
                                    
bool gbDB_ExecSQL_Generic(const char* dbPath,
                          const char* query);
    
int gbDB_ExecSQL_Scalar_ReusingDB(sqlite3* database,
                                  const char* query);

int gbDB_ExecSQL_Scalar(const char* dbPath,
                        const char* query);
                          
bool gbDB_Meta_VACUUM_ReusingDB(sqlite3* database);

bool gbDB_Meta_VACUUM(const char* dbPath);

#if defined (__cplusplus)
}
#endif

#endif