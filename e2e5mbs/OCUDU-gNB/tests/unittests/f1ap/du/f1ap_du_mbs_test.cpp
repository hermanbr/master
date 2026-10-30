// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "f1ap_du_test_helpers.h"
#include "ocudu/asn1/f1ap/common.h"
#include "ocudu/asn1/f1ap/f1ap_pdu_contents.h"
#include "ocudu/ran/up_transport_layer_info.h"
#include <algorithm>
#include <gtest/gtest.h>

using namespace ocudu;
using namespace odu;
using namespace asn1::f1ap;

static qos_flow_level_qos_params_s make_default_mbs_qos_flow_params()
{
  qos_flow_level_qos_params_s qos_params;

  qos_params.qos_characteristics.set_non_dyn_5qi().five_qi = 9;
  qos_params.ngra_nalloc_retention_prio.prio_level          = 1;
  qos_params.ngra_nalloc_retention_prio.pre_emption_cap.value =
      pre_emption_cap_opts::shall_not_trigger_pre_emption;
  qos_params.ngra_nalloc_retention_prio.pre_emption_vulnerability.value =
      pre_emption_vulnerability_opts::not_pre_emptable;

  return qos_params;
}

static multicast_m_rbs_to_be_setup_item_s make_mbs_mrb_setup_item(uint16_t mrb_id, uint8_t qos_flow_id)
{
  multicast_m_rbs_to_be_setup_item_s mrb_item;
  mrb_item.mrb_id                       = mrb_id;
  mrb_item.mrb_qos_info                 = make_default_mbs_qos_flow_params();
  mrb_item.mbs_flows_mapped_to_mrb_list = mbs_flows_mapped_to_mrb_list_l(1);
  mrb_item.mbs_dl_pdcp_sn_len.value     = pdcp_sn_len_opts::twelve_bits;

  mbs_flows_mapped_to_mrb_item_s flow_item;
  flow_item.mbs_qos_flow_id               = qos_flow_id;
  flow_item.mbs_qos_flow_level_qos_params = make_default_mbs_qos_flow_params();
  mrb_item.mbs_flows_mapped_to_mrb_list[0] = flow_item;
  return mrb_item;
}

static multicast_m_rbs_to_be_setup_mod_item_s make_mbs_mrb_setup_mod_item(uint16_t mrb_id, uint8_t qos_flow_id)
{
  multicast_m_rbs_to_be_setup_mod_item_s mrb_item;
  mrb_item.mrb_id                       = mrb_id;
  mrb_item.mrb_qos_info                 = make_default_mbs_qos_flow_params();
  mrb_item.mbs_flows_mapped_to_mrb_list = mbs_flows_mapped_to_mrb_list_l(1);
  mrb_item.mbs_dl_pdcp_sn_len.value     = pdcp_sn_len_opts::eighteen_bits;

  mbs_flows_mapped_to_mrb_item_s flow_item;
  flow_item.mbs_qos_flow_id               = qos_flow_id;
  flow_item.mbs_qos_flow_level_qos_params = make_default_mbs_qos_flow_params();
  mrb_item.mbs_flows_mapped_to_mrb_list[0] = flow_item;
  return mrb_item;
}

static f1ap_message generate_multicast_context_setup_request(uint64_t cu_mbs_f1ap_id)
{
  f1ap_message msg;
  msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_CONTEXT_SETUP);

  auto& setup_req                  = msg.pdu.init_msg().value.multicast_context_setup_request();
  setup_req->gnb_cu_mbs_f1ap_id   = cu_mbs_f1ap_id;
  setup_req->mbs_session_id.tmgi.from_string("000102030405");
  setup_req->snssai.sst.from_string("01");
  setup_req->multicast_m_rbs_to_be_setup_list.resize(1);
  setup_req->multicast_m_rbs_to_be_setup_list[0].load_info_obj(ASN1_F1AP_ID_MULTICAST_M_RBS_TO_BE_SETUP_ITEM);
  setup_req->multicast_m_rbs_to_be_setup_list[0]->multicast_m_rbs_to_be_setup_item() =
      make_mbs_mrb_setup_item(1, 9);

  return msg;
}

static f1ap_message generate_multicast_context_modification_request(uint64_t cu_mbs_f1ap_id, uint64_t du_mbs_f1ap_id)
{
  f1ap_message msg;
  msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_CONTEXT_MOD);

  auto& mod_req                                      = msg.pdu.init_msg().value.multicast_context_mod_request();
  mod_req->gnb_cu_mbs_f1ap_id                       = cu_mbs_f1ap_id;
  mod_req->gnb_du_mbs_f1ap_id                       = du_mbs_f1ap_id;
  mod_req->multicast_m_rbs_to_be_setup_mod_list_present = true;
  mod_req->multicast_m_rbs_to_be_setup_mod_list.resize(1);
  mod_req->multicast_m_rbs_to_be_setup_mod_list[0].load_info_obj(
      ASN1_F1AP_ID_MULTICAST_M_RBS_TO_BE_SETUP_MOD_ITEM);
  mod_req->multicast_m_rbs_to_be_setup_mod_list[0]->multicast_m_rbs_to_be_setup_mod_item() =
      make_mbs_mrb_setup_mod_item(2, 10);

  return msg;
}

