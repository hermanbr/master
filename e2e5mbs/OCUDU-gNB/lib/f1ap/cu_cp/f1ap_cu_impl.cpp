// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "f1ap_cu_impl.h"
#include "asn1_helpers.h"
#include "f1ap_asn1_helpers.h"
#include "f1ap_asn1_utils.h"
#include "log_helpers.h"
#include "procedures/f1_removal_procedure.h"
#include "procedures/f1_setup_procedure.h"
#include "procedures/f1ap_positioning_activation_procedure.h"
#include "procedures/f1ap_positioning_information_exchange_procedure.h"
#include "procedures/f1ap_positioning_measurement_procedure.h"
#include "procedures/f1ap_stop_procedure.h"
#include "procedures/f1ap_trp_information_exchange_procedure.h"
#include "procedures/gnb_cu_configuration_update_procedure.h"
#include "procedures/ue_context_modification_procedure.h"
#include "procedures/ue_context_release_procedure.h"
#include "procedures/ue_context_setup_procedure.h"
#include "ocudu/adt/expected.h"
#include "ocudu/asn1/f1ap/common.h"
#include "ocudu/asn1/f1ap/f1ap.h"
#include "ocudu/asn1/f1ap/f1ap_ies.h"
#include "ocudu/cu_cp/cu_cp_types.h"
#include "ocudu/f1ap/f1ap_message.h"
#include "ocudu/ran/up_transport_layer_info.h"
#include <algorithm>
#include <vector>

using namespace ocudu;
using namespace ocucp;

static asn1::f1ap::qos_flow_level_qos_params_s make_default_mbs_qos_flow_params()
{
  asn1::f1ap::qos_flow_level_qos_params_s qos_params;

  qos_params.qos_characteristics.set_non_dyn_5qi().five_qi = 9;
  qos_params.ngra_nalloc_retention_prio.prio_level          = 1;
  qos_params.ngra_nalloc_retention_prio.pre_emption_cap.value =
      asn1::f1ap::pre_emption_cap_opts::shall_not_trigger_pre_emption;
  qos_params.ngra_nalloc_retention_prio.pre_emption_vulnerability.value =
      asn1::f1ap::pre_emption_vulnerability_opts::not_pre_emptable;

  return qos_params;
}

static asn1::f1ap::mbs_session_id_s make_f1ap_mbs_session_id(const cu_cp_mbs_session_id& session_id)
{
  asn1::f1ap::mbs_session_id_s asn1_session_id;

  asn1_session_id.tmgi.from_string(session_id.tmgi);
  if (session_id.nid.has_value()) {
    asn1_session_id.nid_present = true;
    asn1_session_id.nid.from_string(*session_id.nid);
  }

  return asn1_session_id;
}

