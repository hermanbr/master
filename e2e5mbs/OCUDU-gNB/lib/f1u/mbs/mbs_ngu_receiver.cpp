// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/f1u/mbs/mbs_ngu_receiver.h"
#include "../../gtpu/gtpu_pdu.h"
#include "../../gtpu/gtpu_tunnel_logger.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace ocudu;

namespace {

/// Largest datagram we accept. NG-U traffic is normally <= 1500 B, but allow jumbo frames defensively.
constexpr size_t MBS_NGU_MAX_DATAGRAM_SIZE = 9216;

/// Receive timeout used so the receive thread can observe the stop request even when no traffic arrives.
constexpr int MBS_NGU_RX_TIMEOUT_MS = 200;

} // namespace

mbs_ngu_receiver::mbs_ngu_receiver(mbs_ngu_receiver_config cfg_,
                                   task_executor&          dispatch_exec_,
                                   mbs_ngu_tpdu_handler    handler_) :
  cfg(std::move(cfg_)),
  dispatch_exec(dispatch_exec_),
  handler(std::move(handler_)),
  logger(ocudulog::fetch_basic_logger("GTPU"))
{
}

mbs_ngu_receiver::~mbs_ngu_receiver()
{
  stop();
}

bool mbs_ngu_receiver::start()
{
  if (running.load(std::memory_order_acquire)) {
    return true;
  }

  in_addr group_addr{};
  if (::inet_pton(AF_INET, cfg.multicast_group.c_str(), &group_addr) != 1) {
    logger.error("MBS NG-U: Invalid multicast group address '{}'", cfg.multicast_group);
    return false;
  }
  // Only IPv4 multicast (224.0.0.0/4) is supported on this ingress.
  if ((ntohl(group_addr.s_addr) & 0xf0000000U) != 0xe0000000U) {
    logger.error("MBS NG-U: Address '{}' is not an IPv4 multicast group", cfg.multicast_group);
    return false;
  }

  int fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (fd < 0) {
    logger.error("MBS NG-U: Failed to create socket: {}", ::strerror(errno));
    return false;
  }

  // Allow multiple sockets/processes to bind the same multicast port.
  const int reuse = 1;
  if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0) {
    logger.error("MBS NG-U: Failed to set SO_REUSEADDR: {}", ::strerror(errno));
    ::close(fd);
    return false;
  }
#ifdef SO_REUSEPORT
  (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(reuse));
#endif

  // Bind to the multicast port. Binding to the group address (rather than INADDR_ANY) ensures we only receive
  // datagrams destined to this group, which avoids cross-talk with other UDP traffic on the same port.
  sockaddr_in bind_addr{};
  bind_addr.sin_family      = AF_INET;
  bind_addr.sin_port        = htons(cfg.port);
  bind_addr.sin_addr.s_addr = group_addr.s_addr;
  if (::bind(fd, reinterpret_cast<const sockaddr*>(&bind_addr), sizeof(bind_addr)) != 0) {
    logger.error("MBS NG-U: Failed to bind to {}:{}: {}", cfg.multicast_group, cfg.port, ::strerror(errno));
    ::close(fd);
    return false;
  }

  // Join the multicast group. When a specific interface address is configured, join only on it. Otherwise join on
  // every up, multicast-capable IPv4 interface: the MB-UPF multicast may arrive on a Docker bridge / secondary NIC
  // rather than the default-route interface, and an INADDR_ANY join would bind the membership to the wrong one.
  unsigned nof_joins = 0;
  const auto join_on = [&](in_addr if_addr) {
    ip_mreq mreq{};
    mreq.imr_multiaddr        = group_addr;
    mreq.imr_interface.s_addr = if_addr.s_addr;
    if (::setsockopt(fd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) == 0) {
      ++nof_joins;
      return true;
    }
    // EADDRINUSE means the membership already exists on this interface; treat as success.
    if (errno == EADDRINUSE) {
      ++nof_joins;
      return true;
    }
    return false;
  };

  if (not cfg.iface_address.empty()) {
    in_addr if_addr{};
    if (::inet_pton(AF_INET, cfg.iface_address.c_str(), &if_addr) != 1 or not join_on(if_addr)) {
      logger.error("MBS NG-U: Failed to join {} on interface {}: {}",
                   cfg.multicast_group,
                   cfg.iface_address,
                   ::strerror(errno));
      ::close(fd);
      return false;
    }
  } else {
    ifaddrs* ifaddr = nullptr;
    if (::getifaddrs(&ifaddr) == 0) {
      for (ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr or ifa->ifa_addr->sa_family != AF_INET) {
          continue;
        }
        if ((ifa->ifa_flags & IFF_UP) == 0 or (ifa->ifa_flags & IFF_MULTICAST) == 0 or
            (ifa->ifa_flags & IFF_LOOPBACK) != 0) {
          continue;
        }
        (void)join_on(reinterpret_cast<sockaddr_in*>(ifa->ifa_addr)->sin_addr);
      }
      ::freeifaddrs(ifaddr);
    }
    // Fall back to the default interface if no interface-specific join succeeded.
    if (nof_joins == 0) {
      in_addr any{};
      any.s_addr = htonl(INADDR_ANY);
      (void)join_on(any);
    }
    if (nof_joins == 0) {
      logger.error("MBS NG-U: Failed to join multicast group {} on any interface: {}",
                   cfg.multicast_group,
                   ::strerror(errno));
      ::close(fd);
      return false;
    }
  }

  // Use a receive timeout so the thread can periodically check the stop flag.
  timeval tv{};
  tv.tv_sec  = MBS_NGU_RX_TIMEOUT_MS / 1000;
  tv.tv_usec = (MBS_NGU_RX_TIMEOUT_MS % 1000) * 1000;
  (void)::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  sock_fd = fd;
  running.store(true, std::memory_order_release);
  rx_thread = unique_thread("mbs_ngu_rx", [this]() { run(); });

  logger.info("MBS NG-U: Joined multicast group {}:{} teid={} interfaces={} (PTM ingress active)",
              cfg.multicast_group,
              cfg.port,
              cfg.teid,
              nof_joins);
  return true;
}

