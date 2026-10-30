<h1 align="center">MBS Examples</h1>
<p align="center">
  <img src="https://img.shields.io/badge/Status-Under_Development-yellow" alt="Under Development">
  <img src="https://img.shields.io/badge/License-5G--MAG%20Public%20License%20(v1.0)-blue" alt="License">
</p>

## Introduction

3GPP Release 17 brings Multicast–Broadcast Services (MBS) to the 5G System, based on 5G Core and New Radio. MBS allows
the network to select the most suitable among point-to-multipoint (PTM) or point-to-point (PTP) delivery based on
requirements set by either service providers or network operators and/or taking into account concurrent user demand.

Additional information can be found at: https://5g-mag.github.io/Getting-Started/pages/5g-multicast-broadcast-services/

### About the implementation

This repository contains Docker Compose components to deploy several network functions related to MBS.
The detailed usage instructions are available at
the [Getting Started guides](https://5g-mag.github.io/Getting-Started/pages/5g-multicast-broadcast-services/tutorials/mbs-in-5gc.html).

> [!NOTE]
> Docker images are available for `amd64/x86-64` and `arm64` based systems.

Some of the components are unmodified Open5GS Network Functions, those are marked with the regular Network Function's
name and follow Open5GS' versioning, the latest version available is the `v2.7.2`.

| Network Function | image name          | version |
|------------------|---------------------|---------|
| AUSF             | ghcr.io/5g-mag/ausf | v2.7.2  |
| BSF              | ghcr.io/5g-mag/bsf  | v2.7.2  |
| NRF              | ghcr.io/5g-mag/nrf  | v2.7.2  |
| NSSF             | ghcr.io/5g-mag/nssf | v2.7.2  |
| PCF              | ghcr.io/5g-mag/pcf  | v2.7.2  |
| UDM              | ghcr.io/5g-mag/udm  | v2.7.2  |
| UDR              | ghcr.io/5g-mag/udr  | v2.7.2  |

The following components are being developed for MBS and the latest version available is the `0.1.2`.

| Network Function               | image name                    | version |
|--------------------------------|-------------------------------|---------|
| AMF (with Rel-17 MBS features) | ghcr.io/5g-mag/amf_with_mbs   | 0.1.2   |
| SMF + MB-SMF                   | ghcr.io/5g-mag/smf_mb-smf     | 0.1.2   |
| UPF + MB-UPF                   | ghcr.io/5g-mag/upf_mb-upf     | 0.1.2   |
| Test MBS AF/AS                 | ghcr.io/5g-mag/test_mbs_af_as | 0.1.2   |
| gNB (with Rel-17 MBS features) | ghcr.io/5g-mag/gnb_with_mbs   | 0.1.2   |
| UE (with Rel-17 MBS features)  | ghcr.io/5g-mag/ue_with_mbs    | 0.1.2   |

Those components are being developed using [Open5GS](https://github.com/5G-MAG/open5gs) for the Network Functions AMF,
MB-SMF and MB-UPF, [rt-srsRAN_Project](https://github.com/5G-MAG/rt-srsRAN_Project) for the gNB
and [srsRAN_4G](https://github.com/5G-MAG/srsRAN_4G) for the UE, using the `5mbs` branch.

## Installing Dependencies

Follow the [steps for your distribution](https://docs.docker.com/engine/install/) and install the docker buildx and
docker compose plugins

## Downloading

The MBS Docker images can be obtained:

* From the 5G-MAG's GitHub Container Registry and pulled with Docker (`docker pull ghcr.io/NAMESPACE/IMAGE_NAME`). This
  step is not needed if you run Docker compose as per the [Running](#running) section below.
* The docker images can also be obtained by cloning the repository:

```bash
  cd ~
  git clone --recurse-submodules https://github.com/5G-MAG/rt-mbs-examples.git
```

## Building

You can skip this step if you decide to download the images rather than cloning the repository.

> [!NOTE]
> This method uses the `docker-bake.hcl` file and requires `docker-buildx-plugin`

> [!IMPORTANT]
> When building the images, modify the `FIVEG_MAG_MBS_VERSION` and `OPEN5GS_VERSION` variables in the `.env` file to use
> a specific version.

From the top level directory of the repository run:

```bash
  cd rt-mbs-examples
  docker buildx bake
```

## Running

Two different deployments have been created to test this project, `internal` and `external`:
- The `internal` deployment consists of an end-to-end setup consisting of a 5G Core with MBS features, a gNB and a UE. Everything ready to be tested on the *internal* network using only the components deployed.
- The `external` deployment consists of only the 5G Core with MBS features. It is configured to accept connections of *external* gNBs.

First modify the `.env` file. Change the `DOCKER_HOST_IP=<your_host_ip_address>` with your machine's IP address, like
this `DOCKER_HOST_IP=192.168.1.2`. This lets the UPF + MB-UPF use your machine's Internet connection to route the
traffic using NAT.

To run the Docker images, select either the `internal` deployment or the `external` deployment and from the top level
directory of the repository:

### Internal Deployment

```bash
# to use the internal deployment
docker compose -f compose-files/internal/docker-compose.yaml --env-file=.env up -d
```

```bash
# to tear down the internal deployment
docker compose -f compose-files/internal/docker-compose.yaml --env-file=.env down
```

### External Deployment

```bash
# to use the external deployment
docker compose -f compose-files/external/docker-compose.yaml --env-file=.env up -d
```

```bash
# to tear down the external deployment
docker compose -f compose-files/external/docker-compose.yaml --env-file=.env down
```