static std::vector<uint8_t> make_mbs_qos_flow_ids(const std::vector<uint8_t>& qos_flow_ids)
{
  std::vector<uint8_t> out(qos_flow_ids.begin(), qos_flow_ids.end());
  if (out.empty()) {
    out.push_back(1);
  }
  std::sort(out.begin(), out.end());
  out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

static std::vector<uint16_t> make_default_mbs_mrb_ids()
{
  return {1};
}

static std::vector<uint16_t> make_mbs_mrb_ids_from_setup_list(
    const asn1::f1ap::multicast_m_rbs_setup_list_l& mrb_setup_list)
{
  std::vector<uint16_t> mrb_ids;
  mrb_ids.reserve(mrb_setup_list.size());
  for (const auto& mrb : mrb_setup_list) {
    mrb_ids.push_back(mrb->multicast_m_rbs_setup_item().mrb_id);
  }
  if (mrb_ids.empty()) {
    return make_default_mbs_mrb_ids();
  }
  return mrb_ids;
}

static std::vector<uint16_t> make_mbs_mrb_ids_from_setup_mod_list(
    const asn1::f1ap::multicast_m_rbs_setup_mod_list_l& mrb_setup_mod_list)
{
  std::vector<uint16_t> mrb_ids;
  mrb_ids.reserve(mrb_setup_mod_list.size());
  for (const auto& mrb : mrb_setup_mod_list) {
    mrb_ids.push_back(mrb->multicast_m_rbs_setup_mod_item().mrb_id);
  }
  if (mrb_ids.empty()) {
    return make_default_mbs_mrb_ids();
  }
  return mrb_ids;
}

static asn1::f1ap::multicast_m_rbs_to_be_setup_list_l
make_multicast_mrb_to_be_setup_list(const std::vector<uint8_t>& qos_flow_ids)
{
  const std::vector<uint8_t> mapped_qos_flows = make_mbs_qos_flow_ids(qos_flow_ids);
  asn1::f1ap::multicast_m_rbs_to_be_setup_item_s mrb_item;
  mrb_item.mrb_id                           = 1;
  mrb_item.mrb_qos_info                     = make_default_mbs_qos_flow_params();
  mrb_item.mbs_flows_mapped_to_mrb_list     = asn1::f1ap::mbs_flows_mapped_to_mrb_list_l(mapped_qos_flows.size());
  mrb_item.mbs_dl_pdcp_sn_len.value         = asn1::f1ap::pdcp_sn_len_opts::twelve_bits;

  for (unsigned i = 0, e = mapped_qos_flows.size(); i != e; ++i) {
    asn1::f1ap::mbs_flows_mapped_to_mrb_item_s flow_item;
    flow_item.mbs_qos_flow_id               = mapped_qos_flows[i];
    flow_item.mbs_qos_flow_level_qos_params = make_default_mbs_qos_flow_params();
    mrb_item.mbs_flows_mapped_to_mrb_list[i] = flow_item;
  }

  asn1::f1ap::multicast_m_rbs_to_be_setup_list_l list(1);
  list[0].load_info_obj(ASN1_F1AP_ID_MULTICAST_M_RBS_TO_BE_SETUP_ITEM);
  list[0]->multicast_m_rbs_to_be_setup_item() = mrb_item;
  return list;
}

static asn1::f1ap::multicast_m_rbs_to_be_setup_mod_list_l
make_multicast_mrb_to_be_setup_mod_list(const std::vector<uint8_t>& qos_flow_ids)
{
  const std::vector<uint8_t> mapped_qos_flows = make_mbs_qos_flow_ids(qos_flow_ids);
  asn1::f1ap::multicast_m_rbs_to_be_setup_mod_item_s mrb_item;
  mrb_item.mrb_id                           = 1;
  mrb_item.mrb_qos_info                     = make_default_mbs_qos_flow_params();
  mrb_item.mbs_flows_mapped_to_mrb_list     = asn1::f1ap::mbs_flows_mapped_to_mrb_list_l(mapped_qos_flows.size());
  mrb_item.mbs_dl_pdcp_sn_len.value         = asn1::f1ap::pdcp_sn_len_opts::twelve_bits;

  for (unsigned i = 0, e = mapped_qos_flows.size(); i != e; ++i) {
    asn1::f1ap::mbs_flows_mapped_to_mrb_item_s flow_item;
    flow_item.mbs_qos_flow_id               = mapped_qos_flows[i];
    flow_item.mbs_qos_flow_level_qos_params = make_default_mbs_qos_flow_params();
    mrb_item.mbs_flows_mapped_to_mrb_list[i] = flow_item;
  }

  asn1::f1ap::multicast_m_rbs_to_be_setup_mod_list_l list(1);
  list[0].load_info_obj(ASN1_F1AP_ID_MULTICAST_M_RBS_TO_BE_SETUP_MOD_ITEM);
  list[0]->multicast_m_rbs_to_be_setup_mod_item() = mrb_item;
  return list;
}

f1ap_cu_impl::f1ap_cu_impl(const f1ap_configuration&   f1ap_cfg_,
                           f1ap_message_notifier&      tx_pdu_notifier_,
                           f1ap_du_processor_notifier& f1ap_du_processor_notifier_,
                           timer_manager&              timers_,
                           task_executor&              ctrl_exec_) :
  cfg(f1ap_cfg_),
  logger(ocudulog::fetch_basic_logger("CU-CP-F1")),
  ue_ctxt_list(timer_factory{timers_, ctrl_exec_}, logger),
  du_processor_notifier(f1ap_du_processor_notifier_),
  ctrl_exec(ctrl_exec_),
  tx_pdu_notifier(*this, tx_pdu_notifier_),
  ev_mng(timer_factory{timers_, ctrl_exec_})
{
}

// Note: For fwd declaration of member types, dtor cannot be trivial.
f1ap_cu_impl::~f1ap_cu_impl() {}

async_task<void> f1ap_cu_impl::stop()
{
  return launch_async<f1ap_stop_procedure>(du_processor_notifier, ue_ctxt_list);
}

const f1ap_du_context& f1ap_cu_impl::get_context() const
{
  return du_ctxt;
}

void f1ap_cu_impl::handle_dl_rrc_message_transfer(const f1ap_dl_rrc_message& msg)
{
  f1ap_ue_context* ue_ctxt = ue_ctxt_list.find(msg.ue_index);
  if (ue_ctxt == nullptr) {
    logger.warning("ue={}: Dropping \"DLRRCMessageTransfer\". UE context does not exist", msg.ue_index);
    return;
  }

  ue_ctxt->handle_dl_rrc_message(msg, tx_pdu_notifier);
}

async_task<f1ap_ue_context_setup_response>
f1ap_cu_impl::handle_ue_context_setup_request(const f1ap_ue_context_setup_request&          request,
                                              const std::optional<rrc_ue_transfer_context>& rrc_context)
{
  return launch_async<ue_context_setup_procedure>(
      cfg, request, ue_ctxt_list, du_processor_notifier, tx_pdu_notifier, logger, rrc_context);
}

async_task<ue_index_t> f1ap_cu_impl::handle_ue_context_release_command(const f1ap_ue_context_release_command& msg)
{
  if (!ue_ctxt_list.contains(msg.ue_index)) {
    logger.warning("ue={}: Dropping \"UEContextReleaseCommand\". Cause: UE context does not exist", msg.ue_index);

    return launch_async([](coro_context<async_task<ue_index_t>>& ctx) mutable {
      CORO_BEGIN(ctx);
      CORO_RETURN(ue_index_t::invalid);
    });
  }

  return launch_async<ue_context_release_procedure>(cfg, msg, ue_ctxt_list[msg.ue_index], tx_pdu_notifier);
}

async_task<f1ap_ue_context_modification_response>
f1ap_cu_impl::handle_ue_context_modification_request(const f1ap_ue_context_modification_request& request)
{
  if (!ue_ctxt_list.contains(request.ue_index)) {
    logger.warning("ue={}: Dropping \"UEContextModificationRequest\". UE context does not exist", request.ue_index);

    return launch_async([](coro_context<async_task<f1ap_ue_context_modification_response>>& ctx) mutable {
      CORO_BEGIN(ctx);
      CORO_RETURN(f1ap_ue_context_modification_response{});
    });
  }

  return launch_async<ue_context_modification_procedure>(cfg, request, ue_ctxt_list[request.ue_index], tx_pdu_notifier);
}

bool f1ap_cu_impl::handle_ue_id_update(ue_index_t ue_index, ue_index_t old_ue_index)
{
  if (!ue_ctxt_list.contains(ue_index) or !ue_ctxt_list.contains(old_ue_index)) {
    return false;
  }

  // Mark that an old gNB-DU UE F1AP ID needs to be sent to the DU in the next DL RRC Message Transfer.
  ocudu_assert(ue_ctxt_list[old_ue_index].ue_ids.du_ue_f1ap_id &&
                   ue_ctxt_list[old_ue_index].ue_ids.du_ue_f1ap_id != gnb_du_ue_f1ap_id_t::invalid,
               "GNB-DU-UE-F1AP-ID should be valid");
  ue_ctxt_list[ue_index].pending_old_ue_id = *ue_ctxt_list[old_ue_index].ue_ids.du_ue_f1ap_id;
  return true;
}

void f1ap_cu_impl::handle_paging(const cu_cp_paging_message& msg)
{
  asn1::f1ap::paging_s paging = {};

  // Pack message into PDU.
  f1ap_message paging_msg;
  paging_msg.pdu.set_init_msg();
  paging_msg.pdu.init_msg().load_info_obj(ASN1_F1AP_ID_PAGING);

  fill_asn1_paging_message(paging_msg.pdu.init_msg().value.paging(), msg);

  // Send message to DU.
  tx_pdu_notifier.on_new_message(paging_msg);
}

std::string f1ap_cu_impl::make_mbs_session_key(const cu_cp_mbs_session_id& session_id) const
{
  std::string key = session_id.tmgi;
  if (session_id.nid.has_value()) {
    key += ":";
    key += *session_id.nid;
  }
  return key;
}

uint64_t f1ap_cu_impl::allocate_cu_mbs_f1ap_id()
{
  return next_cu_mbs_f1ap_id++;
}

void f1ap_cu_impl::send_mbs_distribution_setup_request(const std::string&          key,
                                                       mbs_context&               context,
                                                       const std::vector<uint16_t>& mrb_ids)
{
  if (!context.du_mbs_f1ap_id.has_value()) {
    logger.debug("MBS session={}: Skipping multicast distribution setup. Cause: DU MBS F1AP ID is unknown", key);
    return;
  }

  if (context.distribution_context_ref.empty()) {
    context.distribution_context_ref = fmt::format("{:08x}", static_cast<uint32_t>(context.cu_mbs_f1ap_id));
  }

  f1ap_message f1ap_msg;
  f1ap_msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_DISTRIBUTION_SETUP);
  auto& setup_request = f1ap_msg.pdu.init_msg().value.multicast_distribution_setup_request();

  setup_request->gnb_cu_mbs_f1ap_id = context.cu_mbs_f1ap_id;
  setup_request->gnb_du_mbs_f1ap_id = *context.du_mbs_f1ap_id;
  setup_request->mbs_multicast_f1_u_context_descriptor.multicast_f1_u_context_ref_f1.from_string(
      context.distribution_context_ref);
  setup_request->mbs_multicast_f1_u_context_descriptor.mc_f1_u_ctxtusage.value =
      asn1::f1ap::mbs_multicast_f1_u_context_descriptor_s::mc_f1_u_ctxtusage_opts::ptm;

  for (uint16_t mrb_id : mrb_ids) {
    setup_request->multicast_f1_u_context_to_be_setup_list.push_back({});
    auto& item_ie = setup_request->multicast_f1_u_context_to_be_setup_list.back();
    item_ie.load_info_obj(ASN1_F1AP_ID_MULTICAST_F1_U_CONTEXT_TO_BE_SETUP_ITEM);
    auto& item = item_ie->multicast_f1_u_context_to_be_setup_item();
    item.mrb_id = mrb_id;
    // When the 5GC signalled an NG-U shared multicast tunnel, forward it as the MRB traffic source so the DU can
    // ingest the multicast stream directly (integrated gNB). Otherwise fall back to a co-located unicast F1-U
    // endpoint, derived deterministically from the CU MBS F1AP ID and the MRB ID.
    const up_transport_layer_info du_f1u_info =
        context.ngu_tnl_info.has_value()
            ? *context.ngu_tnl_info
            : up_transport_layer_info{transport_layer_address::create_from_string("127.0.10.1"),
                                      int_to_gtpu_teid(static_cast<uint32_t>((context.cu_mbs_f1ap_id << 8U) + mrb_id))};
    up_transport_layer_info_to_asn1(item.mbs_f1u_info_at_du, du_f1u_info);
  }

  context.distribution_setup_sent = true;

  logger.info("MBS session={}: Sending F1AP MulticastDistributionSetupRequest cu_mbs_f1ap_id={} du_mbs_f1ap_id={} "
              "ref={} nof_mrbs={}",
              key,
              context.cu_mbs_f1ap_id,
              *context.du_mbs_f1ap_id,
              context.distribution_context_ref,
              mrb_ids.size());
  tx_pdu_notifier.on_new_message(f1ap_msg);
}

