#ifndef OPENSEARCH_COMMUNICATION
#define OPENSEARCH_COMMUNICATION

// clang-format off
#include <atomic>
#include <memory>
#include <mutex>
#include <queue>
#include <future>
#include <regex>
#include "opensearch_types.h"
#include "opensearch_result_queue.h"

#include "opensearch_json.h"
#include <map>
#include <optional>
#include <string>
#include <aws/core/Aws.h>
#include <aws/core/http/HttpRequest.h>
#include <aws/core/http/HttpResponse.h>
#include <aws/core/http/HttpClientFactory.h>
#include <aws/core/http/HttpClient.h>
#include <aws/core/client/ClientConfiguration.h>
// clang-format on

class OpenSearchCommunication {
   public:
    OpenSearchCommunication();
    ~OpenSearchCommunication();

    // Create function for factory
    std::string GetErrorMessage();
    ConnErrorType GetErrorType();
    // The error that stopped the background cursor thread, if one did.
    // Safe to call while that thread runs; the next ExecDirect clears it.
    std::optional< ErrorDetails > GetCursorError();
    // The text GetErrorMessage gives for these details
    static std::string FormatErrorMessage(const ErrorDetails& details);
    bool ConnectionOptions(runtime_options& rt_opts, bool use_defaults,
                           int expand_dbname, unsigned int option_count);
    bool ConnectionOptions2();
    bool ConnectDBStart();
    ConnStatusType GetConnectionStatus();
    void DropDBConnection();
    void LogMsg(OpenSearchLogLevel level, const char* msg);
    int ExecDirect(const char* query, const char* fetch_size_);
    void SendCursorQueries(std::string cursor);
    OpenSearchResult* PopResult();
    std::string GetClientEncoding();
    bool SetClientEncoding(std::string& encoding);
    static bool IsSQLPluginEnabled(std::shared_ptr< ErrorDetails > error_details);
    bool CheckSQLPluginAvailability();
    std::string GetServerVersion();
    std::string GetServerDistribution();
    std::string GetClusterName();
    std::shared_ptr< Aws::Http::HttpResponse > IssueRequest(
        const std::string& endpoint, const Aws::Http::HttpMethod request_type,
        const std::string& content_type, const std::string& query,
        const std::string& fetch_size = "", const std::string& cursor = "");
    // JSON body for a query or cursor request; nullopt if the query text
    // isn't valid UTF-8 (sent unchanged it would be silently altered)
    static std::optional< std::string > BuildRequestBody(
        const std::string& query, const std::string& fetch_size,
        const std::string& cursor);
    // Sets a clear error and returns false if query isn't valid UTF-8
    bool CheckQueryEncoding(const std::string& query);
    // A response body shortened to at most max_bytes for error text, cut
    // before any UTF-8 sequence that would straddle the limit and marked
    // "... (N bytes total)". Bodies can carry row data and be any size.
    static std::string TruncateResponseBody(const std::string& body,
                                            size_t max_bytes = 256);
    // TruncateResponseBody(body), after logging the whole body at TRACE
    std::string ResponseBodyForMessage(const std::string& body);
    // Sets the error for a non-OK HTTP response: the server's own reason
    // when the body is an OpenSearch error, else the status code and a
    // bounded body (an HTML 502 from a proxy, a plain-text 429, nothing).
    // Doesn't throw on any body.
    void SetErrorFromResponse(long response_code,
                              const std::string& client_error,
                              const std::string& body);
    // That error without setting it, for the cursor thread. message gets
    // the status line.
    ErrorDetails ErrorFromResponse(long response_code,
                                   const std::string& client_error,
                                   const std::string& body,
                                   std::string& message);
    void AwsHttpResponseToString(
        std::shared_ptr< Aws::Http::HttpResponse > response,
        std::string& output);
    void SendCloseCursorRequest(const std::string& cursor);
    void StopResultRetrieval();
    std::vector< std::string > GetColumnsWithSelectQuery(
        const std::string table_name);

    // the endpoint is set according to distribution (ES/OpenSearch)
    std::string sql_endpoint;

   private:
    void InitializeConnection();
    bool CheckConnectionOptions();
    bool EstablishConnection();
    void ConstructOpenSearchResult(OpenSearchResult& result);
    void GetJsonSchema(OpenSearchResult& opensearch_result);
    void PrepareCursorResult(OpenSearchResult& opensearch_result);
    // nullptr if body isn't an OpenSearch error response
    std::shared_ptr< ErrorDetails > ParseErrorResponse(
        const std::string& body);
    void SetErrorDetails(std::string reason, std::string message,
                         ConnErrorType error_type);
    void SetErrorDetails(ErrorDetails details);
    void SetCursorError(const std::string& message, ConnErrorType error_type);
    void SetCursorError(const ErrorDetails& details);
    void ClearCursorError();

    void SetIsAossServerless();
    void SetSqlEndpoint();

    // Declared before m_client_encoding, which is initialized from it.
    const std::vector< std::string > m_supported_client_encodings = {"UTF8"};

    // Keep the order below in sync with the constructor's init list
    // (-Wreorder).
    ConnStatusType m_status;
    ConnErrorType m_error_type;
    std::shared_ptr< ErrorDetails > m_error_details;
    // Set by SetIsAossServerless() in EstablishConnection()
    bool is_aoss_serverless = false;
    bool m_valid_connection_options;
    // Written by both the app thread and the cursor thread
    std::atomic< bool > m_is_retrieving;
    // TODO #35 - Go through and add error messages on exit conditions
    std::string m_error_message;
    OpenSearchResultQueue m_result_queue;
    runtime_options m_rt_opts;
    std::string m_client_encoding;
    std::string m_response_str;
    std::shared_ptr< Aws::Http::HttpClient > m_http_client;
    std::string m_error_message_to_user;
    // The cursor thread's error. It has its own lock, rather than going in
    // m_error_details, because the app thread reads that unlocked.
    std::mutex m_cursor_error_mutex;
    std::optional< ErrorDetails > m_cursor_error;
};

#endif
