// clang-format off
#include "pch.h"
#include "unit_test_helper.h"
#include "dlg_specific.h"
#include "opensearch_communication.h"
#include "opensearch_connection.h"
#include <chrono>
#include <cstdio>
#include <future>
#include <memory>
// clang-format on

const size_t valid_option_count = 4;
const size_t invalid_option_count = 4;
const size_t missing_option_count = 3;
const std::string valid_host = (use_ssl ? "https://localhost" : "localhost");
const std::string valid_port = test_port;
const std::string valid_user = test_user;
const std::string valid_pw = test_password;
const std::string valid_region = "us-west-3";
const std::string valid_tunnel_host = "";
const std::string invalid_host = "10.1.1.189";
const std::string invalid_port = "920";
const std::string invalid_user = "amin";
const std::string invalid_pw = "amin";
const std::string invalid_region = "bad-region";
runtime_options valid_opt_val = {{valid_host, valid_port, "1", "0"},
                                 {"BASIC", valid_user, valid_pw, valid_region, valid_tunnel_host},
                                 {use_ssl, false, "", "", "", ""}};
runtime_options invalid_opt_val = {
    {invalid_host, invalid_port, "1", "0"},
    {"BASIC", invalid_user, invalid_pw, valid_region, valid_tunnel_host},
    {use_ssl, false, "", "", "", ""}};
runtime_options missing_opt_val = {{"", "", "1", "0"},
                                   {"BASIC", "", invalid_pw, valid_region, valid_tunnel_host},
                                   {use_ssl, false, "", "", "", ""}};

TEST(TestOpenSearchConnConnectionOptions, ValidParameters) {
    OpenSearchCommunication conn;
    EXPECT_EQ(true,
              conn.ConnectionOptions(valid_opt_val, 1, 1, valid_option_count));
}

TEST(TestOpenSearchConnConnectionOptions, MissingParameters) {
    OpenSearchCommunication conn;
    EXPECT_EQ(false, conn.ConnectionOptions(missing_opt_val, 1, 1,
                                            missing_option_count));
}

class TestOpenSearchConnConnectDBStart : public testing::Test {
   public:
    TestOpenSearchConnConnectDBStart() {
    }

    void SetUp() {
    }

    void TearDown() {
        m_conn.DropDBConnection();
    }

    ~TestOpenSearchConnConnectDBStart() {
        // cleanup any pending stuff, but no exceptions allowed
    }

    OpenSearchCommunication m_conn;
};

TEST_F(TestOpenSearchConnConnectDBStart, ValidParameters) {
    ASSERT_NE(false, m_conn.ConnectionOptions(valid_opt_val, 1, 1,
                                              valid_option_count));
    EXPECT_EQ(true, m_conn.ConnectDBStart());
    EXPECT_EQ(CONNECTION_OK, m_conn.GetConnectionStatus());
}

TEST_F(TestOpenSearchConnConnectDBStart, InvalidParameters) {
    ASSERT_TRUE(
        m_conn.ConnectionOptions(invalid_opt_val, 1, 1, invalid_option_count));
    EXPECT_EQ(false, m_conn.ConnectDBStart());
    EXPECT_EQ(CONNECTION_BAD, m_conn.GetConnectionStatus());
}

TEST_F(TestOpenSearchConnConnectDBStart, MissingParameters) {
    ASSERT_NE(true, m_conn.ConnectionOptions(missing_opt_val, 1, 1,
                                             missing_option_count));
    EXPECT_EQ(false, m_conn.ConnectDBStart());
    EXPECT_EQ(CONNECTION_BAD, m_conn.GetConnectionStatus());
}

TEST(TestOpenSearchConnDropDBConnection, InvalidParameters) {
    OpenSearchCommunication conn;
    ASSERT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
    ASSERT_TRUE(
        conn.ConnectionOptions(invalid_opt_val, 1, 1, invalid_option_count));
    ASSERT_NE(true, conn.ConnectDBStart());
    ASSERT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
    conn.DropDBConnection();
    EXPECT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
}

TEST(TestOpenSearchConnDropDBConnection, MissingParameters) {
    OpenSearchCommunication conn;
    ASSERT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
    ASSERT_NE(true, conn.ConnectionOptions(missing_opt_val, 1, 1,
                                           missing_option_count));
    ASSERT_NE(true, conn.ConnectDBStart());
    ASSERT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
    conn.DropDBConnection();
    EXPECT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
}

