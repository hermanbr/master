// SPDX-FileCopyrightText: Copyright (C) 2021-2026 Software Radio Systems Limited
// SPDX-License-Identifier: BSD-3-Clause-Open-MPI
// Portions of this file may implement 3GPP specifications, which may be subject to additional licensing requirements.

#pragma once

#include "ocudu/adt/byte_buffer.h"
#include "ocudu/mac/mac_mbs_information_handler.h"
#include "ocudu/pdcp/pdcp_factory.h"
#include "ocudu/pdcp/pdcp_rx.h"
#include "ocudu/pdcp/pdcp_tx.h"
#include "ocudu/pcap/rlc_pcap.h"
#include "ocudu/ran/du_types.h"
#include "ocudu/ran/gnb_du_id.h"
#include "ocudu/ran/rb_id.h"
#include "ocudu/rlc/rlc_entity.h"
#include "ocudu/rlc/rlc_factory.h"
#include "ocudu/security/security.h"
#include "ocudu/support/cpu_architecture_info.h"
#include "ocudu/support/executors/inline_task_executor.h"
#include "ocudu/support/executors/task_executor.h"
#include "ocudu/support/timers.h"
#include <algorithm>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace ocudu {

class mbs_mrb_rlc_tx_upper_notifier : public rlc_tx_upper_layer_data_notifier
{
public:
  void connect(pdcp_tx_lower_interface& pdcp_tx_) { pdcp_tx = &pdcp_tx_; }

  void on_transmitted_sdu(uint32_t max_tx_pdcp_sn, uint32_t desired_buf_size) override
  {
    if (pdcp_tx != nullptr) {
      pdcp_tx->handle_transmit_notification(max_tx_pdcp_sn);
      pdcp_tx->handle_desired_buffer_size_notification(desired_buf_size);
    }
  }
  void on_delivered_sdu(uint32_t max_deliv_pdcp_sn) override
  {
    if (pdcp_tx != nullptr) {
      pdcp_tx->handle_delivery_notification(max_deliv_pdcp_sn);
    }
  }
  void on_retransmitted_sdu(uint32_t max_retx_pdcp_sn) override
  {
    if (pdcp_tx != nullptr) {
      pdcp_tx->handle_retransmit_notification(max_retx_pdcp_sn);
    }
  }
  void on_delivered_retransmitted_sdu(uint32_t max_deliv_retx_pdcp_sn) override
  {
    if (pdcp_tx != nullptr) {
      pdcp_tx->handle_delivery_retransmitted_notification(max_deliv_retx_pdcp_sn);
    }
  }

private:
  pdcp_tx_lower_interface* pdcp_tx = nullptr;
};

class mbs_mrb_rlc_tx_control_notifier : public rlc_tx_upper_layer_control_notifier
{
public:
  void on_protocol_failure() override {}
  void on_max_retx() override {}
};

class mbs_mrb_rlc_rx_upper_notifier : public rlc_rx_upper_layer_data_notifier
{
public:
  void on_new_sdu(byte_buffer_chain pdu) override {}
};

class mbs_mrb_rlc_lower_notifier : public rlc_tx_lower_layer_notifier
{
public:
  void connect(std::function<void(const rlc_buffer_state&)> notifier_) { notifier = std::move(notifier_); }

  void on_buffer_state_update(const rlc_buffer_state& bsr) override
  {
    last_buffer_state = bsr;
    if (notifier) {
      notifier(bsr);
    }
  }

  rlc_buffer_state last_buffer_state;

private:
  std::function<void(const rlc_buffer_state&)> notifier;
};

class mbs_mrb_pdcp_tx_lower_notifier : public pdcp_tx_lower_notifier
{
public:
  void connect(rlc_tx_upper_layer_data_interface& rlc_tx_) { rlc_tx = &rlc_tx_; }

  void on_new_pdu(byte_buffer pdu, bool is_retx) override
  {
    if (rlc_tx != nullptr) {
      rlc_tx->handle_sdu(std::move(pdu), is_retx);
    }
  }

  void on_discard_pdu(uint32_t pdcp_sn) override
  {
    if (rlc_tx != nullptr) {
      rlc_tx->discard_sdu(pdcp_sn);
    }
  }

private:
  rlc_tx_upper_layer_data_interface* rlc_tx = nullptr;
};

class mbs_mrb_pdcp_tx_upper_control_notifier : public pdcp_tx_upper_control_notifier
{
public:
  void on_max_count_reached() override {}
  void on_protocol_failure() override {}
  void on_resume_required() override {}
};

class mbs_mrb_pdcp_rx_upper_data_notifier : public pdcp_rx_upper_data_notifier
{
public:
  void on_new_sdu(byte_buffer sdu) override {}
};

class mbs_mrb_pdcp_rx_upper_control_notifier : public pdcp_rx_upper_control_notifier
{
public:
  void on_protocol_failure() override {}
  void on_integrity_failure() override {}
  void on_max_count_reached() override {}
  void on_resume_required() override {}
};

