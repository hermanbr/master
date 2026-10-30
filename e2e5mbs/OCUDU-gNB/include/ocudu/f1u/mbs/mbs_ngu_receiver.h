// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/byte_buffer.h"
#include "ocudu/ran/gtpu/gtpu_teid.h"
#include "ocudu/support/executors/task_executor.h"
#include "ocudu/support/executors/unique_thread.h"
#include "ocudu/ocudulog/ocudulog.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace ocudu {

/// Callback used to deliver one decapsulated MBS T-PDU (the inner user-plane packet) to the consumer.
using mbs_ngu_tpdu_handler = std::function<void(byte_buffer)>;

/// \brief Configuration for one Release 17 N3mb (NG-U shared) multicast ingress.
struct mbs_ngu_receiver_config {
  /// IPv4 multicast group the MB-UPF transmits the shared NG-U tunnel to (e.g. "239.0.0.4").
  std::string multicast_group;
  /// Local UDP port the MB-UPF transmits to (NG-U / GTP-U port, normally 2152).
  uint16_t port = 2152;
  /// GTP-U TEID expected on the shared NG-U tunnel. PDUs with a different TEID are dropped.
  gtpu_teid_t teid = {};
  /// Optional local interface address used to select the interface that joins the multicast group. When empty, the
  /// kernel selects the default interface.
  std::string iface_address;
  /// Optional expected source address (MB-UPF). When set, datagrams from any other source are dropped.
  std::optional<std::string> expected_source;
};

/// \brief Receives the Release 17 N3mb shared multicast GTP-U stream from the MB-UPF, validates and decapsulates it,
/// and forwards the inner T-PDUs towards the MTCH for point-to-multipoint (PTM) transmission.
///
/// The receiver owns a dedicated UDP socket that joins the configured IPv4 multicast group and a dedicated thread
/// that drains it. Each received GTP-U datagram is validated (header, message type, TEID and optional source
/// address) before its T-PDU is dispatched onto the provided executor, which marshals it into the DU/MAC MBS data
/// path. Running the network receive on its own thread keeps the real-time PHY/MAC executors free of socket I/O.
class mbs_ngu_receiver
{
public:
  mbs_ngu_receiver(mbs_ngu_receiver_config cfg_, task_executor& dispatch_exec_, mbs_ngu_tpdu_handler handler_);
  ~mbs_ngu_receiver();

  mbs_ngu_receiver(const mbs_ngu_receiver&)            = delete;
  mbs_ngu_receiver& operator=(const mbs_ngu_receiver&) = delete;

  /// \brief Create and bind the socket, join the multicast group and start the receive thread.
  /// \return True on success, false if the socket could not be set up.
  bool start();

  /// \brief Stop the receive thread, leave the multicast group and close the socket. Idempotent.
  void stop();

  bool is_running() const { return running.load(std::memory_order_acquire); }

private:
  void run();
  bool process_datagram(span<const uint8_t> datagram, uint32_t src_addr_be);

  const mbs_ngu_receiver_config cfg;
  task_executor&                dispatch_exec;
  const mbs_ngu_tpdu_handler    handler;
  ocudulog::basic_logger&       logger;

  int               sock_fd = -1;
  std::atomic<bool> running{false};
  std::atomic<uint64_t> nof_accepted_pdus{0};
  unique_thread     rx_thread;
};

} // namespace ocudu
