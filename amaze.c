/*
 * Copyright David Leonard, 2000. All rights reserved. <Berkeley license>
 * Modified for Rockbox by Jerry Chapman, 2007
 
 * Based on an ancient game published in Australian Personal Computer 
 * Magazine (1986?) also called 'amaze'. Written in BASIC I recall. No idea
 * who the author was, I'm afraid. I just remember porting it from QBASIC
 * to AppleSoft BASIC, and simplifying the perspective line drawing code.

 * David Leonard's program was written using the curses library.  All of the
 * curses functions referenced were redefined/wrapped for rockbox.
 *
 * Thanks to hcs on #rockbox who was instrumental in explaining and fixing
 * my issues with the stack and pulling me out of the quagmire I was stuck in.
 *
 * All files in this archive are subject to the GNU General Public License.
 * See the file COPYING in the source tree root for full license agreement.
 * 
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 */

#include "plugin.h"
#include "lib/helper.h"
#include "lib/configfile.h"




//can't set these five to zero
int show_compass;		/* -c */
int show_map;			/* -m */
int can_shoot;			/* -s */
int remember_visited;		/* -v */
//int continue_on_win;		/* -n */

typedef struct {
       char chr;
       int attrib;
} fig; //attempt to redefine chtype w/o ncurses

typedef struct {
	bool write_to_screen;
	fig coords[24][80];
	int maxy, maxx, offy, offx;
} winder; //attempt to redefine WINDOW; I live in the south :P

//let's define some colors
#define COLOR_BLACK        LCD_RGBPACK(0,0,0)
#define COLOR_WHITE        LCD_RGBPACK(255,255,255)
#define COLOR_DARKGRAY     LCD_RGBPACK(128,128,128)
#define COLOR_LIGHTGRAY    LCD_RGBPACK(192,192,192)
#define COLOR_RED          LCD_RGBPACK(128,0,0)
#define COLOR_LIGHTRED     LCD_RGBPACK(255,0,0)
#define COLOR_DARKYELLOW   LCD_RGBPACK(128,128,0)
#define COLOR_YELLOW       LCD_RGBPACK(255,255,0)
#define COLOR_GREEN        LCD_RGBPACK(0,128,0)
#define COLOR_LIGHTGREN    LCD_RGBPACK(0,255,0)
#define COLOR_CYAN         LCD_RGBPACK(0,128,128)
#define COLOR_LIGHTCYAN    LCD_RGBPACK(0,255,255)
#define COLOR_BLUE         LCD_RGBPACK(0,0,128)
#define COLOR_LIGHTBLUE    LCD_RGBPACK(0,0,255)
#define COLOR_PURPLE       LCD_RGBPACK(128,0,128)
#define COLOR_PINK         LCD_RGBPACK(255,0,255)
#define COLOR_BROWN        LCD_RGBPACK(128,64,0)
#define COLOR_LIGHTBROWN   LCD_RGBPACK(255,128,64)

#define COLOR_GROUND	   LCD_RGBPACK(51,51,51)
#define COLOR_SKY	   LCD_RGBPACK(51,51,102)
#define COLOR_VISITED	   LCD_RGBPACK(102,51,51)
#define COLOR_PARA	   LCD_RGBPACK(153,153,102)
//#define COLOR_PERP	   LCD_RGBPACK(102,51,0)
#define COLOR_PERP	   LCD_RGBPACK(204,153,102)

enum chrstyle { A_NORMAL, A_WOB_I, A_GOB_I, A_ROB, A_WOBL, A_YOB, A_WOBL_I };

fig SPACE =		{ ' ', A_NORMAL };	/* Space you can walk through */
fig BLOCK =		{ ' ', A_WOB_I }; 	/* A block that you can't walk through */
fig OBSPACE =		{ '#', A_GOB_I }; 	/* Obscured space */
fig VISITED =		{ '.', A_NORMAL };	/* Visited space */
fig GOAL =		{ '%', A_ROB };		/* Exit from the maze */
fig START =		{ '+', A_NORMAL };	/* Starting point in the maze */
//attr_t WIN =		      A_STANDOUT ; //let's make this RED

enum dir { DIR_DOWN=0, DIR_RIGHT=1, DIR_UP=2, DIR_LEFT=3 };
#define _TOD(d) (enum dir)((d) % 4)
#define _TOI(d) (int)(d)
#define LEFT_OF(d) _TOD(_TOI(d) + 1)
#define REVERSE_OF(d) _TOD(_TOI(d) + 2)
#define RIGHT_OF(d) _TOD(_TOI(d) + 3)

fig VLINE =		{ '|', A_NORMAL };		/* | */
fig HLINEt =		{ '_', A_WOBL };		/* _ at top */
fig HLINEb =		{ '_', A_NORMAL };		/* _ at bottom */
fig DIAG1 =		{ '\\', A_NORMAL };		/* \ */
fig DIAG2 =		{ '/', A_NORMAL };		/* / */
fig SKY =		{ ' ', A_WOBL };

