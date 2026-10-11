// ============================================================================
// sgm_core.cpp — 4-Path SGM (LR + RL + TB + BT) with ap_ctrl_hs + s_axilite
// MAX_DISP=16, 12-bit accumulators
// Target: PYNQ-Z2 (Zynq-7020)
// ============================================================================

#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

#define P1          8
#define P2          32
#define MAX_DISP    16
#define MAX_WIDTH   160
#define MAX_HEIGHT  120

typedef ap_uint<8>  pixel_t;
typedef ap_uint<8>  census_t;
typedef ap_uint<8>  cost_t;
typedef ap_uint<12> accum_t;
typedef ap_uint<8>  disp_t;
typedef ap_axis<8,0,0,0> disp_axis_t;

// ============================================================================
// CENSUS TRANSFORM
// ============================================================================
census_t census_transform(pixel_t w[3][3]) {
    #pragma HLS INLINE
    census_t code = 0;
    pixel_t c = w[1][1];
    int b = 0;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) {
            #pragma HLS UNROLL
            if (i == 1 && j == 1) continue;
            if (w[i][j] > c) code |= (1 << b);
            b++;
        }
    return code;
}

// ============================================================================
// HAMMING DISTANCE
// ============================================================================
cost_t hamming(census_t a, census_t b) {
    #pragma HLS INLINE
    ap_uint<8> x = a ^ b;
    cost_t c = 0;
    for (int i = 0; i < 8; i++) {
        #pragma HLS UNROLL
        c += (x >> i) & 1;
    }
    return c;
}

// ============================================================================
// PATH 1: LEFT → RIGHT
// ============================================================================
void aggregate_LR(cost_t cost[MAX_WIDTH][MAX_DISP], accum_t agg[MAX_WIDTH][MAX_DISP], int W) {
    for (int x = 0; x < W; x++) {
        #pragma HLS PIPELINE II=1
        for (int d = 0; d < MAX_DISP; d++) {
            #pragma HLS UNROLL
            if (x == 0) { agg[x][d] = cost[x][d]; continue; }
            accum_t ps  = agg[x-1][d];
            accum_t pm1 = (d > 0)          ? (accum_t)(agg[x-1][d-1] + P1) : (accum_t)4095;
            accum_t pp1 = (d < MAX_DISP-1) ? (accum_t)(agg[x-1][d+1] + P1) : (accum_t)4095;
            accum_t pma = (accum_t)4095;
            for (int i = 0; i < MAX_DISP; i++) {
                if (agg[x-1][i] < pma) pma = agg[x-1][i];
            }
            accum_t mn = ps;
            if (pm1 < mn) mn = pm1;
            if (pp1 < mn) mn = pp1;
            if ((pma + P2) < mn) mn = pma + P2;
            agg[x][d] = (accum_t)(cost[x][d] + mn - pma);
        }
    }
}

// ============================================================================
// PATH 2: RIGHT → LEFT
// ============================================================================
void aggregate_RL(cost_t cost[MAX_WIDTH][MAX_DISP], accum_t agg[MAX_WIDTH][MAX_DISP], int W) {
    for (int x = W - 1; x >= 0; x--) {
        #pragma HLS PIPELINE II=1
        for (int d = 0; d < MAX_DISP; d++) {
            #pragma HLS UNROLL
            if (x == W - 1) { agg[x][d] = cost[x][d]; continue; }
            accum_t ps  = agg[x+1][d];
            accum_t pm1 = (d > 0)          ? (accum_t)(agg[x+1][d-1] + P1) : (accum_t)4095;
            accum_t pp1 = (d < MAX_DISP-1) ? (accum_t)(agg[x+1][d+1] + P1) : (accum_t)4095;
            accum_t pma = (accum_t)4095;
            for (int i = 0; i < MAX_DISP; i++) {
                if (agg[x+1][i] < pma) pma = agg[x+1][i];
            }
            accum_t mn = ps;
            if (pm1 < mn) mn = pm1;
            if (pp1 < mn) mn = pp1;
            if ((pma + P2) < mn) mn = pma + P2;
            agg[x][d] = (accum_t)(cost[x][d] + mn - pma);
        }
    }
}

