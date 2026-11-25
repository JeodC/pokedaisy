/* Minimal frontend contract test; no firmware, BIOS or commercial ROM bundled. */
#include "libretro.h"
#include "pd_env.h"
#include <assert.h>
#include <dlfcn.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned fmt, w,h, frame,mask;
// The enhanced core adds its own options (pokedaisy_ds_layout); the stock wrapper does not. Its block below runs in
// either pixel format: PD_ONLY565=1 makes this a frontend that takes RGB565 alone.
static bool enhanced;
static unsigned char image[512*384*4];
static const char* directory;
// A second screen, once its width is set: what the core sends for it and the finger on it.
static struct pd_second_screen two;
static int seconds; // the pictures it got
static bool env(unsigned cmd,void* p){
 switch(cmd){
 case RETRO_ENVIRONMENT_SET_VARIABLES:
  for(const struct retro_variable* v=p;v&&v->key;++v)if(!strcmp(v->key,"pokedaisy_ds_layout"))enhanced=true;
  return true;
 case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:fmt=*(unsigned*)p;return getenv("PD_ONLY565")?fmt==2:(fmt==1||fmt==2);
 case RETRO_ENVIRONMENT_SET_GEOMETRY:return true;
 case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
 case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:*(const char**)p=directory;return true;
 case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:*(bool*)p=false;return true;
 case PD_ENV_SECOND_SCREEN:{
  struct pd_second_screen* s=p;
  seconds+=s->changed;
  two.pixels=s->pixels;two.src_w=s->src_w;two.src_h=s->src_h;two.panel=s->panel;
  s->w=two.w;s->h=two.h;s->ptr_x=two.ptr_x;s->ptr_y=two.ptr_y;s->ptr_down=two.ptr_down;s->wheel=two.wheel;two.wheel=0;
  return true;
 }
 default:return false;
 }
}
static void video(const void* p,unsigned x,unsigned y,size_t pitch){
 if(!p)return;
 assert(x<=512&&y<=384);w=x;h=y;unsigned b=fmt==1?4:2;
 for(unsigned j=0;j<y;++j)memcpy(image+j*x*b,(const char*)p+j*pitch,x*b);
}
static size_t audio(const int16_t* p,size_t n){(void)p;return n;}
static void poll(void){}
static int16_t input(unsigned p,unsigned d,unsigned i,unsigned id){(void)i;return !p&&d==RETRO_DEVICE_JOYPAD&&id<16?!!(mask&(1u<<id)):0;}
static void ppm(const char* root,const char* name){
 char path[1024];snprintf(path,sizeof path,"%s/%s.ppm",root,name);FILE* f=fopen(path,"wb");assert(f);
 fprintf(f,"P6\n%u %u\n255\n",w,h);
 for(unsigned j=0;j<w*h;++j){
  uint32_t p=fmt==1?((uint32_t*)image)[j]:((uint16_t*)image)[j];
  unsigned r,g,b;
  if(fmt==1){r=p>>16&255;g=p>>8&255;b=p&255;}
  else{r=(p>>11&31)*255/31;g=(p>>5&63)*255/63;b=(p&31)*255/31;}
  fputc(r,f);fputc(g,f);fputc(b,f);
 }fclose(f);
}
#define LOAD(n) __typeof__(&n) n##_fn=dlsym(so,#n);assert(n##_fn)
int main(int argc,char** argv){
 assert(argc>=4);directory=argv[3];
 // The run before left its settings.
 if(argc==4){char path[4096];snprintf(path,sizeof path,"%s/pokedaisy-ds/settings.ini",directory);remove(path);}
 void* so=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);if(!so){puts(dlerror());return 1;}
 LOAD(retro_set_environment);LOAD(retro_set_video_refresh);LOAD(retro_set_audio_sample_batch);
 LOAD(retro_set_input_poll);LOAD(retro_set_input_state);LOAD(retro_init);LOAD(retro_load_game);
 LOAD(retro_run);LOAD(retro_get_memory_data);LOAD(retro_get_memory_size);
 LOAD(retro_serialize_size);LOAD(retro_serialize);LOAD(retro_unserialize);
 LOAD(retro_unload_game);LOAD(retro_deinit);
 retro_set_environment_fn(env);retro_set_video_refresh_fn(video);retro_set_audio_sample_batch_fn(audio);
 retro_set_input_poll_fn(poll);retro_set_input_state_fn(input);retro_init_fn();
 struct retro_game_info game={.path=argv[2]};assert(retro_load_game_fn(&game));
 size_t saves=retro_get_memory_size_fn(RETRO_MEMORY_SAVE_RAM);
 void* save=retro_get_memory_data_fn(RETRO_MEMORY_SAVE_RAM);
 if(argc>4&&strcmp(argv[4],"-")){FILE* f=fopen(argv[4],"rb");assert(f);assert(saves&&fread(save,1,saves,f)==saves);fclose(f);}
 // Unmodified wrapper and enhanced core must accept each other's snapshots.
 if(argc>5&&strcmp(argv[5],"-")){FILE* f=fopen(argv[5],"rb");assert(f);fseek(f,0,SEEK_END);size_t n=ftell(f);rewind(f);void* p=malloc(n);assert(fread(p,1,n,f)==n);fclose(f);assert(retro_unserialize_fn(p,n));free(p);}
 unsigned frames=argc>6?atoi(argv[6]):60;
 for(frame=0;frame<frames;++frame){
  // Platinum: dismiss title, then CONTINUE. Harmless on diagnostic cart.
  mask=(frame%60>=30 && frame%60<34 && frame+240<frames)?1u<<RETRO_DEVICE_ID_JOYPAD_A:0;
  if(frame+200>=frames && frame+196<frames)mask=1u<<RETRO_DEVICE_ID_JOYPAD_START;
  if(getenv("PD_TEST_BUTTON"))mask=(frame%60>=30 && frame%60<34)?1u<<atoi(getenv("PD_TEST_BUTTON")):0;retro_run_fn();
 }
 mask=0;retro_run_fn();ppm(directory,"game");
 size_t n=retro_serialize_size_fn();assert(n>0);void* state=malloc(n);assert(retro_serialize_fn(state,n));
 char path[1024];snprintf(path,sizeof path,"%s/roundtrip.state",directory);FILE* f=fopen(path,"wb");assert(f&&fwrite(state,1,n,f)==n);fclose(f);
 void* ram=retro_get_memory_data_fn(RETRO_MEMORY_SYSTEM_RAM);size_t ramsize=retro_get_memory_size_fn(RETRO_MEMORY_SYSTEM_RAM);assert(ram&&ramsize==4*1024*1024);
 void* before=malloc(ramsize);memcpy(before,ram,ramsize);
 // States restore RAM exactly and retain the frontend's save pointer.
 for(int i=0;i<5;++i)retro_run_fn();assert(retro_unserialize_fn(state,n));assert(!memcmp(before,ram,ramsize));assert(save==retro_get_memory_data_fn(RETRO_MEMORY_SAVE_RAM));
 // PD_EXPECT_ENHANCED: this run is the enhanced core, so its block must run.
 if(getenv("PD_EXPECT_ENHANCED"))assert(enhanced);
 if(getenv("PD_ONLY565"))assert(fmt==2);
 if(enhanced){
  mask=(1u<<RETRO_DEVICE_ID_JOYPAD_SELECT)|(1u<<RETRO_DEVICE_ID_JOYPAD_L2);retro_run_fn();mask=0;retro_run_fn();assert(w==512&&h==384);assert(!memcmp(before,ram,ramsize));ppm(directory,"quick-ring");
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run_fn();mask=0;retro_run_fn();assert(w==341&&h==256);assert(!memcmp(before,ram,ramsize));ppm(directory,"party");
  for(int i=0;i<3;++i){mask=1u<<RETRO_DEVICE_ID_JOYPAD_R;retro_run_fn();mask=0;retro_run_fn();ppm(directory,i==0?"bag":i==1?"map":"dex");}
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_R;retro_run_fn();mask=0;retro_run_fn();ppm(directory,"settings");
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run_fn();mask=0;retro_run_fn();ppm(directory,"settings-picker");
  if(getenv("PD_TEST_LAYOUT")){
    int mode=atoi(getenv("PD_TEST_LAYOUT"));assert(mode>=0&&mode<7);
    for(int i=0;i<mode;i++){mask=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;retro_run_fn();mask=0;retro_run_fn();}
    mask=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run_fn();mask=0;retro_run_fn();
  }else{mask=1u<<RETRO_DEVICE_ID_JOYPAD_B;retro_run_fn();mask=0;retro_run_fn();}
  if(getenv("PD_TEST_GRID")){
    for(int i=0;i<6;i++){mask=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;retro_run_fn();mask=0;retro_run_fn();}
    mask=1u<<RETRO_DEVICE_ID_JOYPAD_A;retro_run_fn();mask=0;retro_run_fn();ppm(directory,"quick-settings");
  }
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_L2;retro_run_fn();mask=0;retro_run_fn();assert(w==512&&h==384);
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_L2;for(int i=0;i<24;i++)retro_run_fn();
  mask=0;retro_run_fn();assert(w==512&&h==384);
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_B;retro_run_fn();mask=0;retro_run_fn();assert(w==512&&h==384);
  if(getenv("PD_TEST_GRID")){
    memcpy(before,ram,ramsize);
    mask=(1u<<RETRO_DEVICE_ID_JOYPAD_SELECT)|(1u<<RETRO_DEVICE_ID_JOYPAD_L2);retro_run_fn();mask=0;retro_run_fn();assert(w==512&&h==384);assert(!memcmp(before,ram,ramsize));ppm(directory,"quick-grid");
    mask=1u<<RETRO_DEVICE_ID_JOYPAD_B;retro_run_fn();mask=0;retro_run_fn();
  }

  ppm(directory,"chosen-layout");
  if(getenv("PD_TEST_MENU")){
    for(int i=0;i<120;++i)retro_run_fn();
    mask=1u<<RETRO_DEVICE_ID_JOYPAD_X;for(int i=0;i<4;++i)retro_run_fn();mask=0;
    for(int i=0;i<90;++i)retro_run_fn();ppm(directory,"native-menu");
    for(int n=0;n<2;++n){mask=1u<<RETRO_DEVICE_ID_JOYPAD_DOWN;for(int i=0;i<4;++i)retro_run_fn();mask=0;for(int i=0;i<4;++i)retro_run_fn();}
    mask=1u<<RETRO_DEVICE_ID_JOYPAD_A;for(int i=0;i<4;++i)retro_run_fn();mask=0;
    for(int i=0;i<120;++i)retro_run_fn();ppm(directory,"native-bag");
    mask=1u<<RETRO_DEVICE_ID_JOYPAD_B;retro_run_fn();mask=0;
    for(int i=0;i<60;++i)retro_run_fn();
  }
  mask=(1u<<RETRO_DEVICE_ID_JOYPAD_SELECT)|(1u<<RETRO_DEVICE_ID_JOYPAD_R2);retro_run_fn();mask=0;retro_run_fn();assert(w==512&&h==384);ppm(directory,"manual-touch");
  mask=1u<<RETRO_DEVICE_ID_JOYPAD_R2;retro_run_fn();mask=0;retro_run_fn();assert(w==512&&h==384);ppm(directory,"bottom");
 }
 if(enhanced&&!getenv("PD_TEST_LAYOUT")&&!getenv("PD_TEST_MENU")){
  // A second screen has the DS's touch screen. The COMPANION button puts the companion there with the buttons, and the strip
  // on the game. The button again, B or START bring both back.
  mask=0;retro_run_fn();retro_unserialize_fn(state,n);two.w=1240;two.h=1080;
  #define RUNS(k) for(int i=0;i<(k);++i)retro_run_fn()
  #define TAP(id) do{mask=1u<<(id);RUNS(2);mask=0;RUNS(14);}while(0)
  #define TOUCH(x,y) do{two.ptr_x=x;two.ptr_y=y;two.ptr_down=1;RUNS(3);two.ptr_down=0;RUNS(6);}while(0)
  #define KEEP(to) memcpy(to,two.pixels,(size_t)two.src_w*two.src_h*4)
  #define SAME(as) (!memcmp(as,two.pixels,(size_t)two.src_w*two.src_h*4))
  static unsigned char plain[sizeof image];static uint32_t settings[640*480],below[640*480],touched[640*480];
  RUNS(6);assert(two.panel&&two.src_w==320&&two.src_h==278);
  seconds=0;RUNS(6);assert(seconds>0&&w==512&&h==384);memcpy(plain,image,sizeof image);
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(!two.panel&&two.src_w==320&&memcmp(plain,image,sizeof image));
  for(int button=0;button<3;++button){
   TAP(button==0?RETRO_DEVICE_ID_JOYPAD_L2:button==1?RETRO_DEVICE_ID_JOYPAD_B:RETRO_DEVICE_ID_JOYPAD_START);
   assert(two.panel&&!memcmp(plain,image,sizeof image));
   TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(!two.panel);
  }
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);
  // The switch under the touch screen shows the companion alone: the buttons stay the game's until L2, and it brings the
  // touch screen back.
  TOUCH(160,259);assert(!two.panel&&!memcmp(plain,image,sizeof image));KEEP(touched);
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(!two.panel&&memcmp(plain,image,sizeof image)&&!SAME(touched));
  TOUCH(160,259);RUNS(14);assert(two.panel&&!memcmp(plain,image,sizeof image));
  // A finger on the PARTY tab while they change places presses nothing, when it's lifted or later. A tap there does.
  TOUCH(160,259);assert(SAME(touched));
  two.ptr_x=100;two.ptr_y=228;two.ptr_down=1;RUNS(3);TAP(RETRO_DEVICE_ID_JOYPAD_L2);TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(two.panel);
  two.ptr_down=0;RUNS(3);TOUCH(160,259);assert(SAME(touched));
  TOUCH(100,228);assert(!SAME(touched));TOUCH(305,228);assert(SAME(touched));
  // A notch of the wheel is a press of the D-pad.
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);KEEP(settings);
  two.wheel=-1;RUNS(6);KEEP(below);assert(!SAME(settings));two.wheel=1;RUNS(6);assert(SAME(settings));
  TAP(RETRO_DEVICE_ID_JOYPAD_DOWN);assert(SAME(below));TAP(RETRO_DEVICE_ID_JOYPAD_UP);assert(SAME(settings));
  // DS LAYOUT to PICTURE IN PICTURE, where the button only moves the buttons, and back to TWO SCREENS with them on it.
  TAP(RETRO_DEVICE_ID_JOYPAD_A);TAP(RETRO_DEVICE_ID_JOYPAD_UP);TAP(RETRO_DEVICE_ID_JOYPAD_A);assert(!two.panel);
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(!two.panel);TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(!two.panel);
  TAP(RETRO_DEVICE_ID_JOYPAD_A);TAP(RETRO_DEVICE_ID_JOYPAD_DOWN);TAP(RETRO_DEVICE_ID_JOYPAD_A);assert(!two.panel&&SAME(settings));
  // DS TOUCH SCREEN's STRETCH: the touch screen over all of it, and the switch at its right edge.
  TAP(RETRO_DEVICE_ID_JOYPAD_DOWN);TAP(RETRO_DEVICE_ID_JOYPAD_A);TAP(RETRO_DEVICE_ID_JOYPAD_DOWN);TAP(RETRO_DEVICE_ID_JOYPAD_A);
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(two.panel&&two.src_w==320&&!memcmp(plain,image,sizeof image));
  TOUCH(310,two.src_h/2);assert(!two.panel);TOUCH(310,two.src_h/2);assert(two.panel);
  // Without it, the companion's back on the game's screen.
  two.w=two.h=0;RUNS(3);TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(w==341&&h==256);
  TAP(RETRO_DEVICE_ID_JOYPAD_L2);assert(w==512&&h==384);
 }
 assert(!retro_unserialize_fn(state,3));
 free(before);free(state);retro_unload_game_fn();retro_deinit_fn();dlclose(so);
 printf("PASS frames=%u format=%u enhanced=%d save_bytes=%zu state_bytes=%zu\n",frames,fmt,enhanced?1:0,saves,n);
}
