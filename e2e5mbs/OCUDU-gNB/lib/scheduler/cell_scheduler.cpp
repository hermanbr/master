// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "cell_scheduler.h"
#include "logging/scheduler_metrics_handler.h"
#include "support/dci_builder.h"
#include "support/dmrs_helpers.h"
#include "support/pdcch/search_space_helper.h"
#include "support/pdsch/pdsch_default_time_allocation.h"
#include "support/pdsch/pdsch_resource_allocation.h"
#include "support/prbs_calculator.h"
#include "support/rb_helper.h"
#include "support/sch_pdu_builder.h"
#include <array>

using namespace ocudu;

static constexpr unsigned mbs_mtch_payload_size_bytes = 1024;
static constexpr unsigned mbs_mtch_min_grant_bytes    = 64;
static constexpr unsigned mbs_mtch_period_slots       = 1;
static constexpr unsigned mbs_time_resource_idx       = 0;

// Capacity/robustness knob for the MTCH PDSCH (change + rebuild the gNB).
// Index into the qam64 MCS table (TS 38.214 Table 5.1.3.1-1). Higher index =
// more bits/slot, but needs higher SINR. PTM is FEEDBACK-LESS (no HARQ/ReTx),
// so the index must be decodable by the *worst* interested receiver.
//   4  : QPSK R~0.30  -- current, most robust (~1.8 Mb/s on 24 PRB)  [default]
//   6  : QPSK R~0.44  -- ~2.6 Mb/s, still QPSK
//   9  : QPSK R~0.66  -- ~4 Mb/s, highest-rate QPSK (needs ~6-8 dB SINR)
//   10+: 16QAM        -- ~5-6 Mb/s but needs ~12-15 dB SINR at ALL UEs
//   17+: 64QAM        -- needs ~18+ dB; only on a strong, stable link
// Kept at 4 because the current RF link is marginal -- raise ONLY after SINR is
// solid, else broadcast loss/freezing gets worse. Capacity also scales with the
// number of MTCH slots/frame (|S_DL|); see mbs_mtch_skip_slot_indices below
// (emptying that set reclaims ~15% of the slots at the cost of CSI-RS overlap).
// NOTE: PRBs are still bounded to CORESET#0 (~24) by DCI format 1_0 in the
// common search space; reaching 5 Mb/s on a robust QPSK config requires the
// full-BWP/wider-DCI work documented in docs/infocom.md, not this knob alone.
static constexpr unsigned mbs_mtch_mcs_index = 4;

// Experimental loss-isolation knob: slot indices (within the radio frame; with
// numerology 1 these run 0..19) that carry CSI-RS. MTCH (MBS data) PDSCH is not
// scheduled in these slots so it cannot collide with CSI-RS, which was suspected
// of causing decode loss at the nrUE. MCCH scheduling is unaffected. To restore
// MTCH in every slot, make is_mbs_mtch_skip_slot() always return false (or empty
// the list below).
static constexpr std::array<unsigned, 3> mbs_mtch_skip_slot_indices = {2, 3, 4};

static bool is_mbs_mtch_skip_slot(unsigned slot_in_frame)
{
  for (unsigned skip_slot : mbs_mtch_skip_slot_indices) {
    if (skip_slot == slot_in_frame) {
      return true;
    }
  }
  return false;
}

cell_scheduler::cell_scheduler(const scheduler_expert_config&                  sched_cfg,
                               const sched_cell_configuration_request_message& msg,
                               const cell_configuration&                       cell_cfg_,
                               ue_scheduler&                                   ue_sched_,
                               cell_metrics_handler&                           metrics_handler) :
  cell_cfg(cell_cfg_),
  res_grid(cell_cfg),
  event_logger(cell_cfg.cell_index, cell_cfg.params.pci),
  metrics(metrics_handler),
  result_logger(sched_cfg.log_broadcast_messages, cell_cfg.params.pci),
  logger(ocudulog::fetch_basic_logger("SCHED")),
  ssb_sch(cell_cfg),
  pdcch_sch(cell_cfg),
  si_sch(cell_cfg, pdcch_sch, msg),
  csi_sch(cell_cfg),
  ra_sch(cell_cfg, pdcch_sch, event_logger, metrics),
  prach_sch(cell_cfg),
  pucch_alloc(cell_cfg, sched_cfg.ue.max_pucchs_per_slot, sched_cfg.ue.max_ul_grants_per_slot),
  uci_alloc(cell_cfg, pucch_alloc),
  // The SRS allocator is only used if srs_prohibit_time is set.
  srs_alloc(cell_cfg, sched_cfg.ue.srs_prohibit_time),
  pg_sch(cell_cfg, pdcch_sch)
{
  // Register new cell in the UE scheduler.
  ue_sched = ue_sched_.add_cell(ue_cell_scheduler_creation_request{
      msg.cell_index, &pdcch_sch, &pucch_alloc, &uci_alloc, &srs_alloc, &res_grid, &metrics, &event_logger});
}

