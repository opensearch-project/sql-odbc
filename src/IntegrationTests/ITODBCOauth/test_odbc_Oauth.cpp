// clang-format off
#include "pch.h"
#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif  // __APPLE__
#include <aws/core/Aws.h>
#include <aws/core/http/HttpClient.h>
#include <aws/core/http/HttpRequest.h>
#include <aws/core/utils/json/JsonSerializer.h>
#ifdef __APPLE__
#pragma clang diagnostic pop
#endif  // __APPLE__
#include "unit_test_helper.h"

using namespace Aws;
using namespace Aws::Client;
using namespace Aws::Utils::Json;
using namespace Aws::Http;
using namespace std;

class OAuth2TokenProvider {
public:

    static std::string GetToken() {
        // Set up HTTP request with client credentials flow
        Aws::String endpoint = "http://localhost:1852/realms/local-development/protocol/openid-connect/token";
        Aws::String username = "opensearch-admin";
        Aws::String password = "opensearch-admin-secret";
        Aws::String scope = "opensearch";        
        Aws::String requestBody = "grant_type=client_credentials&client_id=" + username + "&client_secret=" + password + "&scope=" + scope;
        auto request = CreateHttpRequest(endpoint, HttpMethod::HTTP_POST, Aws::Utils::Stream::DefaultResponseStreamFactoryMethod);

        // Set headers
        request->SetHeaderValue(Aws::Http::CONTENT_TYPE_HEADER, "application/x-www-form-urlencoded");

        // Set body
        std::shared_ptr<Aws::StringStream> bodyStream = Aws::MakeShared<Aws::StringStream>("");
        *bodyStream << requestBody;
        request->AddContentBody(bodyStream);
        request->SetContentLength(std::to_string(requestBody.length()));

        // Create HTTP client
        auto httpClient = Aws::Http::CreateHttpClient(Aws::Client::ClientConfiguration());

        // Send request
        auto httpResponse = httpClient->MakeRequest(request);

        // Check if request was successful
        if (!httpResponse || !httpResponse->GetResponseBody().good()) {
            throw std::runtime_error("Failed to fetch token");
        }

        // Read token from response body
        Aws::StringStream responseBody;
        responseBody << httpResponse->GetResponseBody().rdbuf();
        JsonValue jsonValue(responseBody.str());
        
        if (!jsonValue.WasParseSuccessful()) {
            throw std::runtime_error("Failed to parse JSON response");
        }

        JsonView jsonView = jsonValue.View();

        if (!jsonView.ValueExists("access_token")) {
            throw std::runtime_error("Access token not found in JSON response");
        }

        return jsonView.GetString("access_token").c_str();
    }
};

TEST(OAuth2_Auth, TokenAuthentication) {
    Aws::SDKOptions options;
    Aws::String host = "http://localhost:9200";

    EXPECT_NO_THROW(Aws::InitAPI(options));

    auto request = CreateHttpRequest(host, HttpMethod::HTTP_GET, Aws::Utils::Stream::DefaultResponseStreamFactoryMethod);

    // Obtain the OAuth2 token
    std::string token = OAuth2TokenProvider::GetToken();

    ASSERT_FALSE(token.empty());

    // Set the Authorization header with the Bearer token 
    request->SetAuthorization("Bearer " + token);

    auto http_client = CreateHttpClient(Aws::Client::ClientConfiguration());

    auto response = http_client->MakeRequest(request);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(Aws::Http::HttpResponseCode::OK, response->GetResponseCode());
    
    EXPECT_NO_THROW(Aws::ShutdownAPI(options));
}

TEST(OAuth2_Auth, InvalidToken) {
    Aws::SDKOptions options;
    Aws::String host = "http://localhost:9200";

    EXPECT_NO_THROW(Aws::InitAPI(options));

    auto request = CreateHttpRequest(host, HttpMethod::HTTP_GET, Aws::Utils::Stream::DefaultResponseStreamFactoryMethod);

    // Obtain the OAuth2 token
    std::string token = OAuth2TokenProvider::GetToken();

    ASSERT_FALSE(token.empty());

    // Corrupt the token
    token.pop_back();

    // Set the Authorization header with the Bearer token 
    request->SetAuthorization("Bearer " + token);

    auto http_client = CreateHttpClient(Aws::Client::ClientConfiguration());

    auto response = http_client->MakeRequest(request);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(Aws::Http::HttpResponseCode::FORBIDDEN, response->GetResponseCode());
    
    EXPECT_NO_THROW(Aws::ShutdownAPI(options));
}

TEST(SettingSDKOptions, TurnLoggingOn) {
    Aws::SDKOptions options;
    options.loggingOptions.logLevel = Aws::Utils::Logging::LogLevel::Info;
    EXPECT_NO_THROW(Aws::InitAPI(options));
    EXPECT_NO_THROW(Aws::ShutdownAPI(options));
}

int main(int argc, char** argv) {
    testing::internal::CaptureStdout();
    ::testing::InitGoogleTest(&argc, argv);
    int failures = RUN_ALL_TESTS();
    std::string output = testing::internal::GetCapturedStdout();
    std::cout << output << std::endl;
    std::cout << (failures ? "Not all tests passed." : "All tests passed") << std::endl;
    WriteFileIfSpecified(argv, argv + argc, "-fout", output);
    return failures;
}