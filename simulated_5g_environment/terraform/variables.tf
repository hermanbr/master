variable "image_name" {
  type    = string
  default = "GOLD Ubuntu 22.04 LTS"
}

variable "flavor_name" {
  type    = string
  default = "m1.medium"
}

variable "network_name" {
  type    = string
  default = "dualStack"
}

variable "keypair_name" {
  description = "Name of an SSH keypair already registered in your NREC project (openstack keypair list)."
  type        = string
  default     = "mykey"
}

variable "ssh_private_key_file" {
  description = "Local path to the private key matching var.keypair_name, used by Ansible."
  type        = string
  default     = "~/.ssh/id_ed25519"
}

variable "allow_ssh_from_v4" {
  description = "IPv4 CIDRs allowed to SSH/ping the instances."
  type        = list(string)
  default     = ["0.0.0.0/0"]
}

variable "allow_ssh_from_v6" {
  description = "IPv6 CIDRs allowed to SSH/ping the instances."
  type        = list(string)
  default     = ["::/0"]
}

variable "ue_imsi_prefix" {
  description = "First 14 digits of each test IMSI; the UE instance index (1-based) is appended to make the final digit."
  type        = string
  default     = "99970000000000"
}

variable "ue_key" {
  type    = string
  default = "465B5CE8B199B49FAA5F0A2EE238A6BC"
}

variable "ue_opc" {
  type    = string
  default = "E8ED289DEBA952E4283B54E88E6183CA"
}

.
variable "ue_static_ip_base" {
  description = "CIDR the per-instance static UE IPs are numbered from (instance index + ue_static_ip_offset is the host number)."
  type        = string
  default     = "10.45.0.0/24"
}

variable "ue_static_ip_offset" {
  type    = number
  default = 10
}
