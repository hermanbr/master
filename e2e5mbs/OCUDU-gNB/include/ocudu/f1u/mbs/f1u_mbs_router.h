// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/expected.h"
#include "ocudu/gtpu/gtpu_teid_pool.h"
#include "ocudu/nru/nru_message.h"
#include "ocudu/ran/up_transport_layer_info.h"
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>

namespace ocudu {

/// Identifies one Release 17 multicast F1-U distribution context for one MRB/MTCH bearer.
struct f1u_mbs_context_id {
  std::string multicast_f1u_context_ref;
  uint16_t    mrb_id = 0;

  bool operator<(const f1u_mbs_context_id& other) const
  {
    if (multicast_f1u_context_ref != other.multicast_f1u_context_ref) {
      return multicast_f1u_context_ref < other.multicast_f1u_context_ref;
    }
    return mrb_id < other.mrb_id;
  }

  bool operator==(const f1u_mbs_context_id& other) const
  {
    return multicast_f1u_context_ref == other.multicast_f1u_context_ref and mrb_id == other.mrb_id;
  }
};

/// Local and peer tunnel information for one MBS F1-U MRB binding.
struct f1u_mbs_tunnel_info {
  up_transport_layer_info local;
  up_transport_layer_info peer;
};

enum class f1u_mbs_bind_result { success, duplicate_context, invalid_context, teid_allocation_failed };

/// Callback used by F1-U to deliver one multicast NR-U DL PDU to the DU/F1AP consumer.
using f1u_mbs_dl_pdu_handler = std::function<void(nru_dl_message)>;

/// Registry and router for multicast F1-U MRB distribution contexts.
class f1u_mbs_router
{
public:
  explicit f1u_mbs_router(gtpu_teid_pool& teid_pool_) : teid_pool(teid_pool_) {}
  ~f1u_mbs_router();

  f1u_mbs_router(const f1u_mbs_router&)            = delete;
  f1u_mbs_router& operator=(const f1u_mbs_router&) = delete;

  f1u_mbs_bind_result bind_context(const f1u_mbs_context_id&       id,
                                   const std::string&              local_addr,
                                   const up_transport_layer_info&  peer_tunnel,
                                   f1u_mbs_dl_pdu_handler          handler);

  bool unbind_context(const f1u_mbs_context_id& id);

  bool route_dl_pdu(const f1u_mbs_context_id& id, nru_dl_message msg) const;

  bool route_dl_pdu(gtpu_teid_t local_teid, nru_dl_message msg) const;

  std::optional<f1u_mbs_context_id> find_context(gtpu_teid_t local_teid) const;

  std::optional<f1u_mbs_tunnel_info> get_tunnel_info(const f1u_mbs_context_id& id) const;

  size_t nof_contexts() const { return contexts.size(); }

private:
  struct context {
    f1u_mbs_tunnel_info   tunnel;
    f1u_mbs_dl_pdu_handler handler;
  };

  bool valid_context_id(const f1u_mbs_context_id& id) const;

  gtpu_teid_pool&                   teid_pool;
  std::map<f1u_mbs_context_id, context> contexts;
  std::unordered_map<gtpu_teid_t, f1u_mbs_context_id, gtpu_teid_hasher_t> local_teid_to_context;
};

} // namespace ocudu