void f1ap_cu_impl::send_mbs_distribution_release_command(const std::string& key, const mbs_context& context)
{
  if (!context.du_mbs_f1ap_id.has_value() || context.distribution_context_ref.empty() ||
      !context.distribution_setup_sent) {
    return;
  }

  f1ap_message f1ap_msg;
  f1ap_msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_DISTRIBUTION_RELEASE);
  auto& release_cmd = f1ap_msg.pdu.init_msg().value.multicast_distribution_release_cmd();

  release_cmd->gnb_cu_mbs_f1ap_id = context.cu_mbs_f1ap_id;
  release_cmd->gnb_du_mbs_f1ap_id = *context.du_mbs_f1ap_id;
  release_cmd->mbs_multicast_f1_u_context_descriptor.multicast_f1_u_context_ref_f1.from_string(
      context.distribution_context_ref);
  release_cmd->mbs_multicast_f1_u_context_descriptor.mc_f1_u_ctxtusage.value =
      asn1::f1ap::mbs_multicast_f1_u_context_descriptor_s::mc_f1_u_ctxtusage_opts::ptm;

  logger.info("MBS session={}: Sending F1AP MulticastDistributionReleaseCommand cu_mbs_f1ap_id={} "
              "du_mbs_f1ap_id={} ref={}",
              key,
              context.cu_mbs_f1ap_id,
              *context.du_mbs_f1ap_id,
              context.distribution_context_ref);
  tx_pdu_notifier.on_new_message(f1ap_msg);
}

void f1ap_cu_impl::handle_mbs_session_update_request(const f1ap_mbs_session_update_request& request)
{
  const std::string key = make_mbs_session_key(request.session.session_id);

  mbs_context& context = mbs_contexts[key];
  if (context.cu_mbs_f1ap_id == 0) {
    context.cu_mbs_f1ap_id = allocate_cu_mbs_f1ap_id();
  }
  const bool ngu_tnl_changed = request.session.ngu_tnl_info.has_value() &&
                               (!context.ngu_tnl_info.has_value() ||
                                *context.ngu_tnl_info != *request.session.ngu_tnl_info);
  if (request.session.ngu_tnl_info.has_value()) {
    context.ngu_tnl_info = request.session.ngu_tnl_info;
  }

  if (context.setup_sent) {
    if (!context.du_mbs_f1ap_id.has_value()) {
      logger.debug("MBS session={}: Multicast context setup already sent to DU and waiting for response", key);
      return;
    }
    if (ngu_tnl_changed && context.distribution_setup_sent) {
      context.distribution_update_pending = true;
      logger.info("MBS session={}: NG-U multicast tunnel changed; scheduling F1AP distribution update", key);
    }

    f1ap_message f1ap_msg;
    f1ap_msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_CONTEXT_MOD);
    auto& mod_request = f1ap_msg.pdu.init_msg().value.multicast_context_mod_request();

    mod_request->gnb_cu_mbs_f1ap_id = context.cu_mbs_f1ap_id;
    mod_request->gnb_du_mbs_f1ap_id = *context.du_mbs_f1ap_id;
    mod_request->multicast_m_rbs_to_be_setup_mod_list_present = true;
    mod_request->multicast_m_rbs_to_be_setup_mod_list =
        make_multicast_mrb_to_be_setup_mod_list(request.session.qos_flow_ids);

    logger.info("MBS session={}: Sending F1AP MulticastContextModificationRequest cu_mbs_f1ap_id={} "
                "du_mbs_f1ap_id={} nof_qos_flows={}",
                key,
                context.cu_mbs_f1ap_id,
                *context.du_mbs_f1ap_id,
                request.session.qos_flow_ids.empty() ? 1 : request.session.qos_flow_ids.size());
    tx_pdu_notifier.on_new_message(f1ap_msg);
    return;
  }

  f1ap_message f1ap_msg;
  f1ap_msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_CONTEXT_SETUP);

  auto& setup_request                         = f1ap_msg.pdu.init_msg().value.multicast_context_setup_request();
  setup_request->gnb_cu_mbs_f1ap_id          = context.cu_mbs_f1ap_id;
  setup_request->mbs_session_id              = make_f1ap_mbs_session_id(request.session.session_id);
  setup_request->snssai.sst.from_string("01");
  setup_request->multicast_m_rbs_to_be_setup_list =
      make_multicast_mrb_to_be_setup_list(request.session.qos_flow_ids);

  context.setup_sent = true;

  logger.info("MBS session={}: Sending F1AP MulticastContextSetupRequest cu_mbs_f1ap_id={} nof_qos_flows={}",
              key,
              context.cu_mbs_f1ap_id,
              request.session.qos_flow_ids.empty() ? 1 : request.session.qos_flow_ids.size());
  tx_pdu_notifier.on_new_message(f1ap_msg);
}

