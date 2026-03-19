# llama.cpp RPC with RDMA Transport

- [Background](#background)
- [OS](#os)
- [Hardware](#hardware)
- [Performance](#performance)
- [Building](#building)
- [Usage](#usage)
- [Environment Variables](#environment-variables)
- [Troubleshooting](#troubleshooting)

## Background

The RPC backend in llama.cpp enables distributed inference by offloading computation to remote hosts over TCP. While TCP works well for most networks, the per-message overhead of the kernel network stack becomes a bottleneck for token generation, where each token requires a full round-trip between nodes.

RDMA (Remote Direct Memory Access) bypasses the kernel network stack entirely, allowing NICs to read and write memory directly. RoCEv2 (RDMA over Converged Ethernet v2) brings RDMA to standard Ethernet networks using commodity NICs from vendors like Mellanox/NVIDIA and Broadcom.

When built with `-DGGML_RPC_RDMA=ON`, the RPC backend auto-negotiates RDMA transport during connection setup. If both client and server have RDMA-capable NICs, the connection upgrades transparently. If either side lacks RDMA, it falls back to TCP silently.

## OS


| OS      | Status    | Notes                                               |
| ------- | --------- | --------------------------------------------------- |
| Linux   | Supported | Requires `rdma-core` / `libibverbs-dev`             |
| Windows | N/A       | RDMA code not compiled; TCP-only RPC works normally |
| macOS   | Supported | Thunderbolt 5 RDMA via dlopen. Requires macOS 26.2+, SDK 26.2+ |


## Hardware

RDMA transport requires RoCEv2-capable NICs on both nodes. Tested hardware:


| NIC                                    | Link Speed | Status    |
| -------------------------------------- | ---------- | --------- |
| Mellanox ConnectX-4 Lx (MT27710) 25GbE | 25 Gbps    | Supported |
| Mellanox ConnectX-6 Lx (MT2894) 25GbE  | 25 Gbps    | Supported |
| Apple M4 Pro (Thunderbolt 5, macOS)    | 120 Gbps   | Compile-verified |


Other RoCEv2-capable NICs (ConnectX-5/7, Broadcom NetXtreme-E, etc.) should work but are untested. Mixed NIC generations across nodes are supported.

## Performance

Two-node cluster: AMD Radeon 8060S (gfx1151) iGPUs, ConnectX-4 Lx / ConnectX-6 Lx 25GbE, RoCEv2. Model: Qwen3-Coder-Next 80B Q8_K_XL, layer split (`-sm layer -ts 1/1`) across both nodes, ROCm backend.


| Metric                     | TCP    | RDMA   | Improvement |
| -------------------------- | ------ | ------ | ----------- |
| Prompt processing (pp2048) | 651.48 | 678.42 | **+4.1%**   |
| Token generation (tg256)   | 30.19  | 32.16  | **+6.5%**   |


Token generation benefits most because each token requires a round-trip between nodes. Results will vary with hardware, model size, and split configuration.

## Building

Build with RDMA support by adding `-DGGML_RPC_RDMA=ON`:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DGGML_RPC=ON \
  -DGGML_RPC_RDMA=ON

cmake --build build --target rpc-server llama-bench -j$(nproc)
```

On macOS, no additional dependencies are needed — `librdma.dylib` is loaded at runtime:

```bash
cmake -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DGGML_RPC=ON \
  -DGGML_RPC_RDMA=ON

cmake --build build --target rpc-server llama-cli -j$(sysctl -n hw.ncpu)
```

### Dependencies

Requires `libibverbs-dev` (part of `rdma-core`):

```bash
# Ubuntu / Debian
sudo apt install libibverbs-dev rdma-core

# Fedora / RHEL
sudo dnf install libibverbs-devel rdma-core-devel
```

This is an optional dependency. Without `-DGGML_RPC_RDMA=ON`, the build produces a standard TCP-only binary with no RDMA code or libibverbs linkage.

### macOS (Thunderbolt 5)

Requires macOS Tahoe 26.2 or later with Thunderbolt 5 hardware on both machines.

**Prerequisites:**
1. Both Macs must run macOS 26.2+ with Thunderbolt 5 ports (M4 family or later)
2. Connect with a Thunderbolt 5 (or USB4) cable
3. **Disable Thunderbolt Bridge** — it conflicts with RDMA:

```bash
# Check if Thunderbolt Bridge is active
networksetup -listnetworkserviceorder | grep -i "thunderbolt bridge"

# Disable it (requires admin)
sudo networksetup -setnetworkserviceenabled "Thunderbolt Bridge" off
```

4. Enable RDMA access via Recovery OS if prompted (see Apple documentation)

No compile-time dependencies are needed on macOS. The RDMA library (`librdma.dylib`) is loaded at runtime via `dlopen`. If the library is unavailable or no RDMA devices are found, the connection uses TCP silently.

**Reference:** [EXO project's Thunderbolt setup](https://github.com/exo-explore/exo/blob/main/tmp/set_rdma_network_config.sh) for advanced network configuration.

## Usage

### Server

```bash
bin/rpc-server -H 0.0.0.0 -p 50052 -c
```

### Client

```bash
bin/llama-bench --rpc 192.168.1.45:50052 \
  -m model.gguf -p 2048 -n 256
```

The connection starts as TCP and upgrades to RDMA automatically during the handshake. The server log confirms the upgrade:

```
Accepted client connection
RDMA probed: dev=mlx5_0 gid=5 qpn=328 inline=316
RDMA activated: qpn=328->488 mtu=1024 rx_depth=24
```

When RDMA is not available (no hardware or connecting to a stock `rpc-server`), the connection works over TCP with no user action required.

## Environment Variables


| Variable        | Required | Default     | Description                                                                                                                                                   |
| --------------- | -------- | ----------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `GGML_RDMA_DEV` | No       | auto-detect | RDMA device name (e.g. `mlx5_0`). When set, only this device is considered. When unset, all devices are scanned for a GID matching the TCP socket's local IP. |
| `GGML_RDMA_GID` | No       | auto-detect | GID table index. When unset, the first IPv4-mapped RoCEv2 GID is used.                                                                                        |


These variables are only needed when auto-detection fails, typically in complex network topologies such as Linux bridges where the IP may not appear in the expected GID slot.

## Troubleshooting

### Verify RDMA devices

```bash
ibv_devices       # list RDMA devices
ibv_devinfo       # show device details and port state
```

### Check GID table

```bash
cat /sys/class/infiniband/mlx5_0/ports/1/gids/0
```

GID entries with `fe80::` prefix are link-local (InfiniBand). Look for entries with `::ffff:` prefix -- these are IPv4-mapped RoCEv2 GIDs.

### RDMA not activating

- Ensure both nodes have `rdma-core` installed and RDMA devices visible in `ibv_devices`
- If using a Linux bridge, set `GGML_RDMA_DEV` and `GGML_RDMA_GID` explicitly
- Check that RoCEv2 is enabled on the NIC port
- Enable debug logging with `GGML_RPC_DEBUG=1` to see probe/activate messages

### macOS: Verify RDMA is available

```bash
# Check kernel extensions are loaded
kextstat | grep -i rdma

# Expected output (two kexts):
#   com.apple.iokit.IORDMAFamily
#   com.apple.driver.AppleThunderboltRDMA
```

### macOS: Thunderbolt Bridge conflicts

If RDMA devices are not found, ensure Thunderbolt Bridge is disabled. The bridge claims Thunderbolt interfaces for standard Ethernet, preventing the RDMA transport from using them.

