output "public_ip" {
  value = openstack_compute_instance_v2.open5gs.access_ip_v4
}
