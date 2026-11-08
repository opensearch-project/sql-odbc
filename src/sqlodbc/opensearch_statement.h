#ifndef _OPENSEARCH_STATEMENT_H_
#define _OPENSEARCH_STATEMENT_H_

#include "opensearch_parse_result.h"
#include "qresult.h"
#include "statement.h"

#ifdef __cplusplus
extern "C" {
#endif
RETCODE RePrepareStatement(StatementClass *stmt);
RETCODE PrepareStatement(StatementClass* stmt, const SQLCHAR *stmt_str, SQLINTEGER stmt_sz);
RETCODE ExecuteStatement(StatementClass *stmt, BOOL commit);
QResultClass *SendQueryGetResult(StatementClass *stmt, BOOL commit);
RETCODE AssignResult(StatementClass *stmt);
SQLRETURN OPENSEARCHAPI_Cancel(HSTMT hstmt);
// Appends the next page of rows to stmt's result. SQL_ERROR if a page
// failed, setting stmt's error only if report_error.
SQLRETURN GetNextResultSet(StatementClass *stmt, BOOL report_error);
void ClearOpenSearchResult(void *opensearch_result);
#ifdef __cplusplus
}
#endif

#endif
