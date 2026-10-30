// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ran/up_transport_layer_info.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace ocudu {

/// MBS Session ID as defined for E1AP GlobalMBSSessionID.
struct e1ap_mbs_session_id {
  std::string                tmgi;
  std::optional<std::string> nid;
};

struct e1ap_mbs_mrb_item {
  uint16_t             mrb_id = 1;
  std::vector<uint8_t> qos_flow_ids;
};

struct e1ap_mbs_bearer_context_setup_request {
  uint32_t                         cu_cp_mbs_e1ap_id = 0;
  e1ap_mbs_session_id              session_id;
  std::optional<uint32_t>          area_session_id;
  std::vector<e1ap_mbs_mrb_item>   mrb_to_setup_list;
  std::optional<up_transport_layer_info> ngu_tnl_info;
};

struct e1ap_mbs_bearer_context_setup_response {
  bool                           success             = false;
  uint32_t                       cu_cp_mbs_e1ap_id   = 0;
  uint32_t                       cu_up_mbs_e1ap_id   = 0;
  std::vector<e1ap_mbs_mrb_item> mrb_setup_resp_list;
};

struct e1ap_mbs_bearer_context_modification_request {
  uint32_t                       cu_cp_mbs_e1ap_id = 0;
  uint32_t                       cu_up_mbs_e1ap_id = 0;
  std::vector<e1ap_mbs_mrb_item> mrb_to_setup_modify_list;
  std::vector<uint16_t>          mrb_to_remove_list;
  std::optional<up_transport_layer_info> ngu_tnl_info;
};

struct e1ap_mbs_bearer_context_modification_response {
  bool                           success           = false;
  uint32_t                       cu_cp_mbs_e1ap_id = 0;
  uint32_t                       cu_up_mbs_e1ap_id = 0;
  std::vector<e1ap_mbs_mrb_item> mrb_setup_modify_resp_list;
};

struct e1ap_mbs_bearer_context_release_command {
  uint32_t cu_cp_mbs_e1ap_id = 0;
  uint32_t cu_up_mbs_e1ap_id = 0;
};

struct e1ap_mbs_bearer_context_release_complete {
  uint32_t cu_cp_mbs_e1ap_id = 0;
  uint32_t cu_up_mbs_e1ap_id = 0;
};

struct e1ap_mbs_bearer_context {
  uint32_t                       cu_cp_mbs_e1ap_id = 0;
  uint32_t                       cu_up_mbs_e1ap_id = 0;
  e1ap_mbs_session_id            session_id;
  std::optional<uint32_t>        area_session_id;
  std::vector<e1ap_mbs_mrb_item> mrb_list;
  std::optional<up_transport_layer_info> ngu_tnl_info;
};

} // namespace ocudu