TEST(TestOpenSearchConnDropDBConnection, ValidParameters) {
    OpenSearchCommunication conn;
    ASSERT_NE(false,
              conn.ConnectionOptions(valid_opt_val, 1, 1, valid_option_count));
    ASSERT_NE(false, conn.ConnectDBStart());
    ASSERT_EQ(CONNECTION_OK, conn.GetConnectionStatus());
    conn.DropDBConnection();
    EXPECT_EQ(CONNECTION_BAD, conn.GetConnectionStatus());
}

// Request bodies (no cluster needed)

TEST(TestBuildRequestBody, Query) {
    EXPECT_EQ(std::optional< std::string >(R"({"query":"SELECT 1"})"),
              OpenSearchCommunication::BuildRequestBody("SELECT 1", "", ""));
}

TEST(TestBuildRequestBody, QueryWithFetchSize) {
    EXPECT_EQ(
        std::optional< std::string >(R"({"query":"SELECT 1","fetch_size":"5"})"),
        OpenSearchCommunication::BuildRequestBody("SELECT 1", "5", ""));
    // -1 means "server default", so it's left out
    EXPECT_EQ(std::optional< std::string >(R"({"query":"SELECT 1"})"),
              OpenSearchCommunication::BuildRequestBody("SELECT 1", "-1", ""));
}

TEST(TestBuildRequestBody, Cursor) {
    EXPECT_EQ(std::optional< std::string >(R"({"cursor":"abc"})"),
              OpenSearchCommunication::BuildRequestBody("", "", "abc"));
}

TEST(TestBuildRequestBody, Utf8QueryPassesThrough) {
    EXPECT_EQ(
        std::optional< std::string >("{\"query\":\"WHERE t = 'caf\xc3\xa9'\"}"),
        OpenSearchCommunication::BuildRequestBody("WHERE t = 'caf\xc3\xa9'", "",
                                                  ""));
}

// An ANSI app on a non-UTF-8 codepage passes bytes like Latin-1 0xE9
// straight through SQLExecDirect. Rewriting them to U+FFFD would silently
// change the query (e.g. a WHERE that matches nothing), so refuse instead.
TEST(TestBuildRequestBody, InvalidUtf8QueryIsRejected) {
    EXPECT_EQ(std::nullopt,
              OpenSearchCommunication::BuildRequestBody("WHERE t = 'caf\xe9'",
                                                        "", ""));
}

// Response bodies in error text (no cluster needed)

TEST(TestTruncateResponseBody, ShortBodyIsUnchanged) {
    EXPECT_EQ("", OpenSearchCommunication::TruncateResponseBody("", 8));
    EXPECT_EQ("abc", OpenSearchCommunication::TruncateResponseBody("abc", 8));
    EXPECT_EQ("abcdefgh",
              OpenSearchCommunication::TruncateResponseBody("abcdefgh", 8));
}

TEST(TestTruncateResponseBody, LongBodyIsCutAndMarked) {
    EXPECT_EQ("abcdefgh... (10 bytes total)",
              OpenSearchCommunication::TruncateResponseBody("abcdefghij", 8));
}

TEST(TestTruncateResponseBody, DefaultLimitIs256Bytes) {
    const std::string body(1000, 'x');
    EXPECT_EQ(std::string(256, 'x') + "... (1000 bytes total)",
              OpenSearchCommunication::TruncateResponseBody(body));
}

// U+00E9 is 2 bytes and U+1F600 is 4: neither may be split at the limit
TEST(TestTruncateResponseBody, DoesNotSplitMultibyteSequence) {
    EXPECT_EQ("abc... (6 bytes total)",
              OpenSearchCommunication::TruncateResponseBody("abc\xc3\xa9z", 4));
    EXPECT_EQ("abc\xc3\xa9... (6 bytes total)",
              OpenSearchCommunication::TruncateResponseBody("abc\xc3\xa9z", 5));
    EXPECT_EQ("a... (6 bytes total)",
              OpenSearchCommunication::TruncateResponseBody(
                  "a\xf0\x9f\x98\x80z", 4));
}

// Not UTF-8 at all: still bounded, just cut at the limit
TEST(TestTruncateResponseBody, InvalidUtf8IsStillBounded) {
    const std::string body(10, '\x80');
    EXPECT_EQ(std::string(4, '\x80') + "... (10 bytes total)",
              OpenSearchCommunication::TruncateResponseBody(body, 4));
}

// Non-OK responses (no cluster needed). Proxies and load balancers answer
// with HTML or plain text, not OpenSearch's JSON error, and that must become
// an ODBC error rather than an exception thrown through the C API.

