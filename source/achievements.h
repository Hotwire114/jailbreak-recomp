/* Local achievements (achievements.ini next to launcher.ini), shared by the
 * launcher (C) and the runtime (C++, pc_services.cpp). LGPL-2.1-or-later.
 *
 * [achievements] unlock<i> = unlock time (Unix seconds), 0 = locked.
 * [progress] race_seconds (active race time, pause excluded), finishes,
 * wins (one-player races), modes (bit set per mode finished, RR_MODE_*),
 * active_race_ms (the first 5 minutes, kept from the first version).
 * Help: up to two lines of at most 26 characters (the game's menu box). */
#ifndef RR_ACHIEVEMENTS_H
#define RR_ACHIEVEMENTS_H
enum {RR_MODE_JAILBREAK=1,RR_MODE_FIVEO=2,RR_MODE_SIDECAR=4,RR_MODE_SKULL=8,RR_MODE_COPS=16,RR_MODE_TRIAL=32,
      RR_MODE_SOLO=RR_MODE_JAILBREAK|RR_MODE_FIVEO|RR_MODE_SIDECAR|RR_MODE_TRIAL};
/* Progress shown beside an achievement: counter key in [progress] and goal. */
enum {RR_P_NONE,RR_P_FINISHES,RR_P_WINS,RR_P_HOURS,RR_P_MODES};
typedef struct RrAchievement {const char*name,*help;int progress,goal;} RrAchievement;
static const RrAchievement rr_achievements[]={
    {"First ride","Start a race in Enhanced.",RR_P_NONE,0},
    {"Road regular","Ride 5 active minutes.\nPausing does not count.",RR_P_NONE,0},
    {"Personal touch","Save a rider name in\nthe game's PC menu.",RR_P_NONE,0},
    {"Across the line","Finish a race.\nAny mode.",RR_P_FINISHES,1},
    {"Regular racer","Finish 10 races.\nAny mode.",RR_P_FINISHES,10},
    {"Road veteran","Finish 50 races.\nAny mode.",RR_P_FINISHES,50},
    {"Podium finish","Finish in the top three.\nOne-player races.",RR_P_NONE,0},
    {"Winner","Win a race.\nOne-player races.",RR_P_WINS,1},
    {"Hat trick","Win 3 races.\nOne-player races.",RR_P_WINS,3},
    {"King of the road","Win 10 races.\nOne-player races.",RR_P_WINS,10},
    {"Jail breaker","Finish a Jail Break race.",RR_P_NONE,0},
    {"On patrol","Finish a Five-O race.",RR_P_NONE,0},
    {"Sidecar crew","Finish a Side Car race.",RR_P_NONE,0},
    {"Against the clock","Finish a Time Trial.",RR_P_NONE,0},
    {"All-rounder","Finish Jail Break, Five-O,\nSide Car and Time Trial.",RR_P_MODES,4},
    {"Skull to skull","Finish a Skull-to-Skull\nrace (multiplayer).",RR_P_NONE,0},
    {"Cops and robbers","Finish Cops & Robbers\n(multiplayer).",RR_P_NONE,0},
    {"Couch rivals","Finish a two-player\nsplit-screen race.",RR_P_NONE,0},
    {"Long haul","Ride 1 hour in races.\nPausing does not count.",RR_P_HOURS,1},
    {"Iron butt","Ride 5 hours in races.\nPausing does not count.",RR_P_HOURS,5},
    {"Lieutenant","Reach Lieutenant in a\nsaved Jail Break career.",RR_P_NONE,0},
    {"Captain","Reach Captain in a\nsaved Jail Break career.",RR_P_NONE,0},
    {"Jail Break legend","All 9 qualifications in a\nsaved Jail Break career.",RR_P_NONE,0},
    {"Sergeant","Reach Sergeant in a\nsaved Five-O career.",RR_P_NONE,0},
    {"Smooth operator","Race at 120 FPS.\nDisplay > Frame rate.",RR_P_NONE,0},
    {"Wide open road","Race in 16:9 or 21:9.\nDisplay > Aspect ratio.",RR_P_NONE,0}};
enum {RR_ACH_COUNT=(int)(sizeof rr_achievements/sizeof rr_achievements[0])};
/* Achievement ids used by the runtime. */
enum {RR_A_FIRST_RIDE,RR_A_REGULAR,RR_A_NAME,RR_A_FINISH1,RR_A_FINISH10,RR_A_FINISH50,RR_A_PODIUM,RR_A_WIN1,RR_A_WIN3,RR_A_WIN10,
      RR_A_JAILBREAK,RR_A_FIVEO,RR_A_SIDECAR,RR_A_TRIAL,RR_A_ALL_MODES,RR_A_SKULL,RR_A_COPS,RR_A_TWO_PLAYER,RR_A_HOUR1,RR_A_HOUR5,
      RR_A_LIEUTENANT,RR_A_CAPTAIN,RR_A_ALL_QUALIFIED,RR_A_SERGEANT,RR_A_FPS120,RR_A_WIDE};
static inline int rr_mode_count(int modes){int n=0;for(int b=modes&RR_MODE_SOLO;b;b&=b-1)n++;return n;}
#endif
