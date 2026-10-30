// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "../common/test_helpers.h"
#include "e1ap_cu_up_test_helpers.h"
#include "ocudu/asn1/e1ap/common.h"
#include "ocudu/asn1/e1ap/e1ap_pdu_contents.h"
#include <gtest/gtest.h>

using namespace ocudu;
using namespace ocuup;
using namespace asn1::e1ap;

//////////////////////////////////////////////////////////////////////////////////////
/* Handling of unsupported messages                                                 */
//////////////////////////////////////////////////////////////////////////////////////

TEST_F(e1ap_cu_up_test, when_unsupported_init_msg_received_then_message_ignored)
{
  // Set last message of PDU notifier to successful outcome.
  e1ap_gw.last_tx_e1ap_pdu.pdu.set_init_msg();

  // Generate unupported E1AP PDU.
  e1ap_message unsupported_msg = {};
  unsupported_msg.pdu.set_init_msg();

  e1ap->handle_message(unsupported_msg);

  // Check that PDU has not been forwarded.
  EXPECT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.type(), asn1::e1ap::e1ap_pdu_c::types_opts::options::init_msg);
}

TEST_F(e1ap_cu_up_test, when_unsupported_successful_outcome_received_then_message_ignored)
{
  // Set last message of PDU notifier to init_msg.
  e1ap_gw.last_tx_e1ap_pdu.pdu.set_init_msg();

  // Generate unupported E1AP PDU.
  e1ap_message unsupported_msg = {};
  unsupported_msg.pdu.set_successful_outcome();

  e1ap->handle_message(unsupported_msg);

  // Check that PDU has not been forwarded.
  EXPECT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.type(), asn1::e1ap::e1ap_pdu_c::types_opts::options::init_msg);
}

TEST_F(e1ap_cu_up_test, when_unsupported_unsuccessful_outcome_received_then_message_ignored)
{
  // Set last message of PDU notifier to init_msg.
  e1ap_gw.last_tx_e1ap_pdu.pdu.set_init_msg();

  // Generate unupported E1AP PDU.
  e1ap_message unsupported_msg = {};
  unsupported_msg.pdu.set_unsuccessful_outcome();

  e1ap->handle_message(unsupported_msg);

  // Check that PDU has not been forwarded.
  EXPECT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.type(), asn1::e1ap::e1ap_pdu_c::types_opts::options::init_msg);
}

//////////////////////////////////////////////////////////////////////////////////////
/* Handling of E1 Setup                                                             */
//////////////////////////////////////////////////////////////////////////////////////

/// Test the successful CU-UP initiated E1 setup
TEST_F(e1ap_cu_up_test, when_cu_up_started_then_e1_setup_request_sent)
{
  run_e1_setup_procedure();

  // Check the generated PDU is indeed the E1 Setup Request.
  ASSERT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.type(), asn1::e1ap::e1ap_pdu_c::types_opts::options::init_msg);
  ASSERT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.init_msg().value.type(),
            asn1::e1ap::e1ap_elem_procs_o::init_msg_c::types_opts::options::gnb_cu_up_e1_setup_request);
}

//////////////////////////////////////////////////////////////////////////////////////
/* Handling of Bearer Context Messages                                              */
//////////////////////////////////////////////////////////////////////////////////////

