#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#include "iaxxx-tunnel-intf.h"

#include "tunnel.h"
#include "logger.h"
#include "knowles_tunnel_pcm.h"

#define MAX_TUNNELS         (32)
#define BUF_SIZE            (8192)

struct kt_pcm {
    struct ia_tunneling_hal *tnl_hdl;

    int end_point;
    bool enable_stripping;
    unsigned int start_frame;

    void *pcm_buf;
    unsigned int pcm_buf_size;
    unsigned int pcm_avail_size;
    unsigned int pcm_read_offset;

    void *unparsed_buf;
    unsigned int unparsed_buf_size;
    unsigned int unparsed_avail_size;

    unsigned int lastseqno;
    unsigned int framerepeat;
    unsigned int framedrop;

#ifdef DUMP_UNPARSED_OUTPUT
    FILE* dump_file;
#endif
};

struct raf_format_type {
    uint16_t frameSizeInBytes;    // Frame length in bytes
    uint8_t encoding;             // Encoding
    uint8_t sampleRate;           // Sample rate
};

struct raf_frame_type {
    uint64_t timeStamp;             // Timestamp of the frame
    uint32_t seqNo;                 // Optional sequence number of the frame
    struct raf_format_type format;  // Format information for the frame
    uint32_t data[0];               /* Start of the variable size payload.
                                       It must start at 128 bit aligned
                                       address for all the frames */
};

struct kt_pcm* kt_pcm_open(struct kt_config *kc, struct kt_preroll *preroll)
{
    struct kt_pcm *kt_pcm_hdl = NULL;
    int err = 0;

    kt_pcm_hdl = (struct kt_pcm *) malloc(sizeof(struct kt_pcm));
    if (NULL == kt_pcm_hdl) {
        ALOGE("%s: ERROR Failed to allocated memory", __func__);
        goto exit_on_error;
    }

    kt_pcm_hdl->end_point = kc->end_point;
    kt_pcm_hdl->start_frame = 0;
    kt_pcm_hdl->enable_stripping = false;
    if (preroll->preroll_en) {
        if (0 == preroll->frame_size_in_ms) {
            ALOGE("%s: ERROR Frame size cannot be zero", __func__);
            goto exit_on_error;
        }
        unsigned int preroll_frame = preroll->preroll_time_in_ms /
                                            preroll->frame_size_in_ms;
        kt_pcm_hdl->enable_stripping = true;
        // Start frame is preroll + keyword start frame
        if (preroll->kw_start_frame > preroll_frame) {
            kt_pcm_hdl->start_frame = preroll->kw_start_frame -
                                            preroll_frame;
        } else {
            kt_pcm_hdl->start_frame = 0;
        }
    }
    ALOGD("%s: Start frame = %d", __func__, kt_pcm_hdl->start_frame);

    kt_pcm_hdl->tnl_hdl = ia_start_tunneling(kc->tunnel_output_buffer_size);
    if (NULL == kt_pcm_hdl->tnl_hdl) {
        ALOGE("%s: ERROR Failed to start tunneling", __func__);
        goto exit_on_error;
    }

    err = ia_enable_tunneling_source(kt_pcm_hdl->tnl_hdl, kc->end_point,
                                     TNL_MODE_SYNC, TNL_ENC_Q15);
    if (err != 0) {
        ALOGE("%s: ERROR Failed to enable the tunnel 0x%X",
                __func__, kc->end_point);
        goto exit_on_error;
    }

    kt_pcm_hdl->unparsed_buf_size = BUF_SIZE * 2;
    kt_pcm_hdl->unparsed_avail_size = 0;
    kt_pcm_hdl->unparsed_buf = malloc(kt_pcm_hdl->unparsed_buf_size);
    if (kt_pcm_hdl->unparsed_buf == NULL) {
        ALOGE("%s: ERROR Failed to allocate memory for unparsed buffer",
                __func__);
        goto exit_on_error;
    }

    kt_pcm_hdl->pcm_buf_size = BUF_SIZE * 2;
    kt_pcm_hdl->pcm_avail_size = 0;
    kt_pcm_hdl->pcm_read_offset = 0;
    kt_pcm_hdl->pcm_buf = malloc(kt_pcm_hdl->pcm_buf_size);
    if (kt_pcm_hdl->pcm_buf == NULL) {
        ALOGE("%s: ERROR Failed to allocate memory for pcm buffer",
                __func__);
        goto exit_on_error;
    }

