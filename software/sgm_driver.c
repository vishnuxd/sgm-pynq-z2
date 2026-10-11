#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <time.h>

// ============================================================================
#define SGM_BASE    0x40000000
#define DMA0_BASE   0x41E00000
#define DMA1_BASE   0x41E10000

#define MAP_SIZE    0x10000
#define WIDTH       160
#define HEIGHT      120
#define MAXDISP     16
#define FRAME_SIZE  (WIDTH * HEIGHT)

// AXI DMA registers
#define MM2S_DMACR    0x00
#define MM2S_DMASR    0x04
#define MM2S_SA       0x18
#define MM2S_LENGTH   0x28

#define S2MM_DMACR    0x30
#define S2MM_DMASR    0x34
#define S2MM_DA       0x48
#define S2MM_LENGTH   0x58

#define DMACR_RS      0x0001
#define DMACR_RESET   0x0004

#define DMASR_HALTED  0x0001
#define DMASR_IDLE    0x0002
#define DMASR_IOC_IRQ 0x1000
#define IOC_ENABLE    (1 << 26)

// SGM registers
#define SGM_CTRL      0x00
#define SGM_WIDTH     0x10
#define SGM_HEIGHT    0x18
#define SGM_MAXDISP   0x20

volatile uint32_t *sgm_regs;
volatile uint32_t *dma0_regs;
volatile uint32_t *dma1_regs;

// ============================================================================
void *map_phys(uint32_t phys, size_t size) {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); exit(1); }
    void *v = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, phys);
    close(fd);
    if (v == MAP_FAILED) { perror("mmap"); exit(1); }
    return v;
}

void dma_init(volatile uint32_t *regs) {
    regs[MM2S_DMACR/4] = DMACR_RESET;
    while (regs[MM2S_DMACR/4] & DMACR_RESET);
    regs[S2MM_DMACR/4] = DMACR_RESET;
    while (regs[S2MM_DMACR/4] & DMACR_RESET);
    regs[MM2S_DMACR/4] = DMACR_RS;
    regs[S2MM_DMACR/4] = DMACR_RS;
}

void dma_transfer_mm2s(volatile uint32_t *regs, uint32_t src_phys) {
    regs[MM2S_DMASR/4] = DMASR_IOC_IRQ;
    regs[MM2S_SA/4]    = src_phys;
    __sync_synchronize();
    regs[MM2S_LENGTH/4] = FRAME_SIZE | IOC_ENABLE;
}

void dma_transfer_s2mm(volatile uint32_t *regs, uint32_t dst_phys) {
    regs[S2MM_DMASR/4] = DMASR_IOC_IRQ;
    regs[S2MM_DA/4]    = dst_phys;
    __sync_synchronize();
    regs[S2MM_LENGTH/4] = FRAME_SIZE | IOC_ENABLE;
}

int wait_mm2s(volatile uint32_t *regs) {
    int timeout = 1000000;
        uint32_t sr = regs[MM2S_DMASR/4];
        if (sr & DMASR_IOC_IRQ) return 0;
    }
    return -1;
}

int wait_s2mm(volatile uint32_t *regs) {
    int timeout = 1000000;
    while (timeout--) {
        uint32_t sr = regs[S2MM_DMASR/4];
        if (sr & DMASR_IOC_IRQ) return 0;
    }
    return -1;
}