static f1ap_message generate_multicast_distribution_setup_request(uint64_t cu_mbs_f1ap_id,
                                                                  uint64_t du_mbs_f1ap_id,
                                                                  uint16_t mrb_id = 1)
{
  f1ap_message msg;
  msg.pdu.set_init_msg().load_info_obj(ASN1_F1AP_ID_MULTICAST_DISTRIBUTION_SETUP);

  auto& setup_req = msg.pdu.init_msg().value.multicast_distribution_setup_request();
  setup_req->gnb_cu_mbs_f1ap_id = cu_mbs_f1ap_id;
  setup_req->gnb_du_mbs_f1ap_id = du_mbs_f1ap_id;
  setup_req->mbs_multicast_f1_u_context_descriptor.multicast_f1_u_context_ref_f1.from_string("01020304");
  setup_req->mbs_multicast_f1_u_context_descriptor.mc_f1_u_ctxtusage.value =
      mbs_multicast_f1_u_context_descriptor_s::mc_f1_u_ctxtusage_opts::ptm;
  setup_req->multicast_f1_u_context_to_be_setup_list.resize(1);
  setup_req->multicast_f1_u_context_to_be_setup_list[0].load_info_obj(
      ASN1_F1AP_ID_MULTICAST_F1_U_CONTEXT_TO_BE_SETUP_ITEM);

  auto& item = setup_req->multicast_f1_u_context_to_be_setup_list[0]->multicast_f1_u_context_to_be_setup_item();
  item.mrb_id = mrb_id;
  up_transport_layer_info_to_asn1(item.mbs_f1u_info_at_du,
                                  up_transport_layer_info{transport_layer_address::create_from_string("127.0.20.2"),
                                                          int_to_gtpu_teid(0x12345678)});
  return msg;
}

TEST_F(f1ap_du_test, when_mbs_multicast_context_modification_received_then_du_updates_context_and_replies)
{
  run_f1_setup_procedure();

  f1ap_message setup_request = generate_multicast_context_setup_request(7);
  f1ap->handle_message(setup_request);

  ASSERT_TRUE(f1ap_du_cfg_handler.last_mbs_context_setup.has_value());
  const auto& du_setup_request = *f1ap_du_cfg_handler.last_mbs_context_setup;
  ASSERT_EQ(du_setup_request.mrb_configs.size(), 1U);
  EXPECT_EQ(du_setup_request.mrb_configs[0].mrb_id, 1U);
  EXPECT_EQ(du_setup_request.mrb_configs[0].pdcp_sn_size_dl, pdcp_sn_size::size12bits);

  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.type().value, f1ap_pdu_c::types_opts::successful_outcome);
  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.successful_outcome().value.type().value,
            f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_context_setup_resp);

  const auto& setup_response = f1c_gw.last_tx_pdu().pdu.successful_outcome().value.multicast_context_setup_resp();
  ASSERT_EQ(setup_response->gnb_cu_mbs_f1ap_id, 7U);
  const uint64_t du_mbs_f1ap_id = setup_response->gnb_du_mbs_f1ap_id;

  f1ap_message mod_request = generate_multicast_context_modification_request(7, du_mbs_f1ap_id);
  f1ap->handle_message(mod_request);

  ASSERT_TRUE(f1ap_du_cfg_handler.last_mbs_context_modification.has_value());
  const auto& du_mod_request = *f1ap_du_cfg_handler.last_mbs_context_modification;
  EXPECT_EQ(du_mod_request.gnb_cu_mbs_f1ap_id, 7U);
  EXPECT_EQ(du_mod_request.gnb_du_mbs_f1ap_id, du_mbs_f1ap_id);
  EXPECT_NE(std::find(du_mod_request.mrb_ids.begin(), du_mod_request.mrb_ids.end(), 1U),
            du_mod_request.mrb_ids.end());
  EXPECT_NE(std::find(du_mod_request.mrb_ids.begin(), du_mod_request.mrb_ids.end(), 2U),
            du_mod_request.mrb_ids.end());
  ASSERT_EQ(du_mod_request.mrb_configs.size(), 2U);
  const auto mrb2_cfg = std::find_if(du_mod_request.mrb_configs.begin(),
                                     du_mod_request.mrb_configs.end(),
                                     [](const f1ap_du_mbs_mrb_config& cfg) { return cfg.mrb_id == 2U; });
  ASSERT_NE(mrb2_cfg, du_mod_request.mrb_configs.end());
  EXPECT_EQ(mrb2_cfg->pdcp_sn_size_dl, pdcp_sn_size::size18bits);

  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.type().value, f1ap_pdu_c::types_opts::successful_outcome);
  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.successful_outcome().value.type().value,
            f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_context_mod_resp);
  const auto& mod_response = f1c_gw.last_tx_pdu().pdu.successful_outcome().value.multicast_context_mod_resp();
  EXPECT_EQ(mod_response->gnb_cu_mbs_f1ap_id, 7U);
  EXPECT_EQ(mod_response->gnb_du_mbs_f1ap_id, du_mbs_f1ap_id);
  ASSERT_TRUE(mod_response->multicast_m_rbs_setup_mod_list_present);
  ASSERT_EQ(mod_response->multicast_m_rbs_setup_mod_list.size(), 1U);
  EXPECT_EQ(mod_response->multicast_m_rbs_setup_mod_list[0]->multicast_m_rbs_setup_mod_item().mrb_id, 2U);
}

