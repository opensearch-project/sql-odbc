// clang-format off
#include "pch.h"
#include "unit_test_helper.h"
#include "opensearch_communication.h"
#include "opensearch_helper.h"
#include <algorithm>
// clang-format on

const std::string valid_host = (use_ssl ? "https://localhost" : "localhost");
const std::string valid_port = test_port;
const std::string valid_user = test_user;
const std::string valid_pw = test_password;
const std::string valid_region = "us-west-3";
const std::string valid_tunnel_host = "";
const std::string query =
    "SELECT Origin FROM opensearch_dashboards_sample_data_flights LIMIT 5";
const std::string all_columns_flights_query =
    "SELECT * FROM opensearch_dashboards_sample_data_flights LIMIT 5";
const std::string some_columns_flights_query =
    "SELECT Origin, OriginWeather FROM opensearch_dashboards_sample_data_flights LIMIT 5";
const std::string invalid_query = "SELECT";
const int EXECUTION_SUCCESS = 0;
const int EXECUTION_ERROR = -1;
const std::string fetch_size = "0";
const int all_columns_flights_count = 27;
const int some_columns_flights_count = 2;
runtime_options valid_conn_opt_val = {
    {valid_host, valid_port, "1", "0"},
    {"BASIC", valid_user, valid_pw, valid_region, valid_tunnel_host},
    {use_ssl, false, "", "", "", ""}};

TEST(TestOpenSearchExecDirect, ValidQuery) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());
    EXPECT_EQ(EXECUTION_SUCCESS,
              OpenSearchExecDirect(&conn, some_columns_flights_query.c_str(), fetch_size.c_str()));
}

TEST(TestOpenSearchExecDirect, MissingQuery) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());
    EXPECT_EQ(EXECUTION_ERROR,
              OpenSearchExecDirect(&conn, NULL, fetch_size.c_str()));
}

TEST(TestOpenSearchExecDirect, MissingConnection) {
    EXPECT_EQ(EXECUTION_ERROR,
              OpenSearchExecDirect(NULL, query.c_str(), fetch_size.c_str()));
}

// Conn::ExecDirect

TEST(TestConnExecDirect, ValidQueryAllColumns) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());

    conn.ExecDirect(all_columns_flights_query.c_str(), fetch_size.c_str());
    OpenSearchResult* result = conn.PopResult();
    ASSERT_NE(nullptr, result);
    EXPECT_EQ("SELECT", result->command_type);
    EXPECT_FALSE(result->result_json.empty());
    EXPECT_EQ(all_columns_flights_count, result->num_fields);
    EXPECT_EQ((size_t)all_columns_flights_count, result->column_info.size());
}

TEST(TestConnExecDirect, ValidQuerySomeColumns) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());

    conn.ExecDirect(some_columns_flights_query.c_str(), fetch_size.c_str());
    OpenSearchResult* result = conn.PopResult();
    ASSERT_NE(nullptr, result);
    EXPECT_EQ("SELECT", result->command_type);
    EXPECT_FALSE(result->result_json.empty());
    EXPECT_EQ(some_columns_flights_count, result->num_fields);
    EXPECT_EQ((size_t)some_columns_flights_count, result->column_info.size());
}

TEST(TestConnExecDirect, ColumnAliasesAreUsedAsColumnNames) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());

    conn.ExecDirect(
        "SELECT Origin AS o, OriginWeather FROM "
        "opensearch_dashboards_sample_data_flights LIMIT 1",
        fetch_size.c_str());
    OpenSearchResult* result = conn.PopResult();
    ASSERT_NE(nullptr, result);
    ASSERT_EQ((size_t)2, result->column_info.size());
    EXPECT_EQ("o", result->column_info[0].field_name);
    // Unaliased columns keep their name
    EXPECT_EQ("OriginWeather", result->column_info[1].field_name);
}

TEST(TestConnExecDirect, InvalidQuery) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());

    conn.ExecDirect(invalid_query.c_str(), fetch_size.c_str());
    OpenSearchResult* result = conn.PopResult();
    EXPECT_EQ(NULL, (void*)result);
}

// Conn::PopResult

TEST(TestConnPopResult, PopEmptyQueue) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());

    OpenSearchResult* result = conn.PopResult();
    EXPECT_EQ(NULL, (void*)result);
}

