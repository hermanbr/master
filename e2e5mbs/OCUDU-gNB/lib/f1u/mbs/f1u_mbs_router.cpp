// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/f1u/mbs/f1u_mbs_router.h"

using namespace ocudu;

f1u_mbs_router::~f1u_mbs_router()
{
  for (const auto& entry : contexts) {
    (void)teid_pool.release_teid(entry.second.tunnel.local.gtp_teid);
  }
}

bool f1u_mbs_router::valid_context_id(const f1u_mbs_context_id& id) const
{
  return not id.multicast_f1u_context_ref.empty() and id.mrb_id != 0;
}

f1u_mbs_bind_result f1u_mbs_router::bind_context(const f1u_mbs_context_id&      id,
                                                 const std::string&             local_addr,
                                                 const up_transport_layer_info& peer_tunnel,
                                                 f1u_mbs_dl_pdu_handler         handler)
{
  if (not valid_context_id(id) or local_addr.empty() or not handler) {
    return f1u_mbs_bind_result::invalid_context;
  }
  if (contexts.find(id) != contexts.end()) {
    return f1u_mbs_bind_result::duplicate_context;
  }

  expected<gtpu_teid_t> local_teid = teid_pool.request_teid();
  if (not local_teid.has_value()) {
    return f1u_mbs_bind_result::teid_allocation_failed;
  }

  context ctx;
  ctx.tunnel.local = up_transport_layer_info(transport_layer_address::create_from_string(local_addr),
                                             local_teid.value());
  ctx.tunnel.peer = peer_tunnel;
  ctx.handler     = std::move(handler);
  const gtpu_teid_t allocated_teid = ctx.tunnel.local.gtp_teid;
  contexts.emplace(id, std::move(ctx));
  local_teid_to_context.emplace(allocated_teid, id);
  return f1u_mbs_bind_result::success;
}

bool f1u_mbs_router::unbind_context(const f1u_mbs_context_id& id)
{
  auto it = contexts.find(id);
  if (it == contexts.end()) {
    return false;
  }
  local_teid_to_context.erase(it->second.tunnel.local.gtp_teid);
  (void)teid_pool.release_teid(it->second.tunnel.local.gtp_teid);
  contexts.erase(it);
  return true;
}

bool f1u_mbs_router::route_dl_pdu(const f1u_mbs_context_id& id, nru_dl_message msg) const
{
  auto it = contexts.find(id);
  if (it == contexts.end() or msg.t_pdu.empty()) {
    return false;
  }
  it->second.handler(std::move(msg));
  return true;
}

bool f1u_mbs_router::route_dl_pdu(gtpu_teid_t local_teid, nru_dl_message msg) const
{
  std::optional<f1u_mbs_context_id> id = find_context(local_teid);
  if (not id.has_value()) {
    return false;
  }
  return route_dl_pdu(id.value(), std::move(msg));
}

std::optional<f1u_mbs_context_id> f1u_mbs_router::find_context(gtpu_teid_t local_teid) const
{
  auto it = local_teid_to_context.find(local_teid);
  if (it == local_teid_to_context.end()) {
    return std::nullopt;
  }
  return it->second;
}

std::optional<f1u_mbs_tunnel_info> f1u_mbs_router::get_tunnel_info(const f1u_mbs_context_id& id) const
{
  auto it = contexts.find(id);
  if (it == contexts.end()) {
    return std::nullopt;
  }
  return it->second.tunnel;
}