void cell_scheduler::handle_si_update_request(const si_scheduling_update_request& msg)
{
  si_sch.handle_si_update_request(msg);
}

void cell_scheduler::handle_slice_reconfiguration_request(const du_cell_slice_reconfig_request& slice_reconf_req)
{
  ue_sched->handle_slice_reconfiguration_request(slice_reconf_req);
}

void cell_scheduler::handle_mbs_context_setup(const sched_mbs_context_setup_request& request)
{
  auto& ctx = mbs_contexts[request.session_key];
  ctx.setup = request;
  for (uint16_t mrb_id : request.mrb_ids) {
    ctx.mrb_pending_bytes.try_emplace(mrb_id, 0U);
  }
}

void cell_scheduler::handle_mbs_context_release(const sched_mbs_context_release_request& request)
{
  mbs_contexts.erase(request.session_key);
}

void cell_scheduler::handle_mbs_buffer_state_update(const sched_mbs_buffer_state_indication& bs)
{
  auto ctx = mbs_contexts.find(bs.session_key);
  if (ctx == mbs_contexts.end()) {
    return;
  }
  ctx->second.mrb_pending_bytes[bs.mrb_id] = bs.pending_bytes;
}

void cell_scheduler::handle_crc_indication(const ul_crc_indication& crc_ind)
{
  bool has_msg3_crcs = std::any_of(
      crc_ind.crcs.begin(), crc_ind.crcs.end(), [](const auto& pdu) { return pdu.ue_index == INVALID_DU_UE_INDEX; });

  if (has_msg3_crcs) {
    ul_crc_indication msg3_crcs{};
    ul_crc_indication ue_crcs{};
    msg3_crcs.sl_rx      = crc_ind.sl_rx;
    msg3_crcs.cell_index = crc_ind.cell_index;
    ue_crcs.sl_rx        = crc_ind.sl_rx;
    ue_crcs.cell_index   = crc_ind.cell_index;
    for (const ul_crc_pdu_indication& crc_pdu : crc_ind.crcs) {
      if (crc_pdu.ue_index == INVALID_DU_UE_INDEX) {
        msg3_crcs.crcs.push_back(crc_pdu);
      } else {
        ue_crcs.crcs.push_back(crc_pdu);
      }
    }
    // Forward CRC to Msg3 HARQs that has no ueId yet associated.
    ra_sch.handle_crc_indication(msg3_crcs);
    // Forward remaining CRCs to UE scheduler.
    ue_sched->get_feedback_handler().handle_crc_indication(ue_crcs);
  } else {
    ue_sched->get_feedback_handler().handle_crc_indication(crc_ind);
  }
}

