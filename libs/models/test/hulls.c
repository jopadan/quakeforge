#include "hulls.h"

//  0,0
//   |\   .
//   |s\  .
//   |ss\ .
//   0   1

static mclipnode_t clipnodes_simple_wedge[] = {
	{  0, {             1, CONTENTS_EMPTY}},
	{  1, {CONTENTS_EMPTY, CONTENTS_SOLID}},
};

static plane_t planes_simple_wedge[] = {
	{{1, 0, 0}, 0, 0, 0},		//  0
	{{0.8, 0, 0.6}, 0, 4, 0},	//  1 
};

hull_t hull_simple_wedge = {
	clipnodes_simple_wedge,
	planes_simple_wedge,
	0,
	1,
	{0, 0, 0},
	{0, 0, 0},
};

//    -32  32  48
//  sss|sss|   |sss
//  sss|sss|   |sss
//     0   1   2

static mclipnode_t clipnodes_tpp1[] = {
	{  0, {             1, CONTENTS_SOLID}},
	{  1, {             2, CONTENTS_SOLID}},
	{  2, {CONTENTS_SOLID, CONTENTS_EMPTY}},
};

static plane_t planes_tpp1[] = {
	{{1, 0, 0}, -32, 0, 0},
	{{1, 0, 0},  32, 0, 0},
	{{1, 0, 0},  48, 0, 0},
};