void f1ap_cu_impl::handle_mbs_session_release_request(const f1ap_mbs_session_release_request& request)
{
  const std::string key     = make_mbs_session_key(request.session_id);
  auto              context = mbs_contexts.find(key);
  if (context == mbs_contexts.end()) {
    logger.debug("MBS session={}: Dropping multicast context release. Cause: context does not exist", key);
    return;
  }

  if (!context->second.du_mbs_f1ap_id.has_value()) {
    logger.debug("MBS session={}: Removing multicast context locally. Cause: DU MBS F1AP ID is unknown", key);
    mbs_contexts.erase(context);
    return;
  }

  send_mbs_distribution_release_command(key, context->second);

  f1ap_message f1ap_msg;
  f1ap_msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_CONTEXT_RELEASE);

  auto& release_cmd                   = f1ap_msg.pdu.init_msg().value.multicast_context_release_cmd();
  release_cmd->gnb_cu_mbs_f1ap_id    = context->second.cu_mbs_f1ap_id;
  release_cmd->gnb_du_mbs_f1ap_id    = *context->second.du_mbs_f1ap_id;
  release_cmd->cause.set_radio_network().value = asn1::f1ap::cause_radio_network_opts::normal_release;

  logger.info("MBS session={}: Sending F1AP MulticastContextReleaseCommand cu_mbs_f1ap_id={} du_mbs_f1ap_id={}",
              key,
              context->second.cu_mbs_f1ap_id,
              *context->second.du_mbs_f1ap_id);
  tx_pdu_notifier.on_new_message(f1ap_msg);
}

void f1ap_cu_impl::handle_message(const f1ap_message& msg)
{
  // Run F1AP protocols in Control executor.
  if (not ctrl_exec.execute([this, msg]() {
        // Log received message.
        log_pdu(true, msg);

        switch (msg.pdu.type().value) {
          case asn1::f1ap::f1ap_pdu_c::types_opts::init_msg:
            handle_initiating_message(msg.pdu.init_msg());
            break;
          case asn1::f1ap::f1ap_pdu_c::types_opts::successful_outcome:
            handle_successful_outcome(msg.pdu.successful_outcome());
            break;
          case asn1::f1ap::f1ap_pdu_c::types_opts::unsuccessful_outcome:
            handle_unsuccessful_outcome(msg.pdu.unsuccessful_outcome());
            break;
          default:
            logger.warning("Invalid PDU type");
            break;
        }
      })) {
    logger.warning("Discarding F1AP PDU. Cause: CU-CP task queue is full");
  }
}

void f1ap_cu_impl::remove_ue_context(ue_index_t ue_index)
{
  if (!ue_ctxt_list.contains(ue_index)) {
    logger.debug("ue={}: UE context not found", ue_index);
    return;
  }

  ue_ctxt_list.remove_ue(ue_index);
}

async_task<expected<trp_information_response_t, trp_information_failure_t>>
f1ap_cu_impl::handle_trp_information_request(const trp_information_request_t& request)
{
  logger.info("Handling TRP information request");
  return launch_async<f1ap_trp_information_exchange_procedure>(cfg, request, ev_mng, tx_pdu_notifier, logger);
}

async_task<expected<positioning_information_response_t, positioning_information_failure_t>>
f1ap_cu_impl::handle_positioning_information_request(const positioning_information_request_t& request)
{
  logger.info("Handling positioning information request");

  if (!ue_ctxt_list.contains(request.ue_index)) {
    logger.warning("ue={}: Dropping \"UEPositioningInformationRequest\". UE context does not exist", request.ue_index);

    return launch_async(
        [](coro_context<async_task<expected<positioning_information_response_t, positioning_information_failure_t>>>&
               ctx) mutable {
          CORO_BEGIN(ctx);
          CORO_RETURN(make_unexpected(positioning_information_failure_t{}));
        });
  }

  return launch_async<f1ap_positioning_information_exchange_procedure>(
      cfg, request, ue_ctxt_list[request.ue_index], tx_pdu_notifier, logger);
}

async_task<expected<positioning_activation_response_t, positioning_activation_failure_t>>
f1ap_cu_impl::handle_positioning_activation_request(const positioning_activation_request_t& request)
{
  if (!ue_ctxt_list.contains(request.ue_index)) {
    logger.warning("ue={}: Dropping \"UEPositioningActivationRequest\". UE context does not exist", request.ue_index);

    return launch_async(
        [](coro_context<async_task<expected<positioning_activation_response_t, positioning_activation_failure_t>>>&
               ctx) mutable {
          CORO_BEGIN(ctx);
          CORO_RETURN(make_unexpected(positioning_activation_failure_t{}));
        });
  }

  f1ap_ue_context& ue_ctxt = ue_ctxt_list[request.ue_index];
  ue_ctxt.logger.log_info("Handling positioning activation request");

  return launch_async<f1ap_positioning_activation_procedure>(cfg, request, ue_ctxt, tx_pdu_notifier, logger);
}

async_task<expected<measurement_response_t, measurement_failure_t>>
f1ap_cu_impl::handle_positioning_measurement_request(const measurement_request_t& request)
{
  logger.info("Handling positioning measurement request");
  return launch_async<f1ap_positioning_measurement_procedure>(cfg, request, ev_mng, tx_pdu_notifier, logger);
}

async_task<f1ap_gnb_cu_configuration_update_response>
f1ap_cu_impl::handle_gnb_cu_configuration_update(const f1ap_gnb_cu_configuration_update& request)
{
  return launch_async<gnb_cu_configuration_update_procedure>(cfg, request, tx_pdu_notifier, ev_mng, logger);
}

