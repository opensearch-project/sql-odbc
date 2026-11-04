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

}  // namespace

// The SQL plugin only sends "alias" when the query aliases a column.
TEST(GetSchemaInfo, UsesAliasAsColumnNameWhenPresent) {
    schema_type schema = SchemaOf(
        "{\"schema\": ["
        "  {\"name\": \"Origin\", \"alias\": \"o\", \"type\": \"keyword\"},"
        "  {\"name\": \"OriginWeather\", \"type\": \"keyword\"},"
        "  {\"name\": \"COUNT(*)\", \"alias\": \"n\", \"type\": \"long\"}],"
        " \"datarows\": [], \"total\": 0, \"size\": 0, \"status\": 200}");

    ASSERT_EQ((size_t)3, schema.size());
    EXPECT_EQ("o", schema[0].first);
    EXPECT_EQ("OriginWeather", schema[1].first);
    EXPECT_EQ("n", schema[2].first);
}
