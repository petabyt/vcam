// Include binary files with incbin. Yes, this is assembly :)

// TODO: do a macro

.global eos_lv_jpg
.global eos_lv_jpg_size
.align 4
eos_lv_jpg:
.incbin "bin/eos_liveview.jpg"
.align 4
eos_lv_jpg_size:
.int eos_lv_jpg_size - eos_lv_jpg

.global bin_eos_events_bin
.global bin_eos_events_bin_len
.align 4
bin_eos_events_bin:
.incbin "bin/eos_events.bin"
.align 4
bin_eos_events_bin_len:
.int bin_eos_events_bin_len - bin_eos_events_bin
