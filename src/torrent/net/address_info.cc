#include "config.h"

#include "address_info.h"

namespace torrent {

int
ai_get_addrinfo(const char* nodename, const char* servname, const addrinfo* hints, ai_unique_ptr& res) {
  addrinfo* ai;
  int err = ai__getaddrinfo(nodename, servname, hints, &ai);

  if (err != 0)
    return err;

  res.reset(ai);
  return 0;
}

sa_unique_ptr
ai_find_first_sa(const addrinfo* ai, int family, int socktype) {
  for ( ; ai != nullptr; ai = ai->ai_next) {
    if (family != 0 && family != ai->ai_family)
      continue;
    if (socktype != 0 && socktype != ai->ai_socktype)
      continue;

    return sa_copy(ai->ai_addr);
  }

  return nullptr;
}

sa_unique_ptr
ai_get_first_sa(const char* nodename, const char* servname, const addrinfo* hints) {
  ai_unique_ptr aip;

  if (ai_get_addrinfo(nodename, servname, hints, aip) != 0)
    return nullptr;

  return sa_copy(aip->ai_addr);
}

int
ai_each_inet_inet6_first(const char* nodename, ai_sockaddr_func lambda, int flags) {
  int err;
  ai_unique_ptr ai;

  // TODO: Change to a single call using hints with both inet/inet6.
  if ((err = ai_get_addrinfo(nodename, NULL, ai_make(0, AF_INET, SOCK_STREAM).get(), ai)) != 0 &&
      (err = ai_get_addrinfo(nodename, NULL, ai_make(0, AF_INET6, SOCK_STREAM).get(), ai)) != 0)
    return err;

  lambda(ai->ai_addr);
  return 0;
}

int
ai__getaddrinfo(const char* nodename, const char* servname, const struct addrinfo* hints, struct addrinfo** res) {
  return ::getaddrinfo(nodename, servname, hints, res);
}

}