static e1ap_message generate_mbs_bearer_context_setup_request()
{
  e1ap_message msg;
  msg.pdu.set_init_msg().load_info_obj(ASN1_E1AP_ID_MC_BEARER_CONTEXT_SETUP);
  auto& req = msg.pdu.init_msg().value.mc_bearer_context_setup_request();

  req->gnb_cu_cp_mbs_e1ap_id = 7;
  req->global_mbs_session_id.tmgi.from_string("000102030405");
  req->mc_bearer_context_to_setup.snssai.sst.from_number(1);
  req->mc_bearer_context_to_setup.ie_exts_present                    = true;
  req->mc_bearer_context_to_setup.ie_exts.mbs_area_session_id_present = true;
  req->mc_bearer_context_to_setup.ie_exts.mbs_area_session_id         = 3;

  mcmrb_setup_cfg_item_s mrb;
  mrb.mrb_id                       = 1;
  mrb.mbs_pdcp_cfg.pdcp_sn_size_ul = pdcp_sn_size_e::s_neg18;
  mrb.mbs_pdcp_cfg.pdcp_sn_size_dl = pdcp_sn_size_e::s_neg18;
  mrb.mbs_pdcp_cfg.rlc_mode        = rlc_mode_opts::rlc_um_unidirectional_dl;
  qos_flow_qos_param_item_s qos;
  qos.qos_flow_id = 9;
  qos.qos_flow_level_qos_params.qos_characteristics.set_non_dyn_5qi().five_qi = 9;
  qos.qos_flow_level_qos_params.ngra_nalloc_retention_prio.prio_level         = 1;
  qos.qos_flow_level_qos_params.ngra_nalloc_retention_prio.pre_emption_cap.value =
      pre_emption_cap_opts::shall_not_trigger_pre_emption;
  qos.qos_flow_level_qos_params.ngra_nalloc_retention_prio.pre_emption_vulnerability.value =
      pre_emption_vulnerability_opts::not_pre_emptable;
  mrb.qos_flow_qos_param_list.push_back(qos);
  req->mc_bearer_context_to_setup.mc_mrb_to_setup_list.push_back(mrb);

  return msg;
}

static e1ap_message generate_mbs_bearer_context_release_command()
{
  e1ap_message msg;
  msg.pdu.set_init_msg().load_info_obj(ASN1_E1AP_ID_MC_BEARER_CONTEXT_RELEASE);
  auto& cmd = msg.pdu.init_msg().value.mc_bearer_context_release_cmd();
  cmd->gnb_cu_cp_mbs_e1ap_id = 7;
  cmd->gnb_cu_up_mbs_e1ap_id = 1;
  cmd->cause.set_radio_network() = cause_radio_network_opts::normal_release;
  return msg;
}

static e1ap_message generate_mbs_bearer_context_modification_request()
{
  e1ap_message msg;
  msg.pdu.set_init_msg().load_info_obj(ASN1_E1AP_ID_MC_BEARER_CONTEXT_MOD);
  auto& req = msg.pdu.init_msg().value.mc_bearer_context_mod_request();

  req->gnb_cu_cp_mbs_e1ap_id = 7;
  req->gnb_cu_up_mbs_e1ap_id = 1;

  mcmrb_setup_modify_cfg_item_s mrb;
  mrb.mrb_id                       = 1;
  mrb.mbs_pdcp_cfg_present         = true;
  mrb.mbs_pdcp_cfg.pdcp_sn_size_ul = pdcp_sn_size_e::s_neg18;
  mrb.mbs_pdcp_cfg.pdcp_sn_size_dl = pdcp_sn_size_e::s_neg18;
  mrb.mbs_pdcp_cfg.rlc_mode        = rlc_mode_opts::rlc_um_unidirectional_dl;
  qos_flow_qos_param_item_s qos;
  qos.qos_flow_id = 10;
  qos.qos_flow_level_qos_params.qos_characteristics.set_non_dyn_5qi().five_qi = 9;
  qos.qos_flow_level_qos_params.ngra_nalloc_retention_prio.prio_level         = 1;
  qos.qos_flow_level_qos_params.ngra_nalloc_retention_prio.pre_emption_cap.value =
      pre_emption_cap_opts::shall_not_trigger_pre_emption;
  qos.qos_flow_level_qos_params.ngra_nalloc_retention_prio.pre_emption_vulnerability.value =
      pre_emption_vulnerability_opts::not_pre_emptable;
  mrb.qos_flow_qos_param_list.push_back(qos);
  req->mc_bearer_context_to_modify.mc_mrb_to_setup_modify_list.push_back(mrb);

  return msg;
}

