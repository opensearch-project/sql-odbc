#include "opensearch_communication.h"

// sqlodbc needs to be included before mylog, otherwise mylog will generate
// compiler warnings
// clang-format off
#include "opensearch_odbc.h"
#include "mylog.h"
#include <atomic>
#include <mutex>
#include <aws/core/utils/StringUtils.h>
#include <aws/core/client/RetryStrategy.h>
#include <aws/core/client/AWSClient.h>
#include <aws/core/utils/HashingUtils.h>
#include <aws/core/auth/AWSAuthSigner.h>
#include <aws/core/auth/AWSCredentialsProvider.h>
#include <aws/core/http/HttpClient.h>
// clang-format on

static const std::string SQL_ENDPOINT_OPENSEARCH = "/_plugins/_sql";
static const std::string SQL_ENDPOINT_ELASTICSEARCH = "/_opendistro/_sql";
static const std::string SQL_ENDPOINT_ERROR = "Error";

static const std::string SERVICE_NAME_DEFAULT = "es";
static const std::string SERVICE_NAME_AOSS_SERVERLESS = "aoss";

static const std::string CREDENTIALS_PROFILE = "opensearchodbc";
static const std::string CREDENTIALS_PROVIDER_ALLOCATION_TAG =
    "CREDENTIAL_PROVIDER";

static const std::string DISTRIBUTION_OPENSEARCH = "opensearch";

static const std::string ctype = "application/json";
static const std::string ERROR_MSG_PREFIX =
    "[OpenSearch][SQL ODBC Driver][SQL Plugin] ";
// Response shape checks. These replace JSON-schema validation: the shapes are
// small and fixed, so explicit checks are clearer and need no schema engine.
namespace {
class json_shape_error : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

bool IsString(const json_doc& v) { return v.is_string(); }
bool IsInteger(const json_doc& v) { return v.is_number_integer(); }
bool IsArray(const json_doc& v) { return v.is_array(); }
bool IsObject(const json_doc& v) { return v.is_object(); }

// Check obj[key] with is_type; a missing key fails only when required.
void CheckField(const json_doc& obj, const char* key,
                bool (*is_type)(const json_doc&), const char* type_name,
                bool required) {
    auto it = obj.find(key);
    if (it == obj.end()) {
        if (required)
            throw json_shape_error(std::string("missing required field '")
                                   + key + "'");
        return;
    }
    if (!is_type(*it))
        throw json_shape_error(std::string("field '") + key + "' is not "
                               + type_name);
}

void CheckObject(const json_doc& v, const char* what) {
    if (!v.is_object())
        throw json_shape_error(std::string(what) + " is not an object");
}

// First page of a query result
void CheckResultShape(const json_doc& doc) {
    CheckObject(doc, "response");
    CheckField(doc, "schema", IsArray, "an array", true);
    for (const auto& column : doc.at("schema")) {
        CheckObject(column, "schema entry");
        CheckField(column, "name", IsString, "a string", true);
        CheckField(column, "type", IsString, "a string", true);
    }
    CheckField(doc, "cursor", IsString, "a string", false);
    CheckField(doc, "total", IsInteger, "an integer", true);
    CheckField(doc, "datarows", IsArray, "an array", true);
    CheckField(doc, "size", IsInteger, "an integer", true);
    CheckField(doc, "status", IsInteger, "an integer", true);
}

// Follow-up page fetched with a cursor
void CheckCursorShape(const json_doc& doc) {
    CheckObject(doc, "response");
    CheckField(doc, "cursor", IsString, "a string", false);
    CheckField(doc, "datarows", IsArray, "an array", true);
    CheckField(doc, "status", IsInteger, "an integer", false);
}

void CheckErrorShape(const json_doc& doc) {
    CheckObject(doc, "response");
    CheckField(doc, "error", IsObject, "an object", true);
    const json_doc& error = doc.at("error");
    CheckField(error, "reason", IsString, "a string", true);
    CheckField(error, "details", IsString, "a string", true);
    CheckField(error, "type", IsString, "a string", true);
    CheckField(doc, "status", IsInteger, "an integer", true);
}

std::string ClientErrorOf(const Aws::Http::HttpResponse& response) {
    return response.HasClientError()
               ? std::string(response.GetClientErrorMessage().c_str())
               : std::string();
}
}  // namespace

namespace {
    /**
     * A helper class to initialize/shutdown AWS API once per DLL load/unload.
     */
    class AwsSdkHelper {
      public:
        AwsSdkHelper() :
          m_reference_count(0) {
        }

        AwsSdkHelper& operator++() {
          if (1 == ++m_reference_count) {
            std::scoped_lock lock(m_mutex);
            Aws::InitAPI(m_sdk_options);
          }
          return *this;
        }

        AwsSdkHelper& operator--() {
          if (0 == --m_reference_count) {
            std::scoped_lock lock(m_mutex);
            Aws::ShutdownAPI(m_sdk_options);
          }
          return *this;
        }

        Aws::SDKOptions m_sdk_options;
        std::atomic<int> m_reference_count;
        std::mutex m_mutex;
    };

    AwsSdkHelper AWS_SDK_HELPER;
}

