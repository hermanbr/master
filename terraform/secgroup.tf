resource "openstack_networking_secgroup_v2" "instance_ssh_access" {
  name        = "instance-ssh-access"
  description = "Allow SSH and ICMP from trusted networks"
}

# Allow ssh from IPv4 net
resource "openstack_networking_secgroup_rule_v2" "rule_ssh_access_ipv4" {
  count             = length(var.allow_ssh_from_v4)
  direction         = "ingress"
  ethertype         = "IPv4"
  protocol          = "tcp"
  port_range_min    = 22
  port_range_max    = 22
  remote_ip_prefix  = var.allow_ssh_from_v4[count.index]
  security_group_id = openstack_networking_secgroup_v2.instance_ssh_access.id
}

# Allow ssh from IPv6 net
resource "openstack_networking_secgroup_rule_v2" "rule_ssh_access_ipv6" {
  count             = length(var.allow_ssh_from_v6)
  direction         = "ingress"
  ethertype         = "IPv6"
  protocol          = "tcp"
  port_range_min    = 22
  port_range_max    = 22
  remote_ip_prefix  = var.allow_ssh_from_v6[count.index]
  security_group_id = openstack_networking_secgroup_v2.instance_ssh_access.id
}

# Allow icmp from IPv4 net
resource "openstack_networking_secgroup_rule_v2" "rule_icmp_access_ipv4" {
  count             = length(var.allow_ssh_from_v4)
  direction         = "ingress"
  ethertype         = "IPv4"
  protocol          = "icmp"
  remote_ip_prefix  = var.allow_ssh_from_v4[count.index]
  security_group_id = openstack_networking_secgroup_v2.instance_ssh_access.id
}

# Allow icmp from IPv6 net
resource "openstack_networking_secgroup_rule_v2" "rule_icmp_access_ipv6" {
  count             = length(var.allow_ssh_from_v6)
  direction         = "ingress"
  ethertype         = "IPv6"
  protocol          = "ipv6-icmp"
  remote_ip_prefix  = var.allow_ssh_from_v6[count.index]
  security_group_id = openstack_networking_secgroup_v2.instance_ssh_access.id
}


# Security group for 5G core <-> gNB traffic (N2 and N3 interfaces).
# Rules use remote_group_id (self-referencing) so any instance in this
# group can reach any other instance in it on the 5G interface ports.
resource "openstack_networking_secgroup_v2" "fivegc_internal" {
  name        = "5gc-internal"
  description = "Allow N2 (NGAP/SCTP) and N3 (GTP-U/UDP) between 5G instances"
}

# N2 — NGAP over SCTP, port 38412 (IPv4)
resource "openstack_networking_secgroup_rule_v2" "rule_n2_sctp_ipv4" {
  direction         = "ingress"
  ethertype         = "IPv4"
  protocol          = "sctp"
  port_range_min    = 38412
  port_range_max    = 38412
  remote_group_id   = openstack_networking_secgroup_v2.fivegc_internal.id
  security_group_id = openstack_networking_secgroup_v2.fivegc_internal.id
}

# N2 — NGAP over SCTP, port 38412 (IPv6)
resource "openstack_networking_secgroup_rule_v2" "rule_n2_sctp_ipv6" {
  direction         = "ingress"
  ethertype         = "IPv6"
  protocol          = "sctp"
  port_range_min    = 38412
  port_range_max    = 38412
  remote_group_id   = openstack_networking_secgroup_v2.fivegc_internal.id
  security_group_id = openstack_networking_secgroup_v2.fivegc_internal.id
}

# N3 — GTP-U over UDP, port 2152 (IPv4)
resource "openstack_networking_secgroup_rule_v2" "rule_n3_gtpu_ipv4" {
  direction         = "ingress"
  ethertype         = "IPv4"
  protocol          = "udp"
  port_range_min    = 2152
  port_range_max    = 2152
  remote_group_id   = openstack_networking_secgroup_v2.fivegc_internal.id
  security_group_id = openstack_networking_secgroup_v2.fivegc_internal.id
}

# N3 — GTP-U over UDP, port 2152 (IPv6)
resource "openstack_networking_secgroup_rule_v2" "rule_n3_gtpu_ipv6" {
  direction         = "ingress"
  ethertype         = "IPv6"
  protocol          = "udp"
  port_range_min    = 2152
  port_range_max    = 2152
  remote_group_id   = openstack_networking_secgroup_v2.fivegc_internal.id
  security_group_id = openstack_networking_secgroup_v2.fivegc_internal.id
}



# Security group for UE <-> gNB traffic.
resource "openstack_networking_secgroup_v2" "gnb_internal" {
  name        = "gnb-internal"
  description = "Allow traffic from UE to gNB"
}

# Uu — simulated radio link, UDP 4997 (IPv4)
resource "openstack_networking_secgroup_rule_v2" "rule_uu_ipv4" {
  direction         = "ingress"
  ethertype         = "IPv4"
  protocol          = "udp"
  port_range_min    = 4997
  port_range_max    = 4997
  remote_group_id   = openstack_networking_secgroup_v2.gnb_internal.id
  security_group_id = openstack_networking_secgroup_v2.gnb_internal.id
}

# Uu — simulated radio link, UDP 4997 (IPv4)
resource "openstack_networking_secgroup_rule_v2" "rule_uu_ipv6" {
  direction         = "ingress"
  ethertype         = "IPv6"
  protocol          = "udp"
  port_range_min    = 4997
  port_range_max    = 4997
  remote_group_id   = openstack_networking_secgroup_v2.gnb_internal.id
  security_group_id = openstack_networking_secgroup_v2.gnb_internal.id
}