static struct { int y, x; } dirtab[4] = 
	{ { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
static fig ptab[4] = {
	{ 'v', A_YOB }, //let's make these YELLOW
	{ '>', A_YOB }, 
	{ '^', A_YOB },
	{ '<', A_YOB }
};
int px, py;		/* Player position */
enum dir pdir;		/* Player direction */
fig punder;		/* character under player, if any */
int sx, sy;		/* Start position */
int gx, gy;		/* Goal position */
int gdist;		/* Distance from start to goal */
int won = 0;		/* Reached goal */
int cheated = 0;	/* Cheated somehow */

winder map_winder;
winder umap_winder;
winder view_winder;
winder msg_winder;

winder *map;	/* Actual maze */
winder *umap;	/* Maze map that user sees */
winder *view;	/* 3D perspective view player sees */
winder *msg;	/* Message information window */

int cntr_madeit = 0;
bool showingmaze = true;

#include "pluginbitmaps/amaze_tiles.h"

#define TILE_WIDTH  BMPWIDTH_amaze_tiles	
#define TILE_HEIGHT (BMPHEIGHT_amaze_tiles/10)

enum tile_index { t_down=0, t_right=1, t_up=2, t_left=3, t_visited=4,
		  t_obspace=5, t_goal=6, t_block=7, t_space=8, t_start=9 };

/* save file names */
#define UMAP_FILE PLUGIN_GAMES_DIR "/amaze_umap.sav"
#define MAP_FILE  PLUGIN_GAMES_DIR "/amaze_map.sav"

//void drawview(void);
//void mappmove(int, int, enum dir);

//did we make it or eat shit
void gh(void)
{
	int button;
	
	rb->splash(HZ, "got here!");
	cntr_madeit++;
	button = rb->button_get(true);
}

void clearscreen(void)
{
	rb->lcd_set_background(COLOR_BLACK);
	rb->lcd_set_foreground(COLOR_WHITE);
	rb->lcd_clear_display();
	rb->lcd_update();
}

//implement memcopy()
void *memcpy
(void *dest, const void *src, size_t n)
{
	return rb->memcpy(dest, src, n);
}

//redefine ncurses getmaxyx
void
getmaxyx
(winder *pane, int *y, int *x)
{
	*y = pane->maxy;
	*x = pane->maxx;
}

//redefine ncurses mvaddch
void mvwaddch
(winder *pane, int y, int x, fig c)
{
	char outs[2] = {0,0};
	int chr_mode;
	int maxy, maxx;
	
	outs[0] = c.chr;

	getmaxyx(pane, &maxy, &maxx);
	if (x<0 || x>=maxx || y<0 || y>=maxy) return;

	pane->coords[y][x] = c;

	if (pane->write_to_screen == true) {
		//use rockbox write
		rb->lcd_set_foreground(COLOR_WHITE);
		rb->lcd_set_background(COLOR_BLACK);
		chr_mode = STYLE_DEFAULT;
		switch (c.attrib) {
			case A_NORMAL:
				break;
			case A_ROB:
				rb->lcd_set_foreground(COLOR_RED);
				break;
			case A_YOB:
				rb->lcd_set_foreground(COLOR_YELLOW);
				break;
			case A_WOB_I:
				rb->lcd_set_background(COLOR_WHITE);
				chr_mode = STYLE_INVERT;
				break;
			case A_GOB_I:
				rb->lcd_set_foreground(COLOR_DARKGRAY);
				chr_mode = STYLE_INVERT;
				break;
			case A_WOBL:
				rb->lcd_set_background(COLOR_BLUE);
				break;
			case A_WOBL_I:
				rb->lcd_set_background(COLOR_BLUE);
				chr_mode = STYLE_INVERT;
				break;
		}
		rb->lcd_putsxy((x + pane->offx)*6, (y + pane->offy)*8, outs);
	}

}

//redefine ncurses mvwinch
fig mvwinch
(winder *pane, int y, int x)
{
	return pane->coords[y][x];
}

//redefine ncurses werase
void werase
(winder *pane)
{
	int y, x;
	for (y = 0; y < pane->maxy; y++) 
		for (x = 0; x < pane->maxx; x++)
			mvwaddch(pane,y,x,SPACE);
}

bool isfigequal
(fig a, fig b)
{
	if (a.chr == b.chr && a.attrib == b.attrib)
		return true;
	else
		return false;
}

//start of David Leonard's code

/* Look at position (y,x) in the maze map */
fig
at(int y, int x)
{
	int maxy, maxx;

	if (y == py && x == px)
		return punder;

	getmaxyx(map, &maxy, &maxx);
	if (y < 0 || y >= maxy || x < 0 || x >= maxx)
		return SPACE;
	else {
		return mvwinch(map, y, x);
	}
}

void
copyumap(int y, int x, int fullvis)
{
	fig c;

	c = at(y, x);
	if (!fullvis && isfigequal(c, SPACE) 
	  && !isfigequal(mvwinch(umap, y, x), SPACE))
		c = OBSPACE;
	mvwaddch(umap, y, x, c);
}

struct path {
	int y, x;
	int ttl;		/* Time until this path stops */
	int spawns;		/* Max number of forks this path can do */
	int distance;		/* Distance from start */
	struct path *next;	
};


/*
 * A better maze-digging algorithm.
 * Simultaneously advance multiple digging paths through the map.
 * This is done by having a work queue of advancing paths. 
 * Occasionally a path can fork; thus adding more to the work
 * queue and diversifying the maze.
 */
void
eatmaze(int starty, int startx)
{
	struct path *path_free, *path_head, *path_tail;
	struct path *p, *s;
	//moved out per hcs in #rockbox
	static struct path path_storage[2000];
	int try;
	unsigned i;
	int y, x, dy, dx;
	int sdir;

	/* Set up the free list of path cells */
	for (i = 2; i < sizeof path_storage / sizeof path_storage[0]; i++)
		path_storage[i].next = &path_storage[i-1];
	path_storage[1].next = NULL;
	path_free = &path_storage[sizeof path_storage / sizeof path_storage[0] - 1];

	/* Set up the initial path cell */
	path_storage[0].y = starty;
	path_storage[0].x = startx;
	mvwaddch(map, starty, startx, SPACE);

	/* Set up the initial goal. It will move later. */
	gy = starty;
	gx = startx;
	gdist = 0;

	/* Initial properties of the root path */
	path_storage[0].ttl = 50;
	path_storage[0].spawns = 20;
	path_storage[0].distance = 0;

	/* Put the initial path into the queue */
	path_storage[0].next = NULL;
	path_head = path_tail = &path_storage[0];

	while (path_head != NULL) {
		/* Dequeue */
		p = path_head;
		path_head = p->next;
		if (path_head == NULL)
			path_tail = NULL;

		/* There's a large chance that some paths miss a turn */
		if (rb->rand() % 100 < 60)
			goto requeue;

		/* First thing we do is advance the path. */
		y = p->y;
		x = p->x;

		sdir = rb->rand() % 4;
		for (try = 0; try < 4; try ++) {
			dx = dirtab[(sdir + try) % 4].x;
			dy = dirtab[(sdir + try) % 4].y;

			/* Going back on ourselves? */
			if (!isfigequal(at(y + dy, x + dx), BLOCK))
				continue;

			/* Connecting to another path? */
			if (!isfigequal(at(y + dy * 2, x + dx * 2), BLOCK))
				continue;
			if (!isfigequal(at(y + dy + dx, x + dx - dy), BLOCK))
				continue;
			if (!isfigequal(at(y + dy - dx, x + dx + dy), BLOCK))
				continue;

			break;
		}
		if (try == 4 || p->ttl <= 0) {
			/* Failed: the path is placed on the free list. */
			p->next = path_free;
			path_free = p;
			continue;
		}

		/* Dig the path a bit */
		p->y = y + dy;
		p->x = x + dx;
		mvwaddch(map, p->y, p->x, SPACE);
		p->ttl--;
		p->distance++;

		if (p->distance > gdist) {
			gx = p->x;
			gy = p->y;
			gdist = p->distance;
		}

		/* Decide if we should spawn */
		if (/* rb->rand() % (p->ttl + 1) < p->spawns && */ path_free) {
			/* Take a new path element off the free list */
			s = path_free;
			path_free = s->next;

			/* Insert it at the tail of the queue */
			s->next = NULL;
			if (path_tail) path_tail->next = s;
			else           path_head = s;
			path_tail = s;

			/* Newly spawned path s will inherit most properties from p */
			s->y = p->y;
			s->x = p->x;
			s->ttl = p->ttl + rb->rand() % 10;
			s->spawns = p->spawns;
			s->distance = p->distance;

			/* p->spawns--; */
		}

	requeue:
		/* Put p onto the tail of the queue */
		p->next = NULL;
		if (path_tail) path_tail->next = p;
		else           path_head = p;
		path_tail = p;
	}
}

/* Move the player to a new position/direction in the maze map */
void
mappmove(int newpy, int newpx, enum dir newpdir)
{
	mvwaddch(map, py, px, punder);
	copyumap(py, px, 1);
	punder = at(newpy, newpx);
	py = newpy;
	px = newpx;
	pdir = newpdir;
	copyumap(py, px, 1);
	mvwaddch(umap, py, px, ptab[(int)pdir]);
//	wmove(umap, 0, 0);     not necessary
//	wnoutrefresh(umap);    will do below
	rb->lcd_update();
}

void
clearmap 
(winder *amap)
{
	int maxy, maxx;
	int y, x;

	getmaxyx(map, &maxy, &maxx);

	//werase(&amap);  //clear window

	for (y = 0; y < maxy; y++)
		for (x = 0; x < maxx; x++)
			mvwaddch(amap, y, x, BLOCK);
}

/* Reveal the solution to the user */
void
showmap(void)
{
	int maxy, maxx, y, x;
	fig ch, och;

	getmaxyx(umap, &maxy, &maxx);
	for (y = 0; y < maxy; y++)
		for (x = 0; x < maxx; x++) {
			ch = at(y, x);
			if (isfigequal(ch, SPACE)) {
				och = mvwinch(umap, y, x);
				if (isfigequal(och, BLOCK)
				|| isfigequal(och, OBSPACE))
					ch = OBSPACE;
			}
			mvwaddch(umap, y, x, ch);
		}
	mvwaddch(umap, py, px, ptab[(int)pdir]);
//	wnoutrefresh(umap);
	rb->lcd_update();
}

/*
 * Create a new maze map
 * The algorithm here is quite inferior to that presented in the
 * magazine. I could only remember the gist of it: recursively dig a
 * trail that doesn't touch any other part of the maze, keeping track
 * of all possible points where the path could fork. Later on try those
 * possible branches; put limits on the segment lengths etc.
 */
void
makemaze(void)
{
	int maxy, maxx;
	int i;

	/* Get the window dimensions */
	getmaxyx(map, &maxy, &maxx);

	clearmap(map);

	py = rb->rand() % (maxy - 2) + 1; /* maxy/2 */
	px = rb->rand() % (maxx - 2) + 1; /* maxx/2 */
	
	eatmaze(py, px);
	
	sx = px; /* starting position */
	sy = py;

	/* Face in an interesting direction: */
	pdir = DIR_UP;
	for (i = 0; 
	     i < 4 && isfigequal(at(py + dirtab[pdir].y, 
			     	 px + dirtab[pdir].x), BLOCK);
	     i++)
		pdir = LEFT_OF(pdir);

	mvwaddch(map, py, px, START);
	mvwaddch(map, gy, gx, GOAL);
	punder = START;
	mappmove(py, px, pdir);
}

/* 
 * Position of the start of a vertical block edge
 * Also used for horizontal edges
 *
 * ___
 *  h |\h
 *    | \	f = front
 * f  | s 	s = side
 *    |         v = vertical edge
 *    v /       h = horizontal edge
 * ___|/
 */
#define MAXDIST 6
static int barx[MAXDIST + 2] = { -1, 3, 9, 13, 17, 19, 21, 21 };
static int bary[MAXDIST + 2] = { 0, 2, 5,  7,  9, 10, 11, 11 };

/* Draw the close vertical edge of a block */
void
draw_vert_edge(int dist, int right)
{
	int y;
	for (y = bary[dist]; y <= 22 - bary[dist]; y++)
		if (right)
			mvwaddch(view, y, 43 - barx[dist], VLINE);
		else
			mvwaddch(view, y, barx[dist], VLINE);
}

/* Draw the horizontal edge of a block */
void
draw_horiz_front(int dist, int right)
{
	int x;

	for (x = barx[dist-1] + 1; x < barx[dist]; x++)
		if (right) {
			mvwaddch(view, bary[dist] - 1, 43 - x, HLINEt);
			mvwaddch(view, 22 - bary[dist], 43 - x, HLINEb);
		} else {
			mvwaddch(view, bary[dist] - 1, x, HLINEt);
			mvwaddch(view, 22 - bary[dist], x, HLINEb);
		}
}

/* Draw the horiz edge of a wall in the way */
void
draw_horiz_wall(int dist)
{
	int x;
	for (x = barx[dist] + 1; x <= 43 - (barx[dist] + 1); x++) {
		mvwaddch(view, bary[dist] - 1, x, HLINEt);
		mvwaddch(view, 22 - (bary[dist]), x, HLINEb);
	}
}

/* Draw the (visually) diagonal edge of a block */
void 
draw_horiz_side(int dist, int right)
{
	int y, x;

	for (y = bary[dist], x = barx[dist] + 1; x < barx[dist + 1]; y++, x+=2)
		if (right) {
			mvwaddch(view, y, 43 - x, DIAG2);
			mvwaddch(view, 22 - y, 43 - x, DIAG1);
		} else {
			mvwaddch(view, y, x, DIAG1);
			mvwaddch(view, 22 - y, x, DIAG2);
		}
}

/* Draw the floor in the centre of view */
void
draw_floor_centre(int dist, fig ch)
{
	int y, x, xx;

	for (y = bary[dist], x = barx[dist] + 1; x < barx[dist + 1]; y++, x+=2)
		for (xx = x + 1; xx <= 43 - (x + 1); xx++) {
			mvwaddch(view, y, xx, SKY);
			mvwaddch(view, 22 - y, xx, ch);
		}
}

/* Draw the floor to the side */
void
draw_floor_side(int dist, int right, fig ch)
{
	int y, x, xx;

	for (y = bary[dist], x = barx[dist] + 1; x < barx[dist + 1]; y++, x+=2)
		for (xx = barx[dist]; xx <= x; xx++)
			if (right) {
				mvwaddch(view, y, 43 - xx, SKY);
				mvwaddch(view, 22 - y, 43 - xx, ch);
			} else {
				mvwaddch(view, y, xx, SKY);
				mvwaddch(view, 22 - y, xx, ch);
			}
}

/* Draw the view the player would see */
void
drawview(void)
{
	int dist;
	int dx, dy, x, y;
	int a, l, la, r, ra;
	fig g, gl, gr;
	int lastdist;

	fig dir_ind[6] = { 
			{ '^', A_YOB },
			{ 'v', A_YOB },
			{ '>', A_YOB },
			{ '<', A_YOB },
			{ '|', A_YOB },
			{ '-', A_YOB }
	};

	showingmaze = true;

	if (!show_map) {
		clearmap(umap);
		copyumap(gy, gx, 1);
	}

	dx = dirtab[(int)pdir].x;
	dy = dirtab[(int)pdir].y;

	for (dist = 1; dist < MAXDIST; dist++)
		if (isfigequal(at(py + dy * dist, px + dx * dist), BLOCK))
			break;
	lastdist = dist - 1;
	werase(view);
	clearscreen();
	while (--dist >= 0) {
		x = px + dx * dist;
		y = py + dy * dist;

		/* XXX only looks one cell to the side */

		/* ahead */
		a = (isfigequal(at(y + dy, x + dx), BLOCK));
		/* to the left */
		l = (isfigequal(at(y - dx, x + dy), BLOCK));
		/* left, ahead */
		la = (isfigequal(at(y - dx + dy, x + dy + dx), BLOCK));
		/* to the right */
		r = (isfigequal(at(y + dx, x - dy), BLOCK));
		/* right, ahead */
		ra = (isfigequal(at(y + dx + dy, x - dy + dx), BLOCK));
		/* floor */
		g = at(y, x);
		gl = at(y - dx, x + dy);
		gr = at(y + dx, x - dy);

		if (dist == lastdist) {
			
			if (a || la) draw_vert_edge(dist +1, 0);
			if (a || ra) draw_vert_edge(dist +1, 1);
		}

		if (!isfigequal(g, BLOCK))
			draw_floor_centre(dist, g);
		if (!isfigequal(gl, BLOCK))
			draw_floor_side(dist, 0, gl);
		if (!isfigequal(gr, BLOCK))
			draw_floor_side(dist, 1, gr);

		if (l) {
			draw_horiz_side(dist, 0);
			if (dist != 0) draw_vert_edge(dist, 0);
			draw_vert_edge(dist + 1, 0);
		}
		if (r)  {
			draw_horiz_side(dist, 1);
			if (dist != 0) draw_vert_edge(dist, 1);
			draw_vert_edge(dist + 1, 1);
		}

		if (!l && la)
			draw_horiz_front(dist + 1, 0);
		if (!r && ra)
			draw_horiz_front(dist + 1, 1);
		if (a) {
			draw_horiz_wall(dist + 1);
		}

		copyumap(y + dy, x + dx, 0);	// ahead
		copyumap(y, x, 1);		// here 
		copyumap(y - dx, x + dy, 0);	// left 
		copyumap(y + dx, x - dy, 0);	// right
		if (!l)
			copyumap(y - dx + dy, x + dy + dx, 0);	// left ahead
		if (!r)
			copyumap(y + dx + dy, x - dy + dx, 0);	// right ahead
	}

	if (show_compass) {
		/* Provide a compass pointing 'north' */

		switch (pdir) {
		case DIR_UP:
			mvwaddch(view, 21, 21, dir_ind[0]); // ^
			mvwaddch(view, 22, 21, dir_ind[4]); // |
			break;
		case DIR_DOWN:
			mvwaddch(view, 21, 21, dir_ind[4]); // |
			mvwaddch(view, 22, 21, dir_ind[1]); // v
			break;
		case DIR_LEFT:
			mvwaddch(view, 21, 21, dir_ind[5]); // -
			mvwaddch(view, 21, 22, dir_ind[2]); // >
			break;
		case DIR_RIGHT:
			mvwaddch(view, 21, 20, dir_ind[3]); // <
			mvwaddch(view, 21, 21, dir_ind[5]); // -
			break;
		}
	}

	rb->lcd_update();
	mvwaddch(umap, py, px, ptab[(int)pdir]);
}

//new drawing routines
//CX,CY = center of screen
#define MAX_DEPTH 7
#define MAXX 319
#define MAXY 239
#define CX MAXX / 2
#define CY MAXY / 2

void draw_end_wall(int bx, int by) {
	rb->lcd_set_foreground(COLOR_PERP);
	rb->lcd_fillrect(CX - bx/2, CY - by/2, bx, by);
}

void draw_side(int fx, int bx, int by, int tan_n, int tan_d, bool isleft) {
	int i;
	int signx;

	//rb->lcd_set_foreground(COLOR_PARA);

	if(isleft) signx = -1; else signx = 1;
	
	for(i = bx; i < fx + 1; i++) {
		//add some stripes
		if(i % 3 == 0)
			rb->lcd_set_foreground(COLOR_PERP);
		else
			rb->lcd_set_foreground(COLOR_PARA);
		rb->lcd_drawline(CX + signx * i/2,
				 CY - tan_n * (i-bx)/2 / tan_d - by/2,
				 CX + signx * i/2,
				 CY + tan_n * (i-bx)/2 / tan_d + by/2);
	}
}

void draw_hall(int fx, int bx, int by, bool isleft) {

	rb->lcd_set_foreground(COLOR_PERP);

	if(isleft)
		rb->lcd_fillrect(CX - fx/2, CY - by/2, (fx - bx)/2 + 1, by);
	else
		rb->lcd_fillrect(CX + bx/2, CY - by/2, (fx - bx)/2 + 1, by);
}

void draw_side_tri(int fx, int fy, int bx, int tan_n, int tan_d,
		   bool isvisited, bool isgoal) {
	int i;
	int signx, signy;

	signy = 1;
	
	while(signy >= -1) {
		if(signy == 1)
			if(isgoal)
				rb->lcd_set_foreground(COLOR_RED);
			else if(isvisited)
				rb->lcd_set_foreground(COLOR_VISITED);
			else //if(!isvisited)
				rb->lcd_set_foreground(COLOR_GROUND);
		else
			rb->lcd_set_foreground(COLOR_SKY);
		
		signx = 1;
		
		while(signx >= -1) {
			for(i = fx; i > bx; i--) 
				rb->lcd_drawline(CX + signx * i/2,
						 CY + signy * fy/2,
						 CX + signx * i/2,
						 CY + signy * fy/2
						    + signy * tan_n
						    * (i - fx)/2 / tan_d);
			signx-=2;
		}
		signy-=2;
	}
}

void draw_hall_crnr(int fx, int fy, int bx, int by, 
		    bool isleft, bool isvisited, bool isgoal) {
	
	rb->lcd_set_foreground(COLOR_SKY);
	if(isleft)
		rb->lcd_fillrect(CX - fx/2, CY - fy/2,
				 (fx - bx)/2 + 1, (fy - by)/2);
	else
		rb->lcd_fillrect(CX + bx/2, CY - fy/2,
				 (fx - bx)/2 + 1, (fy - by)/2);
	
	if(isgoal)
		rb->lcd_set_foreground(COLOR_RED);
	else if(isvisited)
		rb->lcd_set_foreground(COLOR_VISITED);
	else
		rb->lcd_set_foreground(COLOR_GROUND);

	if(isleft)
		rb->lcd_fillrect(CX - fx/2, CY + by/2,
				 (fx - bx)/2 + 1, (fy - by)/2);
	else
		rb->lcd_fillrect(CX + bx/2, CY + by/2,
				 (fx - bx)/2 + 1, (fy - by)/2);
}

void draw_center_sq(int fy, int bx, int by, bool isvisited, bool isgoal) {
	rb->lcd_set_foreground(COLOR_SKY);
	rb->lcd_fillrect(CX - bx/2, CY - fy/2, bx, (fy - by)/2);

	if(isgoal)
		rb->lcd_set_foreground(COLOR_RED);
	else if(isvisited)
		rb->lcd_set_foreground(COLOR_VISITED);
	else
		rb->lcd_set_foreground(COLOR_GROUND);
	rb->lcd_fillrect(CX - bx/2, CY + by/2, bx, (fy - by)/2 + 1);
}

void draw_arrow(int sx, int sy, int pass) {
	if (pass > 2) return;
	
	rb->lcd_fillrect(sx, sy, 2, 2);
	switch(pdir) {
		case 0: //down
			rb->lcd_fillrect(sx + 2*pass, sy, 2, 2);
			draw_arrow(sx - 1, sy - 2, pass + 1);
			break;
		case 2: //up
			rb->lcd_fillrect(sx + 2*pass, sy, 2, 2);
			draw_arrow(sx - 1, sy + 2, pass + 1);
			break;
		case 1: //left
			rb->lcd_fillrect(sx, sy + 2*pass, 2, 2);
			draw_arrow(sx + 2, sy - 1, pass + 1);
			break;
		case 3: //right
			rb->lcd_fillrect(sx, sy + 2*pass, 2, 2);
			draw_arrow(sx - 2, sy - 1, pass +1);
			break;
	}
}	

void graphic_view(void) {
	int dist, lastdist;
	int x, y, dx, dy;
	int i;
	int a, l, r;
	int crd_x[MAX_DEPTH + 1], crd_y[MAX_DEPTH + 1];
	bool g, gl, gr;
	bool e, el, er;
	int tan_n, tan_d; // tangent numerator/denominator
	
	dx = dirtab[(int)pdir].x;
	dy = dirtab[(int)pdir].y;

	crd_x[0] = MAXX + 1;
	crd_y[0] = MAXY + 1;
	for (i=1; i < MAX_DEPTH + 1; i++) {
		crd_x[i] = crd_x[i-1]*2/3;
		if(crd_x[i] % 2 != 0) crd_x[i]++;
		crd_y[i] = crd_y[i-1]*2/3;
		if(crd_y[i] % 2 != 0) crd_y[i]++;
	}

	for (dist = 1; dist < MAX_DEPTH; dist++)
		if (isfigequal(at(py + dy * dist, px + dx * dist), BLOCK))
			break;
	lastdist = dist - 1;
	
	if (!show_map) {
		clearmap(umap);
		copyumap(gy, gx, 1);
	}
	
	//clearscreen();

	while (--dist >= 0) {
		x = px + dx * dist;
		y = py + dy * dist;
		
		/* ground */
		g = isfigequal(at(y, x), VISITED);
		gl = isfigequal(at(y - dx, x + dy), VISITED);
		gr = isfigequal(at(y + dx, x - dy), VISITED);
		e = isfigequal(at(y, x), GOAL);
		el = isfigequal(at(y - dx, x + dy), GOAL);
		er = isfigequal(at(y + dx, x - dy), GOAL);

		/* ahead */
		a = isfigequal(at(y + dy, x + dx), BLOCK);
		/* to the left */
		l = isfigequal(at(y - dx, x + dy), BLOCK);
		/* to the right */
		r = isfigequal(at(y + dx, x - dy), BLOCK);

		tan_n = crd_y[dist] - crd_y[dist+1]; 
		tan_d = crd_x[dist] - crd_x[dist+1];
		
		if (a)
			draw_end_wall(crd_x[dist+1], crd_y[dist+1]);
		if (l) {
			draw_side(crd_x[dist], crd_x[dist+1],
				  crd_y[dist+1], tan_n, tan_d, true);
		}
		else {
			draw_hall(crd_x[dist], crd_x[dist+1],
				  crd_y[dist+1], true);
			draw_hall_crnr(crd_x[dist], crd_y[dist],
				       crd_x[dist+1], crd_y[dist+1],
				       true, gl, el);
		}
		if (r) {
			draw_side(crd_x[dist], crd_x[dist+1],
				  crd_y[dist+1], tan_n, tan_d, false);
		}
		else {
			draw_hall(crd_x[dist], crd_x[dist+1],
				  crd_y[dist+1], false);
			draw_hall_crnr(crd_x[dist], crd_y[dist],
				       crd_x[dist+1], crd_y[dist+1],
				       false, gr, er);
		}
		
		draw_center_sq(crd_y[dist],
			       crd_x[dist+1], crd_y[dist+1], g, e);
		draw_side_tri(crd_x[dist], crd_y[dist],
			      crd_x[dist+1],
			      tan_n, tan_d, g, e);

		copyumap(y + dy, x + dx, 0);	// ahead
		copyumap(y, x, 1);		// here 
		copyumap(y - dx, x + dy, 0);	// left 
		copyumap(y + dx, x - dy, 0);	// right
		if (!l)
			copyumap(y - dx + dy, x + dy + dx, 0);	// left ahead
		if (!r)
			copyumap(y + dx + dy, x - dy + dx, 0);	// right ahead
	
	}

	if (show_compass) {
	/* Provide a compass pointing 'north' */
	rb->lcd_set_foreground(COLOR_YELLOW);
		switch(pdir) {
			case 0: //point down
				draw_arrow(CX - 1, CY + crd_y[1]/2 + 12, 0);
				rb->lcd_fillrect(CX - 1, CY + crd_y[1]/2 + 1,
						 2, 4);
				break;
			case 2: //point up
				draw_arrow(CX - 1, CY + crd_y[1]/2 + 1, 0);
				rb->lcd_fillrect(CX - 1, CY + crd_y[1]/2 + 9,
					       	 2, 4);
				break;
			case 1: //point left
				draw_arrow(CX - 10, CY + crd_y[1]/2 + 9, 0);
				rb->lcd_fillrect(CX - 2, CY + crd_y[1]/2 + 9,							 4, 2);
				break;	
			case 3: //point right
				draw_arrow(CX + 6, CY + crd_y[1]/2 + 9, 0);
				rb->lcd_fillrect(CX - 4, CY + crd_y[1]/2 + 9,
						 4, 2);
				break;
		}
	}	
}

/* unused code
void
shoot(void)
{
	int x, y, dx, dy;
	int maxy, maxx;

	dx = dirtab[(int)pdir].x;
	dy = dirtab[(int)pdir].y;
	getmaxyx(map, &maxy, &maxx);

	x = px + dx;
	y = py + dy;
	while (!isfigequal(at(y, x), BLOCK)) {
		x += dx;
		y += dy;
	}
	if (x == 0 || y == 0 || x == maxx - 1 || y == maxy - 1)
		//beep();
		rb->splash(HZ, "SHOOT BEEP");
	else
		mvwaddch(map, y, x, SPACE);
}
*/

void
win(void)
{
	int i;
	char amazed[8] = "amazing!";
	fig newton;

	for (i=0; i <= 8; i++) {
		newton.chr = amazed[i];
		newton.attrib = A_ROB;
		mvwaddch(msg, 0, i + 31, newton);
	}
	won++;
	showmap();
	show_map = 1;
}


/* Try to move the player in the direction given */
void
trymove(enum dir dir)
{
	int nx, ny;

	ny = py + dirtab[(int)dir].y;
	nx = px + dirtab[(int)dir].x;

	if (isfigequal(at(ny, nx), BLOCK)) {
		//beep();
		rb->splash(HZ/8, "Hit a wall!");
		graphic_view();
		return;
	}

	if (isfigequal(at(ny, nx), GOAL))
		win();

	mappmove(ny, nx, pdir);
	if (remember_visited && isfigequal(punder, SPACE))
		punder = VISITED;
	//drawview();
	graphic_view();
}

/*
void
walkleft(void)
{
	int a, l;
	int dx, dy;
	int owon = won;

	//nodelay(umap, 1);
	while (1) {
		rb->lcd_update();
		// usleep(100000); // slower walk
		if (won != owon)
			break;

		dx = dirtab[(int)pdir].x;
		dy = dirtab[(int)pdir].y;
		
		// ahead
		a = (isfigequal(at(py + dy, px + dx), BLOCK));
		// to the left
		l = (isfigequal(at(py - dx, px + dy), BLOCK));

		if (!l) {
			mappmove(py, px, LEFT_OF(pdir));
			drawview();
			rb->sleep(100000);
			trymove(pdir);
			continue;
		}
		if (a) {
			mappmove(py, px, RIGHT_OF(pdir));
			drawview();
			continue;
		} 
		trymove(pdir);
	}
}
*/

void
draw_text_map(void)
{
	int y, x;

	showingmaze = false;
	clearscreen();
	werase(view);

	for (y = 0; y < umap->maxy; y++) 
		for (x = 0; x < umap->maxx; x++) {
			mvwaddch(view, y, x, umap->coords[y][x]);
			mvwaddch(view, py, px, ptab[(int)pdir]);
		}
	rb->button_get(true);
}

void draw_tile_map(void)
{
	int x,y;
	char map_unit;
	int unit_fmt;

	clearscreen();

	#define DRAW_TILE( a )						\
        	rb->lcd_bitmap_part( amaze_tiles, 0,			\
                                     a*TILE_HEIGHT, TILE_WIDTH,		\
                                     x * TILE_WIDTH,  			\
                                     y * TILE_HEIGHT,			\
                                     TILE_WIDTH, TILE_HEIGHT);

	for(y=0; y < umap->maxy; y++)
		for (x=0; x < umap->maxx; x++) {

			map_unit = umap->coords[y][x].chr;
			unit_fmt = umap->coords[y][x].attrib;
			
			switch (map_unit) {
				case '.':
					DRAW_TILE(t_visited);
					break;
				case '#':
					DRAW_TILE(t_obspace);
					break;
				case '%':
					DRAW_TILE(t_goal);
					break;
				case ' ':
					if(unit_fmt==A_NORMAL)
						DRAW_TILE(t_space)
					else
						DRAW_TILE(t_block)
					break;
			}
	}
	
	x = sx;
	y = sy;
	DRAW_TILE(t_start);
	
	x = px;
	y = py;
	DRAW_TILE(pdir);

	rb->lcd_update();
	//rb->button_clear_queue();
	//rb->button_get(true);
}

bool load_map(char *filename, winder *amap)
{
	int fd;
	size_t n;
	int x,y;
	//int i;
	//int valid=0;
	//int num_file_chars= (23*35); // 10 more chars for good luck
	//char buf[num_file_chars];
	int maxx, maxy;

	getmaxyx(amap, &maxy, &maxx);
	maxx++; //to allow for \n
	char line[maxx];
	
	fig newton = BLOCK;

	/* load a map */	
	fd = rb->open(filename, O_RDONLY);
	if (fd < 0) {
		LOGF("Invalid map file: %s\n", filename);
		return false;
	}

	/*
	n=rb->read(fd, buf, num_file_chars);
	if (n <= 0) return false;
	rb->close(fd);
		
	for(i=0, y=0; i < num_file_chars; i++) {
		switch (buf[i]) {
			case ' ':
				newton = SPACE;
				break;
			case 'B':
				newton = BLOCK;
				break;
			case '%':
				newton = GOAL;
				break;
			case '#':
				newton = OBSPACE;
				break;
			case '.':
				newton = VISITED;
				break;
		}
		
		if((i+1) % 35 != 0)
			amap->coords[i / 35][i % 35] = newton;
	}
	*/

	for(y=0; y < maxy ; y++) {
		n = rb->read(fd, line, sizeof(line));
		if (n <= 0) {
			
			return false;
		}
		for(x=0; x < maxx; x++) {
			switch(line[x]) {
				case '\n':
					break;
				case '0': case '1': case '2': case '3':
					py = y;
					px = x;
					pdir = (int)line[x] - 48;
					newton = ptab[(int)pdir];
					break;
				case '+':
					sy = y;
					sx = x;
					newton = START;
					break;
				case ' ':
					newton = SPACE;
					break;
				case 'B':
					newton = BLOCK;
					break;
				case '%':
					newton = GOAL;
					gy = y;
					gx = x;
					break;
				case '#':
					newton = OBSPACE;
					break;
				case '.':
					newton = VISITED;
					break;
			}
		if (line[x] != '\n')
			mvwaddch(amap, y, x, newton);
		}
	}
	rb->close(fd);
	return true;
}

bool load_game(void)
{
	if (load_map(UMAP_FILE, umap) && load_map(MAP_FILE, map))
		return true;
	else
		return false;
}

				
bool save_map(char *filename, winder *amap)
{
	int x,y;
	char map_unit;
	int unit_fmt;
	int fd;
	char line[35];

	line[34]='\n'; //last cell is a linefeed
			

	fd = rb->open(filename, O_WRONLY|O_CREAT|O_TRUNC);	
	if(fd >= 0) {	
		for(y=0; y < amap->maxy; y++) {
			for (x=0; x < amap->maxx; x++) {
				map_unit = amap->coords[y][x].chr;
				unit_fmt = amap->coords[y][x].attrib;
				
				if(y == py && x == px)
					line[x] = (char)(pdir + 48);
				else if(y == sy && x == sx)
					line[x] = '+';
				else {
					switch (map_unit) {
						case ' ':
							if(unit_fmt == A_NORMAL)
								line[x] = ' ';
							else 
								line[x] = 'B';
							break;
						default:		
							line[x] = map_unit;
							break;
					}
				}
			}
			rb->write(fd,line,35);
		}
		rb->close(fd);
	}
	else return false;

	return true;
}

bool save_game(void)
{
	rb->splash(0, "Saving game...");
	if (save_map(UMAP_FILE, umap) && save_map(MAP_FILE, map))
		return true;
	else
		return false;
}
	
int pause_menu(void)
{
	bool menu_quit = false; 
	int result = 1;
	int selected = 0;
	MENUITEM_STRINGLIST(menu,"Options", NULL, "Continue",
			"Save & Continue", "Save & Quit", "Quit");
	
	while(!menu_quit) {
		clearscreen();
		switch(rb->do_menu(&menu, &selected, NULL, false)) {
			case 0:
				menu_quit = true;
				break;
			case 1:
				if (save_game()) {
					//drawview();
					graphic_view();
					menu_quit=true;
				}
				else

				break;
			case 2:
				if (save_game()) {
					result = 2;
					menu_quit = true;
				}
				else
					rb->splash(HZ*3, "Save Error");
				break;
			case 3:
				menu_quit = true;
				result = 0; 
				break;
		}
	}
	return result;
}

int
amaze(bool loading_maze)
{
	int quitting;
	int i;
	//char amazed[6] = "amaze!";
	//fig newton;

	clearscreen();
	rb->lcd_set_backdrop(NULL);

	rb->lcd_setfont(FONT_SYSFIXED);

	map = &map_winder;
	umap = &umap_winder;
	view = &view_winder;
	msg = &msg_winder;
	
	view->write_to_screen = true;
	view->maxy = 23;
	view->maxx = 45;
	view->offy = 0;
	view->offx = 0;

	/* This window is never updated, used purely for storage */
	//map = newwin(23, 79 - 45, 0, 45);
	map->write_to_screen = false;
	map->maxy = 23;
	map->maxx = 79-45;
	map->offy = 0;
	map->offx = 0; //change

	umap->write_to_screen = false;
	umap->maxy = 23;
	umap->maxx = 79-45;
	umap->offy = 0;
	umap->offx = 0; //change

	clearmap(umap);

	msg->write_to_screen = false;
	msg->maxy = 1;
	msg->maxx = 43;
	msg->offy = 0; //change
	msg->offx = 0;

	if (!loading_maze)
		makemaze();
	else
		if (!load_game()) {
			rb->splash(HZ*2, "Final Error loading map.");
			return 0;
		}

	/* Show where the goal is */

	copyumap(gy, gx, 1);

	/*
	werase(msg);
	newton.attrib = A_ROB;
	for (i=0; i < 6; i++) {
		newton.chr = amazed[i];
		mvwaddch(msg, 0, i + 19, newton);
	}
	*/

	//mvwprintw(msg, 0, 19, "amaze!");
	//wnoutrefresh(msg);
	rb->lcd_update();

	quitting = 0;

	mappmove(py, px, pdir);

	if (remember_visited)
		punder = VISITED;
	else
		punder = SPACE;
	
	clearscreen();
	//drawview();
	graphic_view();

	while (!quitting && !won) {
		//wmove(umap, 0, 0);
		//doupdate();
		
		rb->lcd_update();
		
		//nodelay(umap, 0);
		//switch (wgetch(umap)) {

		switch (rb->button_get(true)) {
		case BUTTON_MENU|BUTTON_SELECT:
			i = pause_menu();
			rb->lcd_setfont(FONT_SYSFIXED);
			clearscreen();
			//drawview();
			graphic_view();
			
			switch (i) {
				case 0:
					quitting = 1;
					break;
				case 1:				
					break;
				case 2:
					rb->splash(HZ*3, "See you later!");
					return 0;
					break;
			}
			break;
		case BUTTON_MENU:
			trymove(pdir);
			break;
		case BUTTON_PLAY:
			trymove(REVERSE_OF(pdir));
			break;
		case BUTTON_LEFT|BUTTON_SELECT:
			trymove(LEFT_OF(pdir));
			break;
		case BUTTON_RIGHT|BUTTON_SELECT:
			trymove(RIGHT_OF(pdir));
			break;
		case BUTTON_LEFT: 
			mappmove(py, px, LEFT_OF(pdir));
			//drawview();
			graphic_view();
			break;
		case BUTTON_RIGHT: 
			mappmove(py, px, RIGHT_OF(pdir));
			//drawview();
			graphic_view();
			break;
		case BUTTON_PLAY|BUTTON_SELECT:
			draw_tile_map();
			rb->sleep(250);
			rb->backlight_on();
			rb->sleep(250);
			clearscreen();
			//drawview();
			graphic_view();
			break;
		/*
		case ' ':
			if (can_shoot) {
				shoot();
				cheated++;
				drawview();
			} else
				beep();
			break;
	
		case BUTTON_MENU:
			cheated++;
			walkleft();
			break;
	
		*/
		}

		/*
		if (won && !continue_on_win) {
			//wmove(umap, 0, 0);
			//doupdate();
			rb->lcd_update();
			//beep();
			//sleep(1);
			break;
		}
		*/
	}

	//endwin();

	if (won) {
		if (cheated) {
			rb->splash(HZ*2, "You cheated.");
			//printf("You cheated.\n");
			return 0;
		}
		rb->splash(HZ*3, "You win!");
		//printf("You win!\n");
		return 1;
	} else {
		rb->splash(HZ*3, "You lose.");
		//printf("You lose.\n");
		return 0;
	}
}

bool options_menu(void)
{
	int selection = 0;
	bool menu_quit = false, result = true;

	MENUITEM_STRINGLIST(menu,"Options",NULL,"Play Game", "Load Game",
			"Show Compass","Show Map",
			"Remember Path","Quit");
		
	while(!menu_quit) {
		clearscreen();
		switch(rb->do_menu(&menu, &selection, NULL, false)) {
			case 0:
				if(!amaze(false))
					menu_quit = true;
				break;
			case 1:
				if(!amaze(true))
					menu_quit = true;
				break;
			case 2:
				rb->set_int("Show Compass",
				  "", INT, &show_compass,
				  NULL, 1, 0, 1, NULL);
				break;
			case 3:
				rb->set_int("Show Map",
				  "", INT, &show_map,
				  NULL, 1, 0, 1, NULL);
				break;
			case 4:
				rb->set_int("Remember Path",
				  "", INT, &remember_visited,
				  NULL, 1, 0, 1, NULL);
				break;
			case 5:
				menu_quit = true;
				result = false;
				break;
		}
	}
	return result;
}

enum plugin_status plugin_start(const void* parameter)
{
  (void) parameter;
  
  
  rb->srand(*rb->current_tick);
  
  //hard-code in program default options - will fix
  show_compass=1;
  show_map=1;
  remember_visited=1;

  //let's go, gentlemen, we have some work to do
  rb->lcd_set_backdrop(NULL);
  
  if(!options_menu()) return PLUGIN_OK;
  
  rb->lcd_setfont(FONT_UI);

  return PLUGIN_OK;
}
