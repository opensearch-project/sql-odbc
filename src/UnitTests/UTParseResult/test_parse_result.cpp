// Unit tests for opensearch_parse_result.cpp: response JSON -> column metadata.
// No cluster needed.
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "gtest/gtest.h"
#include "opensearch_parse_result.h"

namespace {

schema_type SchemaOf(const std::string &response) {
    json_doc doc = json_doc::parse(response);
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

// --- Cell text: what every datarows value becomes before ODBC conversion ---

namespace {

row_text_type SingleRowOf(const std::string &row_json) {
    json_doc doc = json_doc::parse(
        "{\"schema\": [], \"datarows\": [" + row_json
        + "], \"total\": 1, \"size\": 1, \"status\": 200}");
    std::vector< row_text_type > rows = GetDataRowsText(doc);
    EXPECT_EQ((size_t)1, rows.size());
    return rows.empty() ? row_text_type() : rows[0];
}

// The text must parse back to exactly the double the server sent.
void ExpectSameDouble(double expected, const std::optional< std::string > &text) {
    ASSERT_TRUE(text.has_value());
    char *end = nullptr;
    double actual = std::strtod(text->c_str(), &end);
    EXPECT_EQ('\0', *end) << "trailing characters in \"" << *text << "\"";
    EXPECT_EQ(expected, actual) << "lossy text \"" << *text << "\"";
}

}  // namespace

TEST(CellText, NullIsNullopt) {
    row_text_type row = SingleRowOf("[null, \"\"]");
    ASSERT_EQ((size_t)2, row.size());
    EXPECT_FALSE(row[0].has_value());
    ASSERT_TRUE(row[1].has_value());  // empty string is not NULL
    EXPECT_EQ("", *row[1]);
}

TEST(CellText, BooleansAndIntegersAreExact) {
    row_text_type row = SingleRowOf(
        "[true, false, 0, -42, 9223372036854775807, -9223372036854775808,"
        " 18446744073709551615]");
    ASSERT_EQ((size_t)7, row.size());
    EXPECT_EQ("true", *row[0]);
    EXPECT_EQ("false", *row[1]);
    EXPECT_EQ("0", *row[2]);
    EXPECT_EQ("-42", *row[3]);
    EXPECT_EQ("9223372036854775807", *row[4]);
    EXPECT_EQ("-9223372036854775808", *row[5]);
    EXPECT_EQ("18446744073709551615", *row[6]);
}

// Regression: doubles were formatted at 6 significant digits
// (123456.789 -> "123457", pi -> "3.14159").
TEST(CellText, DoublesKeepFullPrecision) {
    row_text_type row = SingleRowOf(
        "[123456.789, 3.141592653589793, 1.0E20, 0.1, 1e-7, -2.5,"
        " 1.7976931348623157e308, 4.9e-324]");
    ASSERT_EQ((size_t)8, row.size());
    ExpectSameDouble(123456.789, row[0]);
    ExpectSameDouble(3.141592653589793, row[1]);
    ExpectSameDouble(1.0E20, row[2]);
    ExpectSameDouble(0.1, row[3]);
    ExpectSameDouble(1e-7, row[4]);
    ExpectSameDouble(-2.5, row[5]);
    ExpectSameDouble(1.7976931348623157e308, row[6]);
    ExpectSameDouble(4.9e-324, row[7]);
}

TEST(CellText, StringsAreDecodedContents) {
    row_text_type row = SingleRowOf(
        "[\"plain\", \"q\\\" b\\\\ nl\\n tab\\t\", \"\\u00e9\xF0\x9F\x98\x80\"]");
    ASSERT_EQ((size_t)3, row.size());
    EXPECT_EQ("plain", *row[0]);
    EXPECT_EQ("q\" b\\ nl\n tab\t", *row[1]);
    EXPECT_EQ("\xC3\xA9\xF0\x9F\x98\x80", *row[2]);  // é😀 as UTF-8
}

// Object/nested fields arrive as JSON values; they are passed on as compact
// JSON text, keeping the server's key order.
TEST(CellText, NestedValuesAreCompactJsonInServerOrder) {
    row_text_type row = SingleRowOf(
        "[{\"zeta\": \"z\", \"alpha\": 1, \"n\": 0.1, \"s\": \"a\\\"b\"},"
        " [\"a\", 1, null, true], {}, [],"
        " {\"inner\": {\"y\": [2, 1], \"x\": null}}]");
    ASSERT_EQ((size_t)5, row.size());
    EXPECT_EQ("{\"zeta\":\"z\",\"alpha\":1,\"n\":0.1,\"s\":\"a\\\"b\"}", *row[0]);
    EXPECT_EQ("[\"a\",1,null,true]", *row[1]);
    EXPECT_EQ("{}", *row[2]);
    EXPECT_EQ("[]", *row[3]);
    EXPECT_EQ("{\"inner\":{\"y\":[2,1],\"x\":null}}", *row[4]);
}