    kt_pcm_hdl->lastseqno = 0;
    kt_pcm_hdl->framerepeat = 0;
    kt_pcm_hdl->framedrop = 0;

    return kt_pcm_hdl;

exit_on_error:
    if (kt_pcm_hdl->pcm_buf)
        free(kt_pcm_hdl->pcm_buf);

    if (kt_pcm_hdl->unparsed_buf)
        free(kt_pcm_hdl->unparsed_buf);

    if (kt_pcm_hdl->tnl_hdl) {
        ia_disable_tunneling_source(kt_pcm_hdl->tnl_hdl, kt_pcm_hdl->end_point,
                                    TNL_MODE_SYNC, TNL_ENC_Q15);
        ia_stop_tunneling(kt_pcm_hdl->tnl_hdl);
    }

    if (kt_pcm_hdl)
        free(kt_pcm_hdl);

    return NULL;
}

static int parse_tunnel_buf(struct kt_pcm *kt_pcm_hdl)
{
    // The magic number is ROME in ASCII reversed.
    // So we are looking for EMOR in the byte stream
    const unsigned char magic_num[4] = {0x45, 0x4D, 0x4F, 0x52};
    unsigned short int tunnel_id;
    bool valid_frame = true;
    unsigned char *buf_itr = kt_pcm_hdl->unparsed_buf;
    // Minimum bytes required is magic number + tunnel id + reserved and crc + raf struct
    int min_bytes_req = 4 + 2 + 6 + sizeof(struct raf_frame_type);
    int bytes_avail = kt_pcm_hdl->unparsed_avail_size;
    unsigned char *pcm_buf_itr = NULL;
    int curr_pcm_frame_size;
    bool is_q15_conversion_required = false;

    if (buf_itr == NULL) {
        ALOGE("Invalid input sent to parse_tunnel_buf");
        return 0;
    }

    do {
        // Check for MagicNumber 0x454D4F52
        while (buf_itr[0] != magic_num[0] || buf_itr[1] != magic_num[1] ||
                buf_itr[2] != magic_num[2] || buf_itr[3] != magic_num[3]) {
            buf_itr++;
            bytes_avail--;
            if (bytes_avail <= 0) {
                ALOGE("Could not find the magic number, reading again");
                ALOGE("buf_itr[0] %x buf_itr[1] %x buf_itr[2] %x buf_itr[3] %x",
                        buf_itr[0], buf_itr[1], buf_itr[2], buf_itr[3]);
                return 0;
            }
        }

        // Skip the magic number
        buf_itr += 4;
        bytes_avail -= 4;

        // Read the tunnelID
        tunnel_id = ((unsigned char) (buf_itr[0]) |
                (unsigned char) (buf_itr[1]) << 8);

        // Skip tunnelID
        buf_itr += 2;
        bytes_avail -= 2;

        // Skip Reserved field and CRC - 6 bytes in total
        buf_itr += 6;
        bytes_avail -= 6;

        valid_frame = true;
        // There is only one tunnel data we are looking
        if (tunnel_id > MAX_TUNNELS ) {
            ALOGE("Invalid tunnel id %d\n", tunnel_id);
            valid_frame = false;
        }

        struct raf_frame_type rft;
        memcpy(&rft, buf_itr, sizeof(struct raf_frame_type));

        bool skip_extra_data = false;
        if ((kt_pcm_hdl->enable_stripping == true) &&
            (rft.seqNo < kt_pcm_hdl->start_frame)) {
            skip_extra_data = true;
        }

        // 1 indicates that it is afloat encoding and
        // F indicates it is in q15 encoding
        if (rft.format.encoding == 1) {
            is_q15_conversion_required = true;
            curr_pcm_frame_size = rft.format.frameSizeInBytes / 2;
        } else {
            is_q15_conversion_required = false;
            curr_pcm_frame_size = rft.format.frameSizeInBytes;
        }

        // Skip the raf_frame_type
        buf_itr += sizeof(struct raf_frame_type);
        bytes_avail -= sizeof(struct raf_frame_type);

        if (bytes_avail < rft.format.frameSizeInBytes) {
            ALOGV("Incomplete frame received bytes_avail %d framesize %d",
                        bytes_avail, rft.format.frameSizeInBytes);
            bytes_avail += min_bytes_req;
            break;
        }

        if (valid_frame == true && skip_extra_data == false) {
            if ((kt_pcm_hdl->pcm_avail_size + curr_pcm_frame_size) <
                    kt_pcm_hdl->pcm_buf_size) {
                pcm_buf_itr = (unsigned char*) kt_pcm_hdl->pcm_buf +
                                kt_pcm_hdl->pcm_avail_size;
                memcpy(pcm_buf_itr, buf_itr, rft.format.frameSizeInBytes);
#ifdef ENABLE_DEBUG_DUMPS
                FILE *out_fp = fopen("/data/data/pcm_dump",
                        "ab");
                if (out_fp) {
                    //ALOGE("Dumping to pcm_dump");
                    fwrite(pcm_buf_itr, rft.format.frameSizeInBytes, 1, out_fp);
                    fflush(out_fp);
                    fclose(out_fp);
                } else {
                    ALOGE("Failed to open the out_fp file %s", strerror(errno));
                    ALOGE("out_fp is NULL");
                }
#endif
                kt_pcm_hdl->pcm_avail_size += curr_pcm_frame_size;
            } else {
                ALOGD("Not enough PCM buffer available break now");
                bytes_avail += min_bytes_req;
                break;
            }
        }

        if (kt_pcm_hdl->lastseqno == 0) {
            kt_pcm_hdl->lastseqno = rft.seqNo;
        } else {
            int diff = 0;
            if (rft.seqNo > (kt_pcm_hdl->lastseqno + 1)) {
                // Our current frame number is bigger than one frame
                diff = (rft.seqNo - kt_pcm_hdl->lastseqno - 1);
                ALOGE("Frame drop");
                ALOGE("Previous seq no %u Current seq no %u",
                        kt_pcm_hdl->lastseqno, rft.seqNo);
                kt_pcm_hdl->framedrop += diff;
            } else if (rft.seqNo <= kt_pcm_hdl->lastseqno) {
                diff = (int) ((int) kt_pcm_hdl->lastseqno - (int) rft.seqNo) + 1;
                ALOGE("Frame repeat at seq no %u for %d frames", rft.seqNo, diff);
                kt_pcm_hdl->framerepeat += diff;
            }
            kt_pcm_hdl->lastseqno = rft.seqNo;
        }

        // Skip the data
        buf_itr += rft.format.frameSizeInBytes;
        bytes_avail -= rft.format.frameSizeInBytes;
    } while (bytes_avail > min_bytes_req);

    return bytes_avail;
}

