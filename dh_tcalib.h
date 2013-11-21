/* dh_tcalib.h 
   John Jacobsen / John Jacobsen IT Services for LBNL/IceCube
   Stuff used for time calibration in the driver, as well as
   in test programs.

   Must include <linux/types.h> first.
*/

#ifndef __DH_TCALIB__
#define __DH_TCALIB__

#ifndef __KERNEL__
#define u32 __u32
#define u16 __u16
#define u64 __u64
#endif

/** Time calibration record */
#define DH_MAX_TCAL_WF_LEN 64
struct dh_tcalib_t {
  u32 hdr;
  u64 dor_t0;
  u64 dor_t3;
  u16 dorwf[DH_MAX_TCAL_WF_LEN];
  u64 dom_t1;
  u64 dom_t2;
  u16 domwf[DH_MAX_TCAL_WF_LEN];
};

/* Packed, no-padding length of time calibration structure (in B) */
#define DH_TCAL_STRUCT_LEN (36+2*2*DH_MAX_TCAL_WF_LEN)

/* Ensure struct members are packed with no padding at end */
int dh_tcalib_pack(unsigned char *dest, struct dh_tcalib_t *tcalrec) {
    int len = 0;
    memcpy(dest+len, &tcalrec->hdr, sizeof(tcalrec->hdr));
    len += sizeof(tcalrec->hdr);
    memcpy(dest+len, &tcalrec->dor_t0, sizeof(tcalrec->dor_t0));
    len += sizeof(tcalrec->dor_t0);
    memcpy(dest+len, &tcalrec->dor_t3, sizeof(tcalrec->dor_t3));
    len += sizeof(tcalrec->dor_t3);
    memcpy(dest+len, tcalrec->dorwf, sizeof(tcalrec->dorwf[0])*DH_MAX_TCAL_WF_LEN);
    len += sizeof(tcalrec->dorwf[0])*DH_MAX_TCAL_WF_LEN;
    memcpy(dest+len, &tcalrec->dom_t1, sizeof(tcalrec->dom_t1));
    len += sizeof(tcalrec->dom_t1);
    memcpy(dest+len, &tcalrec->dom_t2, sizeof(tcalrec->dom_t2));
    len += sizeof(tcalrec->dom_t2);
    memcpy(dest+len, tcalrec->domwf, sizeof(tcalrec->domwf[0])*DH_MAX_TCAL_WF_LEN);
    len += sizeof(tcalrec->domwf[0])*DH_MAX_TCAL_WF_LEN;

    return (len == DH_TCAL_STRUCT_LEN);
}

/* Unpack buffer into structure */
int dh_tcalib_unpack(struct dh_tcalib_t *tcalrec, unsigned char *src) {
    int len = 0;
    memcpy(&tcalrec->hdr, src+len, sizeof(tcalrec->hdr));
    len += sizeof(tcalrec->hdr);
    memcpy(&tcalrec->dor_t0, src+len, sizeof(tcalrec->dor_t0));
    len += sizeof(tcalrec->dor_t0);
    memcpy(&tcalrec->dor_t3, src+len, sizeof(tcalrec->dor_t3));
    len += sizeof(tcalrec->dor_t3);
    memcpy(tcalrec->dorwf, src+len, sizeof(tcalrec->dorwf[0])*DH_MAX_TCAL_WF_LEN);
    len += sizeof(tcalrec->dorwf[0])*DH_MAX_TCAL_WF_LEN;
    memcpy(&tcalrec->dom_t1, src+len, sizeof(tcalrec->dom_t1));
    len += sizeof(tcalrec->dom_t1);
    memcpy(&tcalrec->dom_t2, src+len, sizeof(tcalrec->dom_t2));
    len += sizeof(tcalrec->dom_t2);
    memcpy(tcalrec->domwf, src+len, sizeof(tcalrec->domwf[0])*DH_MAX_TCAL_WF_LEN);
    len += sizeof(tcalrec->domwf[0])*DH_MAX_TCAL_WF_LEN;

    return (len == DH_TCAL_STRUCT_LEN);
}

#endif /* __DH_TCALIB__ */

