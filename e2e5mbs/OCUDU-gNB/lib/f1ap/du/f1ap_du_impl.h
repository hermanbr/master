// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "du/ue_context/f1ap_du_ue_manager.h"
#include "f1ap_du_connection_handler.h"
#include "f1ap_du_context.h"
#include "f1ap_du_metrics_collector_impl.h"
#include "ocudu/asn1/f1ap/f1ap.h"
#include "ocudu/f1u/mbs/f1u_mbs_demux_session.h"
#include "ocudu/f1u/mbs/f1u_mbs_router.h"
#include "ocudu/f1u/mbs/mbs_ngu_receiver.h"
#include "ocudu/f1ap/du/f1ap_du.h"
#include "ocudu/gtpu/gtpu_teid_pool.h"
#include "ocudu/support/async/async_task.h"
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace ocudu {
namespace odu {

class f1ap_ue_executor_mapper;
class f1c_connection_client;
class f1ap_event_manager;

class f1ap_du_impl final : public f1ap_du
{
public:
  f1ap_du_impl(f1c_connection_client&   f1c_client_handler_,
               f1ap_du_configurator&    task_sched_,
               task_executor&           ctrl_exec,
               f1ap_ue_executor_mapper& ue_exec_mapper,
               f1ap_du_paging_notifier& paging_notifier_,
               timer_manager&           timers_,
               gtpu_demux_ctrl*         mbs_demux_ctrl_ = nullptr,
               task_executor*           mbs_f1u_exec_   = nullptr);
  ~f1ap_du_impl() override;

  bool             connect_to_cu_cp() override;
  async_task<void> disconnect_from_cu_cp() override;

  // F1AP interface management procedures functions as per TS38.473, Section 8.2.
  async_task<f1_reset_acknowledgement>     handle_f1_reset_request(const f1_reset_request& req) override;
  async_task<f1_setup_result>              handle_f1_setup_request(const f1_setup_request_message& request) override;
  async_task<void>                         handle_f1_removal_request() override;
  async_task<gnbdu_config_update_response> handle_du_config_update(const gnbdu_config_update_request& request) override;
  bool                                     is_f1_setup() const override { return ctxt.f1c_setup; }

  // F1AP RRC Message Transfer Procedure functions as per TS38.473, Section 8.4.
  void handle_rrc_delivery_report(const f1ap_rrc_delivery_report_msg& report) override {}

  // F1AP message handler functions
  void handle_message(const f1ap_message& msg) override;

  // F1AP UE configuration functions
  f1ap_ue_creation_response      handle_ue_creation_request(const f1ap_ue_creation_request& msg) override;
  f1ap_ue_configuration_response handle_ue_configuration_request(const f1ap_ue_configuration_request& msg) override;
  void                           handle_ue_deletion_request(du_ue_index_t ue_index) override;

  // F1AP UE context manager functions
  void handle_ue_context_release_request(const f1ap_ue_context_release_request& request) override;
  void handle_access_success(const f1ap_access_success_event& msg) override;
  async_task<f1ap_ue_context_modification_confirm>
       handle_ue_context_modification_required(const f1ap_ue_context_modification_required& msg) override;
  void handle_ue_inactivity_notification(const f1ap_ue_inactivity_notification_message& msg) override {}
  void handle_notify(const f1ap_notify_message& msg) override {}
  bool has_gnb_cu_ue_f1ap_id(const du_ue_index_t& ue_index) const override
  {
    return get_gnb_cu_ue_f1ap_id(ue_index).has_value();
  }

  // F1AP UE ID translator functions.
  std::optional<gnb_cu_ue_f1ap_id_t> get_gnb_cu_ue_f1ap_id(const du_ue_index_t& ue_index) const override;
  std::optional<gnb_cu_ue_f1ap_id_t> get_gnb_cu_ue_f1ap_id(const gnb_du_ue_f1ap_id_t& gnb_du_ue_f1ap_id) const override;
  gnb_du_ue_f1ap_id_t                get_gnb_du_ue_f1ap_id(const du_ue_index_t& ue_index) override;
  gnb_du_ue_f1ap_id_t                get_gnb_du_ue_f1ap_id(const gnb_cu_ue_f1ap_id_t& gnb_cu_ue_f1ap_id) override;
  du_ue_index_t                      get_ue_index(const gnb_du_ue_f1ap_id_t& gnb_du_ue_f1ap_id) override;
  du_ue_index_t                      get_ue_index(const gnb_cu_ue_f1ap_id_t& gnb_cu_ue_f1ap_id) override;

  f1ap_metrics_collector& get_metrics_collector() override { return metrics; }

  void handle_mbs_f1u_data(f1ap_du_mbs_f1u_data_indication indication) override;

private:
  class tx_pdu_notifier_with_logging;

  /// \brief Notify the DU about the reception of an initiating message.
  /// \param[in] msg The received initiating message.
  void handle_initiating_message(const asn1::f1ap::init_msg_s& msg);

  /// \brief Notify the DU about the reception of a successful outcome message.
  /// \param[in] outcome The successful outcome message.
  void handle_successful_outcome(const asn1::f1ap::successful_outcome_s& outcome);

  /// \brief Notify the DU about the reception of an unsuccessful outcome message.
  /// \param[in] outcome The unsuccessful outcome message.
  void handle_unsuccessful_outcome(const asn1::f1ap::unsuccessful_outcome_s& outcome);

  /// \brief Handle RESET as per TS 38.473, Section 8.2.1.
  void handle_reset(const asn1::f1ap::reset_s& msg);

  /// \brief Handle GNB-CU CONFIGURATION UPDATE as per TS38.473, Section 8.2.5.2.
  void handle_gnb_cu_configuration_update(const asn1::f1ap::gnb_cu_cfg_upd_s& msg);

  /// \brief Handle UE CONTEXT SETUP REQUEST as per TS38.473, Section 8.3.1.
  void handle_ue_context_setup_request(const asn1::f1ap::ue_context_setup_request_s& msg);

  /// \brief Handle UE CONTEXT MODIFICATION REQUEST as per TS38.473, Section 8.3.3.
  void handle_ue_context_release_command(const asn1::f1ap::ue_context_release_cmd_s& msg);

  /// \brief Handle UE CONTEXT MODIFICATION REQUEST as per TS38.473, Section 8.3.4.
  void handle_ue_context_modification_request(const asn1::f1ap::ue_context_mod_request_s& msg);

  /// \brief Handle DL RRC Message Transfer as per TS38.473, Section 8.4.2.2.
  void handle_dl_rrc_message_transfer(const asn1::f1ap::dl_rrc_msg_transfer_s& msg);

  bool handle_rx_message_gnb_cu_ue_f1ap_id(f1ap_du_ue& ue, gnb_cu_ue_f1ap_id_t cu_ue_id);

  void send_error_indication(const asn1::f1ap::cause_c&         cause,
                             std::optional<uint8_t>             transaction_id = {},
                             std::optional<gnb_du_ue_f1ap_id_t> du_ue_id       = {},
                             std::optional<gnb_cu_ue_f1ap_id_t> cu_ue_id       = {});

  /// \brief Handle Paging as per TS38.473, Section 8.7.
  void handle_paging_request(const asn1::f1ap::paging_s& msg);

  /// \brief Handle MULTICAST CONTEXT SETUP as per TS38.473, Section 8.
  void handle_multicast_context_setup_request(const asn1::f1ap::multicast_context_setup_request_s& msg);

  /// \brief Handle MULTICAST CONTEXT MODIFICATION as per TS38.473, Section 8.
  void handle_multicast_context_modification_request(const asn1::f1ap::multicast_context_mod_request_s& msg);

  /// \brief Handle MULTICAST CONTEXT RELEASE as per TS38.473, Section 8.
  void handle_multicast_context_release_command(const asn1::f1ap::multicast_context_release_cmd_s& msg);

  /// \brief Handle MULTICAST DISTRIBUTION SETUP as per TS38.473, Section 8.
  void handle_multicast_distribution_setup_request(const asn1::f1ap::multicast_distribution_setup_request_s& msg);

  /// \brief Handle MULTICAST DISTRIBUTION RELEASE as per TS38.473, Section 8.
  void handle_multicast_distribution_release_command(const asn1::f1ap::multicast_distribution_release_cmd_s& msg);

  /// \brief Handle POSITIONING MEASUREMENT REQUEST as per TS 38.473, Section 8.13.3.
  void handle_positioning_measurement_request(const asn1::f1ap::positioning_meas_request_s& msg);

  /// \brief Handle TRP INFORMATION REQUEST as per TS 38.473, Section 8.13.8.
  void handle_trp_information_request(const asn1::f1ap::trp_info_request_s& msg);

  /// \brief Handle POSITIONING INFORMATION REQUEST as per TS 38.473, Section 8.13.9.
  void handle_positioning_information_request(const asn1::f1ap::positioning_info_request_s& msg);

  /// \brief Log F1AP PDU.
  void log_pdu(bool is_rx, const f1ap_message& pdu);

  struct mbs_context {
    uint64_t                       cu_mbs_f1ap_id = 0;
    uint64_t                       du_mbs_f1ap_id = 0;
    asn1::f1ap::mbs_session_id_s session_id;
    std::vector<uint16_t>          mrb_ids;
    std::vector<f1ap_du_mbs_mrb_config> mrb_configs;
  };

  struct mbs_f1u_distribution_context {
    std::string                           session_key;
    uint16_t                              mrb_id = 0;
    asn1::f1ap::up_transport_layer_info_c du_f1u_info;
    asn1::f1ap::up_transport_layer_info_c cu_f1u_info;
  };

  static std::string make_mbs_session_key(const asn1::f1ap::mbs_session_id_s& session_id);
  static std::string make_mbs_f1u_context_key(const std::string& multicast_f1u_context_ref, uint16_t mrb_id);
  uint64_t           allocate_du_mbs_f1ap_id();
  std::string         get_mbs_f1u_local_addr() const;

  ocudulog::basic_logger&  logger;
  task_executor&           ctrl_exec;
  f1ap_du_configurator&    du_mng;
  f1ap_du_paging_notifier& paging_notifier;
  timer_manager&           timers;

  f1ap_du_connection_handler connection_handler;

  f1ap_du_ue_manager ues;

  f1ap_du_context ctxt;

  std::map<std::string, mbs_context> mbs_contexts;
  uint64_t                           next_du_mbs_f1ap_id = 1;

  std::unique_ptr<gtpu_teid_pool> mbs_f1u_teid_pool;
  std::unique_ptr<f1u_mbs_router> mbs_f1u_router;
  gtpu_demux_ctrl*                mbs_demux_ctrl = nullptr;
  task_executor*                  mbs_f1u_exec   = nullptr;
  std::map<std::string, mbs_f1u_distribution_context> mbs_f1u_distribution_contexts;
  std::map<std::string, std::unique_ptr<f1u_mbs_demux_session>> mbs_f1u_demux_sessions;
  /// N3mb shared multicast ingress receivers, keyed by distribution context key, used when the F1-U traffic source
  /// is an NG-U multicast group (integrated gNB ingesting the MB-UPF stream directly).
  std::map<std::string, std::unique_ptr<mbs_ngu_receiver>> mbs_ngu_receivers;

  std::unique_ptr<f1ap_event_manager> events;

  std::unique_ptr<f1ap_message_notifier> tx_pdu_notifier;

  f1ap_metrics_collector_impl metrics;
};

} // namespace odu
} // namespace ocudu