void cell_scheduler::run_slot(slot_point_extended sl_tx_ext)
{
  // Mark the start of the slot.
  slot_point sl_tx         = sl_tx_ext.without_hyper_sfn();
  auto       slot_start_tp = std::chrono::high_resolution_clock::now();

  // If there are skipped slots, handle them. Otherwise, the cell grid and cached results are not correctly cleared.
  if (OCUDU_LIKELY(res_grid.slot_tx().valid())) {
    while (OCUDU_UNLIKELY(res_grid.slot_tx() + 1 != sl_tx)) {
      slot_point skipped_slot = res_grid.slot_tx() + 1;
      logger.info("cell={}: Detected skipped slot={}.", cell_cfg.cell_index, skipped_slot);
      reset_resource_grid(skipped_slot);
    }
  } else {
    if (OCUDU_UNLIKELY(not active)) {
      // Implicitly activate cell on slot_indication.
      start();
    }
  }

  // > Start with clearing old allocations from the grid.
  reset_resource_grid(sl_tx);

  // > SSB scheduling.
  ssb_sch.run_slot(res_grid, sl_tx);

  // > Schedule CSI-RS.
  csi_sch.run_slot(res_grid[0]);

  // > Schedule SIB1 and SI-message signalling.
  si_sch.run_slot(res_grid);

  // > Schedule PRACH PDUs.
  prach_sch.run_slot(res_grid);

  // > Schedule RARs and Msg3.
  ra_sch.run_slot(res_grid);

  // > Schedule Paging.
  pg_sch.run_slot(res_grid, sl_tx_ext.hyper_sfn());

  // > Schedule MBS MCCH/MTCH.
  schedule_mbs(res_grid, sl_tx);

  // > Schedule UE DL and UL data.
  ue_sched->run_slot(sl_tx);

  // > Mark stop of the slot processing
  auto slot_stop_tp = std::chrono::high_resolution_clock::now();
  auto slot_dur     = std::chrono::duration_cast<std::chrono::microseconds>(slot_stop_tp - slot_start_tp);

  // > Log processed events.
  event_logger.log();

  // > Log the scheduler results.
  result_logger.on_scheduler_result(last_result(), slot_dur);

  // > Push the scheduler results to the metrics handler.
  metrics.push_result(sl_tx_ext, last_result(), slot_dur);
}

static const search_space_configuration* find_mbs_search_space(const cell_configuration& cell_cfg)
{
  const auto& pdcch_common = cell_cfg.params.dl_cfg_common.init_dl_bwp.pdcch_common;
  if (pdcch_common.paging_search_space_id.has_value()) {
    for (const auto& ss : pdcch_common.search_spaces) {
      if (ss.get_id() == *pdcch_common.paging_search_space_id) {
        return &ss;
      }
    }
  }
  for (const auto& ss : pdcch_common.search_spaces) {
    if (ss.is_common_search_space()) {
      return &ss;
    }
  }
  return nullptr;
}

static void fill_mbs_pdsch_from_paging_pdsch(pdsch_information& pdsch, rnti_t rnti)
{
  pdsch.rnti = rnti;
  if (not pdsch.codewords.empty()) {
    pdsch.codewords[0].new_data = true;
  }
}

static bool is_slot_in_mcch_window(const sched_mbs_context_setup_request& setup,
                                   slot_point                             sl_tx,
                                   unsigned                               slots_per_frame,
                                   uint64_t&                              period_idx)
{
  const unsigned period_slots = setup.mcch_repetition_period_rf * slots_per_frame;
  if (period_slots == 0) {
    return false;
  }

  const unsigned offset_slots = setup.mcch_repetition_offset_rf * slots_per_frame + setup.mcch_window_start_slot;
  const unsigned slot_in_period = sl_tx.count() % period_slots;
  period_idx                    = sl_tx.count() / period_slots;

  return slot_in_period >= offset_slots and slot_in_period < offset_slots + setup.mcch_window_duration_slots;
}

