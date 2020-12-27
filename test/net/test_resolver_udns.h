#include "helpers/test_fixture.h"

class test_resolver_udns : public test_fixture {
  CPPUNIT_TEST_SUITE(test_resolver_udns);

  CPPUNIT_TEST(basic);
  CPPUNIT_TEST(enqueue_resolve);
  CPPUNIT_TEST(enqueue_resolve_error);

  CPPUNIT_TEST_SUITE_END();

public:
  void basic();

  void enqueue_resolve();
  void enqueue_resolve_error();
};
