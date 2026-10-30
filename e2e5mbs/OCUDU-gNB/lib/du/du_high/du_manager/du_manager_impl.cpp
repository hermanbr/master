// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "du_manager_impl.h"
#include "du_positioning_handler_factory.h"
#include "procedures/cu_configuration_procedure.h"
#include "procedures/du_cell_stop_procedure.h"
#include "procedures/du_mac_ntn_param_update_procedure.h"
#include "procedures/du_param_config_procedure.h"
#include "procedures/du_ue_reset_procedure.h"
#include "procedures/du_ue_ric_configuration_procedure.h"
#include "procedures/f1c_disconnection_handling_procedure.h"
#include "ocudu/mac/mac_mbs_information_handler.h"
#include "ocudu/mac/mac_pdu_handler.h"
#include "ocudu/support/async/execute_on_blocking.h"
#include "ocudu/support/executors/execute_until_success.h"
#include "ocudu/support/synchronization/sync_event.h"
#include <algorithm>
#include <thread>

using namespace ocudu;
using namespace odu;

static rnti_t derive_mtch_g_rnti(uint64_t du_mbs_f1ap_id)
{
  // Initial PTM MBS support uses a deterministic G-RNTI pool until cell-specific RRC provisioning is added.
  static constexpr uint16_t first_mbs_rnti = 0xfe00;
  static constexpr uint16_t nof_mbs_rntis  = 0x0100;
  return to_rnti(first_mbs_rnti + (du_mbs_f1ap_id % nof_mbs_rntis));
}

static sib20_info get_configured_sib20_or_default(const du_manager_params& params)
{
  if (params.ran.cells.empty() or not params.ran.cells.front().si.si_config.has_value()) {
    return {};
  }

  const auto& sibs = params.ran.cells.front().si.si_config->sibs;
  auto        it   = std::find_if(sibs.begin(), sibs.end(), [](const sib_type_info& sib) {
    return std::holds_alternative<sib20_info>(sib.content);
  });
  return it == sibs.end() ? sib20_info{} : std::get<sib20_info>(it->content);
}

template <typename Request>
static mac_mbs_context_setup_request make_mac_mbs_context_setup_request(const Request& request, const sib20_info& sib20)
{
  mac_mbs_context_setup_request mac_request;
  mac_request.session_key        = request.session_key;
  mac_request.gnb_cu_mbs_f1ap_id = request.gnb_cu_mbs_f1ap_id;
  mac_request.gnb_du_mbs_f1ap_id = request.gnb_du_mbs_f1ap_id;
  mac_request.mcch_rnti          = rnti_t::SI_RNTI;
  mac_request.mtch_rnti          = derive_mtch_g_rnti(request.gnb_du_mbs_f1ap_id);
  mac_request.mcch_repetition_period_rf  = sib20.mcch_repetition_period_rf;
  mac_request.mcch_repetition_offset_rf  = sib20.mcch_repetition_offset_rf;
  mac_request.mcch_window_start_slot     = sib20.mcch_window_start_slot;
  mac_request.mcch_window_duration_slots = sib20.mcch_window_duration_slots;
  mac_request.mrb_ids                    = request.mrb_ids;
  mac_request.mrb_configs.reserve(request.mrb_configs.size());
  for (const f1ap_du_mbs_mrb_config& mrb_cfg : request.mrb_configs) {
    mac_mbs_mrb_config mac_mrb_cfg;
    mac_mrb_cfg.mrb_id          = mrb_cfg.mrb_id;
    mac_mrb_cfg.pdcp_sn_size_dl = mrb_cfg.pdcp_sn_size_dl;
    mac_request.mrb_configs.push_back(mac_mrb_cfg);
  }
  return mac_request;
}

du_manager_impl::du_manager_impl(const du_manager_params& params_) :
  params(params_),
  logger(ocudulog::fetch_basic_logger("DU-MNG")),
  main_ctrl_loop(128),
  cell_mng(params),
  cell_res_alloc(params.ran.cells, params.mac.sched_cfg, params.ran.srbs, params.ran.qos, params.test_cfg),
  metrics(params.metrics, params.services.du_mng_exec, params.services.timers, params.f1ap.metrics),
  ue_mng(params, cell_res_alloc, cell_mng, metrics.get_proc_collector()),
  positioning_handler(create_du_positioning_handler(params, cell_mng, ue_mng, logger)),
  proc_ctxt{params, ctxt, cell_mng, ue_mng, metrics, logger},
  controller(proc_ctxt, main_ctrl_loop)
{
}

