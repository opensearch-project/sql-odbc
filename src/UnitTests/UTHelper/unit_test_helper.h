#ifndef UNIT_TEST_HELPER
#define UNIT_TEST_HELPER

#include <string>
#ifdef USE_SSL
const bool use_ssl = true;
#else
const bool use_ssl = false;
#endif

void WriteFileIfSpecified(char** begin, char** end, const std::string& option,
                          std::string& output);

#endif
