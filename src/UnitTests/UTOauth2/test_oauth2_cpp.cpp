// clang-format off
#include "pch.h"
#ifdef __APPLE__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif  // __APPLE__
#include <aws/core/Aws.h>
#include <aws/core/http/HttpClient.h>
#include <gtest/gtest.h>
#include "unit_test_helper.h"
#include "opensearch_communication.h"
#include <iostream>
#include <cstdlib>
#include <memory>
#include <string>
#include <stdexcept>
#include <array>
#include <fstream>

#include <aws/core/http/standard/StandardHttpRequest.h>
#include <aws/core/http/standard/StandardHttpResponse.h>
#include <aws/core/http/HttpClientFactory.h>
#include <aws/core/utils/memory/stl/AWSStringStream.h>
#include <aws/core/utils/memory/stl/AWSStreamFwd.h>
#include <aws/core/utils/memory/stl/AWSString.h>
#include <aws/core/utils/Outcome.h>

using namespace Aws;
using namespace Aws::Client;
using namespace Aws::Http;
using namespace std;

static const char host[] = "http://localhost:9200";

// Helper function to execute cURL command and get the token
std::string getTokenFromServer() {
    // Set up HTTP request
    Aws::String endpoint = "http://localhost:1852/realms/local-development/protocol/openid-connect/token";
    Aws::String username = "opensearch-admin";
    Aws::String password = "opensearch-admin-secret";
    Aws::String requestBody = "grant_type=password&scope=opensearch&username=" + username + "&password=" + password;
    Aws::Utils::Stream::MemoryStream requestStream(requestBody.c_str());
    Aws::Http::Standard::StandardHttpRequest httpRequest(endpoint, HttpMethod::HTTP_POST, &requestStream, Aws::Utils::Stream::DefaultResponseStreamFactoryMethod);

    // Set headers
    httpRequest.SetHeaderValue("Content-Type", "application/x-www-form-urlencoded");

    // Create HTTP client
    auto httpClient = Aws::Http::CreateHttpClient(Aws::Client::ClientConfiguration());

    // Send request
    auto httpResponse = httpClient->MakeRequest(httpRequest);

    // Check if request was successful
    if (!httpResponse.IsSuccess()) {
        throw std::runtime_error("Failed to fetch token");
    }

    // Read token from response body
    Aws::String responseBody = Aws::Utils::Stream::ReadAllResponseStreams(httpResponse.GetResponseBody());
    size_t tokenStart = responseBody.find("\"access_token\":\"") + 16;
    size_t tokenEnd = responseBody.find("\",\"", tokenStart);
    std::string token = responseBody.substr(tokenStart, tokenEnd - tokenStart);

    return token;
}

class OpenSearchCommunicationTest : public ::testing::Test {
protected:
    OpenSearchCommunication* openSearchComm;
    RuntimeOptions rt_opts;

    void SetUp() override {
        // Fetch the token
        std::string tokenResponse = getTokenFromServer();
        // Parse the token from the response
        size_t tokenStart = tokenResponse.find("\"access_token\":\"") + 16;
        size_t tokenEnd = tokenResponse.find("\",\"", tokenStart);
        std::string token = tokenResponse.substr(tokenStart, tokenEnd - tokenStart);
        // Set up runtime options with the fetched token
        rt_opts.auth.auth_type = "OAUTH2";
        rt_opts.auth.access_token = token;

        openSearchComm = new OpenSearchCommunication();
        openSearchComm->SetRuntimeOptions(rt_opts);
        openSearchComm->InitializeConnection();
    }

    void TearDown() override {
        delete openSearchComm;
    }
};

TEST_F(OpenSearchCommunicationTest, TokenAuthentication) {
    std::string endpoint = "/_cluster/health";
    auto response = openSearchComm->IssueRequest(endpoint, HttpMethod::HTTP_GET, "", "", "", "");
    
    ASSERT_NE(response, nullptr);
    EXPECT_EQ(Aws::Http::HttpResponseCode::OK, response->GetResponseCode());
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
