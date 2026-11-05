#ifndef UNIT_TEST_HELPER
#define UNIT_TEST_HELPER

#include <cstdlib>
#include <string>

// Cluster settings for tests that talk to OpenSearch, overridable from the
// environment (CI runs a security-enabled cluster with a per-run password).
inline std::string TestEnv(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : fallback;
}

#ifdef USE_SSL
const bool use_ssl = TestEnv("ODBC_TEST_USE_SSL", "1") == "1";
#else
const bool use_ssl = TestEnv("ODBC_TEST_USE_SSL", "0") == "1";
#endif
const std::string test_port = TestEnv("ODBC_TEST_PORT", "9200");
const std::string test_user = TestEnv("ODBC_TEST_USER", "admin");
const std::string test_password = TestEnv("ODBC_TEST_PASSWORD", "admin");

void WriteFileIfSpecified(char** begin, char** end, const std::string& option,
                          std::string& output);

#endif
