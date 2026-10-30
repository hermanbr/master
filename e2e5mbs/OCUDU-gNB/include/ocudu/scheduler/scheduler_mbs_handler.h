// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/ran/rnti.h"
#include <cstdint>
#include <string>
#include <vector>

namespace ocudu {

struct sched_mbs_context_setup_request {
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
  std::vector<uint16_t> mrb_ids;
};

struct sched_mbs_context_release_request {
  std::string session_key;
  uint64_t    gnb_cu_mbs_f1ap_id = 0;
  uint64_t    gnb_du_mbs_f1ap_id = 0;
};

struct sched_mbs_buffer_state_indication {
  std::string session_key;
  uint16_t    mrb_id        = 0;
  unsigned    pending_bytes = 0;
};

/// Scheduler interface for DU-wide MCCH/MTCH multicast radio contexts.
class scheduler_mbs_handler
{
public:
  virtual ~scheduler_mbs_handler() = default;

  /// Setup or update an MCCH/MTCH context for subsequent radio scheduling.
  virtual void handle_mbs_context_setup(const sched_mbs_context_setup_request& request) = 0;

  /// Release an MCCH/MTCH context.
  virtual void handle_mbs_context_release(const sched_mbs_context_release_request& request) = 0;

  /// Update queued MTCH bytes for a multicast MRB.
  virtual void handle_mbs_buffer_state_update(const sched_mbs_buffer_state_indication& bs) = 0;
};

} // namespace ocudu