void OpenSearchCommunication::AwsHttpResponseToString(
    std::shared_ptr< Aws::Http::HttpResponse > response, std::string& output) {
    // This function has some unconventional stream operations because we need
    // performance over readability here. Equivalent code done in conventional
    // ways (using stringstream operators) takes ~30x longer than this code
    // below and bottlenecks our query performance

    // Get streambuffer from response and set position to start
    std::streambuf* stream_buffer = response->GetResponseBody().rdbuf();
    stream_buffer->pubseekpos(0);

    // Get size of streambuffer and reserver that much space in the output
    size_t avail = static_cast< size_t >(stream_buffer->in_avail());
    std::vector< char > buf(avail, '\0');
    output.clear();
    output.reserve(avail);

    // Directly copy memory from buffer into our string buffer
    stream_buffer->sgetn(buf.data(), avail);
    output.assign(buf.data(), avail);
}

void OpenSearchCommunication::PrepareCursorResult(OpenSearchResult& opensearch_result) {
    // Prepare document and validate result
    try {
        LogMsg(OPENSEARCH_DEBUG, "Parsing result JSON with cursor.");
        opensearch_result.opensearch_result_doc =
            json_doc::parse(opensearch_result.result_json);
        CheckCursorShape(opensearch_result.opensearch_result_doc);
    } catch (const std::exception& e) {
        // A prefix of the json aids debugging without putting row data or an
        // unbounded body in the error text
        std::string str =
            "Exception obtained '" + std::string(e.what())
            + "' when parsing json string '"
            + ResponseBodyForMessage(opensearch_result.result_json) + "'.";
        throw std::runtime_error(str.c_str());
    }
}

std::shared_ptr< ErrorDetails > OpenSearchCommunication::ParseErrorResponse(
    const std::string& body) {
    // Proxies and load balancers send HTML or plain text, so a body that
    // isn't OpenSearch's error is expected here, not exceptional
    try {
        LogMsg(OPENSEARCH_DEBUG, "Parsing error response (with schema validation)");
        const json_doc doc = json_doc::parse(body);
        CheckErrorShape(doc);

        const json_doc& error = doc.at("error");
        auto error_details = std::make_shared< ErrorDetails >();
        error_details->reason = error.at("reason").get< std::string >();
        error_details->details = error.at("details").get< std::string >();
        error_details->source_type = error.at("type").get< std::string >();
        return error_details;
    } catch (const std::exception& e) {
        LogMsg(OPENSEARCH_DEBUG,
               ("Error response is not an OpenSearch error: "
                + std::string(e.what()))
                   .c_str());
        return nullptr;
    }
}

void OpenSearchCommunication::SetErrorFromResponse(
    long response_code, const std::string& client_error,
    const std::string& body) {
    m_error_details = std::make_shared< ErrorDetails >(ErrorFromResponse(
        response_code, client_error, body, m_error_message));
}

ErrorDetails OpenSearchCommunication::ErrorFromResponse(
    long response_code, const std::string& client_error,
    const std::string& body, std::string& message) {
    message = "Http response code was not OK. Code received: "
              + std::to_string(response_code) + ".";
    if (!client_error.empty())
        message += " Client error: '" + client_error + "'.";
    if (!body.empty())
        message += " Response error: '" + ResponseBodyForMessage(body) + "'.";
    LogMsg(OPENSEARCH_ERROR, message.c_str());

    std::shared_ptr< ErrorDetails > parsed = ParseErrorResponse(body);
    if (parsed)
        return *parsed;
    ErrorDetails details;
    details.reason = "Connection error";
    details.details = message;
    details.source_type = "Dummy type";
    details.type = ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE;
    return details;
}

void OpenSearchCommunication::SetCursorError(const std::string& message,
                                             ConnErrorType error_type) {
    LogMsg(OPENSEARCH_ERROR, message.c_str());
    ErrorDetails details;
    details.reason = "Cursor error";
    details.details = message;
    details.source_type = "Dummy type";
    details.type = error_type;
    SetCursorError(details);
}

void OpenSearchCommunication::SetCursorError(const ErrorDetails& details) {
    std::scoped_lock lock(m_cursor_error_mutex);
    m_cursor_error = details;
}

void OpenSearchCommunication::ClearCursorError() {
    std::scoped_lock lock(m_cursor_error_mutex);
    m_cursor_error.reset();
}

std::optional< ErrorDetails > OpenSearchCommunication::GetCursorError() {
    std::scoped_lock lock(m_cursor_error_mutex);
    return m_cursor_error;
}

void OpenSearchCommunication::SetErrorDetails(std::string reason, std::string message,
                                      ConnErrorType error_type) {
    // Prepare document and validate schema
    auto error_details = std::make_shared< ErrorDetails >();
    error_details->reason = reason;
    error_details->details = message;
    error_details->source_type = "Dummy type";
    error_details->type = error_type;
    m_error_details = error_details;
}

void OpenSearchCommunication::SetErrorDetails(ErrorDetails details) {
    // Prepare document and validate schema
    auto error_details = std::make_shared< ErrorDetails >(details);
    m_error_details = error_details;
}

void OpenSearchCommunication::GetJsonSchema(OpenSearchResult& opensearch_result) {
    // Prepare document and validate schema
    try {
        LogMsg(OPENSEARCH_DEBUG, "Parsing result JSON with schema.");
        opensearch_result.opensearch_result_doc =
            json_doc::parse(opensearch_result.result_json);
        CheckResultShape(opensearch_result.opensearch_result_doc);
    } catch (const std::exception& e) {
        // A prefix of the json aids debugging without putting row data or an
        // unbounded body in the error text
        std::string str =
            "Exception obtained '" + std::string(e.what())
            + "' when parsing json string '"
            + ResponseBodyForMessage(opensearch_result.result_json) + "'.";
        throw std::runtime_error(str.c_str());
    }
}