TEST(TestConnPopResult, PopTwoQueryResults) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());

    conn.ExecDirect(some_columns_flights_query.c_str(), fetch_size.c_str());
    conn.ExecDirect(all_columns_flights_query.c_str(), fetch_size.c_str());

    // Pop some_columns
    OpenSearchResult* result = conn.PopResult();
    ASSERT_NE(nullptr, result);
    EXPECT_EQ(some_columns_flights_count, result->num_fields);

    // Pop all_columns
    result = conn.PopResult();
    ASSERT_NE(nullptr, result);
    EXPECT_EQ(all_columns_flights_count, result->num_fields);
}

// Pagination: walk every page the driver queues for a query

const std::string flights_origin_query =
    "SELECT Origin FROM opensearch_dashboards_sample_data_flights";
const std::string flights_count_query =
    "SELECT COUNT(*) FROM opensearch_dashboards_sample_data_flights";
// plugins.query.size_limit default; caps results when no fetch_size is sent
const size_t server_size_limit = 10000;

namespace {
struct PagedResult {
    bool ok = false;
    size_t rows = 0;
    size_t pages = 0;
    size_t largest_page = 0;
};

// Run a query and pop results until the queue is drained. With a cursor,
// pages after the first come from the background SendCursorQueries thread,
// the same way GetNextResultSet pulls them during SQLFetch.
PagedResult ExecAndDrain(OpenSearchCommunication& conn, const std::string& sql,
                         const std::string& fetch_size_value) {
    PagedResult paged;
    if (conn.ExecDirect(sql.c_str(), fetch_size_value.c_str()) != 0)
        return paged;
    paged.ok = true;
    while (OpenSearchResult* result = conn.PopResult()) {
        const size_t page_rows =
            result->opensearch_result_doc.at("datarows").size();
        paged.rows += page_rows;
        paged.pages++;
        paged.largest_page = std::max(paged.largest_page, page_rows);
        delete result;
    }
    return paged;
}

size_t FlightsCount(OpenSearchCommunication& conn) {
    if (conn.ExecDirect(flights_count_query.c_str(), "-1") != 0)
        return 0;
    OpenSearchResult* result = conn.PopResult();
    if (result == nullptr)
        return 0;
    const size_t count =
        result->opensearch_result_doc.at("datarows").at(0).at(0).get< size_t >();
    delete result;
    return count;
}
}  // namespace

TEST(TestConnPagination, FetchSizeReturnsAllRows) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());
    const size_t total = FlightsCount(conn);
    // Must exceed the page size, or this isn't a multi-page walk
    ASSERT_GT(total, (size_t)1000) << conn.GetErrorMessage();

    PagedResult paged = ExecAndDrain(conn, flights_origin_query, "1000");
    ASSERT_TRUE(paged.ok) << conn.GetErrorMessage();
    RecordProperty("rows", (int)paged.rows);
    RecordProperty("pages", (int)paged.pages);
    EXPECT_EQ(total, paged.rows);
    EXPECT_GE(paged.pages, (total + 999) / 1000);
    EXPECT_LE(paged.largest_page, (size_t)1000);
}

// Characterizes current behavior, not the desired one: the default
// FetchSize (-1) sends no fetch_size, and the server silently caps the
// result at plugins.query.size_limit. (2.x returns one page with no
// cursor; 3.x adds a cursor whose next page is empty. Rows match.)
TEST(TestConnPagination, DefaultFetchSizeIsCappedAtSizeLimit) {
    OpenSearchCommunication conn;
    ASSERT_TRUE(conn.ConnectionOptions(valid_conn_opt_val, false, 0, 0));
    ASSERT_TRUE(conn.ConnectDBStart());
    const size_t total = FlightsCount(conn);
    ASSERT_GT(total, (size_t)0) << conn.GetErrorMessage();

    PagedResult paged = ExecAndDrain(conn, flights_origin_query, "-1");
    ASSERT_TRUE(paged.ok) << conn.GetErrorMessage();
    RecordProperty("rows", (int)paged.rows);
    RecordProperty("pages", (int)paged.pages);
    EXPECT_EQ(std::min(total, server_size_limit), paged.rows);
}