const std::string error_prefix = "[OpenSearch][SQL ODBC Driver][SQL Plugin] ";

TEST(TestSetErrorFromResponse, HtmlBodyGivesStatusAndBody) {
    OpenSearchCommunication conn;
    const std::string html =
        "<html><head><title>502 Bad Gateway</title></head></html>";
    EXPECT_NO_THROW(conn.SetErrorFromResponse(502, "", html));
    EXPECT_EQ(error_prefix
                  + "Connection error: Http response code was not OK. Code "
                    "received: 502. Response error: '"
                  + html + "'.",
              conn.GetErrorMessage());
}

TEST(TestSetErrorFromResponse, LongBodyIsBounded) {
    OpenSearchCommunication conn;
    const std::string body = "<html>" + std::string(1000, 'x');
    EXPECT_NO_THROW(conn.SetErrorFromResponse(504, "", body));
    EXPECT_EQ(error_prefix
                  + "Connection error: Http response code was not OK. Code "
                    "received: 504. Response error: '"
                  + body.substr(0, 256) + "... (1006 bytes total)'.",
              conn.GetErrorMessage());
}

TEST(TestSetErrorFromResponse, EmptyBodyGivesStatus) {
    OpenSearchCommunication conn;
    EXPECT_NO_THROW(conn.SetErrorFromResponse(503, "", ""));
    EXPECT_EQ(error_prefix
                  + "Connection error: Http response code was not OK. Code "
                    "received: 503.",
              conn.GetErrorMessage());
}

TEST(TestSetErrorFromResponse, ClientErrorIsIncluded) {
    OpenSearchCommunication conn;
    EXPECT_NO_THROW(
        conn.SetErrorFromResponse(429, "throttled", "Too Many Requests"));
    EXPECT_EQ(error_prefix
                  + "Connection error: Http response code was not OK. Code "
                    "received: 429. Client error: 'throttled'. Response "
                    "error: 'Too Many Requests'.",
              conn.GetErrorMessage());
}

// JSON that isn't OpenSearch's error shape, e.g. an API gateway's 403
TEST(TestSetErrorFromResponse, OtherJsonGivesStatusAndBody) {
    OpenSearchCommunication conn;
    const std::string body = R"({"message":"Forbidden"})";
    EXPECT_NO_THROW(conn.SetErrorFromResponse(403, "", body));
    EXPECT_EQ(error_prefix
                  + "Connection error: Http response code was not OK. Code "
                    "received: 403. Response error: '"
                  + body + "'.",
              conn.GetErrorMessage());
}

// The server's own error keeps its existing message
TEST(TestSetErrorFromResponse, OpenSearchJsonErrorGivesServerReason) {
    OpenSearchCommunication conn;
    const std::string body =
        R"({"error":{"reason":"Invalid SQL query","details":"bad token",)"
        R"("type":"SyntaxCheckException"},"status":400})";
    EXPECT_NO_THROW(conn.SetErrorFromResponse(400, "", body));
    EXPECT_EQ(error_prefix + "Invalid SQL query: bad token",
              conn.GetErrorMessage());
}

// A failed cursor page (no cluster needed). A cursor that can't be encoded
// as JSON makes IssueRequest return NULL before any connection is used,
// which is the cursor thread's NULL-response path.

namespace {
const std::string unencodable_cursor = "\xff";

// PopResult on another thread, so a hang fails the test instead of the run
bool PopResultReturns(OpenSearchCommunication& conn, OpenSearchResult*& result) {
    std::future< OpenSearchResult* > popped = std::async(
        std::launch::async, [&conn]() { return conn.PopResult(); });
    const bool returned =
        popped.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
    if (!returned)
        conn.StopResultRetrieval();  // unblock PopResult
    result = popped.get();
    return returned;
}
}  // namespace

TEST(TestCursorError, NullResponseDoesNotHangPopResult) {
    OpenSearchCommunication conn;
    conn.SendCursorQueries(unencodable_cursor);
    OpenSearchResult* result = nullptr;
    EXPECT_TRUE(PopResultReturns(conn, result));
    EXPECT_EQ(nullptr, result);
}

TEST(TestCursorError, NullResponseIsReported) {
    OpenSearchCommunication conn;
    conn.SendCursorQueries(unencodable_cursor);
    std::optional< ErrorDetails > error = conn.GetCursorError();
    ASSERT_TRUE(error.has_value());
    EXPECT_EQ(ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE, error->type);
    EXPECT_EQ(error_prefix
                  + "Cursor error: Failed to receive response from cursor. "
                    "Received NULL response.",
              OpenSearchCommunication::FormatErrorMessage(*error));
}