TEST_F(f1ap_du_test, when_mbs_multicast_distribution_setup_received_then_du_returns_local_f1u_tunnel)
{
  run_f1_setup_procedure();

  f1ap_message setup_request = generate_multicast_context_setup_request(7);
  f1ap->handle_message(setup_request);
  const auto& setup_response = f1c_gw.last_tx_pdu().pdu.successful_outcome().value.multicast_context_setup_resp();
  const uint64_t du_mbs_f1ap_id = setup_response->gnb_du_mbs_f1ap_id;

  f1ap_message distribution_request = generate_multicast_distribution_setup_request(7, du_mbs_f1ap_id);
  f1ap->handle_message(distribution_request);

  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.type().value, f1ap_pdu_c::types_opts::successful_outcome);
  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.successful_outcome().value.type().value,
            f1ap_elem_procs_o::successful_outcome_c::types_opts::multicast_distribution_setup_resp);

  const auto& distribution_response =
      f1c_gw.last_tx_pdu().pdu.successful_outcome().value.multicast_distribution_setup_resp();
  ASSERT_EQ(distribution_response->multicast_f1_u_context_setup_list.size(), 1U);
  const auto& item =
      distribution_response->multicast_f1_u_context_setup_list[0]->multicast_f1_u_context_setup_item();
  EXPECT_EQ(item.mrb_id, 1U);
  const up_transport_layer_info local_tunnel = asn1_to_up_transport_layer_info(item.mbs_f1u_info_at_cu);
  EXPECT_EQ(local_tunnel.tp_address.to_string(), "127.0.10.2");
  EXPECT_NE(local_tunnel.gtp_teid, int_to_gtpu_teid(0));
}

TEST_F(f1ap_du_test, when_mbs_f1u_pdu_received_then_du_manager_gets_mbs_data_pdu)
{
  run_f1_setup_procedure();

  f1ap_message setup_request = generate_multicast_context_setup_request(7);
  f1ap->handle_message(setup_request);
  const auto& setup_response = f1c_gw.last_tx_pdu().pdu.successful_outcome().value.multicast_context_setup_resp();
  const uint64_t du_mbs_f1ap_id = setup_response->gnb_du_mbs_f1ap_id;

  f1ap_message distribution_request = generate_multicast_distribution_setup_request(7, du_mbs_f1ap_id);
  f1ap->handle_message(distribution_request);

  nru_dl_message nru;
  nru.t_pdu = byte_buffer::create({0xca, 0xfe, 0x01}).value();
  f1ap->handle_mbs_f1u_data({"01020304", 1, std::move(nru)});

  ASSERT_TRUE(f1ap_du_cfg_handler.last_mbs_data_pdu.has_value());
  EXPECT_EQ(f1ap_du_cfg_handler.last_mbs_data_pdu->session_key, "000102030405");
  EXPECT_EQ(f1ap_du_cfg_handler.last_mbs_data_pdu->mrb_id, 1U);
  EXPECT_EQ(f1ap_du_cfg_handler.last_mbs_data_pdu->pdu, byte_buffer::create({0xca, 0xfe, 0x01}).value());
}

TEST_F(f1ap_du_test, when_mbs_multicast_distribution_setup_references_unknown_mrb_then_f1ap_rejects_it)
{
  run_f1_setup_procedure();

  f1ap_message setup_request = generate_multicast_context_setup_request(7);
  f1ap->handle_message(setup_request);
  const auto& setup_response = f1c_gw.last_tx_pdu().pdu.successful_outcome().value.multicast_context_setup_resp();
  const uint64_t du_mbs_f1ap_id = setup_response->gnb_du_mbs_f1ap_id;

  f1ap_message distribution_request = generate_multicast_distribution_setup_request(7, du_mbs_f1ap_id, 9);
  f1ap->handle_message(distribution_request);

  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.type().value, f1ap_pdu_c::types_opts::unsuccessful_outcome);
  ASSERT_EQ(f1c_gw.last_tx_pdu().pdu.unsuccessful_outcome().value.type().value,
            f1ap_elem_procs_o::unsuccessful_outcome_c::types_opts::multicast_distribution_setup_fail);
  const auto& fail = f1c_gw.last_tx_pdu().pdu.unsuccessful_outcome().value.multicast_distribution_setup_fail();
  ASSERT_EQ(fail->cause.type().value, cause_c::types_opts::radio_network);
  EXPECT_EQ(fail->cause.radio_network().value, cause_radio_network_opts::unknown_or_inconsistent_mrb_id);
}
