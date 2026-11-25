// GPL-3.0. DS adapters hosted inside the portable PokéDaisy companion shell.
#pragma once
#include "companion.h"
#include "map_view.h"
#include "dex_view.h"
#include <string>
extern "C" {
#include "pd_settings.h"
}
struct DsCompanionUi {
  enum Row { Layout, Battles, Hud, CompanionButton, ScreenButton, PenSpeed, QuickStyle, Reset, BattleStrip, HudMinimal, HudBalls, HudRepel, HudMap, DarkMode, AutoRun, TabBar, MoreTabs, HudButton, HudSize, Aspect, Zoom, AutoZoom, FontType, TouchScreen, Rows };
  pd_settings settings{};
  // A host whose picture can EXPAND (the Windows core): its SETTINGS gain ASPECT, ZOOM and AUTO ZOOM IN, and it lays the
  // game out for settings.aspect / settings.zoom (the app's SETTINGS > SCREEN). Off: the DS layouts alone.
  bool hasExpand=false;
  bool secondScreen=false;
  // DS LAYOUT is another choice with a second screen, TWO SCREENS to start with.
  int shown_layout() const{return secondScreen?layoutTwo:layout;}
  void set_second_screen(bool on);
  void draw_switch(pd_canvas*,int x,int y,int w,int h,bool onCompanion,bool pressed);
  // The frontend's own settings (the desktop host's A/B BUTTONS, FF SPEED, KEYBOARD ...: PD_ENV_FRONT_ROWS), at the end of
  // SETTINGS as rows Rows + i, so every setting is in one place. set_front_rows each frame; a press is taken by
  // take_front_press (-1 none) and handed back to the frontend; open_front_rows puts SETTINGS on the first of them.
  static constexpr int FrontMax=8;
  int frontCount=0;
  std::string frontLabel[FrontMax], frontValue[FrontMax], frontGroup[FrontMax];
  int frontPressed=-1;
  void set_front_rows(int count,const char (*labels)[24],const char (*values)[24],const char (*groups)[16]);
  int take_front_press(){const int r=frontPressed;frontPressed=-1;return r;}
  void open_front_rows();
  std::vector<Row> order; // settingsOrder: the list shown, ASPECT / ZOOM / AUTO ZOOM IN first when hasExpand
  void build_order();
  pd_ui frame{}, options{};
  // A companion window's picture around the canvas (the desktop's padding, PD_COMPANION_PAD): the backdrop drawn across
  // it, its part at (backdropX, backdropY) taken under the companion, so the padding carries on the same picture.
  pd_canvas* backdropFrame=nullptr;
  int backdropX=0, backdropY=0;
  pd_snapshot battle{};
  unsigned displayRevision=0;
  int layout=0, layoutTwo=0, autoBattles=1, companionButton=0, screenButton=1, penSpeed=1;
  bool battleStrip=true,hudMinimal=false,darkMode=false,autoRun=false,touchFill=false,bottomCompanion=false;
  int selected=0,page=0,scroll=0,pocket=0,filter=0,mapX=0,mapY=0;
  int learnRow=0, learnLast=0;
  bool summary=false,details=false;
  int bagTabKey=-1; // the D-pad cursor on the bag's pocket tab bar: that tab's pocket id; -1 = on the list
  unsigned bagPockets=1;
  int dexEntry=0,dexRow=0,dexFilterFocus=-1;
  int placeRow=-1,mapAction=-1;
  int guideLoadedPage=-1,guideMap=-1;
  std::vector<GuideRow> guideRows;
  std::vector<DsMapPlace> mapPlaces;
  DsDexArt dexArt;
  int dexMode=-1,dexCachedSpecies=0,dexDetailRow=0;
  DsDexSection dexSection=DsDexSection::Info;
  std::vector<DsDexEvolution> dexEvolutions;
  std::vector<DsGuideSpot> dexSpots;
  bool dexSpotsReady=false;
  DsDexPage dexPage;
  int setting=0,picker=-1,choice=0;
  std::string path,error;
  void init(const std::string& directory,int legacyLayout=0,bool legacyHud=false);
  void set_game(DsGame game,bool soulSilverVersion=false);
  bool save();
  void update_battle();
  void draw(pd_canvas*,const DsCompanion&,bool,const DsMapArt&,const std::vector<std::vector<uint32_t>>&);
  bool control(unsigned pressed); // true closes the companion
  // A pointer (the Windows core's mouse): down / held / up at (x, y) on the companion's canvas, in its own pixels. A tap
  // runs the tab, row, button or gear under it as A would; a drag on a list moves it. True while the companion takes it.
  bool touch(int x, int y, bool down);
  void cancel_touch();
  // The wheel, in notches (>0 down): the open tab's list moves as UP / DOWN would.
  void wheel(int notches);
  unsigned companion_id() const;
  int hud_id() const;
  int screen_id() const;
  int pen_step() const;
private:
  std::vector<ds_touch_hit> hits; // what the last draw laid out, for the pointer
  size_t overlayHits=0;           // where the choice windows' own targets start in it
  int tapX=0, tapY=0, tapId=-1, placeFirst=0, dragY=0;
  pd_map_viewport mapView{};   // where the MAP tab drew the map last, for a click on it
  bool tapHeld=false, tapFrame=false, tapWindow=false, tapClose=false, dragging=false;
  pd_tab tapTab=PD_TAB_PARTY;
  int tap_at(int x, int y) const;
  void tap_act(int id);
  void activate_setting();
  pd_game presentation{};
  pd_battle_source battleSource{};
  const DsCompanion* snapshot=nullptr;
  const DsMapArt* art=nullptr;
  const std::vector<std::vector<uint32_t>>* icons=nullptr;
  bool supported=false;
  bool soulSilver=false;
  DsGame game=DsGame::Platinum;
  const std::vector<int>* dex_order() const;
  void defaults();
  void draw_body(pd_ui*,pd_canvas*);
  void draw_settings(pd_canvas*);
  int value(int row) const;
  int choice_count(int row) const;
  void apply(int row,int value);
};
