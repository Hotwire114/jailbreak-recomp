/* Level of detail steps ("Level of detail" in the launcher's Display page;
 * launcher.ini [display] model_detail), shared by the launcher (C) and the
 * runtime (C++). Each step scales the distance at which the game switches a
 * rider, bike or other race model to a lower-detail version, in percent of
 * the original game's. Step 0 is the original game. LGPL-2.1-or-later. */
#ifndef RR_MODEL_DETAIL_H
#define RR_MODEL_DETAIL_H
/* Multiples of 25 keep every scaled threshold exact (all stock values are
 * multiples of 4), which pc_services.cpp relies on to recognise its own
 * tables. */
static const int rr_detail_percent[] = {100, 150, 200, 300, 400, 600, 800, 1200};
enum { rr_detail_steps = (int)(sizeof rr_detail_percent / sizeof rr_detail_percent[0]) };
/* Highest step measured to hold 60 FPS everywhere, including 16-rider
 * pile-ups and split screen (PROGRESS-0.15-detail.md). Steps above it show
 * a performance warning in the launcher. */
enum { rr_detail_safe = 4 };
#endif