OpenSearchCommunication::OpenSearchCommunication()
#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreorder"
#endif  // __APPLE__
    : m_status(ConnStatusType::CONNECTION_BAD),
      m_error_type(ConnErrorType::CONN_ERROR_SUCCESS),
      m_valid_connection_options(false),
      m_is_retrieving(false),
      m_error_message(""),
      m_result_queue(2),
      m_client_encoding(m_supported_client_encodings[0]),
      m_error_message_to_user("")
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif  // __APPLE__
{
    ++AWS_SDK_HELPER;
}

OpenSearchCommunication::~OpenSearchCommunication() {
    // Release the HTTP client instance to free its resources before releasing AWS SDK.
    // Changing order of these actions would cause crash on disconnect.
    m_http_client.reset();
    --AWS_SDK_HELPER;
}

std::string OpenSearchCommunication::FormatErrorMessage(
    const ErrorDetails& details) {
    return ERROR_MSG_PREFIX + details.reason + ": "
           + std::regex_replace(details.details, std::regex("\\n"), "\\\\n");
}

std::string OpenSearchCommunication::GetErrorMessage() {
    // TODO #35 - Check if they expect NULL or "" when there is no error.
    if (m_error_details) {
        return FormatErrorMessage(*m_error_details);
    } else {
        return ERROR_MSG_PREFIX
               + "No error details available; check the driver logs.";
    }
}

ConnErrorType OpenSearchCommunication::GetErrorType() {
    return m_error_type;
}

bool OpenSearchCommunication::ConnectionOptions(runtime_options& rt_opts,
                                        bool use_defaults, int expand_dbname,
                                        unsigned int option_count) {
    (void)(expand_dbname);
    (void)(option_count);
    (void)(use_defaults);
    m_rt_opts = rt_opts;
    return CheckConnectionOptions();
}

bool OpenSearchCommunication::ConnectionOptions2() {
    return true;
}

