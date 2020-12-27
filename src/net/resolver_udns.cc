#include "config.h"

#ifdef USE_UDNS

#include "resolver_udns.h"

#include <netdb.h>
#include <udns.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include "torrent/common.h"
#include "torrent/poll.h"
#include "torrent/net/address_info.h"
#include "torrent/net/socket_address.h"
#include "torrent/utils/log.h"

#include "globals.h"
#include "manager.h"

#define LT_LOG(log_fmt, ...)                                            \
  lt_log_print(LOG_NET_RESOLVER, "resolver: " log_fmt, __VA_ARGS__);

namespace torrent {
namespace {

inline void
check_valid_query(const query_udns* query, const char* fun_name) {
  if (query == nullptr)
    throw internal_error(std::string(fun_name) + " query == nullptr");
  if (query->resolver == nullptr)
    throw internal_error(std::string(fun_name) + " query->resolver == nullptr");
}

inline void
query_list_erase(resolver_udns::query_list_type& list, query_udns* query) {
  auto itr = std::find_if(list.begin(), list.end(), [query](resolver_udns::query_ptr& q){ return q.get() == query; });

  if (itr != list.end())
    list.erase(itr);
}

inline resolver_udns::query_ptr
query_list_move(resolver_udns::query_list_type& list, query_udns* query) {
  auto itr = std::find_if(list.begin(), list.end(), [query](resolver_udns::query_ptr& q){ return q.get() == query; });

  if (itr == list.end())
    throw internal_error("resolver_udns::query_list_move called with an unknown query pointer");

  auto u_ptr = std::move(*itr);
  list.erase(itr);

  return u_ptr;
}

int
udns_error_to_gaierror(int udnserror) {
  switch (udnserror) {
    case DNS_E_TEMPFAIL:
      return EAI_AGAIN;
    case DNS_E_PROTOCOL:
      // this isn't quite right
      return EAI_FAIL;
    case DNS_E_NXDOMAIN:
      return EAI_NONAME;
    case DNS_E_NODATA:
      return EAI_ADDRFAMILY;
    case DNS_E_NOMEM:
      return EAI_MEMORY;
    case DNS_E_BADQUERY:
      return EAI_NONAME;
    default:
      return EAI_ADDRFAMILY;
  }
}

// TODO: Clear the request when done, process timeout will remove
// queries as the timeout is triggered.

// TODO: Option to wait for both v4 and v6 responses.

// Compatibility layers so udns can call std::function callbacks.
void
a4_callback_wrapper(struct ::dns_ctx *ctx, ::dns_rr_a4 *result, void *data) {
  // Udns will free the a4_query after this callback exits.
  query_udns *query = static_cast<query_udns*>(data);
  query->a4_query = nullptr;

  if (result == nullptr || result->dnsa4_nrr == 0) {
    LT_LOG("ipv4 callback no result (hostname:%s family:%i)", query->hostname.c_str(), query->family);

    if (query->a6_query == nullptr) {
      auto query_ptr = resolver_udns::erase_query(query);
      (*(query_ptr->callback))(nullptr, udns_error_to_gaierror(::dns_status(ctx)));
    } else {
      // Return and wait to see if we get an a6 response.
    }

    return;
  }
  
  if (query->a6_query != nullptr) {
    ::dns_cancel(ctx, query->a6_query);
    query->a6_query = nullptr;
  }

  query->a4_result = sa_make_in_addr_t(result->dnsa4_addr[0].s_addr);
  LT_LOG("a4 query successed (hostname:%s family:%i sa:%s)", query->hostname.c_str(), query->a4_result->sa_family, sap_pretty_str(query->a4_result).c_str());
  resolver_udns::complete_query(query);
}

void
a6_callback_wrapper(struct ::dns_ctx *ctx, ::dns_rr_a6 *result, void *data) {
  // Udns will free the a6_query after this callback exits.
  query_udns *query = static_cast<query_udns*>(data);
  query->a6_query = nullptr;

  if (result == nullptr || result->dnsa6_nrr == 0) {
    LT_LOG("ipv6 callback no result (hostname:%s family:%i)", query->hostname.c_str(), query->family);

    if (query->a4_query == nullptr) {
      auto query_ptr = resolver_udns::erase_query(query);
      (*(query_ptr->callback))(nullptr, udns_error_to_gaierror(::dns_status(ctx)));
    } else {
      // Return and wait to see if we get an a4 response.
    }

    return;
  }

  if (query->a4_query != nullptr) {
    ::dns_cancel(ctx, query->a4_query);
    query->a4_query = nullptr;
  }

  query->a6_result = sa_make_in6_addr(result->dnsa6_addr[0]);
  LT_LOG("a6 query successed (hostname:%s family:%i sa:%s)", query->hostname.c_str(), query->a6_result->sa_family, sap_pretty_str(query->a6_result).c_str());
  resolver_udns::complete_query(query);
}

} // namespace

resolver_udns::resolver_udns() {
  LT_LOG("initializing", 0);

  // reinitialize the default context, no-op
  // TODO don't do this here --- do it once in the manager, or in rtorrent
  ::dns_init(nullptr, 0);
  // thread-safe context isolated to this object:
  m_ctx = ::dns_new(nullptr);
  m_fileDesc = ::dns_open(m_ctx);

  if (m_fileDesc == -1)
    throw internal_error("resolver_udns::resolver_udns call to dns_init failed");

  m_task_timeout.slot() = std::bind(&resolver_udns::process_timeouts, this);
}

resolver_udns::~resolver_udns() {
  LT_LOG("closing", 0);

  priority_queue_erase(&taskScheduler, &m_task_timeout);
  ::dns_close(m_ctx);
  ::dns_free(m_ctx);
  m_ctx = nullptr;
  m_fileDesc = -1;
}

void
resolver_udns::event_read() {
  ::dns_ioevent(m_ctx, 0);
}

const char*
resolver_udns::type_name() const {
  return "resolver_udns";
}

// Wraps udns's dns_submit_a[46] functions. they and it return control
// immediately, without either sending outgoing UDP packets or
// executing callbacks.
void*
resolver_udns::enqueue_resolve(const char* hostname, int family, resolver_callback* callback) {
  query_ptr query(new query_udns { hostname, family, 0, this, callback, nullptr, nullptr });

  if (query->callback == nullptr)
    throw internal_error("torrent::resolver::enqueue_resolve: query->callback == nullptr");

  if (resolve_numeric_query(query)) {
    m_completed_queries.push_back(std::move(query));
    return m_completed_queries.back().get();
  }

  if (family == AF_INET || family == AF_UNSPEC) {
    query->a4_query = ::dns_submit_a4(m_ctx, hostname, 0, a4_callback_wrapper, query.get());

    if (query->a4_query == nullptr) {
      // XXX udns does query parsing up front and will fail immediately
      // during submission of malformed domain names, e.g., `..`. In order to
      // maintain a clean interface, keep track of this query internally
      // so we can call the callback later with a failure code
      if (::dns_status(m_ctx) != DNS_E_BADQUERY)
        throw internal_error("resolver_udns call to dns_submit_a4 failed with unrecoverable error");

      LT_LOG("malformed ipv4 query (hostname:%s family:%i)", hostname, family);
      return move_malformed_query(std::move(query), EAI_NONAME);
    }
  }

  if (family == AF_INET6 || family == AF_UNSPEC) {
    query->a6_query = ::dns_submit_a6(m_ctx, hostname, 0, a6_callback_wrapper, query.get());

    if (query->a6_query == nullptr) {
      if (::dns_status(m_ctx) != DNS_E_BADQUERY)
        throw internal_error("resolver_udns call to dns_submit_a6 failed with unrecoverable error");

      LT_LOG("malformed ipv6 query (hostname:%s family:%i)", hostname, family);
      return move_malformed_query(std::move(query), EAI_NONAME);
    }
  }

  LT_LOG("enqueued query (name:%s family:%i ipv4:%s ipv6:%s)", hostname, family,
         (query->a4_query != nullptr ? "yes" : "no"), (query->a6_query != nullptr ? "yes" : "no"));

  m_pending_queries.push_back(std::move(query));
  return m_pending_queries.back().get();
}

// Wraps the dns_timeouts function. it sends packets and can execute
// arbitrary callbacks.
void
resolver_udns::flush_resolves() {
  while (!m_malformed_queries.empty()) {
    query_ptr query = std::move(m_malformed_queries.back());
    m_malformed_queries.pop_back();

    LT_LOG("flush malformed query (hostname:%s family:%i)", query->hostname.c_str(), query->family);

    (*(query->callback))(nullptr, query->error);
  }

  while (!m_completed_queries.empty()) {
    query_ptr query = std::move(m_completed_queries.back());
    m_completed_queries.pop_back();

    LT_LOG("flush completed query (hostname:%s family:%i)", query->hostname.c_str(), query->family);

    // TODO: Need to handle this better.
    torrent::sa_unique_ptr sap;

    if (query->a4_result) {
      (*(query->callback))(query->a4_result.get(), query->error);
    } else if (query->a6_result) {
      (*(query->callback))(query->a6_result.get(), query->error);
    } else {
      throw internal_error("flush_resolves: completed queries contained query with no results");
    }
  }

  process_timeouts();
}

// TODO: Make adding queries automatically insert_read/etc. Do not flush when adding.

void
resolver_udns::cancel(void* v_query) {
  auto query = static_cast<query_udns*>(v_query);

  if (query == nullptr)
    return; // TODO: Throw instead.

  LT_LOG("cancel query (hostname:%s family:%i)", query->hostname.c_str(), query->family);

  if (query->a4_query != nullptr)
    ::dns_cancel(m_ctx, query->a4_query);

  if (query->a6_query != nullptr)
    ::dns_cancel(m_ctx, query->a6_query);

  query_list_erase(m_pending_queries, query);
  query_list_erase(m_malformed_queries, query);
}

void
resolver_udns::complete_query(query_udns* query) {
  check_valid_query(query, "resolver_udns::complete_query");
  auto q_ptr = query_list_move(query->resolver->m_pending_queries, query);

  LT_LOG("completing query (hostname:%s family:%i)", q_ptr->hostname.c_str(), q_ptr->family);
  
  query->resolver->m_completed_queries.push_back(std::move(q_ptr));
}

resolver_udns::query_ptr
resolver_udns::erase_query(query_udns* query) {
  check_valid_query(query, "resolver_udns::erase_query");
  auto q_ptr = query_list_move(query->resolver->m_pending_queries, query);

  LT_LOG("erasing query (hostname:%s family:%i)", q_ptr->hostname.c_str(), q_ptr->family);
  
  return q_ptr;
}

bool
resolver_udns::resolve_numeric_query(query_ptr& q_ptr) {
  ai_unique_ptr aip;

  if (aip_get_addrinfo(q_ptr->hostname, nullptr, ai_make(AI_NUMERICHOST, q_ptr->family, 0), aip) != 0)
    return false;

  sa_unique_ptr sap = aip_find_first_sa(aip);

  LT_LOG("resolved numeric query (hostname:%s family:%i sa:%s)", q_ptr->hostname.c_str(), q_ptr->family, sap_pretty_str(sap).c_str());

  if (sap == nullptr)
    throw internal_error("resolver_udns::resolve_numeric_query: got nullptr");

  if (sap->sa_family == AF_INET)
    q_ptr->a4_result = std::move(sap);
  else if (sap->sa_family == AF_INET6)
    q_ptr->a6_result = std::move(sap);
  else
    throw internal_error("resolver_udns::resolve_numeric_query: resolved unexpected address family");

  return true;
}

query_udns*
resolver_udns::move_malformed_query(query_ptr query, int error) {
  if (has_pending_query(query) || has_completed_query(query) || has_malformed_query(query))
    throw internal_error("resolver_udns::move_malformed_query: query already in a list");

  query->error = error;
  m_malformed_queries.push_back(std::move(query));
  return m_malformed_queries.back().get();
}

void
resolver_udns::process_timeouts() {
  int timeout = ::dns_timeouts(m_ctx, -1, 0);

  if (timeout == -1) {
    // no pending queries
    poll_event_remove_read(this);
    poll_event_remove_error(this);
  } else {
    poll_event_insert_read(this);
    poll_event_insert_error(this);
    priority_queue_erase(&taskScheduler, &m_task_timeout);
    priority_queue_insert(&taskScheduler, &m_task_timeout, (cachedTime + rak::timer::from_seconds(timeout)).round_seconds());
  }
}

}

#endif