TEST_F(e1ap_cu_up_test, when_valid_mbs_bearer_context_setup_received_then_mbs_response_is_sent)
{
  run_e1_setup_procedure();

  e1ap->handle_message(generate_mbs_bearer_context_setup_request());

  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_setup_request.cu_cp_mbs_e1ap_id, 7);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_setup_request.area_session_id.value(), 3);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_setup_request.mrb_to_setup_list.size(), 1);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_setup_request.mrb_to_setup_list.front().qos_flow_ids.front(), 9);

  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::mc_bearer_context_setup_resp,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
  ASSERT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome()
                .value.mc_bearer_context_setup_resp()
                ->mc_bearer_context_to_setup_resp.mc_mrb_setup_resp_list[0]
                .qosflow_setup[0]
                .qos_flow_id,
            9);
}

TEST_F(e1ap_cu_up_test, when_valid_mbs_bearer_context_modification_received_then_mbs_response_is_sent)
{
  run_e1_setup_procedure();

  e1ap->handle_message(generate_mbs_bearer_context_modification_request());

  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_modification_request.cu_cp_mbs_e1ap_id, 7);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_modification_request.cu_up_mbs_e1ap_id, 1);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_modification_request.mrb_to_setup_modify_list.size(), 1);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_modification_request.mrb_to_setup_modify_list.front()
                .qos_flow_ids.front(),
            10);

  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::mc_bearer_context_mod_resp,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
  ASSERT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome()
                .value.mc_bearer_context_mod_resp()
                ->mc_bearer_context_to_modify_resp.mc_mrb_modify_setup_resp_list[0]
                .qosflow_setup[0]
                .qos_flow_id,
            10);
}

TEST_F(e1ap_cu_up_test, when_valid_mbs_bearer_context_release_received_then_release_complete_is_sent)
{
  run_e1_setup_procedure();

  e1ap->handle_message(generate_mbs_bearer_context_release_command());

  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_release_command.cu_cp_mbs_e1ap_id, 7);
  ASSERT_EQ(cu_up_notifier.last_mbs_bearer_context_release_command.cu_up_mbs_e1ap_id, 1);
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::mc_bearer_context_release_complete,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
  ASSERT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome()
                .value.mc_bearer_context_release_complete()
                ->gnb_cu_up_mbs_e1ap_id,
            1);
}

TEST_F(e1ap_cu_up_test, when_valid_bearer_context_setup_received_then_bearer_context_setup_response_is_sent)
{
  run_e1_setup_procedure();

  test_logger.info("TEST: Receive BearerContextSetupRequest message...");

  // Receive BearerContextSetupRequest message.
  e1ap_message bearer_context_setup = generate_bearer_context_setup_request(9);
  e1ap->handle_message(bearer_context_setup);

  // Check if BearerContextSetupRequest was forwarded to UE manager.
  ASSERT_EQ(cu_up_notifier.last_bearer_context_setup_request.serving_plmn, "20899"); // this is the human readable PLMN.

  // Check the generated PDU is indeed the Bearer Context Setup response.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::bearer_context_setup_resp,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());

  ASSERT_EQ(e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome()
                .value.bearer_context_setup_resp()
                ->sys_bearer_context_setup_resp.ng_ran_bearer_context_setup_resp()
                .pdu_session_res_setup_list[0]
                .drb_setup_list_ng_ran.size(),
            1);
}

