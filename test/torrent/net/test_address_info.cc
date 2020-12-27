#include "config.h"

#include "test_address_info.h"

#include "helpers/network.h"
#include "torrent/net/address_info.h"
#include "torrent/net/socket_address.h"
#include "torrent/utils/log.h"

CPPUNIT_TEST_SUITE_NAMED_REGISTRATION(test_address_info, "torrent/net");

#ifdef EAI_ADDRFAMILY
#define LT_EAI_FAMILY EAI_ADDRFAMILY
#else
#define LT_EAI_FAMILY EAI_FAMILY
#endif

#define TEST_AI_INIT(name)                                              \
  lt_log_print(torrent::LOG_MOCK_CALLS, "ai_begin: %s", name);          \
  auto ai_hint_numerichost = torrent::ai_make(AI_NUMERICHOST);          \
  const auto c_ai_hint_numerichost = torrent::ai_make(AI_NUMERICHOST);  \
  auto ai_hint_numerichost_in = torrent::ai_make(AI_NUMERICHOST, AF_INET); \
  auto ai_hint_numerichost_in6 = torrent::ai_make(AI_NUMERICHOST, AF_INET6);

#define TEST_AI_REDIRECT_INIT(name)             \
  TEST_AI_INIT(name);                           \
  redirect_default_ai_getaddrinfo();

inline void make__fake_aip(addrinfo*& ai) {}
template <typename... SA>
void
make__fake_aip(addrinfo*& ai, const char* sa, SA... v_sa) {
  ai = torrent::ai_make().release();
  ai->ai_addr = wrap_ai_get_first_sa(sa).release();
  ai->ai_family = ai->ai_addr->sa_family;
  //ai->ai_socktype = ai->ai_addr->sa_family;
  make__fake_aip(ai->ai_next, v_sa...);
}

template <typename... SA>
torrent::ai_unique_ptr
make_fake_aip(SA... v_sa) {
  addrinfo* ai = nullptr;
  make__fake_aip(ai, v_sa...);
  return torrent::ai_unique_ptr(ai);
}

void
test_address_info::test_get_addrinfo() {
  { TEST_AI_REDIRECT_INIT("basic");
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet|aif_any> (std::bind(torrent::ai_get_addrinfo, "0.0.0.0", nullptr, nullptr, std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet6|aif_any>(std::bind(torrent::ai_get_addrinfo, "::", nullptr, nullptr, std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet> (std::bind(torrent::ai_get_addrinfo, "1.1.1.1", nullptr, nullptr, std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet6>(std::bind(torrent::ai_get_addrinfo, "ff01::1", nullptr, nullptr, std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet6>(std::bind(torrent::ai_get_addrinfo, "2001:0db8:85a3:0000:0000:8a2e:0370:7334", nullptr, nullptr, std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet> (std::bind(torrent::ai_get_addrinfo, "1.1.1.1", "22123", nullptr, std::placeholders::_1), 22123));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet6>(std::bind(torrent::ai_get_addrinfo, "2001:db8:a::", "22123", nullptr, std::placeholders::_1), 22123));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_none> (std::bind(torrent::ai_get_addrinfo, "localhost", nullptr, nullptr, std::placeholders::_1)));
  };
  { TEST_AI_REDIRECT_INIT("errors");
    CPPUNIT_ASSERT(test_valid_ai_ref_err(std::bind(torrent::ai_get_addrinfo, "1.1.1.300", nullptr, nullptr, std::placeholders::_1), EAI_NONAME));
    CPPUNIT_ASSERT(test_valid_ai_ref_err(std::bind(torrent::ai_get_addrinfo, "2001:db8:a::22123", nullptr, nullptr, std::placeholders::_1), EAI_NONAME));
  };
}

void
test_address_info::test_get_addrinfo_numerichost() {
  { TEST_AI_REDIRECT_INIT("basic");
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet> (std::bind(torrent::ai_get_addrinfo, "1.1.1.1", nullptr, ai_hint_numerichost.get(), std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet> (std::bind(torrent::ai_get_addrinfo, "1.1.1.1", nullptr, ai_hint_numerichost_in.get(), std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet6>(std::bind(torrent::ai_get_addrinfo, "ff01::1", nullptr, ai_hint_numerichost.get(), std::placeholders::_1)));
    CPPUNIT_ASSERT(test_valid_ai_ref<aif_inet6>(std::bind(torrent::ai_get_addrinfo, "ff01::1", nullptr, ai_hint_numerichost_in6.get(), std::placeholders::_1)));
  };
  { TEST_AI_REDIRECT_INIT("errors");
    CPPUNIT_ASSERT(test_valid_ai_ref_err(std::bind(torrent::ai_get_addrinfo, "localhost", nullptr, ai_hint_numerichost.get(), std::placeholders::_1), EAI_NONAME));
    CPPUNIT_ASSERT(test_valid_ai_ref_err(std::bind(torrent::ai_get_addrinfo, "1.1.1.1", nullptr, ai_hint_numerichost_in6.get(), std::placeholders::_1), LT_EAI_FAMILY));
    CPPUNIT_ASSERT(test_valid_ai_ref_err(std::bind(torrent::ai_get_addrinfo, "ff01::1", nullptr, ai_hint_numerichost_in.get(), std::placeholders::_1), LT_EAI_FAMILY));
  };
}

void
test_address_info::test_find_first_sa() {
  { TEST_AI_INIT("unspec");
    CPPUNIT_ASSERT(torrent::ai_find_first_sa(nullptr) == nullptr);
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1")), wrap_ai_get_first_sa("1.1.1.1")));
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("ff01::1")), wrap_ai_get_first_sa("ff01::1")));
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1", "2.2.2.2")), wrap_ai_get_first_sa("1.1.1.1")));
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("ff01::1", "ff01::2")), wrap_ai_get_first_sa("ff01::1")));
  };
  { TEST_AI_INIT("inet");
    CPPUNIT_ASSERT(torrent::ai_find_first_sa(nullptr, AF_INET) == nullptr);
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1"), AF_INET), wrap_ai_get_first_sa("1.1.1.1")));
    CPPUNIT_ASSERT(torrent::aip_find_first_sa(make_fake_aip("ff01::1"), AF_INET) == nullptr);
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1", "2.2.2.2"), AF_INET), wrap_ai_get_first_sa("1.1.1.1")));
    CPPUNIT_ASSERT(torrent::aip_find_first_sa(make_fake_aip("ff01::1", "ff01::2"), AF_INET) == nullptr);
  };
  { TEST_AI_INIT("inet6");
    CPPUNIT_ASSERT(torrent::ai_find_first_sa(nullptr, AF_INET6) == nullptr);
    CPPUNIT_ASSERT(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1"), AF_INET6) == nullptr);
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("ff01::1"), AF_INET6), wrap_ai_get_first_sa("ff01::1")));
    CPPUNIT_ASSERT(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1", "2.2.2.2"), AF_INET6) == nullptr);
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("ff01::1", "ff01::2"), AF_INET6), wrap_ai_get_first_sa("ff01::1")));
  };
  { TEST_AI_INIT("next");
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("ff01::1", "2.2.2.2"), AF_INET), wrap_ai_get_first_sa("2.2.2.2")));
    CPPUNIT_ASSERT(torrent::sap_equal(torrent::aip_find_first_sa(make_fake_aip("1.1.1.1", "ff01::2"), AF_INET6), wrap_ai_get_first_sa("ff01::2")));
  };
}

