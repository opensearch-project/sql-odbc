// SendQueryGetResult / ExecuteStatement when the server's 200 response can't
// be converted to a result set. No cluster needed: a stub HTTP server on
// 127.0.0.1 answers every request with the same body.
// clang-format off
#include "pch.h"
// clang-format on

#ifndef WIN32
#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <string>
#include <thread>

#include "opensearch_communication.h"
#include "opensearch_connection.h"
#include "opensearch_statement.h"

namespace {

// Passes the connect probes (GET / needs version.distribution; the SQL plugin
// check needs a 200) and the response shape check, but its one row is
// shorter than the schema, which the conversion rejects.
const std::string kUnconvertibleBody =
    "{\"version\": {\"distribution\": \"opensearch\", \"number\": \"2.11.0\"},"
    " \"schema\": [{\"name\": \"a\", \"type\": \"keyword\"},"
    "              {\"name\": \"b\", \"type\": \"keyword\"}],"
    " \"datarows\": [[\"only a\"]],"
    " \"total\": 1, \"size\": 1, \"status\": 200}";

// One connection at a time, one response per connection
class StubServer {
   public:
    explicit StubServer(const std::string& body) : m_body(body) {
        m_listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        socklen_t len = sizeof(addr);
        if (m_listen_fd < 0
            || bind(m_listen_fd, reinterpret_cast< sockaddr* >(&addr),
                    sizeof(addr))
                   != 0
            || listen(m_listen_fd, 8) != 0
            || getsockname(m_listen_fd, reinterpret_cast< sockaddr* >(&addr),
                           &len)
                   != 0)
            return;
        m_port = ntohs(addr.sin_port);
        m_thread = std::thread([this]() { Serve(); });
    }

    ~StubServer() {
        m_stop = true;
        if (m_thread.joinable())
            m_thread.join();
        if (m_listen_fd >= 0)
            close(m_listen_fd);
    }

    int Port() const {
        return m_port;
    }

   private:
    void Serve() {
        while (!m_stop) {
            pollfd pfd = {m_listen_fd, POLLIN, 0};
            if (poll(&pfd, 1, 100) <= 0)
                continue;
            int fd = accept(m_listen_fd, nullptr, nullptr);
            if (fd < 0)
                continue;
            ReadRequest(fd);
            std::string response =
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                "Content-Length: "
                + std::to_string(m_body.size())
                + "\r\nConnection: close\r\n\r\n" + m_body;
            (void)send(fd, response.data(), response.size(), 0);
            close(fd);
        }
    }

    // Headers, then Content-Length bytes of body
    static void ReadRequest(int fd) {
        std::string request;
        char buf[4096];
        size_t body_start = std::string::npos;
        size_t content_length = 0;
        while (true) {
            if (body_start != std::string::npos
                && request.size() >= body_start + content_length)
                return;
            pollfd pfd = {fd, POLLIN, 0};
            if (poll(&pfd, 1, 2000) <= 0)
                return;
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0)
                return;
            request.append(buf, static_cast< size_t >(n));
            if (body_start == std::string::npos) {
                size_t end = request.find("\r\n\r\n");
                if (end == std::string::npos)
                    continue;
                body_start = end + 4;
                std::string headers = request.substr(0, end);
                for (char& c : headers)
                    c = static_cast< char >(tolower(c));
                size_t cl = headers.find("content-length:");
                if (cl != std::string::npos)
                    content_length = std::stoul(headers.substr(cl + 15));
                // Else curl waits a second before sending the body
                if (headers.find("expect: 100-continue") != std::string::npos) {
                    const char cont[] = "HTTP/1.1 100 Continue\r\n\r\n";
                    (void)send(fd, cont, sizeof(cont) - 1, 0);
                }
            }
        }
    }

    std::string m_body;
    int m_listen_fd = -1;
    int m_port = 0;
    std::atomic< bool > m_stop{false};
    std::thread m_thread;
};

// A statement on a connection to the stub, as SQLExecDirect would have it
class StubStatement {
   public:
    explicit StubStatement(int port)
        : m_opts{{"127.0.0.1", std::to_string(port), "5", "-1"},
                 {"NONE", "", "", "us-west-3", ""},
                 {false, false, "", "", "", ""}} {
        m_connected = m_comm.ConnectionOptions(m_opts, false, 0, 0)
                      && m_comm.ConnectDBStart();
        m_conn = CC_Constructor();
        m_conn->opensearchconn = &m_comm;
        snprintf(m_conn->connInfo.fetch_size,
                 sizeof(m_conn->connInfo.fetch_size), "%s", "-1");
        m_stmt = SC_Constructor(m_conn);
        m_stmt->statement = strdup("SELECT a, b FROM t");
    }

    ~StubStatement() {
        SC_Destructor(m_stmt);
        m_conn->opensearchconn = NULL;  // m_comm isn't the heap's to free
        CC_Destructor(m_conn);
    }

    bool Connected() const {
        return m_connected;
    }
    StatementClass* Stmt() {
        return m_stmt;
    }

   private:
    runtime_options m_opts;
    OpenSearchCommunication m_comm;
    bool m_connected = false;
    ConnectionClass* m_conn = nullptr;
    StatementClass* m_stmt = nullptr;
};

}  // namespace

// Used to free the result, then write through the NULL pointer
TEST(TestSendQueryGetResult, UnconvertibleResponseCommitReturnsNull) {
    StubServer server(kUnconvertibleBody);
    ASSERT_NE(0, server.Port());
    StubStatement stmt(server.Port());
    ASSERT_TRUE(stmt.Connected());

    EXPECT_EQ(nullptr, SendQueryGetResult(stmt.Stmt(), TRUE));
}

// SQLExecDirect reports the failure, not a generic "no error details"
TEST(TestSendQueryGetResult, UnconvertibleResponseIsAnExecutionError) {
    StubServer server(kUnconvertibleBody);
    ASSERT_NE(0, server.Port());
    StubStatement stmt(server.Port());
    ASSERT_TRUE(stmt.Connected());

    EXPECT_EQ(SQL_ERROR, ExecuteStatement(stmt.Stmt(), TRUE));
    EXPECT_EQ(STMT_EXEC_ERROR, SC_get_errornumber(stmt.Stmt()));
    ASSERT_NE(nullptr, SC_get_errormsg(stmt.Stmt()));
    EXPECT_NE(nullptr, strstr(SC_get_errormsg(stmt.Stmt()),
                              "Failed to convert the server's response"));
}
#endif  // WIN32