int kt_pcm_read(struct kt_pcm *kt_pcm_hdl, void *buffer, const int bytes)
{
    int ret;
    int bytes_read, bytes_rem;

    if (kt_pcm_hdl == NULL) {
        ALOGE("Invalid handle");
        ret = 0;
        goto exit;
    }

    if (bytes > kt_pcm_hdl->pcm_avail_size) {
        // We don't have enough PCM data, read more from the device.
        // First copy the remainder of the PCM buffer to the front
        // of the PCM buffer
        if (0 != kt_pcm_hdl->pcm_avail_size) {
            ALOGV("Copying to the front of the buffer pcm_avail_size %u"
                  " pcm_read_offset %u", kt_pcm_hdl->pcm_avail_size,
                    kt_pcm_hdl->pcm_read_offset);
            memcpy(kt_pcm_hdl->pcm_buf,
                   ((unsigned char*)kt_pcm_hdl->pcm_buf +
                        kt_pcm_hdl->pcm_read_offset),
                    kt_pcm_hdl->pcm_avail_size);
        }
        // Always read from the start of the PCM buffer at this point of time
        kt_pcm_hdl->pcm_read_offset = 0;

read_again:
        // Read data from the kernel, account for the leftover
        // data from previous run
        bytes_read = ia_read_tunnel_data(kt_pcm_hdl->tnl_hdl,
                                         (void *)((unsigned char *)
                                            kt_pcm_hdl->unparsed_buf +
                                            kt_pcm_hdl->unparsed_avail_size),
                                         BUF_SIZE);
        if (bytes_read <= 0) {
            ALOGE("Failed to read data from tunnel");
            ret = 0; // TODO should we try to read a couple of times?
            goto exit;
        }
#ifdef ENABLE_DEBUG_DUMPS
        {
            FILE *fp = fopen("/data/data/unparsed_output", "ab");
            if (fp) {
                fwrite((void *)((unsigned char *) kt_pcm_hdl->unparsed_buf +
                        kt_pcm_hdl->unparsed_avail_size), bytes_read, 1, fp);
                fflush(fp);
                fclose(fp);
            }
        }
#endif

        // Parse the data to get PCM data
        kt_pcm_hdl->unparsed_avail_size += bytes_read;
        bytes_rem = parse_tunnel_buf(kt_pcm_hdl);

#ifdef ENABLE_DEBUG_DUMPS
        if (kt_pcm_hdl->pcm_avail_size != 0) {
            FILE *out_fp = fopen("/data/data/pcm_dump2",
                                  "ab");
            if (out_fp) {
                //ALOGE("Dumping to pcm_dump2");
                fwrite(((unsigned char*) kt_pcm_hdl->pcm_buf +
                            kt_pcm_hdl->pcm_avail_size),
                        kt_pcm_hdl->pcm_avail_size, 1, out_fp);
                fflush(out_fp);
                fclose(out_fp);
            } else {
                ALOGE("Failed to open the pcm_dump2 file %s", strerror(errno));
            }
        }
#endif

        // Copy the left over unparsed data to the front of the buffer
        if (bytes_rem != 0) {
            int offset = kt_pcm_hdl->unparsed_avail_size - bytes_rem;
            memcpy(kt_pcm_hdl->unparsed_buf,
                   ((unsigned char*)kt_pcm_hdl->unparsed_buf + offset),
                    bytes_rem);
        }
        kt_pcm_hdl->unparsed_avail_size = bytes_rem;

        // If stripping is enabled then we didn't read anything to the pcm
        // bufferso read again or if we still don't have enough bytes then
        // read data again.
        if (kt_pcm_hdl->pcm_avail_size == 0 ||
            kt_pcm_hdl->pcm_avail_size < bytes) {
            goto read_again;
        }
    }

    // Copy the PCM data to output buffer and return
    memcpy(buffer,
           ((unsigned char*)kt_pcm_hdl->pcm_buf +
                kt_pcm_hdl->pcm_read_offset),
            bytes);

#ifdef ENABLE_DEBUG_DUMPS
    FILE *out_fp = fopen("/data/data/adnc_dump",
                          "ab");
    if (out_fp) {
        //ALOGE("Dumping to adnc_dump");
        fwrite(buffer, bytes, 1, out_fp);
        fflush(out_fp);
        fclose(out_fp);
    } else {
        ALOGE("Failed to open the adnc_dump file %s", strerror(errno));
    }
#endif

    kt_pcm_hdl->pcm_avail_size -= bytes;
    kt_pcm_hdl->pcm_read_offset += bytes;

    ret = bytes;
exit:
    return ret;
}

int kt_pcm_close(struct kt_pcm *kt_pcm_hdl)
{
    int ret = 0;

    if (kt_pcm_hdl == NULL) {
        ALOGE("Invalid handle");
        ret = -1;
        goto exit;
    }

    if (kt_pcm_hdl->pcm_buf) {
        free(kt_pcm_hdl->pcm_buf);
    }

    if (kt_pcm_hdl->unparsed_buf) {
        free(kt_pcm_hdl->unparsed_buf);
    }

    ret = ia_disable_tunneling_source(kt_pcm_hdl->tnl_hdl,
                                      kt_pcm_hdl->end_point,
                                      TNL_MODE_SYNC,
                                      TNL_ENC_Q15);
    if (ret != 0) {
        ALOGE("Failed to disable the tunneling source");
    }

    ret = ia_stop_tunneling(kt_pcm_hdl->tnl_hdl);
    if (ret != 0) {
        ALOGE("Failed to stop tunneling");
    }

    ALOGE("Total Frames repeated %u", kt_pcm_hdl->framerepeat);
    ALOGE("Total Frames dropped %u", kt_pcm_hdl->framedrop);

    if (kt_pcm_hdl) {
        free(kt_pcm_hdl);
    }

exit:
    return ret;
}
