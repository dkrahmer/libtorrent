#include "helpers/test_fixture.h"

class test_address_info : public test_fixture {
  CPPUNIT_TEST_SUITE(test_address_info);

  CPPUNIT_TEST(test_get_addrinfo);
  CPPUNIT_TEST(test_get_addrinfo_numerichost);
  CPPUNIT_TEST(test_find_first_sa);

  CPPUNIT_TEST(test_helpers);

  CPPUNIT_TEST_SUITE_END();

public:
  void test_get_addrinfo();
  void test_get_addrinfo_numerichost();
  void test_find_first_sa();

  void test_helpers();
};
