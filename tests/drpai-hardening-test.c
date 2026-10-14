// SPDX-License-Identifier: GPL-2.0
/*
 * Negative tests for the input-validation fixes in drpai-if.c.
 *
 * Each case sends one malformed request and checks that the driver rejects it.
 * Run as a user that can open /dev/drpai0, with no inference running (the
 * cases use scratch space 256 MiB into the drp_reserved carveout).
 *
 *   drpai-hardening-test            run every case (fixed driver only)
 *   drpai-hardening-test --vendor   skip the cases that corrupt kernel memory
 *                                   or kick the engine on the unfixed driver
 *
 * Cross-build:
 *   aarch64-linux-gnu-gcc -O2 -Wall -I../include/uapi -o drpai-hardening-test \
 *       drpai-hardening-test.c
 */
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/drpai.h>

#define DEV          "/dev/drpai0"
#define SCRATCH_OFF  0x10000000u    /* 256 MiB into the carveout */
#define OBJ_SIZE     64u

static int failures;
static int vendor;
static uint32_t area_base, area_size;

static void report(const char *name, int ok, const char *detail)
{
    printf("%-44s %s%s%s\n", name, ok ? "PASS" : "FAIL",
           detail ? "  " : "", detail ? detail : "");
    if (!ok)
        failures++;
}

static void skip(const char *name)
{
    printf("%-44s SKIP  (unsafe on the unfixed driver)\n", name);
}

static int dev_open(void)
{
    int fd = open(DEV, O_RDWR);

    if (fd < 0) {
        perror(DEV);
        exit(2);
    }
    return fd;
}

/* ioctl returning -errno, or 0 */
static int xioctl(int fd, unsigned long cmd, void *arg)
{
    return ioctl(fd, cmd, arg) ? -errno : 0;
}

static uint32_t scratch(uint32_t extra)
{
    return area_base + SCRATCH_OFF + extra;
}

/* Fill [addr, addr+len) of the carveout through ASSIGN + write. */
static int carveout_write(uint32_t addr, const void *buf, uint32_t len)
{
    drpai_data_t d = { .address = addr, .size = len };
    int fd = dev_open(), r;

    r = xioctl(fd, DRPAI_ASSIGN, &d);
    if (!r && write(fd, buf, len) != (ssize_t)len)
        r = -errno;
    close(fd);
    return r;
}

static int carveout_read(uint32_t addr, void *buf, uint32_t len)
{
    drpai_data_t d = { .address = addr, .size = len };
    int fd = dev_open(), r;

    r = xioctl(fd, DRPAI_ASSIGN, &d);
    if (!r && read(fd, buf, len) != (ssize_t)len)
        r = -errno;
    close(fd);
    return r;
}

/* ASSIGN_PARAM + write of a param_info text for obj at scratch(0). */
static int load_param_info(int fd, const char *text)
{
    drpai_assign_param_t p = {
        .info_size = (uint32_t)strlen(text),
        .obj = { .address = scratch(0), .size = OBJ_SIZE },
    };
    int r = xioctl(fd, DRPAI_ASSIGN_PARAM, &p);

    if (r)
        return r;
    return write(fd, text, p.info_size) == (ssize_t)p.info_size ? 0 : -errno;
}

/* Unknown command must be -ENOTTY, not -EFAULT. */
static void t_unknown_ioctl(void)
{
    int fd = dev_open();
    int r = xioctl(fd, _IO(DRPAI_IO_TYPE, 0x7f), NULL);

    report("unknown ioctl -> ENOTTY", r == -ENOTTY, strerror(-r));
    close(fd);
}

/* PREPOST_CROP with OFFSET_ADD past obj.size (but still inside the
 * carveout, so the unfixed driver's stray write stays in scratch space). */
static void t_crop_offset(void)
{
    char before[OBJ_SIZE + 64], after[OBJ_SIZE + 64];
    drpai_crop_t c = { 1, 1, 0, 0, { scratch(0), OBJ_SIZE } };
    int fd = dev_open(), r;

    memset(before, 0, sizeof(before));
    carveout_write(scratch(0), before, sizeof(before));
    r = load_param_info(fd, "OFFSET_ADD:4096,drp_lib:crop,\nOFFSET_ADD:4096,x,\n");
    if (!r)
        r = xioctl(fd, DRPAI_PREPOST_CROP, &c);
    report("PREPOST_CROP offset past obj -> EINVAL", r == -EINVAL, strerror(-r));
    close(fd);
    carveout_read(scratch(0), after, sizeof(after));
    report("PREPOST_CROP obj left untouched", !memcmp(before, after, sizeof(after)), NULL);
}

