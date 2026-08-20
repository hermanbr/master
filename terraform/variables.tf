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
  default     = "~/.ssh/id_rsa"
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
