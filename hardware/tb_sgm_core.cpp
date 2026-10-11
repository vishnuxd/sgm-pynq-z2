// ============================================================================
// tb_sgm_core.cpp — Testbench for 4-Path SGM with ap_ctrl_hs
// ============================================================================

#include <iostream>
#include <fstream>
#include <ap_int.h>
#include <hls_stream.h>
#include <ap_axi_sdata.h>

#define WIDTH       160
#define HEIGHT      120
#define MAX_DISP    16

typedef ap_uint<8> pixel_t;
typedef ap_uint<8> disp_t;
typedef ap_axis<8, 0, 0, 0> disp_axis_t;


void sgm_core(
    hls::stream<pixel_t> &left_stream,
    hls::stream<pixel_t> &right_stream,
    hls::stream<disp_axis_t> &disparity_stream,
    int width,
    int height,
    int max_disp
);

// ----------------------------------------------------------------------------
void load_image(const char* filename, pixel_t img[HEIGHT][WIDTH]) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << filename << std::endl;
        for (int y = 0; y < HEIGHT; y++)
            for (int x = 0; x < WIDTH; x++)
                img[y][x] = (pixel_t)((x + y) % 256);
        return;
    }
    int val;
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++) {
            file >> val;
            img[y][x] = (pixel_t)val;
        }
    file.close();
    std::cout << "Loaded " << filename << std::endl;
}

// ----------------------------------------------------------------------------
void save_disparity(const char* filename, disp_t disp[HEIGHT][WIDTH]) {
    std::ofstream file(filename);
    if (!file.is_open()) {
        std::cerr << "Error: Could not open " << filename << std::endl;
        return;
    }
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            file << (int)disp[y][x] << " ";
        }
        file << std::endl;
    }
    file.close();
    std::cout << "Saved " << filename << std::endl;
}

// ----------------------------------------------------------------------------
void median_filter(disp_t in[HEIGHT][WIDTH], disp_t out[HEIGHT][WIDTH]) {
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            if (y < 1 || y >= HEIGHT-1 || x < 1 || x >= WIDTH-1) {
                out[y][x] = in[y][x];
                continue;
            }
            disp_t w[9];
            int idx = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    w[idx++] = in[y+dy][x+dx];
            for (int i = 0; i < 9; i++)
                for (int j = i+1; j < 9; j++)
                    if (w[j] < w[i]) { disp_t t = w[i]; w[i] = w[j]; w[j] = t; }
            out[y][x] = w[4];
        }
    }
}

// ----------------------------------------------------------------------------
int main() {
    static pixel_t left_img[HEIGHT][WIDTH];
    static pixel_t right_img[HEIGHT][WIDTH];
    static disp_t  disparity_raw[HEIGHT][WIDTH];
    static disp_t  disparity_filtered[HEIGHT][WIDTH];

    std::cout << "=== SGM Testbench Started ===" << std::endl;

    load_image("left.txt", left_img);
    load_image("right.txt", right_img);

    hls::stream<pixel_t> left_stream;
    hls::stream<pixel_t> right_stream;
    hls::stream<disp_axis_t> disparity_stream;

    std::cout << "Filling streams..." << std::endl;
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++) {
            left_stream.write(left_img[y][x]);
            right_stream.write(right_img[y][x]);
        }

    std::cout << "Running SGM core..." << std::endl;
    sgm_core(left_stream, right_stream, disparity_stream, WIDTH, HEIGHT, MAX_DISP);

    std::cout << "Reading disparity output..." << std::endl;
    int tlast_count = 0;
    for (int y = 0; y < HEIGHT; y++) {
        for (int x = 0; x < WIDTH; x++) {
            disp_axis_t pkt = disparity_stream.read();
            disparity_raw[y][x] = pkt.data;
            if (pkt.last == 1) {
                tlast_count++;
                std::cout << "TLAST at (" << x << "," << y << ")" << std::endl;
            }
        }
    }

    if (tlast_count != 1) {
        std::cerr << "WARNING: Expected 1 TLAST, got " << tlast_count << std::endl;
    } else {
        std::cout << "TLAST OK — one TLAST at end of frame" << std::endl;
    }

    save_disparity("disparity_raw.txt", disparity_raw);

    // Brightness mask
    for (int y = 0; y < HEIGHT; y++)
        for (int x = 0; x < WIDTH; x++)
            if (left_img[y][x] > 200)
                disparity_raw[y][x] = 0;

    median_filter(disparity_raw, disparity_filtered);
    save_disparity("disparity.txt", disparity_filtered);

    std::cout << "=== SGM Testbench Completed Successfully ===" << std::endl;
    return 0;
}