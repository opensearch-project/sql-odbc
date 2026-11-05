#ifndef OPENSEARCH_JSON_H
#define OPENSEARCH_JSON_H

// The driver's JSON type (nlohmann/json). ordered_json keeps object keys in
// the order the server sent them, which matters when nested values are passed
// on to ODBC clients as JSON text.
//
// Windows builds define NOMINMAX (src/CMakeLists.txt), so include order
// relative to <windows.h> doesn't matter.
#include <nlohmann/json.hpp>

typedef nlohmann::ordered_json json_doc;

#endif  // OPENSEARCH_JSON_H
