/*------------------------------------------------------------------------------
 * unit test : QZSS L6 frame routing in the UBX/SBF receiver decoders (#351)
 *
 * Feeds short receiver streams through input_sbf()/input_ubx() and counts the
 * return codes: 10 CLAS frame, 15 MADOCA-PPP SSR decoded, 16 MADOCA-PPP iono
 * frame, 17 QZNMA frame. Fixtures (tests/utest/l6route_*) are built by
 * scripts/tests/make_l6_route_fixtures.py, which also prints the per-service
 * frame counts asserted here:
 *
 *   l6route_g5.sbf        G5P3287a 4270/4271  CLAS 60, MADOCA 48, QZNMA 12
 *   l6route_iono.sbf      G5P3186k 4270       MADOCA iono (PRN 200) 30
 *   l6route_mcla.sbf      MCLA286e 4069       CLAS 30 (mosaic-CLAS, Source=1)
 *   l6route_d9c.ubx       D9C1286m RXM-QZSSL6 CLAS 60 (two L6D channels)
 *   l6route_ubx_iono.ubx  PRN 200 iono frames re-wrapped as UBX L6D: 30 (D1,
 *                         was routed to the CLAS demux as 10)
 *   l6route_sbf_197.sbf   CLAS frames re-wrapped as QZS5 / PRN 197: 30 (D2,
 *                         was routed to the iono decoder as 16)
 *
 * MADOCA frames return 0 until a message completes, so 15 is only required to
 * occur; the exact 10/16/17 counts prove that no MADOCA or QZNMA frame leaks
 * into the CLAS or iono paths. Also checks the auxiliary L6 PRN table.
 *
 * Explicit CHECK macro (not assert) so it is robust under -DNDEBUG.
 *-----------------------------------------------------------------------------*/
#include <stdio.h>
#include <stdlib.h>

#include "mrtklib/rtklib.h"

static int fails = 0;

#define CHECK(cond, msg)                 \
    do {                                 \
        if (!(cond)) {                   \
            printf("FAIL: %s\n", (msg)); \
            fails++;                     \
        }                                \
    } while (0)

/* feed a fixture through a receiver decoder; count return codes 10/15/16/17 */
static int route_file(const char* file, int format, int* n) {
    raw_t* raw = (raw_t*)calloc(1, sizeof(raw_t));
    rtcm_t* rtcm = (rtcm_t*)calloc(1, sizeof(rtcm_t)); /* ~7.5 MB: heap only */
    FILE* fp;
    int c, ret;

    n[10] = n[15] = n[16] = n[17] = 0;
    if (!raw || !rtcm || !(fp = fopen(file, "rb"))) {
        free(raw);
        free(rtcm);
        return 0;
    }
    init_raw(raw, format);
    init_rtcm(rtcm);
    while ((c = fgetc(fp)) != EOF) {
        ret = format == STRFMT_UBX ? input_ubx(raw, rtcm, (uint8_t)c) : input_sbf(raw, rtcm, (uint8_t)c);
        if (ret >= 10 && ret <= 17) n[ret]++;
    }
    fclose(fp);
    free_rtcm(rtcm);
    free_raw(raw);
    free(raw);
    free(rtcm);
    return 1;
}

static void test_route(const char* file, int format, int n10, int n16, int n17, int need15) {
    int n[18];
    char msg[128];

    if (!route_file(file, format, n)) {
        snprintf(msg, sizeof(msg), "%s: open", file);
        CHECK(0, msg);
        return;
    }
    snprintf(msg, sizeof(msg), "%s: CLAS frames -> 10 (%d, expected %d)", file, n[10], n10);
    CHECK(n[10] == n10, msg);
    snprintf(msg, sizeof(msg), "%s: MADOCA iono frames -> 16 (%d, expected %d)", file, n[16], n16);
    CHECK(n[16] == n16, msg);
    snprintf(msg, sizeof(msg), "%s: QZNMA frames -> 17 (%d, expected %d)", file, n[17], n17);
    CHECK(n[17] == n17, msg);
    if (need15) {
        snprintf(msg, sizeof(msg), "%s: MADOCA SSR decoded -> 15 (%d)", file, n[15]);
        CHECK(n[15] > 0, msg);
    }
    printf("ok  : %-22s 10=%d 15=%d 16=%d 17=%d\n", file, n[10], n[15], n[16], n[17]);
}

static void test_prn_table(void) {
    const l6_prn_info_t* p;
    l6_header_t h = {0};

    CHECK((p = l6_prn_info(200)) && p->iono_slot == 0 && !p->l6e, "PRN 200: L6D iono slot 0");
    CHECK((p = l6_prn_info(201)) && p->iono_slot == 1, "PRN 201: iono slot 1");
    CHECK((p = l6_prn_info(197)) && p->svc == L6_SVC_CLAS && p->iono_slot < 0, "PRN 197: CLAS, no iono slot");
    CHECK((p = l6_prn_info(211)) && p->l6e && p->pair_prn == 201, "PRN 211: L6E of QZS7");
    CHECK(l6_prn_info(198) == NULL && l6_prn_info(208) == NULL, "PRN 198/208: unassigned");

    h.prn = 195, h.svc = L6_SVC_CLAS;
    CHECK(l6_service_expected(&h), "CLAS on 195 expected");
    h.prn = 205, h.svc = L6_SVC_QZNMA;
    CHECK(l6_service_expected(&h), "QZNMA on L6E 205 expected");
    h.prn = 205, h.svc = L6_SVC_CLAS;
    CHECK(!l6_service_expected(&h), "CLAS on L6E 205 not expected");
    h.prn = 200, h.svc = L6_SVC_CLAS;
    CHECK(!l6_service_expected(&h), "CLAS on iono PRN 200 not expected");
    h.prn = 198, h.svc = L6_SVC_NULL;
    CHECK(l6_service_expected(&h), "null message always expected");
    printf("ok  : L6 PRN table\n");
}

int main(void) {
    test_prn_table();
    test_route("l6route_g5.sbf", STRFMT_SEPT, 60, 0, 12, 1);
    test_route("l6route_iono.sbf", STRFMT_SEPT, 0, 30, 0, 0);
    test_route("l6route_mcla.sbf", STRFMT_SEPT, 30, 0, 0, 0);
    test_route("l6route_d9c.ubx", STRFMT_UBX, 60, 0, 0, 0);
    test_route("l6route_ubx_iono.ubx", STRFMT_UBX, 0, 30, 0, 0); /* D1 */
    test_route("l6route_sbf_197.sbf", STRFMT_SEPT, 30, 0, 0, 0); /* D2 */
    if (fails) {
        printf("%d check(s) FAILED\n", fails);
        return 1;
    }
    printf("all checks passed\n");
    return 0;
}