/// DU-local MTCH bearer queue for one MBS MRB.
///
/// This models the downlink MRB data side used by PTM MTCH scheduling. It accepts multicast payload SDUs from F1-U,
/// sends them through PDCP and RLC UM, and emits RLC PDUs for the MAC transport block. The default constructor keeps a
/// simple queue for isolated unit tests, while the gNB path uses the PDCP/RLC-backed constructor.
class mbs_mrb_bearer
{
public:
  mbs_mrb_bearer() = default;

  mbs_mrb_bearer(uint16_t       mrb_id,
                 const mac_mbs_mrb_config& mrb_cfg,
                 timer_manager& timers,
                 task_executor& cell_executor,
                 task_executor& ue_executor,
                 std::function<void(const rlc_buffer_state&)> buffer_state_notifier = {}) :
    rlc_pcap_writer(std::make_unique<null_rlc_pcap>())
  {
    rlc_config cfg{};
    cfg.mode                         = rlc_mode::um_unidir_dl;
    cfg.um.rx.sn_field_length        = mrb_cfg.rlc_sn_size;
    cfg.um.rx.t_reassembly           = mrb_cfg.rlc_t_reassembly_ms;
    cfg.um.tx.sn_field_length        = mrb_cfg.rlc_sn_size;
    cfg.um.tx.pdcp_sn_len            = mrb_cfg.pdcp_sn_size_dl;
    cfg.um.tx.queue_size             = 4096;
    cfg.um.tx.queue_size_bytes       = 4096 * 1500;
    // Disable RLC metrics reporting for MBS MRBs: no metrics notifier is wired for the DU-local MTCH bearer, so a
    // non-zero period would make the periodic collector dereference a null notifier in push_report().
    cfg.metrics_period               = timer_duration{std::chrono::milliseconds{0}};

    const uint8_t drb_id = static_cast<uint8_t>(std::clamp<unsigned>(mrb_id, 1U, drb_id_to_uint(drb_id_t::drb29)));
    rlc_entity_creation_message msg{};
    msg.gnb_du_id             = gnb_du_id_t::min;
    msg.ue_index              = INVALID_DU_UE_INDEX;
    msg.rb_id                 = rb_id_t{uint_to_drb_id(drb_id)};
    msg.config                = cfg;
    msg.rx_upper_dn           = &rlc_rx_upper_notifier;
    msg.tx_upper_dn           = &rlc_tx_upper_notifier;
    msg.tx_upper_cn           = &rlc_tx_control_notifier;
    msg.tx_lower_dn           = &rlc_lower_notifier;
    msg.timers                = &timers;
    msg.pcell_executor        = &cell_executor;
    msg.ue_executor           = &ue_executor;
    msg.rlc_metrics_notif     = nullptr;
    msg.pcap_writer           = rlc_pcap_writer.get();
    rlc_lower_notifier.connect(std::move(buffer_state_notifier));
    rlc_bearer                = create_rlc_entity(msg);

    pdcp_tx_lower_notifier.connect(*rlc_bearer->get_tx_upper_layer_data_interface());

    pdcp_config pdcp_cfg{};
    pdcp_cfg.rb_type                       = pdcp_rb_type::drb;
    pdcp_cfg.rlc_mode                      = pdcp_rlc_mode::um;
    pdcp_cfg.header_compression            = std::nullopt;
    pdcp_cfg.integrity_protection_required = false;
    pdcp_cfg.ciphering_required            = false;
    pdcp_cfg.tx.sn_size                    = mrb_cfg.pdcp_sn_size_dl;
    pdcp_cfg.tx.direction                  = pdcp_security_direction::downlink;
    pdcp_cfg.tx.discard_timer              = pdcp_discard_timer::infinity;
    pdcp_cfg.tx.status_report_required     = false;
    pdcp_cfg.rx.sn_size                    = mrb_cfg.pdcp_sn_size_dl;
    pdcp_cfg.rx.direction                  = pdcp_security_direction::uplink;
    pdcp_cfg.rx.out_of_order_delivery      = false;
    pdcp_cfg.rx.t_reordering               = mrb_cfg.pdcp_t_reordering_dl;
    pdcp_cfg.custom.metrics_period         = timer_duration{std::chrono::milliseconds{0}};

    pdcp_entity_creation_message pdcp_msg{};
    pdcp_msg.ue_index               = static_cast<uint32_t>(INVALID_DU_UE_INDEX);
    pdcp_msg.rb_id                  = rb_id_t{uint_to_drb_id(drb_id)};
    pdcp_msg.config                 = pdcp_cfg;
    pdcp_msg.tx_lower               = &pdcp_tx_lower_notifier;
    pdcp_msg.tx_upper_cn            = &pdcp_tx_upper_control_notifier;
    pdcp_msg.rx_upper_dn            = &pdcp_rx_upper_data_notifier;
    pdcp_msg.rx_upper_cn            = &pdcp_rx_upper_control_notifier;
    pdcp_msg.ue_dl_timer_factory    = timer_factory{timers, pdcp_executor};
    pdcp_msg.ue_ul_timer_factory    = timer_factory{timers, pdcp_executor};
    pdcp_msg.ue_ctrl_timer_factory  = timer_factory{timers, pdcp_executor};
    pdcp_msg.ue_dl_executor         = &pdcp_executor;
    pdcp_msg.ue_ul_executor         = &pdcp_executor;
    pdcp_msg.ue_ctrl_executor       = &pdcp_executor;
    pdcp_msg.crypto_executor        = &pdcp_executor;
    // MBS ingress can execute on any worker in the DU main pool. PDCP selects one security engine per worker index.
    pdcp_msg.max_nof_crypto_workers =
        static_cast<uint32_t>(std::max<size_t>(1, cpu_architecture_info::get().get_host_nof_available_cpus()));
    pdcp_bearer                     = create_pdcp_entity(pdcp_msg);

    rlc_tx_upper_notifier.connect(pdcp_bearer->get_tx_lower_interface());
    pdcp_bearer->get_tx_lower_interface().handle_desired_buffer_size_notification(cfg.um.tx.queue_size_bytes);

    security::sec_128_as_config sec_cfg{};
    sec_cfg.domain      = security::sec_domain::up;
    sec_cfg.k_128_enc   = {};
    sec_cfg.integ_algo  = security::integrity_algorithm::nia1;
    sec_cfg.cipher_algo = security::ciphering_algorithm::nea0;
    pdcp_bearer->get_tx_upper_control_interface().configure_security(
        sec_cfg, security::integrity_enabled::off, security::ciphering_enabled::off);
  }

