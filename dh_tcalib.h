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

#endif /* __DH_TCALIB__ */

