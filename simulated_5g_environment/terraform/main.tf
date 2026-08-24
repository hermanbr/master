terraform {
  required_version = ">= 1.5"
  required_providers {
    openstack = {
      source  = "terraform-provider-openstack/openstack"
      version = "~> 2.1"
    }
    local = {
      source  = "hashicorp/local"
      version = "~> 2.5"
    }
  }
}

# Reads OS_USERNAME / OS_PASSWORD / OS_AUTH_URL / ... from the environment.
# Run `source ../keystone_rc.sh` before `terraform apply`.
provider "openstack" {}

resource "openstack_compute_instance_v2" "open5gs" {
  name            = "open5gs"
  image_name      = var.image_name
  flavor_name     = var.flavor_name
  key_pair        = var.keypair_name
  security_groups = ["default", openstack_networking_secgroup_v2.instance_ssh_access.name, openstack_networking_secgroup_v2.fivegc_internal.name]

  network {
    name = var.network_name
  }
}

resource "openstack_compute_instance_v2" "ueransim-gnb" {
  count           = 1
  name            = "ueransim-gnb-${count.index}"
  image_name      = var.image_name
  flavor_name     = var.flavor_name
  key_pair        = var.keypair_name
  security_groups = ["default", openstack_networking_secgroup_v2.instance_ssh_access.name, openstack_networking_secgroup_v2.fivegc_internal.name, openstack_networking_secgroup_v2.gnb_internal.name]

  network {
    name = var.network_name
  }
}

resource "openstack_compute_instance_v2" "ueransim-ue" {
  count           = 2
  name            = "ueransim-ue-${count.index}"
  image_name      = var.image_name
  flavor_name     = var.flavor_name
  key_pair        = var.keypair_name
  security_groups = ["default", openstack_networking_secgroup_v2.instance_ssh_access.name, openstack_networking_secgroup_v2.gnb_internal.name]

  network {
    name = var.network_name
  }
}

resource "local_file" "ansible_inventory" {
  filename = "${path.module}/../ansible/inventory.ini"
  content = join("\n", concat(
    ["[open5gs]"],
    ["${openstack_compute_instance_v2.open5gs.access_ip_v4} ansible_user=ubuntu ansible_ssh_private_key_file=${var.ssh_private_key_file}"],
    [""],
    ["[ueransim_gnb]"],
    [
      for instance in openstack_compute_instance_v2.ueransim-gnb :
      "${instance.access_ip_v4} ansible_user=ubuntu ansible_ssh_private_key_file=${var.ssh_private_key_file}"
    ],
    [""],
    ["[ueransim_ue]"],
    [
      for idx, instance in openstack_compute_instance_v2.ueransim-ue :
      "${instance.access_ip_v4} ansible_user=ubuntu ansible_ssh_private_key_file=${var.ssh_private_key_file} test_imsi=${var.ue_imsi_prefix}${idx + 1} test_key=${var.ue_key} test_opc=${var.ue_opc} test_static_ip=${cidrhost(var.ue_static_ip_base, var.ue_static_ip_offset + idx)}"
    ],
    [""]
  ))
}