// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#include "lib/mac/mac_dl/mbs_mrb_bearer.h"
#include "ocudu/support/executors/manual_task_worker.h"
#include "ocudu/support/timers.h"
#include "gtest/gtest.h"

using namespace ocudu;

TEST(mbs_mrb_bearer_test, pulls_complete_sdu_when_grant_is_large_enough)
{
  mbs_mrb_bearer bearer;
  bearer.push_sdu(byte_buffer::create({0x01, 0x02, 0x03}).value());

  EXPECT_EQ(bearer.pending(), 3);
  byte_buffer pdu = bearer.pull_pdu(8);

  EXPECT_EQ(pdu, byte_buffer::create({0x01, 0x02, 0x03}).value());
  EXPECT_EQ(bearer.pending(), 0);
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, segments_large_sdu_across_multiple_mtch_grants)
{
  mbs_mrb_bearer bearer;
  bearer.push_sdu(byte_buffer::create({0x10, 0x11, 0x12, 0x13, 0x14}).value());

  byte_buffer first = bearer.pull_pdu(2);
  EXPECT_EQ(first, byte_buffer::create({0x10, 0x11}).value());
  EXPECT_EQ(bearer.pending(), 3);

  byte_buffer second = bearer.pull_pdu(2);
  EXPECT_EQ(second, byte_buffer::create({0x12, 0x13}).value());
  EXPECT_EQ(bearer.pending(), 1);

  byte_buffer third = bearer.pull_pdu(2);
  EXPECT_EQ(third, byte_buffer::create({0x14}).value());
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, preserves_byte_order_across_sdu_boundaries)
{
  mbs_mrb_bearer bearer;
  bearer.push_sdu(byte_buffer::create({0x21, 0x22}).value());
  bearer.push_sdu(byte_buffer::create({0x31}).value());

  EXPECT_EQ(bearer.pull_pdu(8), byte_buffer::create({0x21, 0x22, 0x31}).value());
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, fills_grant_with_multiple_queued_sdus)
{
  mbs_mrb_bearer bearer;
  bearer.push_sdu(byte_buffer::create({0x01, 0x02}).value());
  bearer.push_sdu(byte_buffer::create({0x03, 0x04, 0x05}).value());
  bearer.push_sdu(byte_buffer::create({0x06}).value());

  EXPECT_EQ(bearer.pull_pdu(5), byte_buffer::create({0x01, 0x02, 0x03, 0x04, 0x05}).value());
  EXPECT_EQ(bearer.pending(), 1);
  EXPECT_EQ(bearer.pull_pdu(5), byte_buffer::create({0x06}).value());
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, segments_sdu_then_continues_with_next_sdu_in_same_grant)
{
  mbs_mrb_bearer bearer;
  bearer.push_sdu(byte_buffer::create({0x10, 0x11, 0x12}).value());
  bearer.push_sdu(byte_buffer::create({0x20, 0x21}).value());

  EXPECT_EQ(bearer.pull_pdu(4), byte_buffer::create({0x10, 0x11, 0x12, 0x20}).value());
  EXPECT_EQ(bearer.pending(), 1);
  EXPECT_EQ(bearer.pull_pdu(4), byte_buffer::create({0x21}).value());
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, rlc_backed_bearer_transmits_complete_sdu)
{
  timer_manager      timers;
  manual_task_worker pcell_worker{128};
  manual_task_worker ue_worker{128};
  mac_mbs_mrb_config cfg;
  cfg.mrb_id = 1;
  mbs_mrb_bearer bearer{1, cfg, timers, pcell_worker, ue_worker};

  bearer.push_sdu(byte_buffer::create({0x41, 0x42, 0x43}).value());
  pcell_worker.run_pending_tasks();

  EXPECT_EQ(bearer.pending(), 6);
  EXPECT_EQ(bearer.pull_pdu(8), byte_buffer::create({0x00, 0x80, 0x00, 0x41, 0x42, 0x43}).value());
  ue_worker.run_pending_tasks();
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, rlc_backed_bearer_reports_asynchronous_buffer_state)
{
  timer_manager      timers;
  manual_task_worker pcell_worker{128};
  manual_task_worker ue_worker{128};
  mac_mbs_mrb_config cfg;
  cfg.mrb_id = 1;
  std::vector<unsigned> reported_pending_bytes;
  mbs_mrb_bearer bearer{
      1,
      cfg,
      timers,
      pcell_worker,
      ue_worker,
      [&reported_pending_bytes](const rlc_buffer_state& bs) { reported_pending_bytes.push_back(bs.pending_bytes); }};

  bearer.push_sdu(byte_buffer::create({0x41, 0x42, 0x43}).value());
  pcell_worker.run_pending_tasks();

  ASSERT_FALSE(reported_pending_bytes.empty());
  EXPECT_EQ(reported_pending_bytes.back(), 6);
}

TEST(mbs_mrb_bearer_test, rlc_backed_bearer_segments_sdu_across_mtch_grants)
{
  timer_manager      timers;
  manual_task_worker pcell_worker{128};
  manual_task_worker ue_worker{128};
  mac_mbs_mrb_config cfg;
  cfg.mrb_id = 1;
  mbs_mrb_bearer bearer{1, cfg, timers, pcell_worker, ue_worker};

  bearer.push_sdu(byte_buffer::create({0x41, 0x42, 0x43, 0x44}).value());
  pcell_worker.run_pending_tasks();

  EXPECT_EQ(bearer.pending(), 7);
  EXPECT_EQ(bearer.pull_pdu(4), byte_buffer::create({0x40, 0x80, 0x00, 0x41}).value());
  EXPECT_EQ(bearer.pending(), 6);
  EXPECT_EQ(bearer.pull_pdu(6), byte_buffer::create({0x80, 0x00, 0x03, 0x42, 0x43, 0x44}).value());
  ue_worker.run_pending_tasks();
  EXPECT_TRUE(bearer.empty());
}

TEST(mbs_mrb_bearer_test, rlc_backed_bearer_uses_configured_18bit_pdcp_sn)
{
  timer_manager      timers;
  manual_task_worker pcell_worker{128};
  manual_task_worker ue_worker{128};
  mac_mbs_mrb_config cfg;
  cfg.mrb_id          = 1;
  cfg.pdcp_sn_size_dl = pdcp_sn_size::size18bits;
  mbs_mrb_bearer bearer{1, cfg, timers, pcell_worker, ue_worker};

  bearer.push_sdu(byte_buffer::create({0x51, 0x52}).value());
  pcell_worker.run_pending_tasks();

  EXPECT_EQ(bearer.pending(), 6);
  EXPECT_EQ(bearer.pull_pdu(8), byte_buffer::create({0x00, 0x80, 0x00, 0x00, 0x51, 0x52}).value());
  ue_worker.run_pending_tasks();
  EXPECT_TRUE(bearer.empty());
}
