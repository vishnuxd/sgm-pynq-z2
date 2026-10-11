# 4-Path SGM Stereo Vision Accelerator on PYNQ-Z2

Real-time depth estimation using Semi-Global Matching (SGM) on a low-cost FPGA, validated on the KITTI Vision Benchmark Suite.

![Demo](results/demo.gif)

*Real-time depth estimation on the PYNQ-Z2 — KITTI dataset*

---

## Results

| Metric | Value |
|---|---|
| Platform | PYNQ-Z2 (Zynq-7020) |
| Algorithm | 4-Path Semi-Global Matching |
| Resolution | 160 × 120 |
| Max Disparity | 16 |
| Throughput | **5.9 FPS** |
| Clock | 25 MHz |
| Power | ~1.7 W |
| Depth Accuracy | ±20 cm @ 3 m |
| Validation | KITTI Vision Benchmark Suite |
| WNS | +4.643 ns ✅ |

---

## System Architecture

| Block | Function |
|---|---|
| `sgm_core_0` | Custom HLS IP implementing 4-Path SGM |
| `axi_dma_0` | Streams Left image in, Disparity out |
| `axi_dma_1` | Streams Right image in |
| `ps7_0_axi_periph` | AXI-Lite control interconnect |
| `axi_mem_intercon_hp0` | Input memory highway (HP0) |
| `axi_mem_intercon_hp1` | Output memory highway (HP1) |

### Memory Map

| IP | Base Address | Size |
|---|---|---|
| `sgm_core_0/s_axi_control` | `0x40000000` | 64 K |
| `axi_dma_0/S_AXI_LITE` | `0x41E00000` | 64 K |
| `axi_dma_1/S_AXI_LITE` | `0x41E10000` | 64 K |

---

## Algorithm: 4-Path SGM

Semi-Global Matching computes depth from a stereo image pair in four stages:

1. **Census Transform** — Converts pixels to 8-bit fingerprints based on neighbor comparisons. Robust to lighting changes.
2. **Cost Calculation** — Hamming distance between left/right fingerprints for every disparity.
3. **4-Path Aggregation** — Smooths costs from four directions: L→R, R→L, T→B, and diagonal TL→BR.
4. **Disparity Selection** — Picks the minimum-cost disparity per pixel. Brighter = closer.

---

## Hardware Stack

- **Vitis HLS 2025.2** — C++ → RTL synthesis
- **Vivado 2025.2** — Block design, place & route, bitstream
- **Target** — `xc7z020clg400-1` (PYNQ-Z2)

### Resource Utilization

| Resource | Used | Available | % |
|---|---|---|---|
| LUT | 18,753 | 53,200 | 35% |
| FF | 12,640 | 106,400 | 12% |
| BRAM | 62.5 | 140 | 45% |
| DSP | 0 | 220 | 0% |

---

## Software Stack

| File | Purpose |
|---|---|
| `software/sgm_driver.c` | Direct `/dev/mem` C driver (bypasses OS overhead) |
| `software/upload_demo.py` | Flask web demo with live progress |
| `hardware/image_to_txt.py` | Converts stereo images to TXT for Vitis testbench |
| `hardware/txt_to_image.py` | Visualizes disparity TXT as colorized PNG |

---

## How to Run

### Prerequisites
- PYNQ-Z2 board with PYNQ v3.0+
- Bitstream (see Releases)

### Step 1: Download Bitstream
Download `sgm_v9.bit` and `sgm_v9.hwh` from the Releases page. Copy both to `/home/xilinx/jupyter_notebooks/sgm/`.

**⚠️ The `.bit` and `.hwh` files must have the same name.**

### Step 2: Compile the C Driver
    cd /home/xilinx/jupyter_notebooks/sgm
    gcc -O3 -o sgm_driver sgm_driver.c

### Step 3: Run the Web Demo
    sudo python3 upload_demo.py

Open in browser: `http://<PYNQ_IP>:5000`

---

## Repository Structure

    sgm-pynq-z2/
    ├── hardware/          # Vitis HLS source code
    ├── software/          # PYNQ-side code
    ├── vivado/            # Block design & timing screenshots
    ├── results/           # Demo outputs
    ├── README.md
    ├── LICENSE
    └── .gitignore

---

## 🔬 Validation

Validated on the **KITTI Vision Benchmark Suite** (`2011_09_26_drive_0104_sync`).

**Depth accuracy** (KITTI 54 cm baseline, 160×120 resolution):

| Distance | Accuracy |
|---|---|
| 3.1 m | ±20 cm |
| 6.3 m | ±80 cm |
| 12.5 m | ±3 m |

Best accuracy in the 3–6 meter range — ideal for short-range collision avoidance.

---

## Key Achievements

- ✅ Complete hardware/software co-design from algorithm to silicon
- ✅ Direct register-level C driver bypassing OS overhead
- ✅ Split AXI memory highways (HP0 input, HP1 output)
- ✅ Under 2 W total power consumption
- ✅ Real-time operation on a $200 FPGA


---

## License

MIT License — see [LICENSE](LICENSE).

---

## Acknowledgements

- [KITTI Vision Benchmark Suite](http://www.cvlibs.net/datasets/kitti/) (Geiger et al.)
- Xilinx/AMD Vitis HLS & Vivado
- [PYNQ Project](http://www.pynq.io/)

---