bool OpenSearchCommunication::ConnectDBStart() {
    LogMsg(OPENSEARCH_ALL, "Starting DB connection.");
    m_status = ConnStatusType::CONNECTION_BAD;
    if (!m_valid_connection_options) {
        // TODO: get error message from CheckConnectionOptions
        m_error_message =
            "Invalid connection options, unable to connect to DB.";
        SetErrorDetails("Invalid connection options", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        DropDBConnection();
        return false;
    }

    m_status = ConnStatusType::CONNECTION_NEEDED;
    if (!EstablishConnection()) {
        m_error_message = m_error_message_to_user.empty()
                              ? "Failed to establish connection to DB."
                              : m_error_message_to_user;
        SetErrorDetails("Connection error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        DropDBConnection();
        return false;
    }

    LogMsg(OPENSEARCH_DEBUG, "Connection established.");
    m_status = ConnStatusType::CONNECTION_OK;
    return true;
}

ConnStatusType OpenSearchCommunication::GetConnectionStatus() {
    return m_status;
}

void OpenSearchCommunication::DropDBConnection() {
    LogMsg(OPENSEARCH_ALL, "Dropping DB connection.");
    if (m_http_client) {
        m_http_client.reset();
    }

    m_status = ConnStatusType::CONNECTION_BAD;
    StopResultRetrieval();
}

bool OpenSearchCommunication::CheckConnectionOptions() {
    LogMsg(OPENSEARCH_ALL, "Verifying connection options.");
    m_error_message = "";
    if (m_rt_opts.auth.auth_type != AUTHTYPE_NONE
        && m_rt_opts.auth.auth_type != AUTHTYPE_IAM) {
        if (m_rt_opts.auth.auth_type == AUTHTYPE_BASIC) {
            if (m_rt_opts.auth.username.empty()
                || m_rt_opts.auth.password.empty()) {
                m_error_message = AUTHTYPE_BASIC
                    " authentication requires a username and password.";
                SetErrorDetails("Auth error", m_error_message,
                                ConnErrorType::CONN_ERROR_INVALID_AUTH);
            }
        } else {
            m_error_message = "Unknown authentication type: '"
                              + m_rt_opts.auth.auth_type + "'";
            SetErrorDetails("Auth error", m_error_message,
                            ConnErrorType::CONN_ERROR_INVALID_AUTH);
        }
    } else if (m_rt_opts.conn.server.empty()) {
        m_error_message = "Host connection option was not specified.";
        SetErrorDetails("Connection error", m_error_message,
                        ConnErrorType::CONN_ERROR_UNABLE_TO_ESTABLISH);
    }

    Aws::Http::URI host(m_rt_opts.conn.server.c_str());
    // URI class sets default port if not set
    // Raise a error if server host field contains a port which doesn't match port field
    // We can check URI for non-default port only
    if (((host.GetScheme() == Aws::Http::Scheme::HTTP && host.GetPort() != Aws::Http::HTTP_DEFAULT_PORT)
        || (host.GetScheme() == Aws::Http::Scheme::HTTPS && host.GetPort() != Aws::Http::HTTPS_DEFAULT_PORT))
        && (m_rt_opts.conn.port.length() > 0 && host.GetPort() != atoi(m_rt_opts.conn.port.c_str()))) {
        m_error_message = "Ambigiuos port value specified.";
        SetErrorDetails("Connection error", m_error_message,
                        ConnErrorType::CONN_ERROR_UNABLE_TO_ESTABLISH);
    }

    if (!m_error_message.empty()) {
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        m_valid_connection_options = false;
        return false;
    }

    LogMsg(OPENSEARCH_DEBUG, "Required connection option are valid.");
    m_valid_connection_options = true;
    return m_valid_connection_options;
}

void OpenSearchCommunication::InitializeConnection() {
    Aws::Client::ClientConfiguration config;
    config.scheme = (m_rt_opts.crypt.use_ssl ? Aws::Http::Scheme::HTTPS
                                             : Aws::Http::Scheme::HTTP);
    config.verifySSL = m_rt_opts.crypt.verify_server;
    long response_timeout =
        static_cast< long >(DEFAULT_RESPONSE_TIMEOUT) * 1000L;
    try {
        response_timeout =
            std::stol(m_rt_opts.conn.timeout, nullptr, 10) * 1000L;
    } catch (...) {
    }
    config.connectTimeoutMs = response_timeout;
    config.httpRequestTimeoutMs = response_timeout;
    config.requestTimeoutMs = response_timeout;
    m_http_client = Aws::Http::CreateHttpClient(config);
}

std::optional< std::string > OpenSearchCommunication::BuildRequestBody(
    const std::string& query, const std::string& fetch_size,
    const std::string& cursor) {
    json_doc body = json_doc::object();
    if (!query.empty()) {
        body["query"] = query;
        if (!fetch_size.empty() && fetch_size != "-1")
            body["fetch_size"] = fetch_size;
    } else if (!cursor.empty()) {
        body["cursor"] = cursor;
    }
    // Strict: an ANSI app on a non-UTF-8 codepage passes e.g. Latin-1 bytes
    // straight through, and replacing them would silently change the query
    try {
        return body.dump();
    } catch (const nlohmann::json::type_error&) {
        return std::nullopt;
    }
}

// For app-supplied query text. IssueRequest also refuses invalid UTF-8, but
// its callers can only report a generic NULL response, so check up front
bool OpenSearchCommunication::CheckQueryEncoding(const std::string& query) {
    if (BuildRequestBody(query, "", ""))
        return true;
    m_error_message =
        "Query text is not valid UTF-8. Use the wide-character (W) ODBC "
        "functions or a UTF-8 client encoding.";
    SetErrorDetails("Execution error", m_error_message,
                    ConnErrorType::CONN_ERROR_QUERY_SYNTAX);
    LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
    return false;
}

std::shared_ptr< Aws::Http::HttpResponse >
OpenSearchCommunication::IssueRequest(
    const std::string& endpoint, const Aws::Http::HttpMethod request_type,
    const std::string& content_type, const std::string& query,
    const std::string& fetch_size, const std::string& cursor) {
    // Generate http request
    Aws::Http::URI host(m_rt_opts.conn.server.c_str());
    if (!m_rt_opts.conn.port.empty()) {
        host.SetPort((uint16_t) atoi(m_rt_opts.conn.port.c_str()));
    }
    host.SetPath(endpoint.c_str());

    std::shared_ptr< Aws::Http::HttpRequest > request =
        Aws::Http::CreateHttpRequest(
            host.GetURIString(),
            request_type,
            Aws::Utils::Stream::DefaultResponseStreamFactoryMethod);

    // Set header type
    if (!content_type.empty())
        request->SetHeaderValue(Aws::Http::CONTENT_TYPE_HEADER, Aws::String(ctype.c_str(), ctype.size()));

    // Set body
    if (!query.empty() || !cursor.empty()) {
        std::optional< std::string > body =
            BuildRequestBody(query, fetch_size, cursor);
        if (!body) {
            LogMsg(OPENSEARCH_ERROR,
                   "Not sending request: query text is not valid UTF-8.");
            return nullptr;
        }
        const std::string& body_str = *body;
        std::shared_ptr< Aws::StringStream > aws_ss =
            Aws::MakeShared< Aws::StringStream >("RequestBodyStream");
        *aws_ss << body_str;
        request->AddContentBody(aws_ss);
        request->SetContentLength(Aws::Utils::StringUtils::to_string(body_str.size()));
    }

    // Handle authentication
    const std::string& auth_type = m_rt_opts.auth.auth_type;

    if (auth_type == AUTHTYPE_BASIC) {
        std::string userpw_str =
            m_rt_opts.auth.username + ":" + m_rt_opts.auth.password;
        Aws::Utils::Array< unsigned char > userpw_arr(
            reinterpret_cast< const unsigned char* >(userpw_str.c_str()),
            userpw_str.length());
        Aws::String hashed_userpw =
            Aws::Utils::HashingUtils::Base64Encode(userpw_arr);
        request->SetAuthorization("Basic " + hashed_userpw);
    }

    else if (auth_type == AUTHTYPE_IAM) {
        std::shared_ptr< Aws::Auth::ProfileConfigFileAWSCredentialsProvider >
            credential_provider = Aws::MakeShared<
                Aws::Auth::ProfileConfigFileAWSCredentialsProvider >(
                CREDENTIALS_PROVIDER_ALLOCATION_TAG.c_str(),
                CREDENTIALS_PROFILE.c_str());

        const std::string& service_name =
            is_aoss_serverless
            ? SERVICE_NAME_AOSS_SERVERLESS
            : SERVICE_NAME_DEFAULT;

        Aws::Client::AWSAuthV4Signer signer(credential_provider,
                                            service_name.c_str(),
                                            m_rt_opts.auth.region.c_str());

        if (!m_rt_opts.auth.tunnel_host.empty()) {
            request->SetHeaderValue("host",
                                    Aws::Http::URI(m_rt_opts.auth.tunnel_host.c_str())
                                        .GetAuthority()
                                        .c_str());
        }
        signer.SignRequest(*request);
    }

    // Issue request and return response
    return m_http_client->MakeRequest(request);
}

bool OpenSearchCommunication::IsSQLPluginEnabled(std::shared_ptr< ErrorDetails > error_details) {
    std::string error_type = error_details->source_type;
    if (error_type =="SQLFeatureDisabledException") {
        return false;
    }
    return true;
}

/**
 * @brief Queries server to determine SQL plugin availability.
 *
 * @return true : Successfully queried server for SQL plugin
 * @return false : Failed to query server, no plugin available, exception was caught
 */
bool OpenSearchCommunication::CheckSQLPluginAvailability() {
    LogMsg(OPENSEARCH_ALL, "Checking for SQL plugin status.");
    std::string test_query = "SHOW TABLES LIKE '%'";
    try {
        std::shared_ptr< Aws::Http::HttpResponse > response =
            IssueRequest(sql_endpoint,
                         Aws::Http::HttpMethod::HTTP_POST, ctype, test_query);
        if (response == nullptr) {
            m_error_message =
                "Failed to receive response."
                "Received NULL response.";
            SetErrorDetails("Execution error", m_error_message,
                            ConnErrorType::CONN_ERROR_QUERY_SYNTAX);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
            return false;
        }

        AwsHttpResponseToString(response, m_response_str);
        if (response->GetResponseCode() == Aws::Http::HttpResponseCode::OK) {
            return true;
        }
        else {
            std::shared_ptr< ErrorDetails > error_details =
                ParseErrorResponse(m_response_str);
            if (!error_details) {
                // Not from OpenSearch (e.g. an HTML 502 from a proxy), so
                // say what came back rather than blaming the SQL plugin
                SetErrorFromResponse(
                    static_cast< long >(response->GetResponseCode()),
                    ClientErrorOf(*response), m_response_str);
                m_error_message_to_user = m_error_message;
                return false;
            }
            const std::string body_for_message =
                ResponseBodyForMessage(m_response_str);

            if(!IsSQLPluginEnabled(error_details)) {
                m_error_message_to_user =
                    "SQL plugin is disabled, please enable the plugin "
                    "to use this driver.";
                m_error_message +=
                    "The SQL plugin is disabled. The SQL plugin must be "
                    "enabled in order to use this driver. Response body: '"
                    + body_for_message + "'";
                SetErrorDetails("Connection error", m_error_message,
                                ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            }

            if (response->HasClientError()) {
                m_error_message += " Client error: '"
                                   + response->GetClientErrorMessage() + "'.";
                SetErrorDetails("HTTP client error", m_error_message,
                                ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            }

            if (!m_response_str.empty()) {
                m_error_message +=
                    " Response error: '" + body_for_message + "'.";
                SetErrorDetails("Connection error", m_error_message,
                                ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            }
        }
    } catch (...) {
        m_error_message_to_user =
            "SQL plugin is not available at url: " +
            (m_rt_opts.conn.server + (m_rt_opts.conn.port.empty() ?
            "" : ":" + m_rt_opts.conn.port)) +
            ", please install the SQL plugin to use this driver.";
        m_error_message +=
            "Unexpected exception thrown from the server, "
            "the SQL plugin is not installed or in unhealthy status.";
        SetErrorDetails("Server error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
    }
    return false;
}

bool OpenSearchCommunication::EstablishConnection() {

    LogMsg(OPENSEARCH_ALL, "Attempting to establish DB connection.");

    // Generate HttpClient Connection class if it does not exist
    if (!m_http_client) {
        InitializeConnection();
    }

    // Set whether the connection is to OpenSearch serverless cluster.
    SetIsAossServerless();

    // Set the SQL endpoint to connect to. If this is a serverless connection,
    // the SQL endpoint is always set correctly; if not, the endpoint is
    // determined by sending a request to OpenSearch, which may result in an
    // error.
    SetSqlEndpoint();

    if (is_aoss_serverless && (sql_endpoint == SQL_ENDPOINT_ERROR)) {
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return false;
    }

    // Check whether SQL plugin has been installed and enabled in the
    // OpenSearch server since the SQL plugin is a prerequisite to
    // use this driver.
    if(!CheckSQLPluginAvailability()) {
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return false;
    }

    return true;
}

std::vector< std::string > OpenSearchCommunication::GetColumnsWithSelectQuery(
    const std::string table_name) {
    std::vector< std::string > list_of_column;
    if (table_name.empty()) {
        m_error_type = ConnErrorType::CONN_ERROR_INVALID_NULL_PTR;
        m_error_message = "Query is NULL";
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return list_of_column;
    }

    // Prepare query
    std::string query = "SELECT * FROM " + table_name + " LIMIT 0";
    std::string msg = "Attempting to execute a query \"" + query + "\"";
    LogMsg(OPENSEARCH_DEBUG, msg.c_str());

    // table_name comes from the app (SQLColumns), so it can be non-UTF-8 too
    if (!CheckQueryEncoding(query))
        return list_of_column;

    // Issue request
    std::shared_ptr< Aws::Http::HttpResponse > response =
        IssueRequest(sql_endpoint, Aws::Http::HttpMethod::HTTP_POST,
                     ctype, query);

    // Validate response
    if (response == nullptr) {
        m_error_message =
            "Failed to receive response from query. "
            "Received NULL response.";
        SetErrorDetails("HTTP client error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return list_of_column;
    }

    // Convert body from Aws IOStream to string
    std::unique_ptr< OpenSearchResult > result = std::make_unique< OpenSearchResult >();
    AwsHttpResponseToString(response, result->result_json);

    // If response was not valid, set error
    if (response->GetResponseCode() != Aws::Http::HttpResponseCode::OK) {
        m_error_type = ConnErrorType::CONN_ERROR_QUERY_SYNTAX;
        SetErrorFromResponse(
            static_cast< long >(response->GetResponseCode()),
            ClientErrorOf(*response), result->result_json);
        return list_of_column;
    }

    GetJsonSchema(*result);

    for (const auto& column : result->opensearch_result_doc.at("schema")) {
        std::string column_name = column.at("name").get< std::string >();
        list_of_column.push_back(column_name);
    }

    return list_of_column;
}

std::string OpenSearchCommunication::TruncateResponseBody(
    const std::string& body, size_t max_bytes) {
    if (body.size() <= max_bytes)
        return body;

    // If the byte just past the cut is a UTF-8 continuation byte (10xxxxxx),
    // the cut is mid-sequence: back up to the sequence's lead byte. A
    // sequence is at most 4 bytes, so give up after 3 (input isn't UTF-8).
    auto is_continuation = [&body](size_t i) {
        return (static_cast< unsigned char >(body[i]) & 0xC0) == 0x80;
    };
    size_t cut = max_bytes;
    while (cut > 0 && max_bytes - cut < 3 && is_continuation(cut))
        --cut;
    if (is_continuation(cut))
        cut = max_bytes;

    return body.substr(0, cut) + "... (" + std::to_string(body.size())
           + " bytes total)";
}

std::string OpenSearchCommunication::ResponseBodyForMessage(
    const std::string& body) {
    // Only the most verbose logging gets the whole body
    LogMsg(OPENSEARCH_TRACE, ("Full response body: '" + body + "'").c_str());
    return TruncateResponseBody(body);
}

int OpenSearchCommunication::ExecDirect(const char* query, const char* fetch_size_) {
    m_error_details.reset();
    ClearCursorError();
    if (!query) {
        m_error_message = "Query is NULL";
        SetErrorDetails("Execution error", m_error_message,
                        ConnErrorType::CONN_ERROR_INVALID_NULL_PTR);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return -1;
    } else if (!m_http_client) {
        m_error_message = "Unable to connect. Please try connecting again.";
        SetErrorDetails("Execution error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return -1;
    }

    // Prepare statement
    std::string statement(query);
    std::string fetch_size(fetch_size_);
    std::string msg = "Attempting to execute a query \"" + statement + "\"";
    LogMsg(OPENSEARCH_DEBUG, msg.c_str());

    if (!CheckQueryEncoding(statement))
        return -1;

    // Issue request
    std::shared_ptr< Aws::Http::HttpResponse > response =
        IssueRequest(sql_endpoint, Aws::Http::HttpMethod::HTTP_POST,
                     ctype, statement, fetch_size);

    // Validate response
    if (response == nullptr) {
        m_error_message =
            "Failed to receive response from query. "
            "Received NULL response.";
        SetErrorDetails("Execution error", m_error_message,
                        ConnErrorType::CONN_ERROR_QUERY_SYNTAX);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return -1;
    }

    // Convert body from Aws IOStream to string
    std::unique_ptr< OpenSearchResult > result = std::make_unique< OpenSearchResult >();
    AwsHttpResponseToString(response, result->result_json);

    // If response was not valid, set error
    if (response->GetResponseCode() != Aws::Http::HttpResponseCode::OK) {
        m_error_type = ConnErrorType::CONN_ERROR_QUERY_SYNTAX;
        SetErrorFromResponse(
            static_cast< long >(response->GetResponseCode()),
            ClientErrorOf(*response), result->result_json);
        return -1;
    }

    // Add to result queue and return. nlohmann's exceptions aren't
    // runtime_errors, and none may escape to the C API.
    try {
        ConstructOpenSearchResult(*result);
    } catch (const std::exception& e) {
        m_error_message =
            "Received runtime exception: " + std::string(e.what());
        if (!result->result_json.empty()) {
            m_error_message +=
                " Result body: " + ResponseBodyForMessage(result->result_json);
        }
        SetErrorDetails("Execution error", m_error_message,
                        ConnErrorType::CONN_ERROR_QUERY_SYNTAX);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return -1;
    }

    const std::string cursor = result->cursor;
    while (!m_result_queue.push(QUEUE_TIMEOUT, result.get())) {
        if (ConnStatusType::CONNECTION_OK == m_status) {
            return -1;
        }
    }

    result.release();

    if (!cursor.empty()) {
        // If the response has a cursor, this thread will retrieve more result
        // pages asynchronously. Retrieval counts as started now: a PopResult
        // that beat the thread's start would otherwise see the end of data.
        m_is_retrieving = true;
        std::thread([&, cursor]() { SendCursorQueries(cursor); }).detach();
    }

    return 0;
}

void OpenSearchCommunication::SendCursorQueries(std::string cursor) {
    if (cursor.empty()) {
        return;
    }
    m_is_retrieving = true;

    try {
        while (!cursor.empty() && m_is_retrieving) {
            std::shared_ptr< Aws::Http::HttpResponse > response = IssueRequest(
                sql_endpoint, Aws::Http::HttpMethod::HTTP_POST,
                ctype, "", "", cursor);
            // Errors from here go to SetCursorError, not m_error_details,
            // which the app thread reads unlocked. A failed page must still
            // reach the end of this function, so PopResult stops waiting.
            if (response == nullptr) {
                // No server answer, so no point asking it to close the cursor
                SetCursorError(
                    "Failed to receive response from cursor. "
                    "Received NULL response.",
                    ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
                break;
            }

            std::unique_ptr< OpenSearchResult > result = std::make_unique< OpenSearchResult >();
            AwsHttpResponseToString(response, result->result_json);
            if (response->GetResponseCode()
                != Aws::Http::HttpResponseCode::OK) {
                std::string message;
                SetCursorError(ErrorFromResponse(
                    static_cast< long >(response->GetResponseCode()),
                    ClientErrorOf(*response), result->result_json, message));
                SendCloseCursorRequest(cursor);
                break;
            }
            PrepareCursorResult(*result);

            if (result->opensearch_result_doc.contains("cursor")) {
                cursor = result->opensearch_result_doc.at("cursor").get< std::string >();
                result->cursor = result->opensearch_result_doc.at("cursor").get< std::string >();
            } else {
                SendCloseCursorRequest(cursor);
                cursor.clear();
            }

            while (m_is_retrieving
                   && !m_result_queue.push(QUEUE_TIMEOUT, result.get())) {
            }

            // Don't release when attempting to push to the queue as it may take
            // multiple tries.
            result.release();
        }
    } catch (const std::exception& e) {
        // This runs on a detached thread: an escaping exception terminates
        // the app
        SetCursorError("Received runtime exception: " + std::string(e.what()),
                       ConnErrorType::CONN_ERROR_QUERY_SYNTAX);
        if (!cursor.empty())
            SendCloseCursorRequest(cursor);
    }

    if (!m_is_retrieving) {
        m_result_queue.clear();
    } else {
        m_is_retrieving = false;
    }
}

void OpenSearchCommunication::SendCloseCursorRequest(const std::string& cursor) {
    // Best effort, on the cursor thread: a failure is only logged (the
    // server drops the cursor when its keep-alive runs out) and nothing may
    // throw out of here
    try {
        std::shared_ptr< Aws::Http::HttpResponse > response = IssueRequest(
            sql_endpoint + "/close", Aws::Http::HttpMethod::HTTP_POST, ctype,
            "", "", cursor);
        if (response == nullptr) {
            LogMsg(OPENSEARCH_WARNING,
                   "Failed to receive response from cursor close request. "
                   "Received NULL response.");
        } else if (response->GetResponseCode()
                   != Aws::Http::HttpResponseCode::OK) {
            LogMsg(OPENSEARCH_WARNING,
                   ("Cursor close request failed. Code received: "
                    + std::to_string(
                        static_cast< long >(response->GetResponseCode())))
                       .c_str());
        }
    } catch (const std::exception& e) {
        LogMsg(OPENSEARCH_WARNING,
               ("Cursor close request failed: " + std::string(e.what()))
                   .c_str());
    }
}

void OpenSearchCommunication::StopResultRetrieval() {
    m_is_retrieving = false;
    m_result_queue.clear();
}

void OpenSearchCommunication::ConstructOpenSearchResult(OpenSearchResult& result) {
    GetJsonSchema(result);
    const json_doc& schema_array = result.opensearch_result_doc.at("schema");
    for (const auto& column : schema_array) {
        // The SQL plugin adds "alias" when the query aliases the column
        // (SELECT x AS y), and may send alias == name; it is the label the
        // user asked for.
        std::string column_name = column.contains("alias")
                                      ? column.at("alias").get< std::string >()
                                      : column.at("name").get< std::string >();

        ColumnInfo col_info;
        col_info.field_name = column_name;
        col_info.type_oid = KEYWORD_TYPE_OID;
        col_info.type_size = KEYWORD_TYPE_SIZE;
        col_info.display_size = KEYWORD_DISPLAY_SIZE;
        col_info.length_of_str = KEYWORD_TYPE_SIZE;
        col_info.relation_id = 0;
        col_info.attribute_number = 0;

        result.column_info.push_back(col_info);
    }
    if (result.opensearch_result_doc.contains("cursor")) {
        result.cursor = result.opensearch_result_doc.at("cursor").get< std::string >();
    }
    result.command_type = "SELECT";
    result.num_fields = (uint16_t)schema_array.size();
}

inline void OpenSearchCommunication::LogMsg(OpenSearchLogLevel level, const char* msg) {
#if WIN32
#pragma warning(push)
#pragma warning(disable : 4551)
#endif  // WIN32
    // cppcheck outputs an erroneous missing argument error which breaks build.
    // Disable for this function call
    MYLOG(level, "%s\n", msg);
#if WIN32
#pragma warning(pop)
#endif  // WIN32
}

OpenSearchResult* OpenSearchCommunication::PopResult() {
    OpenSearchResult* result = NULL;
    while (!m_result_queue.pop(QUEUE_TIMEOUT, result) && m_is_retrieving) {
    }

    return result;
}

// TODO #36 - Send query to database to get encoding
std::string OpenSearchCommunication::GetClientEncoding() {
    return m_client_encoding;
}

// TODO #36 - Send query to database to set encoding
bool OpenSearchCommunication::SetClientEncoding(std::string& encoding) {
    if (std::find(m_supported_client_encodings.begin(),
                  m_supported_client_encodings.end(), encoding)
        != m_supported_client_encodings.end()) {
        m_client_encoding = encoding;
        return true;
    }
    LogMsg(OPENSEARCH_ERROR,
           std::string("Failed to find encoding " + encoding).c_str());
    return false;
}

std::string OpenSearchCommunication::GetServerVersion() {
    if (!m_http_client) {
        InitializeConnection();
    }

    // Issue request
    std::shared_ptr< Aws::Http::HttpResponse > response =
        IssueRequest("", Aws::Http::HttpMethod::HTTP_GET, "", "", "");
    if (response == nullptr) {
        m_error_message =
            "Failed to receive response from server version query. "
            "Received NULL response.";
        SetErrorDetails("Connection error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return "";
    }

    // Parse server version
    if (response->GetResponseCode() == Aws::Http::HttpResponseCode::OK) {
        try {
            AwsHttpResponseToString(response, m_response_str);
            json_doc doc = json_doc::parse(m_response_str);
            if (doc.contains("version") && doc.at("version").contains("number")) {
                return doc.at("version").at("number").get< std::string >();
            }

        } catch (const nlohmann::json::type_error& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (const nlohmann::json::parse_error& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (const std::exception& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (...) {
            LogMsg(OPENSEARCH_ERROR,
                   "Unknown exception thrown when parsing main endpoint "
                   "response.");
        }
    }
    LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
    return "";
}

/**
 * @brief Queries supplied URL to validate Server Distribution. Maintains
 * backwards compatibility with opendistro distribution.
 * Not compatible with OpenSearch Serverless.
 *
 * @return std::string : Server distribution name, returns "" on error
 */
std::string OpenSearchCommunication::GetServerDistribution() {
    if (!m_http_client) {
        InitializeConnection();
    }

    std::shared_ptr< Aws::Http::HttpResponse > response =
        IssueRequest("", Aws::Http::HttpMethod::HTTP_GET, "", "", "");
    if (response == nullptr || response->GetResponseCode() == Aws::Http::HttpResponseCode::REQUEST_NOT_MADE) {
        m_error_message =
            "Failed to receive response from server version query. "
            "Received no response from url: "
            + (m_rt_opts.conn.server + (m_rt_opts.conn.port.empty() ?
            "" : ":" + m_rt_opts.conn.port));
        SetErrorDetails("Connection error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        m_error_message_to_user = m_error_message;
        return "";
    }

    // Parse server version distribution
    if (response->GetResponseCode() == Aws::Http::HttpResponseCode::OK) {
        try {
            AwsHttpResponseToString(response, m_response_str);
            json_doc doc = json_doc::parse(m_response_str);
            if (doc.contains("version") && doc.at("version").contains("distribution")) {
                return doc.at("version").at("distribution").get< std::string >();
            }
        } catch (const nlohmann::json::type_error& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (const nlohmann::json::parse_error& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (const std::exception& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (...) {
            LogMsg(OPENSEARCH_ERROR,
                   "Unknown exception thrown when parsing main endpoint "
                   "response.");
        }
    }
    LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
    return "";
}

std::string OpenSearchCommunication::GetClusterName() {
    if (!m_http_client) {
        InitializeConnection();
    }

    // Issue request
    std::shared_ptr< Aws::Http::HttpResponse > response =
        IssueRequest("", Aws::Http::HttpMethod::HTTP_GET, "", "", "");
    if (response == nullptr) {
        m_error_message =
            "Failed to receive response from cluster name query. "
            "Received NULL response.";
        SetErrorDetails("Connection error", m_error_message,
                        ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
        LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        return "";
    }

    // Parse cluster name
    if (response->GetResponseCode() == Aws::Http::HttpResponseCode::OK) {
        try {
            AwsHttpResponseToString(response, m_response_str);
            json_doc doc = json_doc::parse(m_response_str);
            if (doc.contains("cluster_name")) {
                return doc.at("cluster_name").get< std::string >();
            }

        } catch (const nlohmann::json::type_error& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (const nlohmann::json::parse_error& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (const std::exception& e) {
            // parse_error's message quotes the token it stopped at
            m_error_message = "Error parsing main endpoint response: "
                              + TruncateResponseBody(e.what());
            SetErrorDetails("Connection error", m_error_message,
                            ConnErrorType::CONN_ERROR_COMM_LINK_FAILURE);
            LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
        } catch (...) {
            LogMsg(OPENSEARCH_ERROR,
                   "Unknown exception thrown when parsing main endpoint "
                   "response.");
        }
    }
    LogMsg(OPENSEARCH_ERROR, m_error_message.c_str());
    return "";
}

/**
 * @brief Sets URL endpoint for the SQL plugin.
 * Sets it to SQL_ENDPOINT_ERROR if an appropriate
 * endpoint could not be determined.
 */
void OpenSearchCommunication::SetSqlEndpoint() {

    // Serverless Elasticsearch is not supported.
    if (is_aoss_serverless) {
        sql_endpoint = SQL_ENDPOINT_OPENSEARCH;
        return;
    }

    std::string distribution = GetServerDistribution();
    if (distribution.empty()) {
        sql_endpoint = SQL_ENDPOINT_ERROR;
    } else if (distribution == DISTRIBUTION_OPENSEARCH) {
        sql_endpoint = SQL_ENDPOINT_OPENSEARCH;
    } else {
        sql_endpoint = SQL_ENDPOINT_ELASTICSEARCH;
    }
}

/**
 * @brief Sets flag indicating whether this is
 * connecting to an OpenSearch Serverless cluster.
 */
void OpenSearchCommunication::SetIsAossServerless() {

    // Treat the connection as serverless if the server URL corresponds to
    // Amazon OpenSearch Serverless. Limitation: does not support serverless
    // with proxy server URL.
    is_aoss_serverless = m_rt_opts.conn.server.find("aoss.amazonaws.com") != std::string::npos;
}