void du_manager_impl::handle_ul_ccch_indication(const ul_ccch_indication_message& msg)
{
  // Switch DU Manager exec context
  if (not params.services.du_mng_exec.execute([this, msg]() {
        // Start UE create procedure
        ue_mng.handle_ue_create_request(msg);
      })) {
    logger.warning("Discarding UL-CCCH message cell={} tc-rnti={} slot_rx={}. Cause: DU manager task queue is full",
                   msg.cell_index,
                   msg.tc_rnti,
                   msg.slot_rx);
  }
}

void du_manager_impl::handle_crnti_ce_indication(const ul_crnti_ce_indication_message& msg)
{
  if (not params.services.du_mng_exec.execute([this, msg]() {
        du_ue* ue = ue_mng.find_ue(msg.ue_index);
        if (ue == nullptr || !cell_mng.has_cell(msg.cell_index)) {
          return;
        }

        const bool reached_prepared_target = ue->cond_mobility.handle_crnti_ce_indication();
        if (not reached_prepared_target) {
          logger.debug("ue={} cell={}: C-RNTI CE received but no Access Success is expected. Ignoring.",
                       msg.ue_index,
                       msg.cell_index);
          return;
        }

        const auto target_cgi = cell_mng.get_cell_cfg(msg.cell_index).nr_cgi;
        params.f1ap.ue_mng.handle_access_success({msg.ue_index, target_cgi});
      })) {
    logger.warning("cell={} ue={}: Discarding C-RNTI CE indication. Cause: DU manager task queue is full",
                   msg.cell_index,
                   msg.ue_index);
  }
}

void du_manager_impl::handle_f1c_connection_loss()
{
  schedule_async_task(launch_async<f1c_disconnection_handling_procedure>(proc_ctxt));
}

du_ue_index_t du_manager_impl::find_unused_du_ue_index()
{
  return ue_mng.find_unused_du_ue_index();
}

async_task<void> du_manager_impl::handle_f1_reset_request(const std::vector<du_ue_index_t>& ues_to_reset)
{
  return launch_async<du_ue_reset_procedure>(ues_to_reset, ue_mng, params, std::nullopt);
}

async_task<gnbcu_config_update_response>
du_manager_impl::handle_cu_context_update_request(const gnbcu_config_update_request& request)
{
  return launch_async<cu_configuration_procedure>(request, cell_mng, ue_mng, params, metrics);
}

async_task<f1ap_ue_context_creation_response>
du_manager_impl::handle_ue_context_creation(const f1ap_ue_context_creation_request& request)
{
  return ue_mng.handle_ue_create_request(request);
}

async_task<f1ap_ue_context_update_response>
du_manager_impl::handle_ue_context_update(const f1ap_ue_context_update_request& request)
{
  return ue_mng.handle_ue_config_request(request);
}

async_task<void> du_manager_impl::handle_ue_delete_request(const f1ap_ue_delete_request& request)
{
  return ue_mng.handle_ue_delete_request(request);
}

async_task<void> du_manager_impl::handle_ue_drb_deactivation_request(du_ue_index_t ue_index)
{
  return ue_mng.handle_ue_drb_deactivation_request(ue_index);
}

void du_manager_impl::handle_ue_reestablishment(du_ue_index_t new_ue_index, du_ue_index_t old_ue_index)
{
  ue_mng.handle_reestablishment_request(new_ue_index, old_ue_index);
}

void du_manager_impl::handle_ue_config_applied(du_ue_index_t ue_index)
{
  ue_mng.handle_ue_config_applied(ue_index);
}

void du_manager_impl::handle_mbs_context_setup(const f1ap_du_mbs_context_setup_request& request)
{
  mbs_contexts[request.session_key] = request;
  mac_mbs_context_setup_request mac_request = make_mac_mbs_context_setup_request(request, get_configured_sib20_or_default(params));
  params.mac.mgr.get_mbs_handler().handle_mbs_context_setup(mac_request);
  logger.info("MBS session={}: DU manager stored multicast context cu_mbs_f1ap_id={} du_mbs_f1ap_id={} mrbs={}",
              request.session_key,
              request.gnb_cu_mbs_f1ap_id,
              request.gnb_du_mbs_f1ap_id,
              request.mrb_ids.size());
}

void du_manager_impl::handle_mbs_context_modification(const f1ap_du_mbs_context_modification_request& request)
{
  auto& context                = mbs_contexts[request.session_key];
  context.session_key          = request.session_key;
  context.gnb_cu_mbs_f1ap_id  = request.gnb_cu_mbs_f1ap_id;
  context.gnb_du_mbs_f1ap_id  = request.gnb_du_mbs_f1ap_id;
  context.mrb_ids             = request.mrb_ids;
  context.mrb_configs         = request.mrb_configs;

  mac_mbs_context_setup_request mac_request = make_mac_mbs_context_setup_request(request, get_configured_sib20_or_default(params));
  params.mac.mgr.get_mbs_handler().handle_mbs_context_setup(mac_request);
  logger.info("MBS session={}: DU manager modified multicast context cu_mbs_f1ap_id={} du_mbs_f1ap_id={} mrbs={}",
              request.session_key,
              request.gnb_cu_mbs_f1ap_id,
              request.gnb_du_mbs_f1ap_id,
              request.mrb_ids.size());
}