// The cursor thread keeps its error to itself: the connection's error,
// which the app thread reads, isn't written from that thread
TEST(TestCursorError, ConnectionErrorIsUntouched) {
    OpenSearchCommunication conn;
    conn.SendCursorQueries(unencodable_cursor);
    EXPECT_EQ(error_prefix
                  + "No error details available; check the driver logs.",
              conn.GetErrorMessage());
}

TEST(TestCursorError, NoErrorWithoutAFailedCursor) {
    OpenSearchCommunication conn;
    EXPECT_FALSE(conn.GetCursorError().has_value());
}

TEST(TestCursorError, NextQueryClearsIt) {
    OpenSearchCommunication conn;
    conn.SendCursorQueries(unencodable_cursor);
    ASSERT_TRUE(conn.GetCursorError().has_value());
    // Fails (not connected), but starts a new query all the same
    conn.ExecDirect("SELECT 1", "-1");
    EXPECT_FALSE(conn.GetCursorError().has_value());
}

// Server URL scheme (no cluster needed)

namespace {
// Sets UseSSL through the connection-string parser, so the test sees the
// value the driver really stores (atoi: 0 or 1, not '0' or '1')
std::unique_ptr< ConnectionClass > ConnFor(const char* server,
                                           const char* use_ssl_value) {
    auto conn = std::make_unique< ConnectionClass >();
    snprintf(conn->connInfo.server, sizeof(conn->connInfo.server), "%s",
             server);
    copyConnAttributes(&conn->connInfo, INI_SSL_USE, use_ssl_value);
    return conn;
}

std::string ServerUrlFor(const char* server, const char* use_ssl_value) {
    return generateValidServerUrl(ConnFor(server, use_ssl_value).get());
}
}  // namespace

TEST(TestGenerateValidServerUrl, BareHostWithoutSsl) {
    EXPECT_EQ("http://localhost", ServerUrlFor("localhost", "0"));
}

TEST(TestGenerateValidServerUrl, BareHostWithSsl) {
    EXPECT_EQ("https://localhost", ServerUrlFor("localhost", "1"));
}

TEST(TestGenerateValidServerUrl, ExplicitSchemeIsKept) {
    EXPECT_EQ("http://localhost", ServerUrlFor("http://localhost", "1"));
    EXPECT_EQ("https://localhost", ServerUrlFor("https://localhost", "0"));
}

TEST(TestGenerateValidServerUrl, EmptyServer) {
    EXPECT_EQ("", ServerUrlFor("", "0"));
}

// use_ssl picks the HTTP client's scheme; verify_server is its verifySSL
TEST(TestConnOptions, UseSslReachesRuntimeOptions) {
    EXPECT_TRUE(GetRuntimeOptions(ConnFor("localhost", "1").get())
                    .crypt.use_ssl);
    EXPECT_FALSE(GetRuntimeOptions(ConnFor("localhost", "0").get())
                     .crypt.use_ssl);
}

TEST(TestConnOptions, HostnameVerificationReachesRuntimeOptions) {
    auto conn = ConnFor("localhost", "1");
    copyConnAttributes(&conn->connInfo, INI_SSL_HOST_VERIFY, "1");
    EXPECT_TRUE(GetRuntimeOptions(conn.get()).crypt.verify_server);
    copyConnAttributes(&conn->connInfo, INI_SSL_HOST_VERIFY, "0");
    EXPECT_FALSE(GetRuntimeOptions(conn.get()).crypt.verify_server);
}

// Documented default is HostnameVerification=1
TEST(TestConnOptions, HostnameVerificationDefaultsOn) {
    auto conn = std::make_unique< ConnectionClass >();
    CC_conninfo_init(&conn->connInfo, INIT_GLOBALS);
    snprintf(conn->connInfo.server, sizeof(conn->connInfo.server), "%s",
             "localhost");
    EXPECT_TRUE(GetRuntimeOptions(conn.get()).crypt.verify_server);
}

int main(int argc, char** argv) {
    testing::internal::CaptureStdout();
    ::testing::InitGoogleTest(&argc, argv);

    int failures = RUN_ALL_TESTS();

    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    std::cout << (failures ? "Not all tests passed." : "All tests passed")
              << std::endl;
    WriteFileIfSpecified(argv, argv + argc, "-fout", output);

    return failures;
}
