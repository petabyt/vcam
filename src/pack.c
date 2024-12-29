// TODO: Pack data structures for EOS and other vendors

#include <vcam.h>
#include <cl_data.h>

void vcam_unpack_object_info(struct PtpObjectInfo *oi, uint8_t *d) {
	d += ptp_read_u32(d, &oi->storage_id);
	d += ptp_read_u16(d, &oi->obj_format);
	d += ptp_read_u16(d, &oi->protection);
	d += ptp_read_u32(d, &oi->compressed_size);
	d += ptp_read_u16(d, &oi->thumb_format);
	d += ptp_read_u32(d, &oi->thumb_compressed_size);
	d += ptp_read_u32(d, &oi->thumb_width);
	d += ptp_read_u32(d, &oi->thumb_height);
	d += ptp_read_u32(d, &oi->img_width);
	d += ptp_read_u32(d, &oi->img_height);
	d += ptp_read_u32(d, &oi->img_bit_depth);
	d += ptp_read_u32(d, &oi->parent_obj);
	d += ptp_read_u16(d, &oi->assoc_type);
	d += ptp_read_u32(d, &oi->assoc_desc);
	d += ptp_read_u32(d, &oi->sequence_num);
	d += ptp_read_string(d, oi->filename, sizeof(oi->filename));
	d += ptp_read_string(d, oi->date_created, sizeof(oi->date_created));
	d += ptp_read_string(d, oi->date_modified, sizeof(oi->date_modified));
	d += ptp_read_string(d, oi->keywords, sizeof(oi->keywords));
}