// ============================================================================
// PATH 3: TOP → BOTTOM
// ============================================================================
void aggregate_TB(cost_t cost[MAX_WIDTH][MAX_DISP], accum_t agg[MAX_WIDTH][MAX_DISP],
                  accum_t prev[MAX_WIDTH][MAX_DISP], int W, bool first_row) {
    for (int x = 0; x < W; x++) {
        #pragma HLS PIPELINE II=1
        for (int d = 0; d < MAX_DISP; d++) {
            #pragma HLS UNROLL
            if (first_row) { agg[x][d] = cost[x][d]; continue; }
            accum_t ps  = prev[x][d];
            accum_t pm1 = (d > 0)          ? (accum_t)(prev[x][d-1] + P1) : (accum_t)4095;
            accum_t pp1 = (d < MAX_DISP-1) ? (accum_t)(prev[x][d+1] + P1) : (accum_t)4095;
            accum_t pma = (accum_t)4095;
            for (int i = 0; i < MAX_DISP; i++) {
                if (prev[x][i] < pma) pma = prev[x][i];
            }
            accum_t mn = ps;
            if (pm1 < mn) mn = pm1;
            if (pp1 < mn) mn = pp1;
            if ((pma + P2) < mn) mn = pma + P2;
            agg[x][d] = (accum_t)(cost[x][d] + mn - pma);
        }
    }
}

// ============================================================================
// PATH 4: BOTTOM → TOP (approximated as reverse-LR within row)
// ============================================================================
void aggregate_BT(cost_t cost[MAX_WIDTH][MAX_DISP], accum_t agg[MAX_WIDTH][MAX_DISP], int W) {
    for (int x = W - 1; x >= 0; x--) {
        #pragma HLS PIPELINE II=1
        for (int d = 0; d < MAX_DISP; d++) {
            #pragma HLS UNROLL
            if (x == W - 1) { agg[x][d] = cost[x][d]; continue; }
            accum_t ps  = agg[x+1][d];
            accum_t pm1 = (d > 0)          ? (accum_t)(agg[x+1][d-1] + P1) : (accum_t)4095;
            accum_t pp1 = (d < MAX_DISP-1) ? (accum_t)(agg[x+1][d+1] + P1) : (accum_t)4095;
            accum_t pma = (accum_t)4095;
            for (int i = 0; i < MAX_DISP; i++) {
                if (agg[x+1][i] < pma) pma = agg[x+1][i];
            }
            accum_t mn = ps;
            if (pm1 < mn) mn = pm1;
            if (pp1 < mn) mn = pp1;
            if ((pma + P2) < mn) mn = pma + P2;
            agg[x][d] = (accum_t)(cost[x][d] + mn - pma);
        }
    }
}

// ============================================================================
// DISPARITY SELECTION
// ============================================================================
disp_t select_disp(accum_t t[MAX_DISP]) {
    #pragma HLS INLINE
    accum_t mn = (accum_t)4095;
    disp_t bd = 0;
    for (int d = 0; d < MAX_DISP; d++) {
        #pragma HLS UNROLL
        if (t[d] < mn) { mn = t[d]; bd = (disp_t)d; }
    }
    return bd;
}