void mbs_ngu_receiver::stop()
{
  if (not running.exchange(false, std::memory_order_acq_rel)) {
    return;
  }

  // Wake/exit the receive thread and release it before closing the socket.
  rx_thread.join();

  if (sock_fd >= 0) {
    ::close(sock_fd);
    sock_fd = -1;
  }
  logger.info("MBS NG-U: Left multicast group {}:{} (PTM ingress stopped)", cfg.multicast_group, cfg.port);
}

void mbs_ngu_receiver::run()
{
  std::array<uint8_t, MBS_NGU_MAX_DATAGRAM_SIZE> rx_buf;

  while (running.load(std::memory_order_acquire)) {
    sockaddr_in src_addr{};
    socklen_t   src_len = sizeof(src_addr);
    ssize_t     n =
        ::recvfrom(sock_fd, rx_buf.data(), rx_buf.size(), 0, reinterpret_cast<sockaddr*>(&src_addr), &src_len);
    if (n <= 0) {
      if (n < 0 and errno != EAGAIN and errno != EWOULDBLOCK and errno != EINTR) {
        logger.warning("MBS NG-U: recvfrom error: {}", ::strerror(errno));
      }
      continue;
    }

    (void)process_datagram(span<const uint8_t>(rx_buf.data(), static_cast<size_t>(n)), src_addr.sin_addr.s_addr);
  }
}

bool mbs_ngu_receiver::process_datagram(span<const uint8_t> datagram, uint32_t src_addr_be)
{
  // Optional source-address validation (defends against spoofed/foreign senders on the group).
  if (cfg.expected_source.has_value()) {
    in_addr expected{};
    if (::inet_pton(AF_INET, cfg.expected_source->c_str(), &expected) == 1 and expected.s_addr != src_addr_be) {
      logger.debug("MBS NG-U: Dropping datagram from unexpected source");
      return false;
    }
  }

  expected<byte_buffer> raw = byte_buffer::create(datagram);
  if (not raw.has_value()) {
    logger.warning("MBS NG-U: Dropping datagram. Cause: out of memory for {} bytes", datagram.size());
    return false;
  }

  // Dissect and validate the GTP-U header (version, flags, length and extension headers).
  gtpu_tunnel_logger gtpu_logger("GTPU", {std::nullopt, cfg.teid, "DL"});
  gtpu_dissected_pdu dissected;
  if (not gtpu_dissect_pdu(dissected, std::move(raw.value()), gtpu_logger)) {
    logger.warning("MBS NG-U: Dropping malformed GTP-U datagram");
    return false;
  }

  // Only user-plane G-PDUs carry MTCH media; ignore echo/error/other signalling silently.
  if (dissected.hdr.message_type != GTPU_MSG_DATA_PDU) {
    return false;
  }

  // Enforce the expected tunnel: drop traffic for any other TEID sharing the group/port.
  if (dissected.hdr.teid != cfg.teid) {
    logger.debug("MBS NG-U: Dropping G-PDU with unexpected teid={} (expected {})", dissected.hdr.teid, cfg.teid);
    return false;
  }

  byte_buffer t_pdu = gtpu_extract_msg(std::move(dissected));
  if (t_pdu.empty()) {
    return false;
  }

  const uint64_t pdu_count = nof_accepted_pdus.fetch_add(1, std::memory_order_relaxed) + 1;
  if (pdu_count == 1 or (pdu_count % 1024) == 0) {
    logger.info("MBS NG-U: Accepted G-PDU teid={} payload_bytes={} accepted_pdus={}",
                cfg.teid,
                t_pdu.length(),
                pdu_count);
  }

  // Marshal the decapsulated T-PDU onto the data-plane executor; never touch DU/MAC state from the receive thread.
  if (not dispatch_exec.execute([tpdu_handler = handler, pdu = std::move(t_pdu)]() mutable {
        tpdu_handler(std::move(pdu));
      })) {
    logger.warning("MBS NG-U: Dropping T-PDU. Cause: dispatch executor queue is full");
    return false;
  }
  return true;
}
