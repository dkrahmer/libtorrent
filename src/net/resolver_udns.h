#ifndef LIBTORRENT_NET_RESOLVER_UDNS_H
#define LIBTORRENT_NET_RESOLVER_UDNS_H

#include <algorithm>
#include <cinttypes>
#include <functional>
#include <list>
#include <memory>

#include "torrent/net/socket_event.h"
#include "torrent/connection_manager.h"
#include "torrent/net/socket_address.h"

#include "rak/priority_queue_default.h"

struct dns_ctx;
struct dns_query;

namespace torrent {

class resolver_udns;

// 'error' is any EAI_* error.
struct query_udns {
  std::string hostname;
  int family;
  int error;

  resolver_udns* resolver;
  resolver_callback *callback;
  ::dns_query* a4_query;
  ::dns_query* a6_query;

  sa_unique_ptr a4_result;
  sa_unique_ptr a6_result;
};

class resolver_udns : public socket_event {
public:
  typedef std::unique_ptr<query_udns> query_ptr;
  typedef std::vector<query_ptr>      query_list_type;

  resolver_udns();
  ~resolver_udns();

  void     event_read() override;
  auto     type_name() const -> const char* override;

  void*    enqueue_resolve(const char* hostname, int family, resolver_callback* callback);
  void     flush_resolves();
  void     cancel(void* v_query);

  auto     pending_queries() const -> const query_list_type&;
  auto     completed_queries() const -> const query_list_type&;
  auto     malformed_queries() const -> const query_list_type&;

  inline bool has_pending_query(const void* query) const;
  inline bool has_completed_query(const void* query) const;
  inline bool has_malformed_query(const void* query) const;

  inline bool has_pending_query(const query_ptr& query) const;
  inline bool has_completed_query(const query_ptr& query) const;
  inline bool has_malformed_query(const query_ptr& query) const;

  static void complete_query(query_udns* query);
  static auto erase_query(query_udns* query) -> resolver_udns::query_ptr;

private:
  bool     resolve_numeric_query(query_ptr& query);
  auto     move_malformed_query(query_ptr query, int error) -> query_udns*;
  void     process_timeouts();

  rak::priority_item m_task_timeout;

  ::dns_ctx*      m_ctx;
  query_list_type m_pending_queries;
  query_list_type m_completed_queries;
  query_list_type m_malformed_queries;
};

inline const resolver_udns::query_list_type& resolver_udns::pending_queries() const { return m_pending_queries; }
inline const resolver_udns::query_list_type& resolver_udns::completed_queries() const { return m_completed_queries; }
inline const resolver_udns::query_list_type& resolver_udns::malformed_queries() const { return m_malformed_queries; }

inline bool
resolver_udns::has_pending_query(const void* query) const {
  return std::find_if(m_pending_queries.begin(), m_pending_queries.end(),
                      [query](const resolver_udns::query_ptr& q){ return q.get() == static_cast<const query_udns*>(query); })
    != m_pending_queries.end();
}

inline bool
resolver_udns::has_completed_query(const void* query) const {
  return std::find_if(m_completed_queries.begin(), m_completed_queries.end(),
                      [query](const resolver_udns::query_ptr& q){ return q.get() == static_cast<const query_udns*>(query); })
    != m_completed_queries.end();
}

inline bool
resolver_udns::has_malformed_query(const void* query) const {
  return std::find_if(m_malformed_queries.begin(), m_malformed_queries.end(),
                      [query](const resolver_udns::query_ptr& q){ return q.get() == static_cast<const query_udns*>(query); })
    != m_malformed_queries.end();
}

inline bool resolver_udns::has_pending_query(const query_ptr& query) const { return has_pending_query(query.get()); }
inline bool resolver_udns::has_completed_query(const query_ptr& query) const { return has_completed_query(query.get()); }
inline bool resolver_udns::has_malformed_query(const query_ptr& query) const { return has_malformed_query(query.get()); }

}

#endif