void f1ap_cu_impl::handle_initiating_message(const asn1::f1ap::init_msg_s& msg)
{
  switch (msg.value.type().value) {
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::options::f1_setup_request:
      handle_f1_setup_request(msg.value.f1_setup_request());
      break;
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::init_ul_rrc_msg_transfer:
      handle_initial_ul_rrc_message(msg.value.init_ul_rrc_msg_transfer());
      break;
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::ul_rrc_msg_transfer:
      handle_ul_rrc_message(msg.value.ul_rrc_msg_transfer());
      break;
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::f1_removal_request:
      du_processor_notifier.schedule_async_task(launch_async<f1_removal_procedure>(
          msg.value.f1_removal_request(), tx_pdu_notifier, du_processor_notifier, ue_ctxt_list, logger));
      break;
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::options::ue_context_release_request:
      handle_ue_context_release_request(msg.value.ue_context_release_request());
      break;
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::options::access_success:
      handle_access_success(msg.value.access_success());
      break;
    case asn1::f1ap::f1ap_elem_procs_o::init_msg_c::types_opts::options::gnb_du_cfg_upd:
      handle_du_cfg_update(msg.value.gnb_du_cfg_upd());
      break;
    default:
      logger.warning("Initiating message of type {} is not supported", msg.value.type().to_string());
      break;
  }
}

void f1ap_cu_impl::handle_access_success(const asn1::f1ap::access_success_s& msg)
{
  if (!ue_ctxt_list.contains(int_to_gnb_cu_ue_f1ap_id(msg->gnb_cu_ue_f1ap_id))) {
    logger.warning("cu_ue={} du_ue={}: Dropping \"AccessSuccess\". UE context does not exist",
                   msg->gnb_cu_ue_f1ap_id,
                   msg->gnb_du_ue_f1ap_id);
    return;
  }

  f1ap_ue_context& ue_ctxt = ue_ctxt_list[int_to_gnb_cu_ue_f1ap_id(msg->gnb_cu_ue_f1ap_id)];
  ue_ctxt.logger.log_debug("Received \"AccessSuccess\"");

  auto cgi = cgi_from_asn1(msg->nr_cgi);
  if (!cgi.has_value()) {
    logger.warning("cu_ue={}: Dropping \"AccessSuccess\". Cause: Failed to parse NR-CGI", msg->gnb_cu_ue_f1ap_id);
    return;
  }

  f1ap_access_success ind;
  ind.ue_index = ue_ctxt.ue_ids.ue_index;
  ind.cgi      = cgi.value();

  du_processor_notifier.on_access_success(ind);
}

void f1ap_cu_impl::handle_du_cfg_update(const asn1::f1ap::gnb_du_cfg_upd_s& request)
{
  f1ap_message f1ap_msg;

  // TODO: for now, always reply with a config update acknowledge.
  f1ap_msg.pdu.set_successful_outcome().load_info_obj(ASN1_F1AP_ID_GNB_DU_CFG_UPD);
  auto& resp = f1ap_msg.pdu.successful_outcome().value.gnb_du_cfg_upd_ack();

  resp->transaction_id = request->transaction_id;

  // Send F1AP PDU to DU.
  tx_pdu_notifier.on_new_message(f1ap_msg);
}

void f1ap_cu_impl::handle_f1_setup_request(const asn1::f1ap::f1_setup_request_s& request)
{
  current_transaction_id = request->transaction_id;

  handle_f1_setup_procedure(request, du_ctxt, tx_pdu_notifier, du_processor_notifier, logger);
}

void f1ap_cu_impl::handle_initial_ul_rrc_message(const asn1::f1ap::init_ul_rrc_msg_transfer_s& msg)
{
  const gnb_du_ue_f1ap_id_t du_ue_id = int_to_gnb_du_ue_f1ap_id(msg->gnb_du_ue_f1ap_id);

  expected<nr_cell_global_id_t> cgi = cgi_from_asn1(msg->nr_cgi);
  if (not cgi.has_value()) {
    logger.warning("du_ue={}: Dropping \"InitialULRRCMessageTransfer\". Invalid CGI", fmt::underlying(du_ue_id));
    return;
  }

  rnti_t crnti = to_rnti(msg->c_rnti);
  if (crnti == rnti_t::INVALID_RNTI) {
    logger.warning("du_ue={}: Dropping \"InitialULRRCMessageTransfer\". Cause: Invalid C-RNTI",
                   fmt::underlying(du_ue_id));
    return;
  }

  if (msg->sul_access_ind_present) {
    logger.debug("du_ue={}: Ignoring SUL access indicator", fmt::underlying(du_ue_id));
  }

  if (msg->rrc_container_rrc_setup_complete_present) {
    logger.warning("du_ue={}: Ignoring RRC Container RRCSetupComplete. Cause: Network Sharing with multiple cell-ID "
                   "broadcast is not supported",
                   fmt::underlying(du_ue_id));
  }

  const gnb_cu_ue_f1ap_id_t cu_ue_f1ap_id = ue_ctxt_list.allocate_gnb_cu_ue_f1ap_id();
  if (cu_ue_f1ap_id == gnb_cu_ue_f1ap_id_t::invalid) {
    logger.warning("du_ue={}: Dropping \"InitialULRRCMessageTransfer\". Cause: Failed to allocate CU-UE-F1AP-ID",
                   fmt::underlying(du_ue_id));
    return;
  }

  // Request UE index allocation from DU processor.
  ue_index_t ue_index = du_processor_notifier.request_new_ue_creation();
  if (ue_index == ue_index_t::invalid) {
    logger.warning("du_ue={}: Dropping \"InitialULRRCMessageTransfer\". Cause: Failed to create UE context",
                   fmt::underlying(du_ue_id));
    return;
  }

  // Add UE context.
  f1ap_ue_context& ue_ctxt = ue_ctxt_list.add_ue(ue_index, cu_ue_f1ap_id);
  ue_ctxt_list.add_du_ue_f1ap_id(cu_ue_f1ap_id, du_ue_id);

  // Request RRC UE entity creation.
  ue_rrc_context_creation_request req;
  req.ue_index = ue_index;
  req.c_rnti   = crnti;
  req.cgi      = cgi.value();
  if (msg->du_to_cu_rrc_container_present) {
    req.du_to_cu_rrc_container = byte_buffer(msg->du_to_cu_rrc_container);
  } else {
    // Assume the DU can't serve the UE, so the CU-CP should reject the UE, see TS 38.473 section 8.4.1.2.
    // We will forward an empty container to the RRC UE entity, that will trigger an RRC Reject.
    logger.debug("du_ue={}: Forwarding \"InitialULRRCMessageTransfer\" to RRC to reject the UE. Cause: Missing DU "
                 "to CU container",
                 fmt::underlying(du_ue_id));
  }
  req.rrc_container                    = msg->rrc_container.copy();
  ue_rrc_context_creation_outcome resp = du_processor_notifier.on_ue_rrc_context_creation_request(req);

  // Return if the UE creation was not successful.
  // Note: The higher layers will release the UE context, so we don't need to do anything here and must return directly.
  if (not resp.has_value()) {
    return;
  }

  // If the UE context creation was successful, create/update the UE context in the CU-CP and forward the RRC container
  // to RRC. Otherwise the UE will be rejected and the context will be released by higher layers.
  if (resp->ue_index != ue_index && ue_ctxt_list.contains(resp->ue_index)) {
    // This is a resumed UE. Update the existing context with the new DU UE F1AP ID.
    f1ap_ue_context& resume_ue_ctxt = ue_ctxt_list[resp->ue_index];
    ue_ctxt_list.add_du_ue_f1ap_id(resume_ue_ctxt.ue_ids.cu_ue_f1ap_id, du_ue_id);

    // Reset release state so the context can be released again if needed (e.g. RRC Resume with invalid MAC).
    resume_ue_ctxt.reset_release_state(); // TODO: This is a temporary solution, F1AP context should not be reused

    resume_ue_ctxt.logger.log_info("Updated resumed UE context with new DU UE F1AP ID");

    // Remove the newly created context since it's a resumed UE and we should reuse the existing context.
    ue_ctxt_list.remove_ue(ue_index);
  } else {
    // Add SRB notifiers to the UE context.
    ue_ctxt_list.add_srb0_rrc_notifier(resp->ue_index, resp->f1ap_srb0_notifier);
    ue_ctxt_list.add_srb1_rrc_notifier(resp->ue_index, resp->f1ap_srb1_notifier);
    ue_ctxt_list.add_srb2_rrc_notifier(resp->ue_index, resp->f1ap_srb2_notifier);

    ue_ctxt.logger.log_info("Added UE context");
  }

  // Forward RRC container.
  std::variant<f1c_initial_ul_bearer_handler*, f1c_ul_bearer_handler*> bearer_var =
      ue_ctxt_list[resp->ue_index].get_ul_bearer_manager().get_srb(srb_id_t::srb0);

  ocudu_assert(std::holds_alternative<f1c_initial_ul_bearer_handler*>(bearer_var), "Incorrect SRB type for SRB0");
  auto* bearer = std::get<f1c_initial_ul_bearer_handler*>(bearer_var);
  ocudu_assert(bearer, "SRB0 should be always active");
  bearer->handle_initial_ul_rrc_message(msg->rrc_container.copy(), crnti);
}

