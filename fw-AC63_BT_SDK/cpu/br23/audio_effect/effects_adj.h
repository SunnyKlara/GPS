#ifndef __EFFECTS_ADJ__H
#define __EFFECTS_ADJ__H


#include "system/includes.h"
#include "config/config_interface.h"
#include "asm/crc16.h"
#include "generic/log.h"
#include "media/audio_eq.h"
#include "app_config.h"


//AudioEffects ID(AEID) List: EQ/DRC等模块ID，识别不同模式下EQ\DRC效果用
typedef enum {
//通话下行音效处理
    AEID_ESCO_DL_EQ = 1,
    AEID_ESCO_DL_DRC,
    AEID_ESCO_DL_DRC_PRO,

//通话上行音效处理
    AEID_ESCO_UL_EQ,
    AEID_ESCO_UL_DRC,
    AEID_ESCO_UL_DRC_PRO,

//音乐播放音效处理
    AEID_MUSIC_EQ,//fl fr eq
    AEID_MUSIC_DRC,
    AEID_MUSIC_DRC_PRO,

    AEID_MUSIC_RL_EQ,


} AudioEffectsID; //模块id

#ifndef EQ_SECTION_MAX
#define EQ_SECTION_MAX (10)
#endif

#define mSECTION_MAX EQ_SECTION_MAX
struct music_eq_tool {
    float global_gain;
    int seg_num;          //eq效果文件存储的段数
    int enable_section;   //
    struct eq_seg_info seg[mSECTION_MAX];   //eq系数存储地址
};

extern struct music_eq_tool music_eq_parm;

#endif/*__EFFECTS_ADJ__H*/