TEST_F(e1ap_cu_up_test, when_invalid_bearer_context_setup_received_then_bearer_context_setup_failure_is_sent)
{
  run_e1_setup_procedure();

  test_logger.info("TEST: Receive BearerContextSetupRequest message...");

  // Receive BearerContextSetupRequest message.
  e1ap_message bearer_context_setup = generate_invalid_bearer_context_setup_request(9);
  e1ap->handle_message(bearer_context_setup);

  // Check the generated PDU is indeed the Bearer Context Setup Failure.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::unsuccessful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::options::bearer_context_setup_fail,
            e1ap_gw.last_tx_e1ap_pdu.pdu.unsuccessful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test,
       when_invalid_bearer_context_setup_inactivity_timer_received_then_bearer_context_setup_failure_is_sent)
{
  run_e1_setup_procedure();

  test_logger.info("TEST: Receive BearerContextSetupRequest message...");

  // Receive BearerContextSetupRequest message.
  e1ap_message bearer_context_setup = generate_invalid_bearer_context_setup_request_inactivity_timer(9);
  e1ap->handle_message(bearer_context_setup);

  // Check the generated PDU is indeed the Bearer Context Setup Failure.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::unsuccessful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::options::bearer_context_setup_fail,
            e1ap_gw.last_tx_e1ap_pdu.pdu.unsuccessful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test,
       when_valid_bearer_context_modification_received_then_bearer_context_modification_response_is_sent)
{
  run_e1_setup_procedure();

  // Setup Bearer Context.
  this->setup_bearer(9);

  test_logger.info("TEST: Receive BearerContextModificationRequest message...");

  // Receive BearerContextModificationRequest.
  e1ap_message bearer_context_modification = generate_bearer_context_modification_request(9, 0);
  e1ap->handle_message(bearer_context_modification);

  // Check the generated PDU is indeed the Bearer Context Modification Response.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::bearer_context_mod_resp,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test,
       when_invalid_bearer_context_modification_received_then_bearer_context_modification_failure_is_sent)
{
  run_e1_setup_procedure();

  // Setup Bearer Context.
  this->setup_bearer(9);

  test_logger.info("TEST: Receive BearerContextModificationRequest message...");

  // Receive BearerContextModificationRequest message
  e1ap_message bearer_context_modification = generate_invalid_bearer_context_modification_request(9, 0);
  e1ap->handle_message(bearer_context_modification);

  // Check the generated PDU is indeed the Bearer Context Modification Failure
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::unsuccessful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::options::bearer_context_mod_fail,
            e1ap_gw.last_tx_e1ap_pdu.pdu.unsuccessful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test, when_valid_bearer_context_release_command_received_then_bearer_context_release_complete_is_sent)
{
  run_e1_setup_procedure();

  // Setup Bearer Context.
  this->setup_bearer(9);

  test_logger.info("TEST: Receive BearerContextReleaseCommand message...");

  // Receive BearerContextReleaseCommand.
  e1ap_message bearer_context_release_cmd = generate_bearer_context_release_command(9, 0);
  e1ap->handle_message(bearer_context_release_cmd);

  // Check the generated PDU is indeed the Bearer Context Modification Response.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::bearer_context_release_complete,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test, when_valid_full_e1_reset_received_then_reset_ack_sent)
{
  run_e1_setup_procedure();

  // Setup Bearer Context.
  this->setup_bearer(9);

  // Receive E1 Reset message.
  e1ap_message e1_reset = generate_e1_reset({});
  e1ap->handle_message(e1_reset);

  // Check the generated PDU is indeed the E1 Reset ACK.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::reset_ack,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test, when_valid_part_of_e1_reset_received_then_reset_ack_sent)
{
  run_e1_setup_procedure();

  // Setup Bearer Context.
  this->setup_bearer(9);

  // Receive E1 Reset message.
  e1ap_message e1_reset = generate_e1_reset({{gnb_cu_cp_ue_e1ap_id_t{0}, gnb_cu_up_ue_e1ap_id_t{0}}});
  e1ap->handle_message(e1_reset);

  // Check the generated PDU is indeed the E1 Reset ACK.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::successful_outcome, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::successful_outcome_c::types_opts::options::reset_ack,
            e1ap_gw.last_tx_e1ap_pdu.pdu.successful_outcome().value.type());
}

TEST_F(e1ap_cu_up_test, when_bearer_context_valid_and_resume_requested_dl_status_notification_sent)
{
  run_e1_setup_procedure();

  // Setup Bearer Context.
  this->setup_bearer(9);

  // Receive E1 Reset message.
  cu_up_ue_index_t ue_index{0};
  e1ap->handle_dl_data_notification_required(ue_index);

  // Check the generated PDU is indeed the DL Data Notification.
  ASSERT_EQ(asn1::e1ap::e1ap_pdu_c::types_opts::options::init_msg, e1ap_gw.last_tx_e1ap_pdu.pdu.type());
  ASSERT_EQ(asn1::e1ap::e1ap_elem_procs_o::init_msg_c::types_opts::options::dl_data_notif,
            e1ap_gw.last_tx_e1ap_pdu.pdu.init_msg().value.type());
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