void cell_scheduler::schedule_mbs(cell_resource_allocator& grid, slot_point sl_tx)
{
  if (mbs_contexts.empty() or grid[0].result.dl.mbs_grants.full() or grid[0].result.dl.dl_pdcchs.full() or
      not cell_cfg.is_dl_enabled(sl_tx)) {
    return;
  }

  // The common MBS PDSCH is allocated with k0=0, so sl_tx is also the PDSCH
  // slot; skipping these slot indices keeps MTCH PDSCH off the CSI-RS slots.
  const bool schedule_mtch =
      ((sl_tx.count() % mbs_mtch_period_slots) == 0) and not is_mbs_mtch_skip_slot(sl_tx.slot_index());
  bool has_mcch_to_schedule = false;
  for (auto& context_entry : mbs_contexts) {
    uint64_t period_idx = 0;
    if (is_slot_in_mcch_window(context_entry.second.setup, sl_tx, cell_cfg.nof_slots_per_frame, period_idx) and
        context_entry.second.last_mcch_period_scheduled != period_idx) {
      has_mcch_to_schedule = true;
      break;
    }
  }

  if (not has_mcch_to_schedule and not schedule_mtch) {
    return;
  }

  const auto* ss_cfg = find_mbs_search_space(cell_cfg);
  if (ss_cfg == nullptr) {
    logger.warning("cell={}: Skipping MBS scheduling. Cause: common search space not configured", cell_cfg.cell_index);
    return;
  }
  if (not pdcch_helper::is_pdcch_monitoring_active(sl_tx, *ss_cfg)) {
    return;
  }

  const auto& pdsch_td_alloc_list = cell_cfg.params.dl_cfg_common.init_dl_bwp.pdsch_common.pdsch_td_alloc_list.empty()
                                        ? pdsch_default_time_allocations_default_A_table(
                                              cell_cfg.params.dl_cfg_common.init_dl_bwp.generic_params.cp,
                                              cell_cfg.params.dmrs_typeA_pos)
                                        : cell_cfg.params.dl_cfg_common.init_dl_bwp.pdsch_common.pdsch_td_alloc_list;
  if (pdsch_td_alloc_list.empty() or mbs_time_resource_idx >= pdsch_td_alloc_list.size()) {
    return;
  }

  const auto& pdsch_td_cfg = pdsch_td_alloc_list[mbs_time_resource_idx];
  if (not cell_cfg.is_dl_enabled(sl_tx + pdsch_td_cfg.k0)) {
    return;
  }

  const dmrs_information dmrs_info =
      make_dmrs_info_common(pdsch_td_alloc_list, mbs_time_resource_idx, cell_cfg.params.pci, cell_cfg.params.dmrs_typeA_pos);
  const unsigned            nof_symb_sh      = pdsch_td_cfg.symbols.length();
  const unsigned            nof_dmrs_per_rb  = calculate_nof_dmrs_per_rb(dmrs_info);
  const sch_mcs_description mcs_descr        = pdsch_mcs_get_config(pdsch_mcs_table::qam64, sch_mcs_index{mbs_mtch_mcs_index});
  cell_slot_resource_allocator& pdsch_alloc  = grid[sl_tx + pdsch_td_cfg.k0];
  const auto& init_dl_bwp = cell_cfg.params.dl_cfg_common.init_dl_bwp;
  const crb_interval mbs_crb_limits =
      init_dl_bwp.pdcch_common.coreset0.has_value()
          ? init_dl_bwp.pdcch_common.coreset0->coreset0_crbs()
          : pdsch_helper::get_ra_crb_limits_common(init_dl_bwp, ss_cfg->get_id());

  auto schedule_one_mbs_grant = [&](const mbs_sched_context& ctx,
                                    bool                     is_mcch,
                                    uint16_t                 mtch_mrb_id,
                                    unsigned                 mtch_pending) {
    if (grid[0].result.dl.dl_pdcchs.full() or pdsch_alloc.result.dl.mbs_grants.full()) {
      return false;
    }

    const sched_mbs_context_setup_request& setup = ctx.setup;
    const rnti_t                           rnti  = is_mcch ? setup.mcch_rnti : setup.mtch_rnti;
    if (rnti == rnti_t::INVALID_RNTI) {
      return false;
    }

    unsigned payload_size =
        is_mcch ? setup.mcch_payload_size_bytes : std::min(mbs_mtch_payload_size_bytes, mtch_pending);
    if (payload_size == 0) {
      return false;
    }
    const unsigned grant_target_size = is_mcch ? payload_size : std::max(payload_size, mbs_mtch_min_grant_bytes);

    const crb_bitmap& used_crbs =
        pdsch_alloc.dl_res_grid.used_crbs(cell_cfg.params.dl_cfg_common.init_dl_bwp.generic_params, pdsch_td_cfg.symbols);
    const crb_interval available_crbs =
        rb_helper::find_empty_interval_of_length(used_crbs, MAX_NOF_PRBS, mbs_crb_limits);
    if (available_crbs.empty()) {
      return false;
    }

    const sch_prbs_tbs mbs_prbs_tbs =
        get_nof_prbs(prbs_calculator_sch_config{grant_target_size, nof_symb_sh, nof_dmrs_per_rb, 0, mcs_descr, 1, 0},
                     available_crbs.length());
    const unsigned min_acceptable_tbs = is_mcch ? payload_size : std::min(grant_target_size, mbs_mtch_min_grant_bytes);
    if (mbs_prbs_tbs.tbs_bytes.value() < min_acceptable_tbs) {
      return false;
    }
    if (mbs_prbs_tbs.tbs_bytes.value() < payload_size) {
      if (is_mcch) {
        return false;
      }
      payload_size = mbs_prbs_tbs.tbs_bytes.value();
    }
    if (payload_size == 0) {
      return false;
    }

    const crb_interval crbs{available_crbs.start(), available_crbs.start() + mbs_prbs_tbs.nof_prbs};

    pdcch_dl_information* pdcch =
        pdcch_sch.alloc_dl_pdcch_common(grid[sl_tx], rnti, ss_cfg->get_id(), aggregation_level::n4);
    if (pdcch == nullptr) {
      return false;
    }

    build_dci_f1_0_p_rnti(
        pdcch->dci, cell_cfg.params.dl_cfg_common.init_dl_bwp, crbs, mbs_time_resource_idx, sch_mcs_index{mbs_mtch_mcs_index});

    pdsch_alloc.dl_res_grid.fill(
        grant_info{cell_cfg.params.dl_cfg_common.init_dl_bwp.generic_params.scs, pdsch_td_cfg.symbols, crbs});

    dl_mbs_allocation& grant = pdsch_alloc.result.dl.mbs_grants.emplace_back();
    grant.channel            = is_mcch ? dl_mbs_allocation::channel_type::mcch : dl_mbs_allocation::channel_type::mtch;
    grant.session_key        = setup.session_key;
    grant.mrb_id             = is_mcch ? (setup.mrb_ids.empty() ? 0 : setup.mrb_ids.front()) : mtch_mrb_id;
    grant.payload_size       = payload_size;
    build_pdsch_f1_0_p_rnti(
        grant.pdsch_cfg, cell_cfg, mbs_prbs_tbs.tbs_bytes, pdcch->dci.p_rnti_f1_0, crbs, pdsch_td_cfg.symbols, dmrs_info);
    fill_mbs_pdsch_from_paging_pdsch(grant.pdsch_cfg, rnti);
    return true;
  };

  if (has_mcch_to_schedule) {
    for (auto& context_entry : mbs_contexts) {
      uint64_t period_idx = 0;
      if (not is_slot_in_mcch_window(context_entry.second.setup, sl_tx, cell_cfg.nof_slots_per_frame, period_idx) or
          context_entry.second.last_mcch_period_scheduled == period_idx) {
        continue;
      }
      if (schedule_one_mbs_grant(context_entry.second, true, 0, 0)) {
        context_entry.second.last_mcch_period_scheduled = period_idx;
      } else {
        break;
      }
    }
  }

  if (schedule_mtch) {
    for (const auto& context_entry : mbs_contexts) {
      for (const auto& mrb_entry : context_entry.second.mrb_pending_bytes) {
        if (mrb_entry.second == 0) {
          continue;
        }
        if (not schedule_one_mbs_grant(context_entry.second, false, mrb_entry.first, mrb_entry.second)) {
          return;
        }
      }
    }
  }
}

