// End-to-end tests through the unixODBC driver manager, as an application
// uses the driver: this binary links libodbc only, and the DM loads the
// built libsqlodbc.so from the Driver= path in the connection string.
// ut_conn calls the driver's internals directly, so it can't see bugs where
// the DM sits between the driver and its own exported entry points (e.g.
// SQLFetch stopping after the first cursor page on Linux).
//
// The driver is Unicode-only on Linux, so these use the W API. SQLWCHAR is
// 2 bytes on unixODBC while wchar_t is 4, so strings are char16_t (u"")
// rather than L"".

#include <gtest/gtest.h>
#include <signal.h>
#include <sql.h>
#include <sqlext.h>
#include <sys/wait.h>
#include <unistd.h>

#include <string>

#include "unit_test_helper.h"

static_assert(sizeof(SQLWCHAR) == sizeof(char16_t),
              "unixODBC SQLWCHAR is expected to be UTF-16");

namespace {

const std::string flights = "opensearch_dashboards_sample_data_flights";

// Built driver, unless overridden (e.g. to test an installed copy)
const std::string driver_path = TestEnv("ODBC_TEST_DRIVER", DM_TEST_DRIVER);

// ASCII-only conversions: everything these tests send or check is ASCII
std::u16string U16(const std::string& s) {
    return std::u16string(s.begin(), s.end());
}

std::string Narrow(const SQLWCHAR* s, size_t len) {
    std::string out;
    for (size_t i = 0; i < len; ++i)
        out.push_back(static_cast< char >(s[i]));
    return out;
}

SQLWCHAR* W(std::u16string& s) {
    return reinterpret_cast< SQLWCHAR* >(&s[0]);
}

struct Diag {
    std::string sqlstate;
    std::string message;
};

Diag FirstDiag(SQLSMALLINT handle_type, SQLHANDLE handle) {
    SQLWCHAR state[6] = {0};
    SQLWCHAR msg[1024] = {0};
    SQLINTEGER native = 0;
    SQLSMALLINT msg_len = 0;
    SQLRETURN ret = SQLGetDiagRecW(handle_type, handle, 1, state, &native,
                                   msg, sizeof(msg) / sizeof(msg[0]), &msg_len);
    if (!SQL_SUCCEEDED(ret))
        return {"", "(no diagnostic record)"};
    if (msg_len > static_cast< SQLSMALLINT >(sizeof(msg) / sizeof(msg[0]) - 1))
        msg_len = sizeof(msg) / sizeof(msg[0]) - 1;
    return {Narrow(state, 5), Narrow(msg, static_cast< size_t >(msg_len))};
}

std::string DiagText(SQLSMALLINT handle_type, SQLHANDLE handle) {
    Diag d = FirstDiag(handle_type, handle);
    return d.sqlstate + ": " + d.message;
}

std::string ConnectionString(const std::string& host, const std::string& port,
                             bool ssl, const std::string& extra) {
    std::string s = "Driver=" + driver_path + ";Host=" + host
                    + ";Port=" + port + ";useSSL=" + (ssl ? "1" : "0")
                    + ";hostnameVerification=0;responseTimeout=60";
    if (!test_user.empty())
        s += ";Auth=BASIC;User=" + test_user + ";Password=" + test_password;
    return s + extra;
}

std::string ClusterConnectionString(const std::string& extra) {
    return ConnectionString("localhost", test_port, use_ssl, extra);
}

class DriverManager : public testing::Test {
   protected:
    SQLHENV env = SQL_NULL_HENV;
    SQLHDBC dbc = SQL_NULL_HDBC;
    SQLHSTMT stmt = SQL_NULL_HSTMT;

