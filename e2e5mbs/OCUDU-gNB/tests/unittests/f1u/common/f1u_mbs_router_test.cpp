// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "ocudu/f1u/mbs/f1u_mbs_router.h"
#include "ocudu/gtpu/gtpu_teid_pool_factory.h"
#include "gtest/gtest.h"

using namespace ocudu;

static std::unique_ptr<gtpu_teid_pool> make_teid_pool(unsigned nof_teids = 8)
{
  gtpu_allocator_creation_request request;
  request.max_nof_teids = nof_teids;
  return create_gtpu_allocator(request);
}

static up_transport_layer_info make_peer_tunnel()
{
  return up_transport_layer_info(transport_layer_address::create_from_string("239.0.0.4"), int_to_gtpu_teid(0x1234));
}

TEST(f1u_mbs_router_test, binds_context_and_allocates_local_teid)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 1};
  auto result = router.bind_context(id, "10.33.33.10", make_peer_tunnel(), [](nru_dl_message) {});

  ASSERT_EQ(result, f1u_mbs_bind_result::success);
  ASSERT_EQ(router.nof_contexts(), 1);

  std::optional<f1u_mbs_tunnel_info> tunnel = router.get_tunnel_info(id);
  ASSERT_TRUE(tunnel.has_value());
  EXPECT_EQ(tunnel->local.tp_address.to_string(), "10.33.33.10");
  EXPECT_NE(tunnel->local.gtp_teid, int_to_gtpu_teid(0));
  EXPECT_EQ(tunnel->peer.tp_address.to_string(), "239.0.0.4");
  EXPECT_EQ(tunnel->peer.gtp_teid, int_to_gtpu_teid(0x1234));
}

TEST(f1u_mbs_router_test, rejects_duplicate_and_invalid_contexts)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 1};
  ASSERT_EQ(router.bind_context(id, "10.33.33.10", make_peer_tunnel(), [](nru_dl_message) {}),
            f1u_mbs_bind_result::success);
  EXPECT_EQ(router.bind_context(id, "10.33.33.10", make_peer_tunnel(), [](nru_dl_message) {}),
            f1u_mbs_bind_result::duplicate_context);
  EXPECT_EQ(router.bind_context({"", 1}, "10.33.33.10", make_peer_tunnel(), [](nru_dl_message) {}),
            f1u_mbs_bind_result::invalid_context);
  EXPECT_EQ(router.bind_context({"ctx-b", 0}, "10.33.33.10", make_peer_tunnel(), [](nru_dl_message) {}),
            f1u_mbs_bind_result::invalid_context);
  EXPECT_EQ(router.bind_context({"ctx-b", 2}, "", make_peer_tunnel(), [](nru_dl_message) {}),
            f1u_mbs_bind_result::invalid_context);
  EXPECT_EQ(router.bind_context({"ctx-b", 2}, "10.33.33.10", make_peer_tunnel(), {}),
            f1u_mbs_bind_result::invalid_context);
}

TEST(f1u_mbs_router_test, routes_non_empty_dl_pdu_to_bound_context)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  unsigned           nof_pdus = 0;
  byte_buffer        last_pdu;
  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 7};
  ASSERT_EQ(router.bind_context(id,
                                "10.33.33.10",
                                make_peer_tunnel(),
                                [&nof_pdus, &last_pdu](nru_dl_message msg) {
                                  ++nof_pdus;
                                  last_pdu = std::move(msg.t_pdu);
                                }),
            f1u_mbs_bind_result::success);

  nru_dl_message msg;
  msg.t_pdu = byte_buffer::create({0xde, 0xad, 0xbe, 0xef}).value();
  EXPECT_TRUE(router.route_dl_pdu(id, std::move(msg)));
  EXPECT_EQ(nof_pdus, 1);
  EXPECT_EQ(last_pdu, byte_buffer::create({0xde, 0xad, 0xbe, 0xef}).value());
}

TEST(f1u_mbs_router_test, routes_non_empty_dl_pdu_by_local_teid)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  unsigned           nof_pdus = 0;
  byte_buffer        last_pdu;
  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 7};
  ASSERT_EQ(router.bind_context(id,
                                "10.33.33.10",
                                make_peer_tunnel(),
                                [&nof_pdus, &last_pdu](nru_dl_message msg) {
                                  ++nof_pdus;
                                  last_pdu = std::move(msg.t_pdu);
                                }),
            f1u_mbs_bind_result::success);

  std::optional<f1u_mbs_tunnel_info> tunnel = router.get_tunnel_info(id);
  ASSERT_TRUE(tunnel.has_value());
  EXPECT_EQ(router.find_context(tunnel->local.gtp_teid), id);

  nru_dl_message msg;
  msg.t_pdu = byte_buffer::create({0xca, 0xfe}).value();
  EXPECT_TRUE(router.route_dl_pdu(tunnel->local.gtp_teid, std::move(msg)));
  EXPECT_EQ(nof_pdus, 1);
  EXPECT_EQ(last_pdu, byte_buffer::create({0xca, 0xfe}).value());
}

TEST(f1u_mbs_router_test, rejects_unbound_or_empty_dl_pdu)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  unsigned           nof_pdus = 0;
  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 1};
  ASSERT_EQ(router.bind_context(id, "10.33.33.10", make_peer_tunnel(), [&nof_pdus](nru_dl_message) { ++nof_pdus; }),
            f1u_mbs_bind_result::success);

  nru_dl_message empty;
  EXPECT_FALSE(router.route_dl_pdu(id, std::move(empty)));
  EXPECT_EQ(nof_pdus, 0);

  nru_dl_message msg;
  msg.t_pdu = byte_buffer::create({0x01}).value();
  EXPECT_FALSE(router.route_dl_pdu({"unknown", 1}, std::move(msg)));
  EXPECT_EQ(nof_pdus, 0);
}

TEST(f1u_mbs_router_test, unbind_removes_local_teid_route)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  unsigned           nof_pdus = 0;
  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 1};
  ASSERT_EQ(router.bind_context(id, "10.33.33.10", make_peer_tunnel(), [&nof_pdus](nru_dl_message) { ++nof_pdus; }),
            f1u_mbs_bind_result::success);

  std::optional<f1u_mbs_tunnel_info> tunnel = router.get_tunnel_info(id);
  ASSERT_TRUE(tunnel.has_value());
  gtpu_teid_t local_teid = tunnel->local.gtp_teid;

  ASSERT_TRUE(router.unbind_context(id));
  EXPECT_FALSE(router.find_context(local_teid).has_value());

  nru_dl_message msg;
  msg.t_pdu = byte_buffer::create({0x01}).value();
  EXPECT_FALSE(router.route_dl_pdu(local_teid, std::move(msg)));
  EXPECT_EQ(nof_pdus, 0);
}

TEST(f1u_mbs_router_test, unbind_releases_context)
{
  auto            pool = make_teid_pool();
  f1u_mbs_router router(*pool);

  f1u_mbs_context_id id{.multicast_f1u_context_ref = "ctx-a", .mrb_id = 1};
  ASSERT_EQ(router.bind_context(id, "10.33.33.10", make_peer_tunnel(), [](nru_dl_message) {}),
            f1u_mbs_bind_result::success);
  EXPECT_TRUE(router.unbind_context(id));
  EXPECT_FALSE(router.unbind_context(id));
  EXPECT_EQ(router.nof_contexts(), 0);
  EXPECT_FALSE(router.get_tunnel_info(id).has_value());
}
