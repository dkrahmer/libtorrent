#ifndef LIBTORRENT_NET_ADDRESS_INFO_H
#define LIBTORRENT_NET_ADDRESS_INFO_H

#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <netdb.h>
#include <torrent/common.h>
#include <torrent/net/socket_address.h>

namespace torrent {

struct ai_deleter {
  void operator()(addrinfo* ai) const { freeaddrinfo(ai); }
};

typedef std::unique_ptr<addrinfo, ai_deleter> ai_unique_ptr;
typedef std::unique_ptr<const addrinfo, ai_deleter> c_ai_unique_ptr;
typedef std::function<void (const sockaddr*)> ai_sockaddr_func;

void          ai_clear(addrinfo* ai);
ai_unique_ptr ai_make(int flags = 0, int family = 0, int socktype = 0);

int           ai_get_addrinfo(const char* nodename, const char* servname, const addrinfo* hints, ai_unique_ptr& res) LIBTORRENT_EXPORT;
sa_unique_ptr ai_find_first_sa(const addrinfo* ai, int family = 0, int socktype = 0) LIBTORRENT_EXPORT;

//
// Helper functions:
//

sa_unique_ptr ai_get_first_sa(const char* nodename, const char* servname = nullptr, const addrinfo* hints = nullptr) LIBTORRENT_EXPORT;
sa_unique_ptr ai__get_first_sa(const char* nodename, const char* servname = nullptr, const addrinfo* hints = nullptr) LIBTORRENT_EXPORT;

int  ai_each_inet_inet6_first(const char* nodename, ai_sockaddr_func lambda, int flags = 0) LIBTORRENT_EXPORT;

// Get all addrinfo's, iterate, etc.

//
// Safe conversion from unique_ptr arguments:
//

inline void aip_clear(ai_unique_ptr& aip) { return ai_clear(aip.get()); }
inline int  aip_get_addrinfo(const char* nodename, const char* servname, const ai_unique_ptr& hints, ai_unique_ptr& res) { return ai_get_addrinfo(nodename, servname, hints.get(), res); }
inline int  aip_get_addrinfo(const char* nodename, const char* servname, const c_ai_unique_ptr& hints, ai_unique_ptr& res) { return ai_get_addrinfo(nodename, servname, hints.get(), res); }
inline int  aip_get_addrinfo(const std::string& nodename, const char* servname, const ai_unique_ptr& hints, ai_unique_ptr& res) { return ai_get_addrinfo(nodename.c_str(), servname, hints.get(), res); }
inline int  aip_get_addrinfo(const std::string& nodename, const char* servname, const c_ai_unique_ptr& hints, ai_unique_ptr& res) { return ai_get_addrinfo(nodename.c_str(), servname, hints.get(), res); }
inline auto aip_find_first_sa(const ai_unique_ptr& aip, int family = 0, int socktype = 0) -> sa_unique_ptr { return ai_find_first_sa(aip.get(), family, socktype); }

inline auto aip_get_first_sa(const char* nodename, const char* servname = nullptr, const ai_unique_ptr& hints = ai_unique_ptr()) -> sa_unique_ptr { return ai_get_first_sa(nodename, servname, hints.get()); }
inline auto aip_get_first_sa(const char* nodename, const char* servname = nullptr, const c_ai_unique_ptr& hints = ai_unique_ptr()) -> sa_unique_ptr { return ai_get_first_sa(nodename, servname, hints.get()); }
inline auto aip_get_first_sa(const std::string& nodename, const char* servname = nullptr, const ai_unique_ptr& hints = ai_unique_ptr()) -> sa_unique_ptr { return ai_get_first_sa(nodename.c_str(), servname, hints.get()); }
inline auto aip_get_first_sa(const std::string& nodename, const char* servname = nullptr, const c_ai_unique_ptr& hints = ai_unique_ptr()) -> sa_unique_ptr { return ai_get_first_sa(nodename.c_str(), servname, hints.get()); }

//
// Mock function wrappers:
//

[[gnu::weak]] int ai__getaddrinfo(const char* nodename, const char* servname, const struct addrinfo* hints, struct addrinfo** res) LIBTORRENT_EXPORT;

//
// Implementations:
//

inline void
ai_clear(addrinfo* ai) {
  std::memset(ai, 0, sizeof(addrinfo));  
}

inline ai_unique_ptr
ai_make(int flags, int family, int socktype) {
  ai_unique_ptr aip(new addrinfo);

  aip_clear(aip);
  aip->ai_flags = flags;
  aip->ai_family = family;
  aip->ai_socktype = socktype;

  return aip;
}

}

#endif