    void SetUp() override {
        ASSERT_TRUE(SQL_SUCCEEDED(
            SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env)));
        ASSERT_TRUE(SQL_SUCCEEDED(SQLSetEnvAttr(
            env, SQL_ATTR_ODBC_VERSION,
            reinterpret_cast< SQLPOINTER >(SQL_OV_ODBC3), 0)));
        ASSERT_TRUE(
            SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_DBC, env, &dbc)));
    }

    void TearDown() override {
        if (stmt != SQL_NULL_HSTMT)
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        if (dbc != SQL_NULL_HDBC) {
            SQLDisconnect(dbc);
            SQLFreeHandle(SQL_HANDLE_DBC, dbc);
        }
        if (env != SQL_NULL_HENV)
            SQLFreeHandle(SQL_HANDLE_ENV, env);
    }

    void Connect(const std::string& conn_str) {
        std::u16string in = U16(conn_str);
        SQLWCHAR out[1024];
        SQLSMALLINT out_len = 0;
        SQLRETURN ret = SQLDriverConnectW(
            dbc, nullptr, W(in), SQL_NTS, out, sizeof(out) / sizeof(out[0]),
            &out_len, SQL_DRIVER_NOPROMPT);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << DiagText(SQL_HANDLE_DBC, dbc);
        ASSERT_TRUE(
            SQL_SUCCEEDED(SQLAllocHandle(SQL_HANDLE_STMT, dbc, &stmt)));
    }

    void Exec(const std::string& query) {
        std::u16string q = U16(query);
        SQLRETURN ret = SQLExecDirectW(stmt, W(q), SQL_NTS);
        ASSERT_TRUE(SQL_SUCCEEDED(ret)) << DiagText(SQL_HANDLE_STMT, stmt);
    }

    // Fetches to the end; returns the row count and the final return code
    long FetchAll(SQLRETURN& last) {
        long rows = 0;
        while (SQL_SUCCEEDED(last = SQLFetch(stmt)))
            ++rows;
        return rows;
    }

    long Count(const std::string& table) {
        Exec("SELECT COUNT(*) FROM " + table);
        EXPECT_TRUE(SQL_SUCCEEDED(SQLFetch(stmt)))
            << DiagText(SQL_HANDLE_STMT, stmt);
        SQLBIGINT count = -1;
        SQLLEN ind = 0;
        EXPECT_TRUE(SQL_SUCCEEDED(
            SQLGetData(stmt, 1, SQL_C_SBIGINT, &count, sizeof(count), &ind)))
            << DiagText(SQL_HANDLE_STMT, stmt);
        SQLCloseCursor(stmt);
        return static_cast< long >(count);
    }
};