/* PREPOST_INADDR with OFFSET_ADD past obj.size. */
static void t_inaddr_offset(void)
{
    drpai_inout_t io;
    int fd = dev_open(), r;

    memset(&io, 0, sizeof(io));
    strcpy(io.name, "in0");
    io.data.address = scratch(0x1000);
    io.data.size = 64;
    io.obj.address = scratch(0);
    io.obj.size = OBJ_SIZE;
    r = load_param_info(fd, "x,prop:input,in0,OFFSET_ADD:4096,\n");
    if (!r)
        r = xioctl(fd, DRPAI_PREPOST_INADDR, &io);
    report("PREPOST_INADDR offset past obj -> EINVAL", r == -EINVAL, strerror(-r));
    close(fd);
}

/* A name without a NUL terminator must not be read past the struct. */
static void t_inaddr_name_unterminated(void)
{
    drpai_inout_t io;
    int fd = dev_open(), r;

    memset(&io, 'A', sizeof(io.name));
    io.data.address = scratch(0x1000);
    io.data.size = 64;
    io.obj.address = scratch(0);
    io.obj.size = OBJ_SIZE;
    r = load_param_info(fd, "x,prop:input,in0,OFFSET_ADD:0,\n");
    if (!r)
        r = xioctl(fd, DRPAI_PREPOST_INADDR, &io);
    /* no matching line: the search ends with -EFAULT, without an oops */
    report("PREPOST_INADDR unterminated name", r == -EFAULT, strerror(-r));
    close(fd);
}

/* SET_SEQ must reject num == 0 and num > DRPAI_SEQ_NUM. */
static void t_set_seq(void)
{
    drpai_seq_t s;
    int fd = dev_open(), r0, r21;

    memset(&s, 0, sizeof(s));
    s.order[0] = DRPAI_EXE_AI;
    s.num = DRPAI_SEQ_NUM + 1;
    r21 = xioctl(fd, DRPAI_SET_SEQ, &s);
    s.num = 0;
    r0 = xioctl(fd, DRPAI_SET_SEQ, &s);
    report("SET_SEQ num=21 -> EINVAL", r21 == -EINVAL, strerror(-r21));
    report("SET_SEQ num=0 -> EINVAL", r0 == -EINVAL, strerror(-r0));
    s.num = 1;
    xioctl(fd, DRPAI_SET_SEQ, &s);     /* leave AI mode selected */
    close(fd);
}

/* A param_info offset that wraps 32 bits must not rewrite anything. */
static void t_dynamic_param_wrap(void)
{
    uint8_t zero[OBJ_SIZE] = { 0 }, back[OBJ_SIZE];
    drpai_data_dynamic_t dyn = {
        .start_address = scratch(0), .offset = 0, .size = OBJ_SIZE,
        .file_type = DRPAI_FILE_TYPE_DRP_PARAM,
    };
    int fd = dev_open(), r;
    uint32_t v;

    carveout_write(scratch(0), zero, sizeof(zero));
    /* 0xFFFFFFFC + 8 truncates to 4 in 32 bits: inside the buffer, so the
     * unfixed driver rewrites offset 4 instead of rejecting the entry */
    r = load_param_info(fd, "OFFSET_ADD:4294967292,x,\nParam:raddr,offset:8,\n");
    if (!r)
        r = xioctl(fd, DRPAI_ASSIGN_DYNAMIC, &dyn);
    if (!r && write(fd, zero, sizeof(zero)) != (ssize_t)sizeof(zero))
        r = -errno;
    close(fd);
    carveout_read(scratch(0), back, sizeof(back));
    memcpy(&v, back + 4, 4);
    report("wrapped param offset not rewritten", !r && v == 0,
           r ? strerror(-r) : (v ? "offset 4 was rewritten" : NULL));
}

/* REG_DUMP read with a count that is not a multiple of 4. (A huge count
 * is not tested: it would read the whole register window.) */
static void t_reg_dump_read(void)
{
    char buf[8];
    int fd, r;
    ssize_t n;

    if (vendor) {
        skip("REG_DUMP read odd count");
        return;
    }
    fd = dev_open();
    r = xioctl(fd, DRPAI_REG_DUMP, NULL);
    n = r ? -1 : read(fd, buf, 6);
    report("REG_DUMP read(6) returns 4", n == 4, NULL);
    close(fd);
    /* the semaphore must be free again: a fresh GET_STATUS must not time out */
    fd = dev_open();
    {
        drpai_status_t st;
        r = xioctl(fd, DRPAI_GET_STATUS, &st);
    }
    report("priv->sem released after read", r != -ETIMEDOUT, strerror(-r));
    close(fd);
}

