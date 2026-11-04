// Unit tests for opensearch_parse_result.cpp: response JSON -> column metadata.
// No cluster needed.
#include <string>

#include "gtest/gtest.h"
#include "opensearch_parse_result.h"

namespace {

schema_type SchemaOf(const std::string &response) {
    json_doc doc;
    doc.parse(response);
    schema_type schema;
    GetSchemaInfo(schema, doc);
    return schema;
}

// The SQL plugin adds "alias" when the query aliases a column (and sometimes
// with alias == name).
const std::string kAliasedResponse =
    "{\"schema\": ["
    "  {\"name\": \"Origin\", \"alias\": \"o\", \"type\": \"keyword\"},"
    "  {\"name\": \"OriginWeather\", \"type\": \"keyword\"},"
    "  {\"name\": \"COUNT(*)\", \"alias\": \"n\", \"type\": \"long\"},"
    "  {\"name\": \"Dest\", \"alias\": \"Dest\", \"type\": \"keyword\"}],"
    " \"datarows\": [], \"total\": 0, \"size\": 0, \"status\": 200}";

}  // namespace

// Label: SQL_DESC_LABEL / SQL_DESC_NAME / SQLDescribeCol
TEST(GetSchemaInfo, LabelIsAliasWhenPresent) {
    schema_type schema = SchemaOf(kAliasedResponse);

    ASSERT_EQ((size_t)4, schema.size());
    EXPECT_EQ("o", schema[0].label);
    EXPECT_EQ("OriginWeather", schema[1].label);
    EXPECT_EQ("n", schema[2].label);
    EXPECT_EQ("Dest", schema[3].label);
}

// Base name: SQL_DESC_BASE_COLUMN_NAME stays the underlying column
TEST(GetSchemaInfo, BaseNameIsUnderlyingColumnName) {
    schema_type schema = SchemaOf(kAliasedResponse);

    ASSERT_EQ((size_t)4, schema.size());
    EXPECT_EQ("Origin", schema[0].base_name);
    EXPECT_EQ("OriginWeather", schema[1].base_name);
    EXPECT_EQ("COUNT(*)", schema[2].base_name);
    EXPECT_EQ("Dest", schema[3].base_name);
}
