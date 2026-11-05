// Unit tests for convert.c: server text value -> application C type.
// No cluster needed; these call copy_and_convert_field() directly on a bare
// statement handle.
#include <string>
#include <vector>

#include "convert.h"
#include "gtest/gtest.h"
#include "opensearch_connection.h"
#include "opensearch_types.h"
#include "statement.h"

namespace {

class ConvertTest : public ::testing::Test {
   protected:
    void SetUp() override {
        m_conn = CC_Constructor();
        ASSERT_NE(nullptr, m_conn);
        m_stmt = SC_Constructor(m_conn);
        ASSERT_NE(nullptr, m_stmt);
    }

    void TearDown() override {
        if (m_stmt)
            SC_Destructor(m_stmt);
        if (m_conn)
            CC_Destructor(m_conn);
    }

    // Convert `value` (typed `field_type`) to SQL_C_CHAR.
    std::string AsChar(OID field_type, const char* value) {
        char buf[64] = {};
        SQLLEN ind = 0;
        EXPECT_EQ(COPY_OK,
                  copy_and_convert_field(m_stmt, field_type, -1,
                                         const_cast< char* >(value), SQL_C_CHAR,
                                         0, buf, sizeof(buf), &ind, &ind));
        std::string out(buf);
        EXPECT_EQ(static_cast< SQLLEN >(out.size()), ind);
        return out;
    }

    // Convert `value` (typed `field_type`) to SQL_C_WCHAR, narrowed back to
    // ASCII for comparison.
    std::string AsWChar(OID field_type, const char* value) {
        SQLWCHAR buf[64] = {};
        SQLLEN ind = 0;
        EXPECT_EQ(COPY_OK, copy_and_convert_field(m_stmt, field_type, -1,
                                                  const_cast< char* >(value),
                                                  SQL_C_WCHAR, 0, buf,
                                                  sizeof(buf), &ind, &ind));
        std::string out;
        for (size_t i = 0; i < sizeof(buf) / sizeof(buf[0]) && buf[i] != 0; i++)
            out.push_back(static_cast< char >(buf[i]));
        EXPECT_EQ(static_cast< SQLLEN >(out.size() * sizeof(SQLWCHAR)), ind);
        return out;
    }

    ConnectionClass* m_conn = nullptr;
    StatementClass* m_stmt = nullptr;
};

TEST_F(ConvertTest, TimeWithoutFractionAsChar) {
    EXPECT_EQ("12:34:56", AsChar(OPENSEARCH_TYPE_TIME, "12:34:56"));
}

// The fractional-seconds branch used to sprintf() through a NULL pointer.
TEST_F(ConvertTest, TimeWithFractionAsChar) {
    EXPECT_EQ("12:34:56.789", AsChar(OPENSEARCH_TYPE_TIME, "12:34:56.789"));
}

TEST_F(ConvertTest, TimeWithFractionAsWChar) {
    EXPECT_EQ("01:02:03.5", AsWChar(OPENSEARCH_TYPE_TIME, "01:02:03.5"));
}

TEST_F(ConvertTest, TimeWithNanosecondsAsChar) {
    EXPECT_EQ("23:59:59.123456789",
              AsChar(OPENSEARCH_TYPE_TIME, "23:59:59.123456789"));
}

TEST_F(ConvertTest, TimestampWithFractionAsChar) {
    EXPECT_EQ("2024-02-29 13:45:10.123456",
              AsChar(OPENSEARCH_TYPE_TIMESTAMP, "2024-02-29 13:45:10.123456"));
}

}  // namespace
