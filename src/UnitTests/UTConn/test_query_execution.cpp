// clang-format off
#include "pch.h"
#include "unit_test_helper.h"
#include "opensearch_communication.h"
#include "opensearch_helper.h"
#include <algorithm>
#include <chrono>
#include <future>
#include <memory>
#include <thread>
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

// The cursor thread's lifetime. A small fetch size gives a query far more
// pages than the queue holds, so the thread is still fetching (or waiting
// on a full queue) when the app stops, re-executes or disconnects. Run under
// ASan or helgrind to see a thread outliving its connection.

const std::string many_pages_fetch_size = "10";

namespace {
std::unique_ptr< OpenSearchCommunication > ConnectedComm() {
    auto conn = std::make_unique< OpenSearchCommunication >();
    if (!conn->ConnectionOptions(valid_conn_opt_val, false, 0, 0)
        || !conn->ConnectDBStart())
        return nullptr;
    return conn;
}

// Run f on another thread, so a hang fails the test instead of the run
template < typename F >
bool FinishesWithin(std::chrono::seconds limit, F f) {
    std::future< void > done = std::async(std::launch::async, f);
    if (done.wait_for(limit) != std::future_status::ready) {
        done.wait();
        return false;
    }
    done.get();
    return true;
}

// Long enough for a thread left running to fetch a page or two more
void LetACursorThreadRun() {
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
}
}  // namespace

TEST(TestCursorThread, DisconnectWhileRetrieving) {
    std::unique_ptr< OpenSearchCommunication > conn = ConnectedComm();
    ASSERT_NE(nullptr, conn);
    ASSERT_EQ(0, conn->ExecDirect(flights_origin_query.c_str(),
                                  many_pages_fetch_size.c_str()))
        << conn->GetErrorMessage();
    delete conn->PopResult();
    // OpenSearchDisconnect deletes the connection
    EXPECT_TRUE(FinishesWithin(std::chrono::seconds(10),
                               [&conn]() { conn.reset(); }));
    LetACursorThreadRun();
}

TEST(TestCursorThread, DropDBConnectionWhileRetrieving) {
    std::unique_ptr< OpenSearchCommunication > conn = ConnectedComm();
    ASSERT_NE(nullptr, conn);
    ASSERT_EQ(0, conn->ExecDirect(flights_origin_query.c_str(),
                                  many_pages_fetch_size.c_str()))
        << conn->GetErrorMessage();
    EXPECT_TRUE(FinishesWithin(std::chrono::seconds(10),
                               [&conn]() { conn->DropDBConnection(); }));
    LetACursorThreadRun();
    EXPECT_EQ(nullptr, conn->PopResult());
}

// SQLFreeStmt(SQL_CLOSE) and SQLCloseCursor: no page arrives afterwards,
// including when the stop beats the thread's start
TEST(TestCursorThread, StopResultRetrievalStopsThePages) {
    std::unique_ptr< OpenSearchCommunication > conn = ConnectedComm();
    ASSERT_NE(nullptr, conn);
    ASSERT_EQ(0, conn->ExecDirect(flights_origin_query.c_str(),
                                  many_pages_fetch_size.c_str()))
        << conn->GetErrorMessage();
    EXPECT_TRUE(FinishesWithin(std::chrono::seconds(10),
                               [&conn]() { conn->StopResultRetrieval(); }));
    LetACursorThreadRun();
    EXPECT_EQ(nullptr, conn->PopResult());
}

// A new query drops the old one's pages instead of queueing behind them
TEST(TestCursorThread, ExecDirectWhileRetrieving) {
    std::unique_ptr< OpenSearchCommunication > conn = ConnectedComm();
    ASSERT_NE(nullptr, conn);
    ASSERT_EQ(0, conn->ExecDirect(flights_origin_query.c_str(),
                                  many_pages_fetch_size.c_str()))
        << conn->GetErrorMessage();
    LetACursorThreadRun();  // the queue is full of the old query's pages
    ASSERT_EQ(0, conn->ExecDirect(flights_count_query.c_str(), "-1"))
        << conn->GetErrorMessage();
    OpenSearchResult* result = conn->PopResult();
    ASSERT_NE(nullptr, result);
    EXPECT_EQ("COUNT(*)", result->column_info.at(0).field_name);
    delete result;
    EXPECT_EQ(nullptr, conn->PopResult());
}