// ============================================================================
// TOP FUNCTION — ap_ctrl_hs with s_axilite
// ============================================================================
void sgm_core(
    hls::stream<pixel_t>     &left_stream,
    hls::stream<pixel_t>     &right_stream,
    hls::stream<disp_axis_t> &disparity_stream,
    int width,
    int height,
    int max_disp
) {
    #pragma HLS INTERFACE axis      port=left_stream
    #pragma HLS INTERFACE axis      port=right_stream
    #pragma HLS INTERFACE axis      port=disparity_stream
    #pragma HLS INTERFACE s_axilite port=width    bundle=control
    #pragma HLS INTERFACE s_axilite port=height   bundle=control
    #pragma HLS INTERFACE s_axilite port=max_disp bundle=control
    #pragma HLS INTERFACE s_axilite port=return   bundle=control

    static pixel_t left_line[3][MAX_WIDTH];
    static pixel_t right_line[3][MAX_WIDTH + MAX_DISP];
    #pragma HLS ARRAY_PARTITION variable=left_line  dim=1 complete
    #pragma HLS ARRAY_PARTITION variable=right_line dim=1 complete

    static cost_t  cost[MAX_WIDTH][MAX_DISP];
    static accum_t agg_LR[MAX_WIDTH][MAX_DISP];
    static accum_t agg_RL[MAX_WIDTH][MAX_DISP];
    static accum_t agg_TB[MAX_WIDTH][MAX_DISP];
    static accum_t agg_BT[MAX_WIDTH][MAX_DISP];
    static accum_t prev_TB[MAX_WIDTH][MAX_DISP];
    static accum_t total[MAX_WIDTH][MAX_DISP];

    for (int y = 0; y < height; y++) {
        #pragma HLS LOOP_TRIPCOUNT min=120 max=120

        // ---- Stage 1: Census + Matching Cost ----
        for (int x = 0; x < width; x++) {
            #pragma HLS PIPELINE II=1
            pixel_t lp = left_stream.read();
            pixel_t rp = right_stream.read();

            for (int i = 0; i < 2; i++) {
                #pragma HLS UNROLL
                left_line[i][x]  = left_line[i+1][x];
                right_line[i][x] = right_line[i+1][x];
            }
            left_line[2][x]  = lp;
            right_line[2][x] = rp;

            if (y < 2 || x < 2) {
                for (int d = 0; d < MAX_DISP; d++) {
                    #pragma HLS UNROLL
                    cost[x][d] = 0;
                }
                continue;
            }

            pixel_t lw[3][3];
            #pragma HLS ARRAY_PARTITION variable=lw complete
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) {
                    #pragma HLS UNROLL
                    lw[i][j] = left_line[i][x-2+j];
                }
            census_t lc = census_transform(lw);

            for (int d = 0; d < max_disp; d++) {
                #pragma HLS UNROLL
                census_t rc = 0;
                if (x >= d + 2) {
                    pixel_t rw[3][3];
                    #pragma HLS ARRAY_PARTITION variable=rw complete
                    for (int i = 0; i < 3; i++)
                        for (int j = 0; j < 3; j++) {
                            #pragma HLS UNROLL
                            rw[i][j] = right_line[i][x-d-2+j];
                        }
                    rc = census_transform(rw);
                }
                cost[x][d] = hamming(lc, rc);
            }
        }

        // ---- Stage 2: 4-PATH AGGREGATION ----
        aggregate_LR(cost, agg_LR, width);                          // Path 1
        aggregate_RL(cost, agg_RL, width);                          // Path 2
        aggregate_TB(cost, agg_TB, prev_TB, width, (y == 0));       // Path 3
        aggregate_BT(cost, agg_BT, width);                          // Path 4

        // Save TB result for next row
        for (int x = 0; x < width; x++) {
            #pragma HLS PIPELINE II=1
            for (int d = 0; d < MAX_DISP; d++) {
                #pragma HLS UNROLL
                prev_TB[x][d] = agg_TB[x][d];
            }
        }

        // ---- Stage 3: Combine all 4 paths ----
        for (int x = 0; x < width; x++) {
            #pragma HLS PIPELINE II=1
            for (int d = 0; d < MAX_DISP; d++) {
                #pragma HLS UNROLL
                total[x][d] = (accum_t)(agg_LR[x][d] + agg_RL[x][d]
                                      + agg_TB[x][d] + agg_BT[x][d]);
            }
        }

        // ---- Stage 4: Disparity Selection + Output ----
        for (int x = 0; x < width; x++) {
            #pragma HLS PIPELINE II=1
            disp_t d = 0;
            if (x >= 2 && x < width - 2 && y >= 2)
                d = select_disp(total[x]);

            disp_axis_t pkt;
            pkt.data = d;
            pkt.keep = 1;
            pkt.strb = 1;
            pkt.last = ((x == width - 1) && (y == height - 1)) ? 1 : 0;
            disparity_stream.write(pkt);
        }
    }
}