hull_t hull_tpp1 = {
	clipnodes_tpp1,
	planes_tpp1,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//    -32  32  48
//  sss|sss|   |sss
//  sss|sss|   |sss
//     1   0   2

static mclipnode_t clipnodes_tpp2[] = {
	{  0, {             2,              1}},
	{  1, {CONTENTS_SOLID, CONTENTS_SOLID}},
	{  2, {CONTENTS_SOLID, CONTENTS_EMPTY}},
};

static plane_t planes_tpp2[] = {
	{{1, 0, 0},  32, 0, 0},
	{{1, 0, 0}, -32, 0, 0},
	{{1, 0, 0},  48, 0, 0},
};

hull_t hull_tpp2 = {
	clipnodes_tpp2,
	planes_tpp2,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//    -32  32  48
//  sss|   |www|sss
//  sss|   |www|sss
//     1   0   2

static mclipnode_t clipnodes_tppw[] = {
	{  0, {             2,              1}},
	{  1, {CONTENTS_EMPTY, CONTENTS_SOLID}},
	{  2, {CONTENTS_SOLID, CONTENTS_WATER}},
};

static plane_t planes_tppw[] = {
	{{1, 0, 0},  32, 0, 0},
	{{1, 0, 0}, -32, 0, 0},
	{{1, 0, 0},  48, 0, 0},
};

hull_t hull_tppw = {
	clipnodes_tppw,
	planes_tppw,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//     2
//  eee|eee
// 0,32+--- 1
//  eee|sss
//  ---+--- 0
//  ss0,0ss
static mclipnode_t clipnodes_step1[] = {
	{  0, {             1, CONTENTS_SOLID}},
	{  1, {CONTENTS_EMPTY,              2}},
	{  2, {CONTENTS_SOLID, CONTENTS_EMPTY}},
};

static plane_t planes_step1[] = {
	{{0, 0, 1},   0, 2, 0},
	{{0, 0, 1},  32, 2, 0},
	{{1, 0, 0},   0, 0, 0},
};

hull_t hull_step1 = {
	clipnodes_step1,
	planes_step1,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//     0
//  eee|eee
// 0,32+--- 1
//  eee|sss
//  ---+sss 2
//  ss0,0ss
static mclipnode_t clipnodes_step2[] = {
	{  0, {             1,              2}},
	{  1, {CONTENTS_EMPTY, CONTENTS_SOLID}},
	{  2, {CONTENTS_EMPTY, CONTENTS_SOLID}},
};

static plane_t planes_step2[] = {
	{{1, 0, 0},   0, 0, 0},
	{{0, 0, 1},  32, 2, 0},
	{{0, 0, 1},   0, 2, 0},
};

hull_t hull_step2 = {
	clipnodes_step2,
	planes_step2,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//     0
//  eee|eee
// 2---+0,32
//  sss|eee
//  sss+--- 1
//  ss0,0ss
static mclipnode_t clipnodes_step3[] = {
	{  0, {             1,              2}},
	{  1, {CONTENTS_EMPTY, CONTENTS_SOLID}},
	{  2, {CONTENTS_EMPTY, CONTENTS_SOLID}},
};

static plane_t planes_step3[] = {
	{{1, 0, 0},   0, 0, 0},
	{{0, 0, 1},   0, 2, 0},
	{{0, 0, 1},  32, 2, 0},
};

hull_t hull_step3 = {
	clipnodes_step3,
	planes_step3,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//   3 2
//  s|e|eee
// 4-+e|-20,40
//  e|e|eee
// 0,32+--- 1
//  eee|sss
//  ---+--- 0
//  ss0,0ss
static mclipnode_t clipnodes_covered_step[] = {
	{  0, {             1, CONTENTS_SOLID}},
	{  1, {             3,              2}},
	{  2, {CONTENTS_SOLID, CONTENTS_EMPTY}},
	{  3, {CONTENTS_EMPTY,              4}},
	{  4, {CONTENTS_SOLID, CONTENTS_EMPTY}},
};

static plane_t planes_covered_step[] = {
	{{0, 0, 1},   0, 2, 0},
	{{0, 0, 1},  32, 2, 0},
	{{1, 0, 0},   0, 0, 0},
	{{1, 0, 0}, -20, 0, 0},
	{{0, 0, 1},  40, 2, 0},
};

hull_t hull_covered_step = {
	clipnodes_covered_step,
	planes_covered_step,
	0,
	4,
	{0, 0, 0},
	{0, 0, 0},
};

//     0
//  eee|eee
//  eee+--- 1
//  ee/0,0s
//   2 ssss
static mclipnode_t clipnodes_ramp[] = {
	{  0, {             1,              2}},
	{  1, {CONTENTS_EMPTY, CONTENTS_SOLID}},
	{  2, {CONTENTS_EMPTY, CONTENTS_SOLID}},
};

static plane_t planes_ramp[] = {
	{{   1, 0,   0},   0, 0, 0},
	{{   0, 0,   1},   0, 2, 0},
	{{-0.6, 0, 0.8},   0, 4, 0},
};

hull_t hull_ramp = {
	clipnodes_ramp,
	planes_ramp,
	0,
	2,
	{0, 0, 0},
	{0, 0, 0},
};

//   2   1
// ss|sss|ss
// ss+-3-+ss 8
// ss|eee|ss
// ss+-4-+ss -8
// ss|sss|ss
//  -8   8
//  looking at plane 0: back of 0 is empty, front of 0 has above hole
static mclipnode_t clipnodes_hole[] = {
	{  0, {             1, CONTENTS_EMPTY}},
	{  1, {CONTENTS_SOLID,              2}},
	{  2, {             3, CONTENTS_SOLID}},
	{  3, {CONTENTS_SOLID,              4}},
	{  4, {CONTENTS_EMPTY, CONTENTS_SOLID}},
};

static plane_t planes_hole[] = {
	{{ 0, 1, 0},   0, 1, 0},
	{{ 1, 0, 0},   8, 0, 0},
	{{ 1, 0, 0},  -8, 0, 0},
	{{ 0, 0, 1},   8, 2, 0},
	{{ 0, 0, 1},  -8, 2, 0},
};

hull_t hull_hole = {
	clipnodes_hole,
	planes_hole,
	0,
	4,
	{0, 0, 0},
	{0, 0, 0},
};

//     2   3
//  eee|eee|eee
// 0,32+---+--- 1
//  eee|sss|eee
//  ---+---+--- 0
//  ss0,0s8,0ss
static mclipnode_t clipnodes_ridge[] = {
	{  0, {             1, CONTENTS_SOLID}},
	{  1, {CONTENTS_EMPTY,              2}},
	{  2, {             3, CONTENTS_EMPTY}},
	{  3, {CONTENTS_EMPTY, CONTENTS_SOLID}},
};

static plane_t planes_ridge[] = {
	{{0, 0, 1},   0, 2, 0},
	{{0, 0, 1},  32, 2, 0},
	{{1, 0, 0},   0, 0, 0},
	{{1, 0, 0},   8, 0, 0},
};

hull_t hull_ridge = {
	clipnodes_ridge,
	planes_ridge,
	0,
	3,
	{0, 0, 0},
	{0, 0, 0},
};

//    1     2
//   ss\sss/eeee
//   sss\s/eeeee
// 3 ----. 0,0 e
//   wwwww\eeeee
//   wwwwww\eeee
// 0 -------.--- -20
//   sssssssssss
//   sssssssssss
static mclipnode_t clipnodes_cave[] = {
	{  0, {             1, CONTENTS_SOLID}},
	{  1, {             2,              3}},
	{  2, {CONTENTS_SOLID, CONTENTS_EMPTY}},
	{  3, {CONTENTS_SOLID, CONTENTS_WATER}},
};

static plane_t planes_cave[] = {
	{{   0, 0,   1}, -20, 2, 0},
	{{ 0.6, 0, 0.8},   0, 3, 0},
	{{-0.8, 0, 0.6},   0, 3, 0},
	{{   0, 0,   1},   0, 2, 0},
};

hull_t hull_cave = {
	clipnodes_cave,
	planes_cave,
	0,
	3,
	{0, 0, 0},
	{0, 0, 0},
};