// Every cursor page reaches the application, not just the first
TEST_F(DriverManager, FetchSizePagesThroughAllRows) {
    ASSERT_NO_FATAL_FAILURE(Connect(ClusterConnectionString(";fetchSize=1000")));
    long expected = Count(flights);
    ASSERT_EQ(expected, 13059);

    ASSERT_NO_FATAL_FAILURE(Exec("SELECT FlightNum FROM " + flights));
    SQLRETURN last = SQL_SUCCESS;
    long rows = FetchAll(last);
    EXPECT_EQ(last, SQL_NO_DATA) << DiagText(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(rows, expected);
}

// Without fetchSize the plugin returns a single page, capped at its default
// size limit (plugins.query.size_limit, 10000), and no cursor
TEST_F(DriverManager, DefaultFetchSizeReturnsSizeLimitRows) {
    ASSERT_NO_FATAL_FAILURE(Connect(ClusterConnectionString("")));
    ASSERT_NO_FATAL_FAILURE(Exec("SELECT FlightNum FROM " + flights));
    SQLRETURN last = SQL_SUCCESS;
    long rows = FetchAll(last);
    EXPECT_EQ(last, SQL_NO_DATA) << DiagText(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(rows, 10000);
}

// Column metadata comes through the DM's W entry points intact: aliases are
// the column names, and a double keeps its full precision
TEST_F(DriverManager, ColumnMetadataAndDoublePrecision) {
    ASSERT_NO_FATAL_FAILURE(Connect(ClusterConnectionString("")));
    ASSERT_NO_FATAL_FAILURE(
        Exec("SELECT CAST(1 AS DOUBLE) / 3 AS third, Carrier AS airline FROM "
             + flights + " WHERE Carrier = 'OpenSearch Dashboards Airlines'"
             " LIMIT 2"));

    SQLSMALLINT cols = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLNumResultCols(stmt, &cols)));
    ASSERT_EQ(cols, 2);

    SQLWCHAR name[64];
    SQLSMALLINT name_len = 0, type = 0, digits = 0, nullable = 0;
    SQLULEN size = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLDescribeColW(stmt, 1, name, 64, &name_len,
                                              &type, &size, &digits, &nullable)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(Narrow(name, static_cast< size_t >(name_len)), "third");
    // The driver maps double to SQL_FLOAT, ODBC's double-precision float
    EXPECT_EQ(type, SQL_FLOAT);

    SQLWCHAR label[64];
    SQLSMALLINT label_bytes = 0;
    SQLLEN numeric = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttributeW(stmt, 2, SQL_DESC_LABEL, label,
                                               sizeof(label), &label_bytes,
                                               &numeric)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(Narrow(label, static_cast< size_t >(label_bytes)
                                / sizeof(SQLWCHAR)),
              "airline");
    ASSERT_TRUE(SQL_SUCCEEDED(SQLColAttributeW(
        stmt, 1, SQL_DESC_CONCISE_TYPE, nullptr, 0, nullptr, &numeric)));
    EXPECT_EQ(numeric, SQL_FLOAT);

    // A column can be read only once per row, so the double is read twice
    // from two rows: once as a C double, once as text
    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(stmt)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    double third = 0;
    SQLLEN ind = 0;
    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLGetData(stmt, 1, SQL_C_DOUBLE, &third, sizeof(third), &ind)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(third, 1.0 / 3.0);

    SQLWCHAR text[64];
    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLGetData(stmt, 2, SQL_C_WCHAR, text, sizeof(text), &ind)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(Narrow(text, static_cast< size_t >(ind) / sizeof(SQLWCHAR)),
              "OpenSearch Dashboards Airlines");

    ASSERT_TRUE(SQL_SUCCEEDED(SQLFetch(stmt)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    ASSERT_TRUE(SQL_SUCCEEDED(
        SQLGetData(stmt, 1, SQL_C_WCHAR, text, sizeof(text), &ind)))
        << DiagText(SQL_HANDLE_STMT, stmt);
    ASSERT_GT(ind, 0);
    EXPECT_EQ(Narrow(text, static_cast< size_t >(ind) / sizeof(SQLWCHAR)),
              "0.3333333333333333");
}

// cursor_stub.py as a child process: a fake server on 127.0.0.1 whose first
// page has 3 rows and a cursor, and whose cursor page is an HTML 502
class CursorStub {
   public:
    CursorStub() {
        int to_child[2], from_child[2];
        if (pipe(to_child) != 0 || pipe(from_child) != 0)
            return;
        pid_ = fork();
        if (pid_ == 0) {
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            close(to_child[1]);
            close(from_child[0]);
            execlp("python3", "python3", DM_TEST_CURSOR_STUB,
                   static_cast< char* >(nullptr));
            _exit(127);
        }
        close(to_child[0]);
        close(from_child[1]);
        stdin_ = to_child[1];
        // First line of output is the port it listens on
        char buf[16] = {0};
        size_t used = 0;
        while (used < sizeof(buf) - 1) {
            ssize_t n = read(from_child[0], buf + used, 1);
            if (n <= 0 || buf[used] == '\n')
                break;
            ++used;
        }
        close(from_child[0]);
        buf[used] = '\0';
        port_ = buf;
    }

    ~CursorStub() {
        // Closing its stdin is the stub's signal to exit
        if (stdin_ >= 0)
            close(stdin_);
        if (pid_ > 0) {
            int status = 0;
            for (int i = 0; i < 50; ++i) {
                if (waitpid(pid_, &status, WNOHANG) == pid_)
                    return;
                usleep(100 * 1000);
            }
            kill(pid_, SIGKILL);
            waitpid(pid_, &status, 0);
        }
    }

    const std::string& port() const {
        return port_;
    }

   private:
    pid_t pid_ = -1;
    int stdin_ = -1;
    std::string port_;
};

// A failed cursor page is an error after the rows before it, not the end of
// the result set
TEST_F(DriverManager, CursorPageErrorIsReportedAfterReceivedRows) {
    CursorStub stub;
    ASSERT_FALSE(stub.port().empty()) << "cursor_stub.py did not start";
    ASSERT_NO_FATAL_FAILURE(Connect(ConnectionString(
        "127.0.0.1", stub.port(), false, ";fetchSize=3")));
    ASSERT_NO_FATAL_FAILURE(Exec("SELECT n FROM stub"));

    SQLRETURN last = SQL_SUCCESS;
    long rows = FetchAll(last);
    EXPECT_EQ(rows, 3);
    ASSERT_EQ(last, SQL_ERROR);
    Diag diag = FirstDiag(SQL_HANDLE_STMT, stmt);
    EXPECT_EQ(diag.sqlstate, "08S01") << diag.message;
    EXPECT_NE(diag.message.find("502"), std::string::npos) << diag.message;
}

}  // namespace
