// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/f1u/mbs/f1u_mbs_demux_session.h"
#include "ocudu/gtpu/gtpu_config.h"
#include "ocudu/gtpu/gtpu_tunnel_nru_factory.h"

using namespace ocudu;

f1u_mbs_demux_session::f1u_mbs_demux_session(f1u_mbs_router&    router_,
                                             gtpu_demux_ctrl&   demux_,
                                             task_executor&     tunnel_exec_,
                                             f1u_mbs_context_id id_) :
  router(router_), demux(demux_), tunnel_exec(tunnel_exec_), id(std::move(id_))
{
}

f1u_mbs_demux_session::~f1u_mbs_demux_session()
{
  stop();
}

f1u_mbs_demux_bind_result f1u_mbs_demux_session::bind(const std::string&             local_addr,
                                                      const up_transport_layer_info& peer_tunnel,
                                                      f1u_mbs_dl_pdu_handler         handler)
{
  if (bound) {
    return f1u_mbs_demux_bind_result::router_rejected_context;
  }

  if (router.bind_context(id, local_addr, peer_tunnel, std::move(handler)) != f1u_mbs_bind_result::success) {
    return f1u_mbs_demux_bind_result::router_rejected_context;
  }

  std::optional<f1u_mbs_tunnel_info> tunnel_info = router.get_tunnel_info(id);
  if (not tunnel_info.has_value()) {
    (void)router.unbind_context(id);
    return f1u_mbs_demux_bind_result::router_rejected_context;
  }

  local_teid = tunnel_info->local.gtp_teid;

  gtpu_tunnel_nru_rx_creation_message msg{};
  msg.ue_index          = 0;
  msg.rx_cfg.node       = nru_node::du;
  msg.rx_cfg.local_teid = local_teid;
  msg.rx_lower          = this;

  tunnel_rx = create_gtpu_tunnel_nru_rx(msg);

  expected<std::unique_ptr<gtpu_demux_dispatch_queue>> queue =
      demux.add_tunnel(local_teid, tunnel_exec, tunnel_rx.get());
  if (not queue.has_value()) {
    tunnel_rx.reset();
    (void)router.unbind_context(id);
    local_teid = int_to_gtpu_teid(0);
    return f1u_mbs_demux_bind_result::demux_registration_failed;
  }

  dispatch_queue = std::move(queue.value());
  bound          = true;
  return f1u_mbs_demux_bind_result::success;
}

void f1u_mbs_demux_session::stop()
{
  if (not bound) {
    return;
  }

  (void)demux.remove_tunnel(local_teid);
  if (dispatch_queue != nullptr) {
    dispatch_queue->stop();
  }
  dispatch_queue.reset();
  tunnel_rx.reset();
  (void)router.unbind_context(id);
  local_teid = int_to_gtpu_teid(0);
  bound      = false;
}

std::optional<f1u_mbs_tunnel_info> f1u_mbs_demux_session::get_tunnel_info() const
{
  return router.get_tunnel_info(id);
}

void f1u_mbs_demux_session::on_new_sdu(nru_dl_message dl_message)
{
  (void)router.route_dl_pdu(local_teid, std::move(dl_message));
}

void f1u_mbs_demux_session::on_new_sdu(nru_ul_message ul_message)
{
  // MBS F1-U is downlink-only for multicast traffic delivery.
}
