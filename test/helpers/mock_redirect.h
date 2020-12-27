#ifndef LIBTORRENT_HELPERS_MOCK_REDIRECT_H
#define LIBTORRENT_HELPERS_MOCK_REDIRECT_H

#include "helpers/mock_function.h"
#include "torrent/net/address_info.h"

template<typename R, typename... Args>
void
mock_redirect(R fn(Args...), std::function<R (Args...)> func) {
  typedef mock_function_map<R, Args...> mock_map;
  mock_map::redirects[reinterpret_cast<void*>(fn)] = func;
}

inline void
redirect_default_ai_getaddrinfo() {
  mock_redirect(&torrent::ai__getaddrinfo, std::function<decltype(torrent::ai__getaddrinfo)>(std::bind(&::getaddrinfo, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4)));
}

#endif