/* Build a START argument whose every region is valid inside scratch space. */
static void valid_proc(drpai_data_t proc[DRPAI_INDEX_NUM])
{
    int i;

    for (i = 0; i < DRPAI_INDEX_NUM; i++) {
        proc[i].address = scratch(0x10000 * (uint32_t)(i + 1));
        proc[i].size = 64;
    }
}

/* A zero-size descriptor must be rejected before any write. */
static void t_start_zero_desc(void)
{
    drpai_data_t proc[DRPAI_INDEX_NUM];
    int fd, r;

    if (vendor) {
        skip("START with zero-size DRP desc");
        return;
    }
    fd = dev_open();
    valid_proc(proc);
    proc[DRPAI_INDEX_DRP_DESC].size = 0;
    r = xioctl(fd, DRPAI_START, proc);
    report("START zero-size DRP desc -> EINVAL", r == -EINVAL, strerror(-r));
    valid_proc(proc);
    proc[DRPAI_INDEX_AIMAC_DESC].size = 0;
    r = xioctl(fd, DRPAI_START, proc);
    report("START zero-size AIMAC desc -> EINVAL", r == -EINVAL, strerror(-r));
    close(fd);
}

/* AI-mode INPUT outside the carveout and outside Linux RAM. */
static void t_start_bad_input(void)
{
    drpai_data_t proc[DRPAI_INDEX_NUM];
    int fd, r;

    if (vendor) {
        skip("START with INPUT outside RAM");
        return;
    }
    fd = dev_open();
    valid_proc(proc);
    proc[DRPAI_INDEX_INPUT].address = 0x10000000;  /* below DRAM: MMIO space */
    r = xioctl(fd, DRPAI_START, proc);
    report("START INPUT outside RAM -> EINVAL", r == -EINVAL, strerror(-r));
    close(fd);
}

/* DRP-mode cfg/param outside the carveout. */
static void t_start_drp_bad_addr(void)
{
    drpai_data_t proc_drp[DRPAI_SEQ_NUM * 2];
    drpai_seq_t s;
    int fd, r, i;

    if (vendor) {
        skip("DRP-mode START outside carveout");
        return;
    }
    fd = dev_open();
    memset(&s, 0, sizeof(s));
    s.num = 1;
    s.order[0] = DRPAI_EXE_DRP;
    r = xioctl(fd, DRPAI_SET_SEQ, &s);
    for (i = 0; i < DRPAI_SEQ_NUM * 2; i++) {
        proc_drp[i].address = scratch(0x10000 * (uint32_t)(i + 1));
        proc_drp[i].size = 64;
    }
    proc_drp[1].address = 0x10000000;               /* param outside */
    if (!r)
        r = xioctl(fd, DRPAI_START, proc_drp);
    report("DRP-mode START param outside -> EINVAL", r == -EINVAL, strerror(-r));
    s.order[0] = DRPAI_EXE_AI;
    xioctl(fd, DRPAI_SET_SEQ, &s);
    close(fd);
}

/* Smoke test: poll on an idle device returns without blocking. */
static void t_poll(void)
{
    struct pollfd p;
    int fd = dev_open(), r;

    p.fd = fd;
    p.events = POLLIN;
    r = poll(&p, 1, 0);
    report("poll on idle device", r >= 0, NULL);
    close(fd);
}

int main(int argc, char **argv)
{
    drpai_data_t area;
    int fd;

    vendor = argc > 1 && !strcmp(argv[1], "--vendor");
    fd = dev_open();
    if (xioctl(fd, DRPAI_GET_DRPAI_AREA, &area)) {
        perror("DRPAI_GET_DRPAI_AREA");
        return 2;
    }
    close(fd);
    area_base = area.address;
    area_size = area.size;
    printf("carveout 0x%08x size 0x%08x, scratch at 0x%08x%s\n",
           area_base, area_size, scratch(0), vendor ? " (vendor mode)" : "");
    if (area_size <= SCRATCH_OFF + 0x200000) {
        fprintf(stderr, "carveout too small for the scratch area\n");
        return 2;
    }

    t_unknown_ioctl();
    t_crop_offset();
    t_inaddr_offset();
    t_inaddr_name_unterminated();
    t_set_seq();
    t_dynamic_param_wrap();
    t_reg_dump_read();
    t_start_zero_desc();
    t_start_bad_input();
    t_start_drp_bad_addr();
    t_poll();

    printf("%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
