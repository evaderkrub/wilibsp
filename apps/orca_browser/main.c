#include "fw2.h"
#include "platform/diag.h"
#include "platform/psram.h"
#include "story_data.h"
#include "lvgl_port.h"
#include "lvgl.h"
#include "pico/stdlib.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#define NAVY lv_color_hex(0x0A0A0B)
#define TEAL lv_color_hex(0xFF2E43)
#define CYAN lv_color_hex(0xFF2E43)
#define CORAL lv_color_hex(0xFFB340)
#define CREAM lv_color_hex(0xECEDF1)
#define PANEL lv_color_hex(0x141417)
#define MUTED lv_color_hex(0x9295A0)
#define FAINT lv_color_hex(0x62656F)
static uint8_t story_index;
static uint8_t sound_mode=2;
static uint8_t active_cat;
static uint8_t list_page;
static bool audio_ready;
static uint32_t *sound_buf;
#define SOUND_FRAMES 24576u
static const uint8_t first_story[4]={0,20,63,65};
static const char *cat_name[4]={"ORCA FACTS","FAMOUS ORCAS","ORCAS IN POP CULTURE","ORCA BOOKS & MOVIES"};
static const uint8_t cat_count[4]={20,43,2,7};

static void screen_home(void); static void screen_story(void); static void screen_qr(bool watch);
static lv_image_dsc_t scaled_art;
static void prepare_scaled_art(const lv_image_dsc_t *src){
uint8_t *d=(uint8_t *)PSRAM_BASE+SOUND_FRAMES*4u;memset(d,0,8+24*240);d[0]=24;d[1]=14;d[2]=74;d[3]=255;d[4]=255;d[5]=255;d[6]=255;d[7]=255;
for(int y=0;y<120;y++)for(int x=0;x<96;x++){uint8_t on=(src->data[8+y*12+(x>>3)]>>(7-(x&7)))&1;for(int yy=0;yy<2;yy++)for(int xx=0;xx<2;xx++){int dx=x*2+xx,dy=y*2+yy;if(on)d[8+dy*24+(dx>>3)]|=1u<<(7-(dx&7));}}
memset(&scaled_art,0,sizeof(scaled_art));scaled_art.header.magic=LV_IMAGE_HEADER_MAGIC;scaled_art.header.cf=LV_COLOR_FORMAT_I1;scaled_art.header.w=192;scaled_art.header.h=240;scaled_art.header.stride=24;scaled_art.data_size=8+24*240;scaled_art.data=d;
}
static void wipe(void){lv_obj_clean(lv_screen_active());lv_obj_set_style_bg_color(lv_screen_active(),NAVY,0);lv_obj_set_style_bg_opa(lv_screen_active(),LV_OPA_COVER,0);}
static lv_obj_t *label(lv_obj_t *p,const char *txt,int x,int y,int w,const lv_font_t *font,lv_color_t color){lv_obj_t *l=lv_label_create(p);lv_label_set_text(l,txt);lv_obj_set_width(l,w);lv_label_set_long_mode(l,LV_LABEL_LONG_WRAP);lv_obj_set_pos(l,x,y);lv_obj_set_style_text_font(l,font,0);lv_obj_set_style_text_color(l,color,0);return l;}
static lv_obj_t *button(lv_obj_t *p,const char *txt,int x,int y,int w,int h,lv_event_cb_t cb,void *ud){lv_obj_t *b=lv_button_create(p);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,h);lv_obj_set_style_bg_color(b,lv_color_hex(0x0D0D0F),0);lv_obj_set_style_border_color(b,lv_color_hex(0x35353B),0);lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_radius(b,0,0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,ud);lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,txt);lv_obj_set_style_text_font(l,&lv_font_montserrat_14,0);lv_obj_set_style_text_color(l,CREAM,0);lv_obj_center(l);return b;}
static lv_obj_t *box(lv_obj_t *p,int x,int y,int w,int h,lv_color_t bg){lv_obj_t *o=lv_obj_create(p);lv_obj_set_pos(o,x,y);lv_obj_set_size(o,w,h);lv_obj_set_style_bg_color(o,bg,0);lv_obj_set_style_border_width(o,0,0);lv_obj_set_style_radius(o,0,0);lv_obj_set_style_pad_all(o,0,0);lv_obj_clear_flag(o,LV_OBJ_FLAG_SCROLLABLE);return o;}
static void eye_anim(void *o,int32_t v){lv_obj_set_size((lv_obj_t*)o,v,v);}
extern const uint8_t ocean_adpcm[], orca_adpcm[];
extern const lv_image_dsc_t freewili_logo;
extern const lv_image_dsc_t orca_art_pod,orca_art_echo,orca_art_matriarch,orca_art_noise;
extern const uint32_t ocean_adpcm_samples, orca_adpcm_samples;
static const uint16_t ima_step[89]={7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,22385,24623,27086,29794,32767};
static const int8_t ima_index[8]={-1,-1,-1,-1,2,4,6,8};
static uint32_t stereo(int16_t v){uint16_t s=(uint16_t)v;return ((uint32_t)s<<16)|s;}
static void decode_real_sound(const uint8_t *src,uint32_t samples){
    int pred=0,idx=0;
    for(uint32_t i=0;i<samples;i++){
        uint8_t code=(src[i>>1]>>((i&1)*4))&15; int step=ima_step[idx];
        int delta=step>>3;if(code&4)delta+=step;if(code&2)delta+=step>>1;if(code&1)delta+=step>>2;
        pred+=(code&8)?-delta:delta;if(pred>32767)pred=32767;if(pred< -32768)pred=-32768;
        idx+=ima_index[code&7];if(idx<0)idx=0;if(idx>88)idx=88;int boosted=pred*2;if(boosted>32767)boosted=32767;if(boosted< -32768)boosted=-32768;sound_buf[i]=stereo((int16_t)boosted);
    }
}
static void audio_init_once(void){if(audio_ready)return;codec_nau88c10_init();codec_nau88c10_dac_mute(false);audio_i2s_duplex_init(8000);audio_capture_start();audio_ready=true;}
static void sound_toggle(lv_event_t *e){(void)e;audio_init_once();audio_i2s_duplex_play_stop();sound_mode=(sound_mode+1)%3;if(sound_mode){if(sound_mode==1)decode_real_sound(ocean_adpcm,ocean_adpcm_samples);else decode_real_sound(orca_adpcm,orca_adpcm_samples);codec_nau88c10_set_output(CODEC_OUT_SPEAKER);codec_nau88c10_dac_mute(false);audio_i2s_duplex_play_stream_loop(sound_buf,SOUND_FRAMES);}else codec_nau88c10_speaker_low_power();screen_story();}
static void cat_click(lv_event_t *e){active_cat=(uint8_t)(uintptr_t)lv_event_get_user_data(e);list_page=0;screen_home();}
static void home_click(lv_event_t *e){(void)e;screen_home();}
static void topic_click(lv_event_t *e){story_index=(uint8_t)(uintptr_t)lv_event_get_user_data(e);screen_story();}
static void list_prev(lv_event_t *e){(void)e;if(list_page)list_page--;screen_home();}
static void list_next(lv_event_t *e){(void)e;uint8_t pages=(cat_count[active_cat]+4)/5;if(list_page+1<pages)list_page++;screen_home();}
static void prev_click(lv_event_t *e){(void)e;if(story_index)story_index--;screen_story();}
static void next_click(lv_event_t *e){(void)e;if(story_index+1<g_orca_story_count)story_index++;screen_story();}
static void qr_click(lv_event_t *e){(void)e;screen_qr(false);} static void story_click(lv_event_t *e){(void)e;screen_story();}
static void read_click(lv_event_t *e){(void)e;screen_qr(false);} static void watch_click(lv_event_t *e){(void)e;screen_qr(true);}
static void site_click(lv_event_t *e){(void)e;wipe();label(lv_screen_active(),"FREEWILI.COM",18,12,250,&lv_font_montserrat_20,CYAN);lv_obj_t *q=lv_qrcode_create(lv_screen_active());lv_obj_set_size(q,220,220);lv_obj_align(q,LV_ALIGN_CENTER,0,8);const char *u="https://freewili.com/";lv_qrcode_update(q,u,strlen(u));button(lv_screen_active(),"BACK",380,270,88,38,home_click,NULL);}
static void draw_mascot(void){
box(lv_screen_active(),0,0,190,272,lv_color_hex(0x0D0D0F));
static lv_point_precise_t body[]={{24,126},{39,92},{74,72},{126,72},{158,93},{170,124},{151,151},{109,164},{62,157},{31,140},{24,126}};static lv_point_precise_t hna[]={{50,91},{39,52},{68,78}};static lv_point_precise_t hnb[]={{132,77},{157,49},{149,94}};
lv_obj_t *ln=lv_line_create(lv_screen_active());lv_line_set_points(ln,body,11);lv_obj_set_style_line_color(ln,TEAL,0);lv_obj_set_style_line_width(ln,2,0);lv_obj_t *h1=lv_line_create(lv_screen_active());lv_line_set_points(h1,hna,3);lv_obj_set_style_line_color(h1,TEAL,0);lv_obj_set_style_line_width(h1,2,0);lv_obj_t *h2=lv_line_create(lv_screen_active());lv_line_set_points(h2,hnb,3);lv_obj_set_style_line_color(h2,TEAL,0);lv_obj_set_style_line_width(h2,2,0);
for(int i=0;i<2;i++){lv_obj_t *eye=box(lv_screen_active(),i?116:62,104,9,9,CORAL);lv_obj_set_style_radius(eye,9,0);lv_anim_t a;lv_anim_init(&a);lv_anim_set_var(&a,eye);lv_anim_set_exec_cb(&a,eye_anim);lv_anim_set_values(&a,7,14);lv_anim_set_duration(&a,750);lv_anim_set_playback_duration(&a,750);lv_anim_set_repeat_delay(&a,700);lv_anim_set_repeat_count(&a,LV_ANIM_REPEAT_INFINITE);lv_anim_start(&a);}
label(lv_screen_active(),"^ ^ ^ ^ ^",67,128,70,&lv_font_montserrat_14,CREAM);label(lv_screen_active(),"FREE-WILI 2",23,190,155,&lv_font_montserrat_20,CREAM);label(lv_screen_active(),"ORCA FIELD NOTES",27,216,150,&lv_font_montserrat_14,TEAL);box(lv_screen_active(),24,235,142,1,lv_color_hex(0x35353B));label(lv_screen_active(),"EXPLORE / LEARN / PROTECT",25,244,155,&lv_font_montserrat_14,MUTED);
}
static void screen_home(void){
wipe();box(lv_screen_active(),0,0,480,48,lv_color_hex(0x34343A));label(lv_screen_active(),"ORCA / FIELD NOTES",16,9,260,&lv_font_montserrat_20,CREAM);label(lv_screen_active(),"72 DOCUMENTED STORIES",17,32,220,&lv_font_montserrat_14,TEAL);lv_obj_t *logo=button(lv_screen_active(),"",336,4,132,42,site_click,NULL);lv_obj_set_style_bg_color(logo,lv_color_hex(0x34343A),0);lv_obj_set_style_border_width(logo,0,0);lv_obj_set_style_pad_all(logo,0,0);lv_obj_t *mark=lv_image_create(logo);lv_image_set_src(mark,&freewili_logo);lv_obj_center(mark);
for(int c=0;c<4;c++){char tab[38];snprintf(tab,sizeof(tab),"%02d %s",c+1,c==0?"FACTS":c==1?"FAMOUS":c==2?"CULTURE":"MEDIA");lv_obj_t *b=button(lv_screen_active(),tab,c*120,52,120,40,cat_click,(void*)(uintptr_t)c);lv_obj_set_style_bg_color(b,c==active_cat?TEAL:PANEL,0);lv_obj_set_style_text_color(lv_obj_get_child(b,0),c==active_cat?lv_color_white():MUTED,0);}
char section[80];snprintf(section,sizeof(section),"SECTION %02u / %s / %02u STORIES",active_cat+1,cat_name[active_cat],cat_count[active_cat]);label(lv_screen_active(),section,16,101,440,&lv_font_montserrat_14,CORAL);
int start=list_page*5,end=start+5;if(end>cat_count[active_cat])end=cat_count[active_cat];for(int n=start;n<end;n++){uint8_t idx=first_story[active_cat]+n;char row[112];snprintf(row,sizeof(row),"%02u   %s",idx+1,g_orca_stories[idx].title);lv_obj_t *it=button(lv_screen_active(),row,14,124+(n-start)*28,452,27,topic_click,(void*)(uintptr_t)idx);lv_obj_set_style_bg_color(it,(n-start)%2?lv_color_hex(0x101012):PANEL,0);lv_obj_set_style_border_color(it,lv_color_hex(0x29292E),0);lv_obj_t *il=lv_obj_get_child(it,0);lv_obj_set_style_text_align(il,LV_TEXT_ALIGN_LEFT,0);lv_obj_set_width(il,420);}
uint8_t pages=(cat_count[active_cat]+4)/5;char pg[48];snprintf(pg,sizeof(pg),"PAGE %u / %u",list_page+1,pages);button(lv_screen_active(),"< PREVIOUS",14,270,120,42,list_prev,NULL);label(lv_screen_active(),pg,199,283,110,&lv_font_montserrat_14,MUTED);button(lv_screen_active(),"NEXT >",346,270,120,42,list_next,NULL);
}
static void screen_story(void){wipe();const orca_story_t *st=&g_orca_stories[story_index];lv_obj_t *v=box(lv_screen_active(),0,0,187,272,lv_color_hex(0x101D20));box(v,0,180,187,92,lv_color_hex(0x080A0B));for(int i=0;i<4;i++)box(v,-20,28+i*24,230,1,lv_color_hex(i?0x244448:0x40676A));const lv_image_dsc_t *arts[4]={&orca_art_pod,&orca_art_echo,&orca_art_matriarch,&orca_art_noise};lv_obj_t *art=lv_image_create(v);prepare_scaled_art(arts[story_index&3]);lv_image_set_src(art,&scaled_art);lv_obj_set_pos(art,-2,16);char counter[24];snprintf(counter,sizeof(counter),"%02u / 72",story_index+1);label(lv_screen_active(),counter,402,10,65,&lv_font_montserrat_14,FAINT);label(lv_screen_active(),cat_name[st->category],202,12,190,&lv_font_montserrat_14,TEAL);label(lv_screen_active(),st->title,202,31,262,&lv_font_montserrat_20,CREAM);label(lv_screen_active(),st->body,202,84,262,&lv_font_montserrat_14,lv_color_hex(0xD2D7D7));box(lv_screen_active(),202,226,3,28,CORAL);label(lv_screen_active(),"SCAN SOURCE TO GO DEEPER",213,229,249,&lv_font_montserrat_14,CORAL);box(lv_screen_active(),202,260,262,3,lv_color_hex(0x252529));box(lv_screen_active(),235,260,(262*(story_index+1))/72,3,TEAL);box(lv_screen_active(),0,272,480,48,lv_color_hex(0x0D0D0F));button(lv_screen_active(),"< PREV",0,272,96,48,prev_click,NULL);button(lv_screen_active(),sound_mode==0?"~ SOUND":sound_mode==1?"~ OCEAN":"~ ORCA",96,272,96,48,sound_toggle,NULL);button(lv_screen_active(),"^ HOME",192,272,96,48,home_click,NULL);button(lv_screen_active(),"[ ] SOURCE",288,272,96,48,qr_click,NULL);button(lv_screen_active(),"NEXT >",384,272,96,48,next_click,NULL);}
static void screen_qr(bool watch){wipe();const orca_story_t *s=&g_orca_stories[story_index];label(lv_screen_active(),watch?"WATCH ON PHONE":"LEARN MORE ON PHONE",16,10,300,&lv_font_montserrat_20,CREAM);lv_obj_t *q=lv_qrcode_create(lv_screen_active());lv_obj_set_size(q,220,220);lv_obj_set_pos(q,18,48);const char *u=watch?s->watch:s->source;lv_qrcode_update(q,u,strlen(u));label(lv_screen_active(),s->title,252,52,210,&lv_font_montserrat_14,CYAN);button(lv_screen_active(),"READ",252,150,96,38,read_click,NULL);button(lv_screen_active(),"WATCH",356,150,96,38,watch_click,NULL);button(lv_screen_active(),"BACK",356,274,96,38,story_click,NULL);}
int main(void){board_init();size_t psram_bytes=psram_init();sound_buf=(uint32_t *)PSRAM_BASE;if(psram_bytes<SOUND_FRAMES*4u){for(;;)tight_loop_contents();}st7796_init();board_backlight_set(1);st7796_fill_screen(0x1706);st7796_draw_text(72,140,3,0xFFFF,0x1706,"ORCA BROWSER BOOTING");DIAG("orca_browser: panel boot banner drawn\n");lv_init();lvgl_port_init();screen_home();lv_refr_now(NULL);audio_init_once();decode_real_sound(orca_adpcm,orca_adpcm_samples);codec_nau88c10_set_output(CODEC_OUT_SPEAKER);codec_nau88c10_dac_mute(false);audio_i2s_duplex_play_stream_loop(sound_buf,SOUND_FRAMES);DIAG("orca_browser: LVGL up, stories=%u\n",g_orca_story_count);absolute_time_t beat=make_timeout_time_ms(1000);for(;;){lvgl_port_run();if(absolute_time_diff_us(get_absolute_time(),beat)<=0){DIAG("orca_browser: alive\n");beat=make_timeout_time_ms(1000);}sleep_ms(2);}}
