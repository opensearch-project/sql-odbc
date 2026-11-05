// Tests for driver log file naming (mylog.c). Unix-only: Windows resolves the
// executable name through GetModuleFileName instead.
//
// The readlink() bug this guards against reads uninitialised stack memory, so
// it only fails reliably under valgrind/ASan; run it under memcheck in CI.
#ifndef WIN32

#include "gtest/gtest.h"

extern "C" const char *GetExeProgramName();

TEST(MyLog, ExeProgramNameIsBasenameOfExecutable) {
    EXPECT_STREQ("ut_mylog", GetExeProgramName());
}

#endif  // WIN32
