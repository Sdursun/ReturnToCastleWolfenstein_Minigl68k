extern int rtcw_so_sbss_segment_start;
extern int rtcw_so_bss_segment_start;
extern int rtcw_so_sdata_segment_start;
extern int rtcw_so_data_segment_start;

extern int rtcw_so_sbss_segment_end;
extern int rtcw_so_bss_segment_end;
extern int rtcw_so_sdata_segment_end;
extern int rtcw_so_data_segment_end;

void rtcw_so_get_segment_info(void **sbss_segment_start, void **bss_segment_start, void **sdata_segment_start, void **data_segment_start, void **sbss_segment_end, void **bss_segment_end, void **sdata_segment_end, void **data_segment_end)
{
	*sbss_segment_start = &rtcw_so_sbss_segment_start;
	*bss_segment_start = &rtcw_so_bss_segment_start;
	*sdata_segment_start = &rtcw_so_sdata_segment_start;
	*data_segment_start = &rtcw_so_data_segment_start;

	*sbss_segment_end = &rtcw_so_sbss_segment_end;
	*bss_segment_end = &rtcw_so_bss_segment_end;
	*sdata_segment_end = &rtcw_so_sdata_segment_end;
	*data_segment_end = &rtcw_so_data_segment_end;
}