void du_manager_impl::handle_mbs_context_release(const f1ap_du_mbs_context_release_request& request)
{
  const auto nof_erased = mbs_contexts.erase(request.session_key);
  params.mac.mgr.get_mbs_handler().handle_mbs_context_release(
      {request.session_key, request.gnb_cu_mbs_f1ap_id, request.gnb_du_mbs_f1ap_id});
  logger.info("MBS session={}: DU manager released multicast context cu_mbs_f1ap_id={} du_mbs_f1ap_id={} found={}",
              request.session_key,
              request.gnb_cu_mbs_f1ap_id,
              request.gnb_du_mbs_f1ap_id,
              nof_erased != 0);
}

void du_manager_impl::handle_mbs_data_pdu(f1ap_du_mbs_data_request request)
{
  auto context = mbs_contexts.find(request.session_key);
  if (context == mbs_contexts.end()) {
    logger.warning("MBS session={}: Dropping MTCH payload. Cause: multicast context does not exist",
                   request.session_key);
    return;
  }
  if (std::find(context->second.mrb_ids.begin(), context->second.mrb_ids.end(), request.mrb_id) ==
      context->second.mrb_ids.end()) {
    logger.warning("MBS session={}: Dropping MTCH payload. Cause: MRB {} is not configured",
                   request.session_key,
                   request.mrb_id);
    return;
  }

  mac_mbs_data_indication mac_data;
  mac_data.session_key = std::move(request.session_key);
  mac_data.mrb_id      = request.mrb_id;
  mac_data.pdu         = std::move(request.pdu);
  params.mac.mgr.get_mbs_handler().handle_mbs_data_pdu(std::move(mac_data));
}

async_task<du_mac_sched_control_config_response>
du_manager_impl::configure_ue_mac_scheduler(const du_mac_sched_control_config& reconf)
{
  return launch_async<du_ue_ric_configuration_procedure>(reconf, ue_mng, params);
}

async_task<du_param_config_response> du_manager_impl::handle_operator_config(const du_param_config_request& req,
                                                                             task_executor& continuation_exec)
{
  return launch_async([this, &continuation_exec, req, resp = expected<du_param_config_response>{}](
                          coro_context<async_task<du_param_config_response>>& ctx) mutable {
    CORO_BEGIN(ctx);

    // Move to DU manager execution context.
    CORO_AWAIT(defer_on_blocking(params.services.du_mng_exec, params.services.timers));

    // Launch procedure in the DU manager task scheduler.
    CORO_AWAIT_VALUE(resp,
                     when_coroutine_completed_on_task_sched(
                         main_ctrl_loop, launch_async<du_param_config_procedure>(req, params, cell_mng)));

    // Move back to the caller execution context.
    CORO_AWAIT(defer_on_blocking(continuation_exec, params.services.timers));

    CORO_RETURN(resp.value());
  });
}

du_param_config_response du_manager_impl::handle_sync_operator_config(const du_param_config_request& req)
{
  sync_event               ev;
  du_param_config_response resp;

  // Switch to DU manager execution context.
  execute_until_success(
      params.services.du_mng_exec, params.services.timers, [this, &req, &resp, tk = ev.get_token()]() mutable {
        // Dispatch procedure in DU manager task scheduler.
        // Also, bind the lifetime of the token to the procedure completion.
        schedule_async_task(launch_async([this, &resp, &req, tk = std::move(tk)](coro_context<async_task<void>>& ctx) {
          CORO_BEGIN(ctx);
          // Launch config procedure.
          CORO_AWAIT_VALUE(resp, launch_async<du_param_config_procedure>(req, params, cell_mng));
          CORO_RETURN();
        }));
      });

  // Block waiting for sync event token to go out of scope.
  ev.wait();
  return resp;
}

void du_manager_impl::handle_ntn_param_update(const du_ntn_param_update_request& req)
{
  schedule_async_task(launch_async([&req, this](coro_context<async_task<void>>& ctx) {
    CORO_BEGIN(ctx);

    if (not ctxt.running) {
      // Already stopped.
      CORO_EARLY_RETURN();
    }
    CORO_AWAIT(start_du_ntn_param_update(req, params, cell_mng));

    CORO_RETURN();
  }));
}