// ============================================================================
int main(int argc, char **argv) {
    if (argc < 7) {
        fprintf(stderr,
                "Usage: %s <left_phys> <right_phys> <disp_phys> "
                "<num_frames> <input.bin> <output.bin>\n", argv[0]);
        return 1;
    }

    uint32_t left_phys  = strtoul(argv[1], NULL, 0);
    uint32_t right_phys = strtoul(argv[2], NULL, 0);
    uint32_t disp_phys  = strtoul(argv[3], NULL, 0);
    int      num_frames = atoi(argv[4]);
    const char *infile  = argv[5];
    const char *outfile = argv[6];

    printf("========================================\n");
    printf("SGM C Driver\n");
    printf("========================================\n");
    printf("Left  phys: 0x%08X\n", left_phys);
    printf("Right phys: 0x%08X\n", right_phys);
    printf("Disp  phys: 0x%08X\n", disp_phys);
    printf("Frames:     %d\n", num_frames);
    printf("========================================\n\n");

    sgm_regs  = map_phys(SGM_BASE,  MAP_SIZE);
    dma0_regs = map_phys(DMA0_BASE, MAP_SIZE);
    dma1_regs = map_phys(DMA1_BASE, MAP_SIZE);

    // Configure SGM
    sgm_regs[SGM_CTRL/4]    = 0;
    sgm_regs[SGM_WIDTH/4]   = WIDTH;
    sgm_regs[SGM_HEIGHT/4]  = HEIGHT;
    sgm_regs[SGM_MAXDISP/4] = MAXDISP;

    // Init DMA
    dma_init(dma0_regs);
    dma_init(dma1_regs);

    // Map CMA buffers
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("open /dev/mem"); return 1; }

    uint8_t *left_buf  = mmap(NULL, FRAME_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, left_phys);
    uint8_t *right_buf = mmap(NULL, FRAME_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, right_phys);
    uint8_t *disp_buf  = mmap(NULL, FRAME_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, disp_phys);
    close(fd);

    if (left_buf == MAP_FAILED || right_buf == MAP_FAILED || disp_buf == MAP_FAILED) {
        perror("mmap CMA");
        return 1;
    }

    FILE *fin  = fopen(infile, "rb");
    if (!fin)  { perror("fopen input");  return 1; }
    FILE *fout = fopen(outfile, "wb");
    if (!fout) { perror("fopen output"); return 1; }

    uint8_t *temp_l = malloc(FRAME_SIZE);
    uint8_t *temp_r = malloc(FRAME_SIZE);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int frames_done = 0;

    for (int i = 0; i < num_frames; i++) {
        if (fread(temp_l, 1, FRAME_SIZE, fin) != FRAME_SIZE) {
            fprintf(stderr, "EOF at frame %d\n", i);
            break;
        }
        if (fread(temp_r, 1, FRAME_SIZE, fin) != FRAME_SIZE) break;

        memcpy(left_buf,  temp_l, FRAME_SIZE);
        memcpy(right_buf, temp_r, FRAME_SIZE);
        __sync_synchronize();

        // Start DMA
        dma_transfer_s2mm(dma0_regs, disp_phys);
        dma_transfer_mm2s(dma0_regs, left_phys);
        dma_transfer_mm2s(dma1_regs, right_phys);

        // Pulse ap_start
        sgm_regs[SGM_CTRL/4] = 1;
        __sync_synchronize();

        // Wait
        if (wait_s2mm(dma0_regs) < 0) {
            fprintf(stderr, "S2MM timeout @ %d (SR=0x%08X)\n", i,
                    dma0_regs[S2MM_DMASR/4]);
            break;
        }
        if (wait_mm2s(dma0_regs) < 0) {
            fprintf(stderr, "MM2S0 timeout @ %d (SR=0x%08X)\n", i,
                    dma0_regs[MM2S_DMASR/4]);
            break;
        }
        if (wait_mm2s(dma1_regs) < 0) {
            fprintf(stderr, "MM2S1 timeout @ %d (SR=0x%08X)\n", i,
                    dma1_regs[MM2S_DMASR/4]);
            break;
        }

        fwrite(disp_buf, 1, FRAME_SIZE, fout);
        frames_done++;

        if ((i+1) % 20 == 0) {
            clock_gettime(CLOCK_MONOTONIC, &t1);
            double el = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
            printf("Frame %d: %.1f FPS\n", i+1, (i+1)/el);
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;

    printf("\n========================================\n");
    printf("✅ %d frames in %.2f s | %.1f FPS\n",
           frames_done, elapsed, frames_done/elapsed);
    printf("========================================\n");

    fclose(fin); fclose(fout);
    free(temp_l); free(temp_r);
    munmap(left_buf, FRAME_SIZE);
    munmap(right_buf, FRAME_SIZE);
    munmap(disp_buf, FRAME_SIZE);
    munmap((void*)sgm_regs, MAP_SIZE);
    munmap((void*)dma0_regs, MAP_SIZE);
    munmap((void*)dma1_regs, MAP_SIZE);

    return 0;
}