u8 wvdd_efuse_level[4] = {WVDD_VOL_SEL_065V,WVDD_VOL_SEL_070V,WVDD_VOL_SEL_075V,WVDD_VOL_SEL_080V};
static u8 wvdd_trim(u8 trim)
{
    u8 wvdd_lev = 0;
    u8 wvdd_efuse_lev = ((p33_rd_page(1) >> 24) & 0x3);
    if (trim) {
        wvdd_lev = wvdd_efuse_level[wvdd_efuse_lev]; 
    
    } else {
        wvdd_lev = get_wvdd_trim_level();
    
    }

    if (wvdd_lev < WVDD_VOL_SEL_065V) {
        wvdd_lev = WVDD_VOL_SEL_080V;  
    }

    printf("trim: %d, wvdd_lev: %d\n", trim, wvdd_lev);
    M2P_WDVDD = wvdd_lev;
    return wvdd_lev;
}


