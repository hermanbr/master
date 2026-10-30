// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/byte_buffer.h"
#include "ocudu/pdcp/pdcp_config.h"
#include "ocudu/ran/rnti.h"
#include "ocudu/rlc/rlc_config.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ocudu {

struct mac_mbs_mrb_config {
  uint16_t       mrb_id                    = 0;
  pdcp_sn_size   pdcp_sn_size_dl           = pdcp_sn_size::size12bits;
  pdcp_t_reordering pdcp_t_reordering_dl   = pdcp_t_reordering::ms40;
  rlc_um_sn_size rlc_sn_size               = rlc_um_sn_size::size6bits;
  int32_t        rlc_t_reassembly_ms       = 40;
};

/// DU-wide MBS radio context handed to MAC after F1AP multicast context setup.
struct mac_mbs_context_setup_request {
  std::string session_key;
  uint64_t    gnb_cu_mbs_f1ap_id = 0;
  uint64_t    gnb_du_mbs_f1ap_id = 0;
  rnti_t      mcch_rnti          = rnti_t::SI_RNTI;
  rnti_t      mtch_rnti          = rnti_t::INVALID_RNTI;
  unsigned    mcch_repetition_period_rf   = 16;
  unsigned    mcch_repetition_offset_rf   = 0;
  unsigned    mcch_window_start_slot      = 2;
  unsigned    mcch_window_duration_slots  = 2;
  unsigned    mcch_payload_size_bytes     = 128;
  std::vector<uint16_t>           mrb_ids;
  std::vector<mac_mbs_mrb_config> mrb_configs;
};

struct mac_mbs_context_release_request {
  std::string session_key;
  uint64_t    gnb_cu_mbs_f1ap_id = 0;
  uint64_t    gnb_du_mbs_f1ap_id = 0;
};

/// MBS user-plane PDU for an MTCH MRB.
struct mac_mbs_data_indication {
  std::string session_key;
  uint16_t    mrb_id = 0;
  byte_buffer pdu;
};

struct mac_mbs_buffer_state_indication {
  std::string session_key;
  uint16_t    mrb_id        = 0;
  unsigned    pending_bytes = 0;
};

/// Interface used by DU manager to provision MBS control/data radio contexts in MAC.
class mac_mbs_information_handler
{
public:
  virtual ~mac_mbs_information_handler() = default;

  /// Handle setup/update of an MCCH/MTCH MBS radio context.
  virtual void handle_mbs_context_setup(const mac_mbs_context_setup_request& request) = 0;

  /// Handle release of an MCCH/MTCH MBS radio context.
  virtual void handle_mbs_context_release(const mac_mbs_context_release_request& request) = 0;

  /// Handle one multicast MTCH bearer payload.
  virtual void handle_mbs_data_pdu(mac_mbs_data_indication msg) = 0;
};

} // namespace ocudu