void f1ap_cu_impl::handle_ul_rrc_message(const asn1::f1ap::ul_rrc_msg_transfer_s& msg)
{
  f1ap_ue_context* ue_ctxt = ue_ctxt_list.find(int_to_gnb_cu_ue_f1ap_id(msg->gnb_cu_ue_f1ap_id));
  if (ue_ctxt == nullptr) {
    logger.warning("cu_ue={} du_ue={}: Dropping \"ULRRCMessageTransfer\". UE context does not exist",
                   msg->gnb_cu_ue_f1ap_id,
                   msg->gnb_du_ue_f1ap_id);
    return;
  }

  if (int_to_srb_id(msg->srb_id) == srb_id_t::srb0) {
    logger.warning("cu_ue={} du_ue={}: Dropping \"ULRRCMessageTransfer\". Using SRB0 for non-initial UL RRC message",
                   msg->gnb_cu_ue_f1ap_id,
                   msg->gnb_du_ue_f1ap_id);
    return;
  }

  std::variant<f1c_initial_ul_bearer_handler*, f1c_ul_bearer_handler*> bearer_var =
      ue_ctxt->get_ul_bearer_manager().get_srb(int_to_srb_id(msg->srb_id));

  ocudu_assert(std::holds_alternative<f1c_ul_bearer_handler*>(bearer_var),
               "Incorrect SRB type for {}",
               int_to_srb_id(msg->srb_id));

  auto* bearer = std::get<f1c_ul_bearer_handler*>(bearer_var);
  if (bearer == nullptr) {
    logger.warning("cu_ue={} du_ue={}: Dropping \"ULRRCMessageTransfer\". Cause: SRB{} has not been activated",
                   msg->gnb_cu_ue_f1ap_id,
                   msg->gnb_du_ue_f1ap_id,
                   int_to_srb_id(msg->srb_id));
    return;
  }
  bearer->handle_ul_rrc_message(msg->rrc_container.copy());
}

void f1ap_cu_impl::handle_ue_context_release_request(const asn1::f1ap::ue_context_release_request_s& msg)
{
  if (!ue_ctxt_list.contains(int_to_gnb_cu_ue_f1ap_id(msg->gnb_cu_ue_f1ap_id))) {
    logger.warning("cu_ue={} du_ue={}: Dropping \"UeContextReleaseRequest\". UE context does not exist",
                   msg->gnb_cu_ue_f1ap_id,
                   msg->gnb_du_ue_f1ap_id);
    return;
  }

  f1ap_ue_context& ue_ctxt = ue_ctxt_list[int_to_gnb_cu_ue_f1ap_id(msg->gnb_cu_ue_f1ap_id)];

  if (ue_ctxt.marked_for_release) {
    // UE context is already being released. Ignore the request.
    ue_ctxt.logger.log_debug("\"UeContextReleaseRequest\" ignored. UE context release procedure has already started");
    return;
  }

  ue_ctxt.logger.log_debug("Received \"UeContextReleaseRequest\"");

  f1ap_ue_context_release_request req;
  req.ue_index = ue_ctxt.ue_ids.ue_index;
  req.cause    = asn1_to_cause(msg->cause);

  du_processor_notifier.on_du_initiated_ue_context_release_request(req);
}

