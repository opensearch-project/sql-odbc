#ifndef _OPENSEARCH_PARSE_RESULT_H_
#define _OPENSEARCH_PARSE_RESULT_H_
#include "qresult.h"

#ifdef __cplusplus
std::string GetResultParserError();
extern "C" {
#endif
#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
#include <optional>
#include <string>
#include <vector>

#include "opensearch_helper.h"
// const char* is used instead of string for the cursor, because a NULL cursor
// is sometimes used Cannot pass q_res as reference because it breaks qresult.h
// macros that expect to use -> operator
BOOL CC_from_OpenSearchResult(QResultClass *q_res, ConnectionClass *conn,
                      const char *cursor,
                              OpenSearchResult &opensearch_result);
BOOL CC_Metadata_from_OpenSearchResult(QResultClass *q_res, ConnectionClass *conn,
                               const char *cursor,
                                       OpenSearchResult &opensearch_result);
BOOL CC_No_Metadata_from_OpenSearchResult(QResultClass *q_res, ConnectionClass *conn,
                                  const char *cursor,
                                          OpenSearchResult &opensearch_result);
BOOL CC_Append_Table_Data(json_doc &opensearch_result_doc, QResultClass *q_res,
                          size_t doc_schema_size, ColumnInfoClass &fields);

// One entry of a response's "schema" array
struct ColumnSchema {
    std::string label;      // alias if the query aliased the column, else name
    std::string base_name;  // underlying column name ("name")
    OID type_oid;
};
typedef std::vector< ColumnSchema > schema_type;
void GetSchemaInfo(schema_type &schema, json_doc &opensearch_result_doc);

// Each "datarows" cell as the text handed to the ODBC conversion layer
// (std::nullopt for JSON null). Same conversion AssignRowData uses.
typedef std::vector< std::optional< std::string > > row_text_type;
std::vector< row_text_type > GetDataRowsText(json_doc &opensearch_result_doc);
#endif
#endif