void cell_scheduler::handle_error_indication(slot_point sl_tx, scheduler_slot_handler::error_outcome event)
{
  ue_sched->handle_error_indication(sl_tx, event);
}

void cell_scheduler::reset_resource_grid(slot_point sl_tx)
{
  // Reset cell resource grid.
  res_grid.slot_indication(sl_tx);

  // Reset PDCCH slot context.
  pdcch_sch.slot_indication(sl_tx);

  // Reset PUCCH slot context.
  pucch_alloc.slot_indication(sl_tx);

  // Reset UCI slot context.
  uci_alloc.slot_indication(sl_tx);

  // Reset SRS slot context.
  srs_alloc.slot_indication(sl_tx);
}

void cell_scheduler::start()
{
  if (active) {
    return;
  }
  active = true;
  logger.info("cell={}: Cell scheduling was activated.", fmt::underlying(cell_cfg.cell_index));

  ue_sched->start();
}

void cell_scheduler::stop()
{
  // From this point onwards, no slot indications are expected until the cell is reenabled.

  if (not active) {
    // Do nothing.
    return;
  }
  active = false;
  logger.info("cell={}: Cell scheduling was deactivated.", fmt::underlying(cell_cfg.cell_index));

  // Stop sub-schedulers.
  ssb_sch.stop();
  si_sch.stop();
  prach_sch.stop();
  ra_sch.stop();
  pg_sch.stop();
  ue_sched->stop();

  // Reset resource grid and sub-allocators.
  res_grid.stop();
  pdcch_sch.stop();
  pucch_alloc.stop();
  uci_alloc.stop();

  // Report last metrics.
  metrics.handle_cell_deactivation();
}