// TODO: Move this to a separate directory.
void
test_address_info::test_helpers() {
  torrent::sin_unique_ptr sin_zero = torrent::sin_from_sa(wrap_ai_get_first_sa("0.0.0.0"));
  CPPUNIT_ASSERT(sin_zero != nullptr);
  CPPUNIT_ASSERT(sin_zero->sin_family == AF_INET);
  CPPUNIT_ASSERT(sin_zero->sin_port == 0);
  CPPUNIT_ASSERT(sin_zero->sin_addr.s_addr == in_addr().s_addr);

  torrent::sin_unique_ptr sin_1 = torrent::sin_from_sa(wrap_ai_get_first_sa("1.2.3.4"));
  CPPUNIT_ASSERT(sin_1 != nullptr);
  CPPUNIT_ASSERT(sin_1->sin_family == AF_INET);
  CPPUNIT_ASSERT(sin_1->sin_port == 0);
  CPPUNIT_ASSERT(sin_1->sin_addr.s_addr == htonl(0x01020304));

  torrent::sin6_unique_ptr sin6_zero = torrent::sin6_from_sa(wrap_ai_get_first_sa("::"));
  CPPUNIT_ASSERT(sin6_zero != nullptr);
  CPPUNIT_ASSERT(sin6_zero->sin6_family == AF_INET6);
  CPPUNIT_ASSERT(sin6_zero->sin6_port == 0);
  CPPUNIT_ASSERT(compare_sin6_addr(sin6_zero->sin6_addr, in6_addr{0}));

  torrent::sin6_unique_ptr sin6_1 = torrent::sin6_from_sa(wrap_ai_get_first_sa("ff01::1"));
  CPPUNIT_ASSERT(sin6_1 != nullptr);
  CPPUNIT_ASSERT(sin6_1->sin6_family == AF_INET6);
  CPPUNIT_ASSERT(sin6_1->sin6_port == 0);
  CPPUNIT_ASSERT(compare_sin6_addr(sin6_1->sin6_addr, in6_addr{0xff, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}));
  CPPUNIT_ASSERT(!compare_sin6_addr(sin6_1->sin6_addr, in6_addr{0xff, 0x01, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2}));
}