  mbs_mrb_bearer(mbs_mrb_bearer&&)                 = delete;
  mbs_mrb_bearer& operator=(mbs_mrb_bearer&&)      = delete;
  mbs_mrb_bearer(const mbs_mrb_bearer&)            = delete;
  mbs_mrb_bearer& operator=(const mbs_mrb_bearer&) = delete;

  ~mbs_mrb_bearer()
  {
    if (pdcp_bearer != nullptr) {
      pdcp_bearer->stop();
    }
    if (rlc_bearer != nullptr) {
      rlc_bearer->stop();
    }
  }

  void push_sdu(byte_buffer sdu)
  {
    if (rlc_bearer != nullptr) {
      pdcp_bearer->get_tx_upper_data_interface().handle_sdu(std::move(sdu));
      return;
    }
    pending_bytes += sdu.length();
    sdus.push_back(std::move(sdu));
  }

  byte_buffer pull_pdu(size_t max_bytes)
  {
    if (rlc_bearer != nullptr) {
      byte_buffer pdu;
      if (max_bytes == 0) {
        return pdu;
      }
      std::vector<uint8_t> tmp(max_bytes);
      const size_t pdu_len = rlc_bearer->get_tx_lower_layer_interface()->pull_pdu(tmp);
      if (pdu_len == 0) {
        return pdu;
      }
      if (not pdu.append(tmp.begin(), tmp.begin() + pdu_len)) {
        return byte_buffer{};
      }
      return pdu;
    }

    if (max_bytes == 0 or sdus.empty()) {
      return byte_buffer{};
    }

    byte_buffer pdu;
    size_t      nof_pulled_bytes = 0;
    while (nof_pulled_bytes < max_bytes and not sdus.empty()) {
      byte_buffer& front         = sdus.front();
      const size_t bytes_to_pull = std::min(max_bytes - nof_pulled_bytes, front.length());
      if (not pdu.append(front.begin(), front.begin() + bytes_to_pull)) {
        return byte_buffer{};
      }

      front.trim_head(bytes_to_pull);
      nof_pulled_bytes += bytes_to_pull;
      if (front.empty()) {
        sdus.pop_front();
      }
    }
    pending_bytes = pending_bytes > nof_pulled_bytes ? pending_bytes - nof_pulled_bytes : 0U;
    return pdu;
  }

  unsigned pending() const
  {
    if (rlc_bearer != nullptr) {
      return rlc_bearer->get_tx_lower_layer_interface()->get_buffer_state().pending_bytes;
    }
    return pending_bytes;
  }

  bool empty() const { return pending() == 0; }

private:
  std::deque<byte_buffer> sdus;
  unsigned                pending_bytes = 0;

  inline_task_executor              pdcp_executor;
  mbs_mrb_rlc_tx_upper_notifier   rlc_tx_upper_notifier;
  mbs_mrb_rlc_tx_control_notifier rlc_tx_control_notifier;
  mbs_mrb_rlc_rx_upper_notifier   rlc_rx_upper_notifier;
  mbs_mrb_rlc_lower_notifier      rlc_lower_notifier;
  std::unique_ptr<rlc_pcap>       rlc_pcap_writer;
  std::unique_ptr<rlc_entity>     rlc_bearer;

  mbs_mrb_pdcp_tx_lower_notifier         pdcp_tx_lower_notifier;
  mbs_mrb_pdcp_tx_upper_control_notifier pdcp_tx_upper_control_notifier;
  mbs_mrb_pdcp_rx_upper_data_notifier    pdcp_rx_upper_data_notifier;
  mbs_mrb_pdcp_rx_upper_control_notifier pdcp_rx_upper_control_notifier;
  std::unique_ptr<pdcp_entity>           pdcp_bearer;
};

} // namespace ocudu
