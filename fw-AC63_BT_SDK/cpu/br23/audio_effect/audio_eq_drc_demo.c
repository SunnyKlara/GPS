
#include "system/includes.h"
#include "media/includes.h"
#include "app_config.h"
#include "clock_cfg.h"
#include "media/audio_eq.h"
#include "audio_eq_drc_demo.h"
#include "effects_adj.h"


//系数切换
void eq_sw_demo()
{
    eq_mode_sw();//7种默认系数切换
}

//获取当前eq系数表类型
void eq_mode_get_demo()
{
    u8 mode ;
    mode = eq_mode_get_cur();
}
//自定义系数表动态更新
//本demo 示意更新中心截止频率，增益，总增益，如需设置更多参数，请查看eq_config.h头文件的demo
void eq_update_demo()
{
    eq_mode_set_custom_info(0, 200, 2);//第0段,200Hz中心截止频率，2db
    eq_mode_set_custom_info(5, 2000, 2);//第5段,2000Hz中心截止频率，2db

    set_global_gain(EQ_MODE_CUSTOM, -1);//-1表示 -1dB
    eq_mode_set(EQ_MODE_CUSTOM);//设置系数、总增益更新
}


#if defined(TCFG_EQ_ENABLE) && TCFG_EQ_ENABLE
struct music_eq_tool music_eq_parm = {0};
struct music_eq_tool fl_eq_parm_tmp;//fl通道eq
#endif
struct audio_eq *music_eq_open(u32 sample_rate, u8 ch_num)
{
#if defined(TCFG_EQ_ENABLE) && TCFG_EQ_ENABLE
    memcpy(&fl_eq_parm_tmp, &music_eq_parm, sizeof(fl_eq_parm_tmp));
    struct audio_eq_param parm = {0};
    parm.channels = ch_num;
    parm.no_wait = 0;
    parm.cb = eq_get_filter_info;
    parm.sr = sample_rate;
    parm.eq_name = AEID_MUSIC_EQ;
    parm.max_nsection = fl_eq_parm_tmp.seg_num;
    parm.nsection = fl_eq_parm_tmp.seg_num;
    parm.seg = fl_eq_parm_tmp.seg;
    parm.global_gain = fl_eq_parm_tmp.global_gain;

    parm.fade = 1;//使能系数淡入
    parm.fade_step = 0.2f;//淡入步进（0.1f~1.0f）
    parm.g_fade_step = 0.4f;//总增益步进
    parm.f_fade_step = 100;//中心截止频率步进Hz

    struct audio_eq *eq = audio_dec_eq_open(&parm);
    clock_add(EQ_CLK);
    return eq;
#endif //TCFG_EQ_ENABLE

    return NULL;
}

void music_eq_close(struct audio_eq *eq)
{
#if defined(TCFG_EQ_ENABLE) && TCFG_EQ_ENABLE
    if (eq) {
        audio_dec_eq_close(eq);
        clock_remove(EQ_CLK);
    }
#endif/*TCFG_EQ_ENABLE*/
}

#if TCFG_EQ_DIVIDE_ENABLE
#if defined(TCFG_EQ_ENABLE) && TCFG_EQ_ENABLE
struct music_eq_tool rl_eq_parm_tmp;//rl通道eq
#endif
struct audio_eq *music_eq_rl_rr_open(u32 sample_rate, u8 ch_num)
{
#if defined(TCFG_EQ_ENABLE) && TCFG_EQ_ENABLE
    memcpy(&rl_eq_parm_tmp, &rl_eq_parm, sizeof(rl_eq_parm_tmp));
    struct audio_eq_param parm = {0};
    parm.channels = ch_num;
    parm.no_wait = 0;
    parm.cb = eq_get_filter_info;
    parm.sr = sample_rate;
    parm.eq_name = AEID_MUSIC_RL_EQ;

    parm.max_nsection = rl_eq_parm_tmp.seg_num;
    parm.nsection = rl_eq_parm_tmp.seg_num;
    parm.seg = rl_eq_parm_tmp.seg;
    parm.global_gain = rl_eq_parm_tmp.global_gain;

    parm.fade = 1;//增益更新差异大，会引入哒哒音，此处使能系数淡入
    parm.fade_step = 0.2f;//淡入步进（0.1f~1.0f）
    parm.g_fade_step = 0.4f;//总增益步进

    struct audio_eq *eq = audio_dec_eq_open(&parm);
    clock_add(EQ_CLK);
    return eq;
#endif //TCFG_EQ_ENABLE
    return NULL;
}

void music_eq_rl_rr_close(struct audio_eq *eq)
{
#if defined(TCFG_EQ_ENABLE) && TCFG_EQ_ENABLE
    if (eq) {
        audio_dec_eq_close(eq);
        clock_remove(EQ_CLK);
    }
#endif/*TCFG_EQ_ENABLE*/
}
#endif
