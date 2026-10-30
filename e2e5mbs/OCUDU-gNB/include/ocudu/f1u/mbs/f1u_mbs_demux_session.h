// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/f1u/mbs/f1u_mbs_router.h"
#include "ocudu/gtpu/gtpu_demux.h"
#include "ocudu/gtpu/gtpu_tunnel_common_rx.h"
#include "ocudu/gtpu/gtpu_tunnel_nru_rx.h"
#include "ocudu/support/executors/task_executor.h"
#include <memory>

namespace ocudu {

enum class f1u_mbs_demux_bind_result { success, router_rejected_context, demux_registration_failed };

/// Binds one multicast F1-U MRB context to the shared GTP-U demux.
///
/// The demux dispatches live UDP/GTP-U packets by local TEID. This session owns the corresponding NR-U RX tunnel and
/// forwards decoded NR-U DL messages to the MBS router, which then delivers them to the DU/RLC consumer.
class f1u_mbs_demux_session final : private gtpu_tunnel_nru_rx_lower_layer_notifier
{
public:
  f1u_mbs_demux_session(f1u_mbs_router& router_,
                        gtpu_demux_ctrl& demux_,
                        task_executor&   tunnel_exec_,
                        f1u_mbs_context_id id_);
  ~f1u_mbs_demux_session() override;

  f1u_mbs_demux_session(const f1u_mbs_demux_session&)            = delete;
  f1u_mbs_demux_session& operator=(const f1u_mbs_demux_session&) = delete;

  f1u_mbs_demux_bind_result bind(const std::string&             local_addr,
                                 const up_transport_layer_info& peer_tunnel,
                                 f1u_mbs_dl_pdu_handler         handler);

  void stop();

  bool is_bound() const { return bound; }

  std::optional<f1u_mbs_tunnel_info> get_tunnel_info() const;

private:
  void on_new_sdu(nru_dl_message dl_message) override;
  void on_new_sdu(nru_ul_message ul_message) override;

  f1u_mbs_router&    router;
  gtpu_demux_ctrl&   demux;
  task_executor&     tunnel_exec;
  f1u_mbs_context_id id;

  std::unique_ptr<gtpu_tunnel_common_rx_upper_layer_interface> tunnel_rx;
  std::unique_ptr<gtpu_demux_dispatch_queue>                   dispatch_queue;
  gtpu_teid_t                                                  local_teid = int_to_gtpu_teid(0);
  bool                                                         bound      = false;
};

} // namespace ocudu