void f1ap_cu_impl::handle_successful_outcome(const asn1::f1ap::successful_outcome_s& outcome)
{
  auto get_ue_ctxt_in_ue_assoc_msg = [this](const asn1::f1ap::successful_outcome_s& outcome_) -> f1ap_ue_context* {
    std::optional<gnb_cu_ue_f1ap_id_t> cu_ue_id = get_gnb_cu_ue_f1ap_id(outcome_);
    // The GNB-CU-UE-F1AP-ID field is mandatory in all UE associated successful messages.
    if (!cu_ue_id.has_value()) {
      logger.warning("Discarding received \"{}\". Cause: Missing mandatory GNB-CU-UE-F1AP-ID field",
                     outcome_.value.type().to_string());
      return nullptr;
    }

    f1ap_ue_context* ue_ctxt = ue_ctxt_list.find(*cu_ue_id);
    if (ue_ctxt == nullptr) {
      logger.warning("cu_ue={}: Discarding received \"{}\". Cause: UE was not found",
                     fmt::underlying(*cu_ue_id),
                     outcome_.value.type().to_string());
      return nullptr;
    }
    return ue_ctxt;
  };

  std::optional<uint8_t> transaction_id = std::nullopt;

  switch (outcome.value.type().value) {
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::ue_context_release_complete:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.context_release_complete.set(outcome.value.ue_context_release_complete());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::ue_context_setup_resp:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.context_setup_outcome.set(outcome.value.ue_context_setup_resp());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::ue_context_mod_resp:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.context_modification_outcome.set(outcome.value.ue_context_mod_resp());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::trp_info_resp:
      transaction_id = get_transaction_id(outcome);
      if (not transaction_id.has_value()) {
        logger.error("Successful outcome of type {} is not supported", outcome.value.type().to_string());
        break;
      }
      if (not ev_mng.transactions.set_response(transaction_id.value(), outcome)) {
        logger.warning("Unexpected transaction id={}", transaction_id.value());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::positioning_info_resp:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.positioning_information_outcome.set(outcome.value.positioning_info_resp());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::positioning_activation_resp:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.positioning_activation_outcome.set(outcome.value.positioning_activation_resp());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::positioning_meas_resp:
      transaction_id = get_transaction_id(outcome);
      if (not transaction_id.has_value()) {
        logger.error("Successful outcome of type {} is not supported", outcome.value.type().to_string());
        break;
      }
      if (not ev_mng.transactions.set_response(transaction_id.value(), outcome)) {
        logger.warning("Unexpected transaction id={}", transaction_id.value());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::gnb_cu_cfg_upd_ack:
      transaction_id = get_transaction_id(outcome);
      if (not transaction_id.has_value()) {
        logger.error("Successful outcome of type {} is not supported", outcome.value.type().to_string());
        break;
      }
      if (not ev_mng.transactions.set_response(transaction_id.value(), outcome)) {
        logger.warning("Unexpected transaction id={}", transaction_id.value());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_context_setup_resp: {
      const auto& setup_resp = outcome.value.multicast_context_setup_resp();
      auto        context    = std::find_if(mbs_contexts.begin(), mbs_contexts.end(), [&setup_resp](const auto& entry) {
        return entry.second.cu_mbs_f1ap_id == setup_resp->gnb_cu_mbs_f1ap_id;
      });
      if (context == mbs_contexts.end()) {
        logger.warning("cu_mbs_f1ap_id={}: Discarding MulticastContextSetupResponse. Cause: context does not exist",
                       setup_resp->gnb_cu_mbs_f1ap_id);
        break;
      }
      context->second.du_mbs_f1ap_id = setup_resp->gnb_du_mbs_f1ap_id;
      logger.info("MBS session={}: Received MulticastContextSetupResponse du_mbs_f1ap_id={}",
                  context->first,
                  setup_resp->gnb_du_mbs_f1ap_id);
      send_mbs_distribution_setup_request(context->first,
                                          context->second,
                                          make_mbs_mrb_ids_from_setup_list(setup_resp->multicast_m_rbs_setup_list));
      break;
    }
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_context_release_complete: {
      const auto& release_complete = outcome.value.multicast_context_release_complete();
      auto        context = std::find_if(mbs_contexts.begin(), mbs_contexts.end(), [&release_complete](const auto& entry) {
        return entry.second.cu_mbs_f1ap_id == release_complete->gnb_cu_mbs_f1ap_id;
      });
      if (context == mbs_contexts.end()) {
        logger.debug("cu_mbs_f1ap_id={}: Received MulticastContextReleaseComplete for unknown context",
                     release_complete->gnb_cu_mbs_f1ap_id);
        break;
      }
      logger.info("MBS session={}: Received MulticastContextReleaseComplete", context->first);
      mbs_contexts.erase(context);
      break;
    }
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_context_mod_resp: {
      const auto& mod_resp = outcome.value.multicast_context_mod_resp();
      auto        context  = std::find_if(mbs_contexts.begin(), mbs_contexts.end(), [&mod_resp](const auto& entry) {
        return entry.second.cu_mbs_f1ap_id == mod_resp->gnb_cu_mbs_f1ap_id &&
               entry.second.du_mbs_f1ap_id.has_value() &&
               *entry.second.du_mbs_f1ap_id == mod_resp->gnb_du_mbs_f1ap_id;
      });
      if (context == mbs_contexts.end()) {
        logger.warning("cu_mbs_f1ap_id={} du_mbs_f1ap_id={}: Discarding MulticastContextModificationResponse. "
                       "Cause: context does not exist",
                       mod_resp->gnb_cu_mbs_f1ap_id,
                       mod_resp->gnb_du_mbs_f1ap_id);
        break;
      }
      logger.info("MBS session={}: Received MulticastContextModificationResponse setup_mod_mrbs={} modified_mrbs={}",
                  context->first,
                  mod_resp->multicast_m_rbs_setup_mod_list.size(),
                  mod_resp->multicast_m_rbs_modified_list.size());
      if ((!context->second.distribution_setup_sent || context->second.distribution_update_pending) &&
          mod_resp->multicast_m_rbs_setup_mod_list_present) {
        send_mbs_distribution_setup_request(
            context->first,
            context->second,
            make_mbs_mrb_ids_from_setup_mod_list(mod_resp->multicast_m_rbs_setup_mod_list));
        context->second.distribution_update_pending = false;
      }
      break;
    }
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_distribution_setup_resp: {
      const auto& dist_resp = outcome.value.multicast_distribution_setup_resp();
      auto        context   = std::find_if(mbs_contexts.begin(), mbs_contexts.end(), [&dist_resp](const auto& entry) {
        return entry.second.cu_mbs_f1ap_id == dist_resp->gnb_cu_mbs_f1ap_id;
      });
      if (context == mbs_contexts.end()) {
        logger.debug("cu_mbs_f1ap_id={}: Received MulticastDistributionSetupResponse for unknown context",
                     dist_resp->gnb_cu_mbs_f1ap_id);
        break;
      }
      // The DU has accepted the multicast F1-U distribution context. The MBS PTM bearer (MCCH/MTCH on the shared
      // G-RNTI) is now fully set up; multicast media flows DU-side via the bound F1-U/NG-U tunnel.
      logger.info("MBS session={}: Multicast distribution setup completed du_mbs_f1ap_id={} nof_mrbs={}",
                  context->first,
                  dist_resp->gnb_du_mbs_f1ap_id,
                  dist_resp->multicast_f1_u_context_setup_list.size());
      break;
    }
    case asn1::f1ap::f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_distribution_release_complete: {
      const auto& dist_rel = outcome.value.multicast_distribution_release_complete();
      logger.info("cu_mbs_f1ap_id={} du_mbs_f1ap_id={}: Multicast distribution release completed",
                  dist_rel->gnb_cu_mbs_f1ap_id,
                  dist_rel->gnb_du_mbs_f1ap_id);
      break;
    }
    default:
      logger.warning("Successful outcome of type {} is not supported", outcome.value.type().to_string());
      break;
  }
}

void f1ap_cu_impl::handle_unsuccessful_outcome(const asn1::f1ap::unsuccessful_outcome_s& outcome)
{
  auto get_ue_ctxt_in_ue_assoc_msg = [this](const asn1::f1ap::unsuccessful_outcome_s& outcome_) -> f1ap_ue_context* {
    std::optional<gnb_cu_ue_f1ap_id_t> cu_ue_id = get_gnb_cu_ue_f1ap_id(outcome_);
    // The GNB-CU-UE-F1AP-ID field is mandatory in all UE associated failure messages.
    ocudu_assert(cu_ue_id,
                 "Discarding received \"{}\". Cause: GNB-CU-UE-F1AP-ID field is mandatory",
                 outcome_.value.type().to_string());

    f1ap_ue_context* ue_ctxt = ue_ctxt_list.find(*cu_ue_id);
    if (ue_ctxt == nullptr) {
      logger.warning("cu_ue={}: Discarding received \"{}\". Cause: UE was not found.",
                     fmt::underlying(*cu_ue_id),
                     outcome_.value.type().to_string());
      return nullptr;
    }
    return ue_ctxt;
  };

  std::optional<uint8_t> transaction_id = std::nullopt;

  switch (outcome.value.type().value) {
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::ue_context_setup_fail:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.context_setup_outcome.set(outcome.value.ue_context_setup_fail());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::ue_context_mod_fail:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.context_modification_outcome.set(outcome.value.ue_context_mod_fail());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::trp_info_fail:
      transaction_id = get_transaction_id(outcome);
      if (not transaction_id.has_value()) {
        logger.error("Unsuccessful outcome of type {} is not supported", outcome.value.type().to_string());
        break;
      }
      if (not ev_mng.transactions.set_response(transaction_id.value(), make_unexpected(outcome))) {
        logger.warning("Unexpected transaction id={}", transaction_id.value());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::positioning_info_fail:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.positioning_information_outcome.set(outcome.value.positioning_info_fail());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::positioning_activation_fail:
      if (auto* ue_ctxt = get_ue_ctxt_in_ue_assoc_msg(outcome)) {
        ue_ctxt->ev_mng.positioning_activation_outcome.set(outcome.value.positioning_activation_fail());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::positioning_meas_fail:
      transaction_id = get_transaction_id(outcome);
      if (not transaction_id.has_value()) {
        logger.error("Unsuccessful outcome of type {} is not supported", outcome.value.type().to_string());
        break;
      }
      if (not ev_mng.transactions.set_response(transaction_id.value(), make_unexpected(outcome))) {
        logger.warning("Unexpected transaction id={}", transaction_id.value());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::gnb_cu_cfg_upd_fail:
      transaction_id = get_transaction_id(outcome);
      if (not transaction_id.has_value()) {
        logger.error("Unsuccessful outcome of type {} is not supported", outcome.value.type().to_string());
        break;
      }
      if (not ev_mng.transactions.set_response(transaction_id.value(), make_unexpected(outcome))) {
        logger.warning("Unexpected transaction id={}", transaction_id.value());
      }
      break;
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::multicast_context_setup_fail: {
      const auto& setup_fail = outcome.value.multicast_context_setup_fail();
      auto        context    = std::find_if(mbs_contexts.begin(), mbs_contexts.end(), [&setup_fail](const auto& entry) {
        return entry.second.cu_mbs_f1ap_id == setup_fail->gnb_cu_mbs_f1ap_id;
      });
      if (context == mbs_contexts.end()) {
        logger.warning("cu_mbs_f1ap_id={}: Received MulticastContextSetupFailure for unknown context",
                       setup_fail->gnb_cu_mbs_f1ap_id);
        break;
      }
      logger.warning("MBS session={}: Received MulticastContextSetupFailure", context->first);
      mbs_contexts.erase(context);
      break;
    }
    case asn1::f1ap::f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::multicast_context_mod_fail: {
      const auto& mod_fail = outcome.value.multicast_context_mod_fail();
      logger.warning("cu_mbs_f1ap_id={} du_mbs_f1ap_id={}: Received MulticastContextModificationFailure",
                     mod_fail->gnb_cu_mbs_f1ap_id,
                     mod_fail->gnb_du_mbs_f1ap_id);
      break;
    }
    default:
      logger.warning("Unsuccessful outcome of type {} is not supported", outcome.value.type().to_string());
      break;
  }
}

void f1ap_cu_impl::log_pdu(bool is_rx, const f1ap_message& pdu)
{
  using namespace asn1::f1ap;

  if (not logger.info.enabled()) {
    return;
  }

  // In case of F1 Setup, the gNB-DU-Id might not be set yet.
  gnb_du_id_t du_id = du_ctxt.gnb_du_id;
  if (du_id == gnb_du_id_t::invalid) {
    if (pdu.pdu.type().value == f1ap_pdu_c::types_opts::init_msg and
        pdu.pdu.init_msg().value.type().value == f1ap_elem_procs_o::init_msg_c::types_opts::f1_setup_request) {
      du_id = int_to_gnb_du_id(pdu.pdu.init_msg().value.f1_setup_request()->gnb_du_id);
    }
  }

  // Fetch UE index.
  auto                      cu_ue_id = get_gnb_cu_ue_f1ap_id(pdu.pdu);
  std::optional<ue_index_t> ue_idx;
  if (cu_ue_id.has_value()) {
    const auto* ue_ptr = ue_ctxt_list.find(cu_ue_id.value());
    if (ue_ptr != nullptr and ue_ptr->ue_ids.ue_index != ue_index_t::invalid) {
      ue_idx = ue_ptr->ue_ids.ue_index;
    }
  }

  // Log PDU.
  log_f1ap_pdu(logger, is_rx, du_id, ue_idx, pdu, cfg.json_log_enabled);
}

void f1ap_cu_impl::tx_pdu_notifier_with_logging::on_new_message(const f1ap_message& msg)
{
  // Log message.
  parent.log_pdu(false, msg);

  // Forward message to DU.
  decorated.on_new_message(msg);
}
