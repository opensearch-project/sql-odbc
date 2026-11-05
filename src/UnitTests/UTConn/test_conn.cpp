// clang-format off
#include "pch.h"
#include "unit_test_helper.h"
#include "opensearch_communication.h"
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
