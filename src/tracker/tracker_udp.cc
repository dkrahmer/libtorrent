// libTorrent - BitTorrent library
// Copyright (C) 2005, Jari Sundell
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
// 
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// In addition, as a special exception, the copyright holders give
// permission to link the code of portions of this program with the
// OpenSSL library under certain conditions as described in each
// individual source file, and distribute linked combinations
// including the two.
//
// You must obey the GNU General Public License in all respects for
// all of the code used other than OpenSSL.  If you modify file(s)
// with this exception, you may extend this exception to your version
// of the file(s), but you are not obligated to do so.  If you do not
// wish to do so, delete this exception statement from your version.
// If you delete this exception statement from all source files in the
// program, then also delete it here.
//
// Contact:  Jari Sundell <jaris@ifi.uio.no>
//
//           Skomakerveien 33
//           3185 Skoppum, NORWAY

#include "config.h"

#include <cerrno>
#include <stdlib.h>
#include <sstream>
#include <sigc++/bind.h>

#include "net/manager.h"
#include "torrent/exceptions.h"

#include "tracker_udp.h"

namespace torrent {

std::string
_string_to_hex(const std::string& src) {
  std::stringstream stream;

  stream << std::hex << std::uppercase;

  for (std::string::const_iterator itr = src.begin(); itr != src.end(); ++itr)
    stream << ((unsigned char)*itr >> 4) << ((unsigned char)*itr & 0xf);

  return stream.str();
}

TrackerUdp::TrackerUdp(TrackerInfo* info, const std::string& url) :
  TrackerBase(info, url),
  m_readBuffer(NULL),
  m_writeBuffer(NULL) {

  m_taskTimeout.set_iterator(taskScheduler.end());
  m_taskTimeout.set_slot(sigc::mem_fun(*this, &TrackerUdp::receive_timeout));
}

TrackerUdp::~TrackerUdp() {
  close();
}
  
bool
TrackerUdp::is_busy() const {
  return m_fd.is_valid();
}

void
TrackerUdp::send_state(TrackerInfo::State state,
		       uint64_t down,
		       uint64_t up,
		       uint64_t left) {
  close();

  //return receive_failed("UDP tracker support not enabled");

  if (!parse_url())
    return receive_failed("Could not parse UDP hostname or port");

  if (!m_fd.open_datagram() ||
      !m_fd.set_nonblock() ||
      //(!m_bindAddress.is_any() && !m_fd.bind(m_bindAddress)))
      !m_fd.bind(m_bindAddress))
    return receive_failed("Could not open UDP socket");

  m_readBuffer = new ReadBuffer;
  m_writeBuffer = new WriteBuffer;

  m_sendState = state;
  m_sendDown = down;
  m_sendUp = up;
  m_sendLeft = left;

  prepare_connect_input();

  taskScheduler.insert(&m_taskTimeout, Timer::cache() + m_info->get_udp_timeout() * 1000000);
  m_tries = m_info->get_udp_tries();

  pollManager.read_set().insert(this);
  pollManager.write_set().insert(this);
  pollManager.except_set().insert(this);
}

void
TrackerUdp::close() {
  if (!m_fd.is_valid())
    return;

  delete m_readBuffer;
  delete m_writeBuffer;

  m_readBuffer = NULL;
  m_writeBuffer = NULL;

  taskScheduler.erase(&m_taskTimeout);

  pollManager.read_set().erase(this);
  pollManager.write_set().erase(this);
  pollManager.except_set().erase(this);

  m_fd.close();
  m_fd.clear();
}

void
TrackerUdp::receive_failed(const std::string& msg) {
  close();
  m_slotFailed(msg);
}

void
TrackerUdp::receive_timeout() {
  if (--m_tries == 0) {
    receive_failed("Unable to connect to UDP tracker.");
  } else {
    m_slotLog("Unable to connect to UDP tracker, trying again.");
    taskScheduler.insert(&m_taskTimeout, Timer::cache() + m_info->get_udp_timeout() * 1000000);
    pollManager.write_set().insert(this);
  }
}

void
TrackerUdp::read() {
  SocketAddress sa;

  int s = receive(m_readBuffer->begin(), m_readBuffer->reserved(), &sa);

//   if (s < 0)
//     m_slotLog("UDP read() got error " + std::string(std::strerror(get_errno())));
//   else if (s >= 4)
//     m_slotLog("UDP read() got message from " + sa.get_address());
//   else
//     m_slotLog("UDP read() got zero or less than 4");

  if (s < 4)
    return;

  m_readBuffer->reset_position();
  m_readBuffer->set_end(s);

  // Make sure sa is from the source we expected?

  // Do something with the content here.
  switch (m_readBuffer->read32()) {
  case 0:
    if (m_action != 0 || !process_connect_output())
      return;

//     m_slotLog("UDP read() received connect action.");

    prepare_announce_input();

    taskScheduler.erase(&m_taskTimeout);
    taskScheduler.insert(&m_taskTimeout, Timer::cache() + m_info->get_udp_timeout() * 1000000);

    m_tries = m_info->get_udp_tries();
    pollManager.write_set().insert(this);
    return;

  case 1:
    if (m_action != 1 || !process_announce_output())
      return;

//     m_slotLog("UDP read() received announce action.");
    return close();

  case 3:
    if (!process_error_output())
      return;

//     m_slotLog("UDP read() received error action.");
    return;

  default:
//     m_slotLog("UDP read() received unknown action.");
    return;
  };
}

void
TrackerUdp::write() {
  if (m_writeBuffer->size_end() == 0)
    throw internal_error("TrackerUdp::write() called but the write buffer is empty.");

  int s = send(m_writeBuffer->begin(), m_writeBuffer->size_end(), &m_connectAddress);

//   if (s != m_writeBuffer->size_end())
//     m_slotLog("UDP write failed");
//   else
//     m_slotLog("UDP write \"" + _string_to_hex(std::string((char*)m_writeBuffer->begin(), m_writeBuffer->size_end())));

  pollManager.write_set().erase(this);
}

void
TrackerUdp::except() {
  m_slotLog("TrackerUdp::except() called");
}

bool
TrackerUdp::parse_url() {
  int port;
  char hostname[256];
      
  if (std::sscanf(m_url.c_str(), "udp://%256[^:]:%i", hostname, &port) != 2 ||
      hostname[0] == '\0' ||
      port <= 0 || port >= (1 << 16))
    return false;

  m_connectAddress.set_hostname(hostname);
  m_connectAddress.set_port(port);

  return !m_connectAddress.is_port_any() && !m_connectAddress.is_address_any();
}

void
TrackerUdp::prepare_connect_input() {
  m_writeBuffer->reset_position();
  m_writeBuffer->write64(m_connectionId = magic_connection_id);
  m_writeBuffer->write32(m_action = 0);
  m_writeBuffer->write32(m_transactionId = random());

  m_writeBuffer->prepare_end();
}

void
TrackerUdp::prepare_announce_input() {
  m_writeBuffer->reset_position();

  m_writeBuffer->write64(m_connectionId);
  m_writeBuffer->write32(m_action = 1);
  m_writeBuffer->write32(m_transactionId = random());

  m_writeBuffer->write_range(m_info->get_hash().begin(), m_info->get_hash().end());
  m_writeBuffer->write_range(m_info->get_me()->get_id().begin(), m_info->get_me()->get_id().end());

  m_writeBuffer->write64(m_sendDown);
  m_writeBuffer->write64(m_sendLeft);
  m_writeBuffer->write64(m_sendUp);
  m_writeBuffer->write32(m_sendState);

  m_writeBuffer->write32(0); // Set IP address if available.
  m_writeBuffer->write32(0xfefefefe); // Set the key.
  m_writeBuffer->write32(m_info->get_numwant());
  m_writeBuffer->write_16(m_info->get_me()->get_port());

  m_writeBuffer->prepare_end();

  if (m_writeBuffer->size_end() != 98)
    throw internal_error("TrackerUdp::prepare_announce_input() ended up with the wrong size");
}

bool
TrackerUdp::process_connect_output() {
  if (m_readBuffer->size_end() < 16 ||
      m_readBuffer->read32() != m_transactionId)
    return false;

  m_connectionId = m_readBuffer->read64();

  return true;
}

bool
TrackerUdp::process_announce_output() {
  if (m_readBuffer->size_end() < 20 ||
      m_readBuffer->read32() != m_transactionId)
    return false;

  m_slotSetInterval(m_readBuffer->read32());

  uint32_t leechers = m_readBuffer->read32();
  uint32_t seeders = m_readBuffer->read32();

  PeerList plist;

  while (m_readBuffer->position() + 6 <= m_readBuffer->end()) {
    // Hmm... consider representing ip addresses as SocketAddress
    // internally. This is just... bad...
    std::stringstream buf;

    buf << (int)m_readBuffer->read8() << '.'
	<< (int)m_readBuffer->read8() << '.'
	<< (int)m_readBuffer->read8() << '.'
	<< (int)m_readBuffer->read8();

    plist.push_back(PeerInfo("", buf.str(), m_readBuffer->read_16()));
  }

  m_slotSuccess(plist);
  return true;
}
  
bool
TrackerUdp::process_error_output() {
  if (m_readBuffer->size_end() < 8 ||
      m_readBuffer->read32() != m_transactionId)
    return false;

  receive_failed("Received error message: " + std::string(m_readBuffer->position(), m_readBuffer->end()));
  return true;
}

}