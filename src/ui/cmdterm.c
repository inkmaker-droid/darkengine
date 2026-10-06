/*
@Copyright Looking Glass Studios, Inc.
1996,1997,1998,1999,2000 Unpublished Work.
*/

// $Header: r:/t2repos/thief2/src/ui/cmdterm.c,v 1.28 2000/02/19 13:28:19 toml Exp $

#include <string.h>
#include <stdio.h>
#include <ctype.h>

#include <kbcook.h>
#include <keydefs.h>
#include <2d.h>
#include <event.h>
#include <gadbox.h>
#include <gadtext.h>
#include <gcompose.h>
#include <guistyle.h>
#include <hotkey.h>
#include <appagg.h>
#include <resapi.h>
#include <fonrstyp.h>

#include <cmdterm.h>
#include <command.h>
#include <gen_bind.h>
#include <scrnman.h>
#include <status.h>
#include <simstate.h>

#include <config.h>

#include <mprintf.h>
#include <memall.h>
#include <dbmem.h>   // must be last header! 


enum StateBits
{
   kStatePaused = 1 << 0,
   kStateInUse  = 1 << 1,
   kStateVisible = 1 << 2,
};


struct _CmdTerm
{
   LGadRoot root;
   LGadTextBox titlebox;
   LGadTextBox outputbox;
   LGadTextBox textbox;
   LGadBox scrollbar;
   char titlebuf[80];
   char outputbuf[32768];
   char cmdbuf[256];
   int visiblelines;
   int scroll;
   int scrollbarwidth;
   int scrolldragoffset;
   int mousehandler;
   BOOL scrolldragging;
   BOOL mousehandlerinstalled;
   guiStyle style;
   IRes* fontres;
   grs_canvas* previousguicanvas;
   BOOL canvasredirected;
   ulong flags;
   ulong statebits;
} CmdTerm;

#define CMDTERM_MAX_LINES 4096
#define CMDTERM_LINE_LENGTH 256

static char CmdTermLines[CMDTERM_MAX_LINES][CMDTERM_LINE_LENGTH];
static int CmdTermLineCount = 0;
static int CmdTermNextLine = 0;

static BOOL cmdterm_is_scrollable(void)
{
   return (CmdTerm.flags & kCmdTermScrollable) != 0;
}

static int cmdterm_max_scroll(void)
{
   int max_scroll = CmdTermLineCount - CmdTerm.visiblelines;
   return max_scroll > 0 ? max_scroll : 0;
}

static void cmdterm_set_scroll(int scroll)
{
   int max_scroll = cmdterm_max_scroll();

   if (scroll < 0)
      scroll = 0;
   if (scroll > max_scroll)
      scroll = max_scroll;
   CmdTerm.scroll = scroll;
}

static void cmdterm_scroll_by(int lines)
{
   cmdterm_set_scroll(CmdTerm.scroll + lines);
}

static void cmdterm_scrollbar_geometry(int height, int* thumb_top,
                                       int* thumb_height)
{
   int max_scroll = cmdterm_max_scroll();
   int height_inside = height - 2;
   int size;
   int travel;

   if (height_inside < 1)
      height_inside = 1;

   if (CmdTermLineCount <= 0 || CmdTermLineCount <= CmdTerm.visiblelines)
      size = height_inside;
   else
      size = (int)(((long)height_inside * CmdTerm.visiblelines) /
                   CmdTermLineCount);

   if (size < 8)
      size = 8;
   if (size > height_inside)
      size = height_inside;

   travel = height_inside - size;
   *thumb_height = size;
   if (max_scroll > 0 && travel > 0)
      *thumb_top = 1 +
         (int)(((long)travel * (max_scroll - CmdTerm.scroll)) / max_scroll);
   else
      *thumb_top = 1;
}

static void cmdterm_restore_canvas(void)
{
   if (CmdTerm.canvasredirected)
   {
      DefaultGUIcanvas = CmdTerm.previousguicanvas;
      CmdTerm.previousguicanvas = NULL;
      CmdTerm.canvasredirected = FALSE;
   }
}

static void cmdterm_rebuild_output(void)
{
   int oldest;
   int max_scroll;
   int first;
   int shown;
   int i;
   int used = 0;

   CmdTerm.outputbuf[0] = '\0';
   if (CmdTerm.visiblelines <= 0 || CmdTermLineCount <= 0)
      return;

   max_scroll = CmdTermLineCount - CmdTerm.visiblelines;
   if (max_scroll < 0)
      max_scroll = 0;
   if (CmdTerm.scroll > max_scroll)
      CmdTerm.scroll = max_scroll;
   if (CmdTerm.scroll < 0)
      CmdTerm.scroll = 0;

   shown = CmdTermLineCount < CmdTerm.visiblelines
         ? CmdTermLineCount : CmdTerm.visiblelines;
   first = CmdTermLineCount - shown - CmdTerm.scroll;
   if (first < 0)
      first = 0;
   oldest = (CmdTermNextLine - CmdTermLineCount + CMDTERM_MAX_LINES)
          % CMDTERM_MAX_LINES;

   for (i = 0; i < shown && first + i < CmdTermLineCount; ++i)
   {
      const char* line = CmdTermLines[(oldest + first + i)
                                      % CMDTERM_MAX_LINES];
      int length = strlen(line);
      if (used + length + 2 >= (int)sizeof(CmdTerm.outputbuf))
         break;
      if (used)
         CmdTerm.outputbuf[used++] = '\n';
      memcpy(CmdTerm.outputbuf + used, line, length);
      used += length;
      CmdTerm.outputbuf[used] = '\0';
   }
}

static void cmdterm_add_line(const char* text, int length)
{
   if (length >= CMDTERM_LINE_LENGTH)
      length = CMDTERM_LINE_LENGTH - 1;
   memcpy(CmdTermLines[CmdTermNextLine], text, length);
   CmdTermLines[CmdTermNextLine][length] = '\0';
   CmdTermNextLine = (CmdTermNextLine + 1) % CMDTERM_MAX_LINES;
   if (CmdTermLineCount < CMDTERM_MAX_LINES)
      ++CmdTermLineCount;
}

void cmdterm_print(const char* text)
{
   const char* start;

   if (!text)
      return;

   start = text;
   while (*start)
   {
      const char* end = start;
      while (*end && *end != '\r' && *end != '\n')
         ++end;

      if (end == start)
         cmdterm_add_line("", 0);
      else
      {
         while (end - start >= CMDTERM_LINE_LENGTH)
         {
            cmdterm_add_line(start, CMDTERM_LINE_LENGTH - 1);
            start += CMDTERM_LINE_LENGTH - 1;
         }
         cmdterm_add_line(start, end - start);
      }

      if (!*end)
         break;
      start = end + 1;
      if (*end == '\r' && *start == '\n')
         ++start;
   }

   CmdTerm.scroll = 0;
   cmdterm_rebuild_output();
}

void cmdterm_redraw(void)
{
   if (!(CmdTerm.statebits & kStateInUse))
      return;

   if (!cmdterm_is_scrollable())
   {
      LGadUpdateTextBox(&CmdTerm.textbox);
      LGadDrawBox(VB(&CmdTerm.textbox),NULL);
      return;
   }

   if (!(CmdTerm.statebits & kStateVisible))
      return;

   LGadUpdateTextBox(&CmdTerm.outputbox);
   LGadUpdateTextBox(&CmdTerm.textbox);
   LGadDrawBox(VB(&CmdTerm.root),NULL);
}

static void cmdterm_draw(void)
{
   cmdterm_redraw();
   ScrnForceUpdate();
}

static void cmdterm_scroll_and_draw(int lines)
{
   cmdterm_scroll_by(lines);
   cmdterm_rebuild_output();
   cmdterm_draw();
}

static void cmdterm_scrollbar_draw(void* data, LGadBox* box)
{
   int width = grd_canvas->bm.w;
   int height = grd_canvas->bm.h;
   int thumb_top;
   int thumb_height;

   (void)data;
   (void)box;

   cmdterm_scrollbar_geometry(height, &thumb_top, &thumb_height);

   gr_set_fcolor(guiStyleGetColor(&CmdTerm.style, StyleColorBG));
   gr_rect(0, 0, width - 1, height - 1);
   gr_set_fcolor(guiStyleGetColor(&CmdTerm.style, StyleColorBorder));
   gr_box(0, 0, width - 1, height - 1);

   if (width > 4 && thumb_height > 0)
   {
      gr_set_fcolor(guiStyleGetColor(&CmdTerm.style,
         CmdTerm.scrolldragging ? StyleColorHilite : StyleColorFG));
      gr_rect(2, thumb_top, width - 3, thumb_top + thumb_height - 1);
   }
}

static bool cmdterm_scrollbar_mouse(short x, short y, short action,
                                    short wheel, LGadBox* box)
{
   Rect absolute;
   int thumb_top;
   int thumb_height;

   (void)x;

   region_abs_rect(LGadBoxRegion(box), LGadBoxRect(box), &absolute);
   cmdterm_scrollbar_geometry(RectHeight(&absolute), &thumb_top,
                              &thumb_height);

   if (wheel)
   {
      cmdterm_scroll_and_draw(3 * wheel);
      return TRUE;
   }

   if (action & MOUSE_LDOWN)
   {
      int local_y = y - absolute.ul.y;

      if (local_y >= thumb_top &&
          local_y < thumb_top + thumb_height && cmdterm_max_scroll() > 0)
      {
         CmdTerm.scrolldragging = TRUE;
         CmdTerm.scrolldragoffset = local_y - thumb_top;
         uiGrabFocus(LGadBoxRegion(box), UI_EVENT_MOUSE | UI_EVENT_MOUSE_MOVE);
         LGadDrawBox(box, NULL);
      }
      else if (local_y < thumb_top)
         cmdterm_scroll_and_draw(CmdTerm.visiblelines);
      else if (local_y >= thumb_top + thumb_height)
         cmdterm_scroll_and_draw(-CmdTerm.visiblelines);
      return TRUE;
   }

   if ((action & MOUSE_LUP) && CmdTerm.scrolldragging)
   {
      CmdTerm.scrolldragging = FALSE;
      uiReleaseFocus(LGadBoxRegion(box), UI_EVENT_MOUSE | UI_EVENT_MOUSE_MOVE);
      LGadDrawBox(box, NULL);
      return TRUE;
   }

   return FALSE;
}

static bool cmdterm_scrollbar_motion(short x, short y, LGadBox* box)
{
   Rect absolute;
   int thumb_top;
   int thumb_height;
   int travel;
   int max_scroll;

   (void)x;

   if (!CmdTerm.scrolldragging)
      return FALSE;

   region_abs_rect(LGadBoxRegion(box), LGadBoxRect(box), &absolute);
   cmdterm_scrollbar_geometry(RectHeight(&absolute), &thumb_top,
                              &thumb_height);
   travel = RectHeight(&absolute) - 2 - thumb_height;
   max_scroll = cmdterm_max_scroll();
   thumb_top = y - absolute.ul.y - CmdTerm.scrolldragoffset;

   if (thumb_top < 1)
      thumb_top = 1;
   if (thumb_top > 1 + travel)
      thumb_top = 1 + travel;

   if (travel > 0 && max_scroll > 0)
      cmdterm_set_scroll(max_scroll -
         (int)(((long)(thumb_top - 1) * max_scroll + travel / 2) / travel));
   else
      cmdterm_set_scroll(0);

   cmdterm_rebuild_output();
   cmdterm_draw();
   return TRUE;
}

static BOOL cmdterm_mouse_handler(uiEvent* event, Region* region, void* state)
{
   uiMouseEvent* mouse = (uiMouseEvent*)event;

   (void)region;
   (void)state;

   if (event->type != UI_EVENT_MOUSE || !mouse->wheel ||
       !(CmdTerm.statebits & kStateVisible))
      return FALSE;

   cmdterm_scroll_and_draw(3 * mouse->wheel);
   return TRUE;
}

static void cmdterm_print_help(const char* command)
{
   const char* argument = command;

   while (*argument && isspace((unsigned char)*argument))
      ++argument;
   if (strnicmp(argument, "help", 4) ||
       (argument[4] && !isspace((unsigned char)argument[4])))
      return;

   argument += 4;
   while (*argument && isspace((unsigned char)*argument))
      ++argument;

   if (!*argument)
   {
      cmdterm_print("Commands:");
      cmdterm_print("help [command]                 show command help");
      cmdterm_print("openmission [number/shortname] open an installed mission");
      cmdterm_print("listmissions                   list installed missions");
      cmdterm_print("immunity [on/off]              toggle player damage");
      cmdterm_print("flying [on/off]                toggle gravity");
      cmdterm_print("playerphysics [on/off]          toggle collision and gravity");
      cmdterm_print("invisible [on/off]             toggle invisibility");
      cmdterm_print("reticle [on/off]               toggle center reticle");
      cmdterm_print("retinfo                        describe reticle target");
      cmdterm_print("listscripts                    list loaded scripts");
      cmdterm_print("debugscripts [on/off]          toggle script logging");
      return;
   }

   {
      char name[64];
      char line[256];
      int length = 0;
      Command* found;

      while (argument[length] &&
             !isspace((unsigned char)argument[length]) &&
             length < (int)sizeof(name) - 1)
         ++length;
      memcpy(name, argument, length);
      name[length] = '\0';
      found = CommandFindString(name);
      if (found)
      {
         _snprintf(line, sizeof(line) - 1, "%s: %s", found->name,
                   found->comment ? found->comment : "no description");
         line[sizeof(line) - 1] = '\0';
         cmdterm_print(line);
      }
   }
}

void cmdterm_setup_cmds(void);
void cmdterm_focus(char* arg);

///////////////////////////
// stupid command history system

#define MAX_HISTORY_CNT 256

#define HISTORY_NO_LAST (-1)

#define HISTORY_FILE    "loc_hist.hst"
#define PREHISTORY_FILE "pre_hist.hst"

static char *cmd_history_text[MAX_HISTORY_CNT];
static int   cmd_history_idx=0, cmd_history_last_found=0;
static BOOL  cmd_try_command=FALSE;

#define history_string(idx) (cmd_history_text[idx])

//////////////////
// history idx manager
static int _history_get_cur_idx(void)
{
   int cur_idx=cmd_history_last_found;
   if (cur_idx==HISTORY_NO_LAST) cur_idx=cmd_history_idx;
   return cur_idx;
}

static void _history_set_idx(int idx)
{
   cmd_history_last_found=idx;
}

static int _history_get_idx(int offset)
{
   int idx=_history_get_cur_idx();
   idx+=MAX_HISTORY_CNT+offset;
   idx%=MAX_HISTORY_CNT;
   return idx;
}

// clear the history variables
static void _hist_clear(void)
{
   cmd_history_idx=cmd_history_last_found=0;
   cmd_try_command=FALSE;
}

/////////////////////
// file i/o of history list

// write out a history text file
static void _history_write_file(char *fname)
{
   FILE *fp;   // well, files are evil, but oh well
   int i;

   if ((fp=fopen(fname,"w"))!=NULL)
   {
      for (i=0; i<MAX_HISTORY_CNT; i++)
      {
         int idx=(cmd_history_idx+i)%MAX_HISTORY_CNT;
         if (cmd_history_text[idx])
         {
            fputs(cmd_history_text[idx],fp);
            fputs("\n",fp);
         }
         else
            fputs("NULL\n",fp);
      }
      fclose(fp);
   }
   else
      Warning(("Can't save out command history file\n"));
}

// read in a history text file
static void _history_read_file(char *fname, BOOL clean)
{
   FILE *fp;   // well, files are evil, but oh well
   char buf[256];
   int i=0, idx, max_i=-1;

   if ((fp=fopen(fname,"r"))!=NULL)
   {
      while ((!feof(fp))&&(i<MAX_HISTORY_CNT))
      {
         if (clean) idx=i;
         else       idx=(cmd_history_idx+i)%MAX_HISTORY_CNT;
         if (fgets(buf,256,fp))
         {
            if (strlen(buf)>0)
               if (buf[strlen(buf)-1]==0x0a) buf[strlen(buf)-1]='\0';
            if (cmd_history_text[idx]!=NULL)
               Free(cmd_history_text[idx]);
            if ((strncmp(buf,"NULL",4)==0)||(buf[0]=='\0'))
               cmd_history_text[idx]=NULL;
            else
            {
               cmd_history_text[idx]=(char *)Malloc(strlen(buf)+2);
               strcpy(cmd_history_text[idx],buf);
               if (i>max_i) max_i=i;
            }
            i++;
         }
      }
      fclose(fp);
   }   
   if (clean) cmd_history_idx=max_i+1;
   else       cmd_history_idx=(cmd_history_idx+max_i+1)%MAX_HISTORY_CNT;
}

///////////////////
// actually add this command to the history ring
static void history_add(char *txt)
{
   int history_idx,previous_idx;
   char *ptr;

   // set this regardless, for now, unless we blow it off
   config_set_string("last_command",txt);

   // first, are we the same as either the history val
#define NO_MULTI_HISTORY
#ifdef NO_MULTI_HISTORY
   history_idx=_history_get_cur_idx(); // history command this may have come from
   if (history_string(history_idx))
      if (stricmp(history_string(history_idx),txt)==0)  // same
         return;
#endif
   previous_idx=(cmd_history_idx+MAX_HISTORY_CNT-1)%MAX_HISTORY_CNT;
   if (previous_idx!=history_idx)      // previous command
      if (history_string(previous_idx))
         if (stricmp(history_string(previous_idx),txt)==0)  // same
            return;
   ptr=(char *)Malloc(strlen(txt)+2);
   if (cmd_history_text[cmd_history_idx])
      Free(cmd_history_text[cmd_history_idx]);
   strcpy(ptr,txt);
   cmd_history_text[cmd_history_idx]=ptr;
   cmd_history_idx=(cmd_history_idx+1)%MAX_HISTORY_CNT;
}

////////////////////
// initialization/cleanup

// start up history system
void history_start(void)
{  // look for a history.cfg file
   _hist_clear();
   _history_read_file(HISTORY_FILE,TRUE);
   _history_read_file(PREHISTORY_FILE,FALSE);
}

// free all history memory
void history_free_all(void)
{
   int i;
   _history_write_file(HISTORY_FILE);
   for (i=0; i<MAX_HISTORY_CNT; i++)
      if (cmd_history_text[i])
      {
         Free(cmd_history_text[i]);
         cmd_history_text[i]=NULL;
      }
   _hist_clear();
}

/////////////////////
// commands/setup

static void history_new_cmdterm(void)
{
   cmd_history_last_found=HISTORY_NO_LAST;
   cmd_try_command=FALSE;
}

///////////////////
// used by the actual client

// find in the history ring
static char *history_find(char *base)
{
   int i;
   
   for (i=1; i<MAX_HISTORY_CNT; i++)
   {  // relativize it into right count space
      int idx=_history_get_idx(-i);
      if (idx==cmd_history_idx)
      {
         cmd_history_last_found=HISTORY_NO_LAST;  // ok, no history context now
         break;                                   // wrapped around, so switch to commands
      }
      if (cmd_history_text[idx])
         if (strnicmp(cmd_history_text[idx],base,strlen(base))==0)
         {
            _history_set_idx(idx);
            return cmd_history_text[idx];
         }
   }
   return NULL;
}

static char *history_get(int offset)
{
   int idx, base=_history_get_idx(0);

   do {
      idx=_history_get_idx(offset);
      _history_set_idx(idx);
   } while ((idx!=base)&&(cmd_history_text[idx]==NULL));
   return cmd_history_text[idx];
}

#ifdef DBG_ON
static void history_show(void)
{
   int i;
   for (i=0; i<MAX_HISTORY_CNT; i++)
      if (cmd_history_text[i])
         mprintf("%d> %s\n",i,cmd_history_text[i]);
}
#endif

// returns correct cursor position
static int move_cursor_into_command(char *cmd, int max_len)
{
   if (cmd)
   {
      char *space=strchr(cmd,' ');
      int len=strlen(cmd);
      if (len==0)
         return 0;
      else if ((space==NULL)&&(len<max_len))
      {
         strcpy(cmd+len," ");
         space=cmd+len;
      }
      while (*space==' ') space++;
      return space-cmd;
   }
   return 0;
}

////////////////
// callback for textbox

static short speckeys[] = 
{
   KB_FLAG_DOWN|KEY_ENTER,
   KB_FLAG_DOWN|KEY_ESC,
   KB_FLAG_DOWN|KB_FLAG_CTRL|'g',
   KB_FLAG_DOWN|KB_FLAG_CTRL|'n',
   KB_FLAG_DOWN|KB_FLAG_CTRL|'p',
   KB_FLAG_DOWN|KEY_TAB,
   KB_FLAG_DOWN|KEY_PGUP,
   KB_FLAG_DOWN|KEY_PAD_PGUP,
   KB_FLAG_DOWN|KEY_PGDN,
   KB_FLAG_DOWN|KEY_PAD_PGDN,
   KB_FLAG_DOWN|KEY_HOME,
   KB_FLAG_DOWN|KEY_PAD_HOME,
   KB_FLAG_DOWN|KEY_END,
   KB_FLAG_DOWN|KEY_PAD_END,
   0
};

static char last_prefix[sizeof(CmdTerm.cmdbuf)]=" ";

#pragma off(unreferenced)
bool cmdterm_textbox_cb(LGadTextBox* box, LGadTextBoxEvent event, int evdata, void* data)
{
   char  help_buf[256], *help_ptr=NULL;
   char* text = LGadTextBoxText(box);
   BOOL update = FALSE, clear = FALSE, new_base=FALSE;
   int cursor_pos;
   
   if (event == TEXTBOX_SPECKEY)
   {
      char *tmp=NULL, *ret;
      unsigned long context;

      switch(evdata)
      {
      case KB_FLAG_DOWN|KEY_ENTER:
         if (!cmdterm_is_scrollable())
         {
            Status("");

            if (CmdTerm.statebits & kStatePaused)
            {
               SimStateUnpause();
               CmdTerm.statebits &= ~kStatePaused;
            }

            IInputBinder_GetContext (g_pInputBinder, &context);
            if (context == HK_COMMAND_MODE && HotkeyContext == HK_GAME_MODE)
               IInputBinder_SetContext (g_pInputBinder, HK_GAME_MODE, TRUE);

            if ((ret = IInputBinder_ProcessCmd (g_pInputBinder, text)) != NULL)
               Status(ret);
            else
               history_add(text);
            *text = '\0';
            update = TRUE;
            clear = TRUE;
            break;
         }

         if (text && *text)
         {
            char entered[sizeof(CmdTerm.cmdbuf) + 3];
            _snprintf(entered, sizeof(entered) - 1, "> %s", text);
            entered[sizeof(entered) - 1] = '\0';
            cmdterm_print(entered);
            Status("");

            // Temporarily unpause while executing so commands may safely
            // change modes or simulation state.
            if (CmdTerm.statebits & kStatePaused)
            {
               SimStateUnpause();
               CmdTerm.statebits &= ~kStatePaused;
            }

            IInputBinder_GetContext (g_pInputBinder, &context);
            if (context == HK_COMMAND_MODE && HotkeyContext == HK_GAME_MODE)
               IInputBinder_SetContext (g_pInputBinder, HK_GAME_MODE, TRUE);

            ret = IInputBinder_ProcessCmd (g_pInputBinder, text);
            if (ret != NULL)
            {
               Status(ret);
               cmdterm_print(ret);
            }
            else
            {
               history_add(text);
               cmdterm_print_help(text);
            }

            if (CmdTerm.statebits & kStateInUse)
            {
               SimStatePause();
               CmdTerm.statebits |= kStatePaused;
               if (HotkeyContext == HK_GAME_MODE)
                  IInputBinder_SetContext (g_pInputBinder,
                                           HK_COMMAND_MODE, TRUE);
            }
         }
         *text = '\0';
         update = TRUE;
         break;
      case KB_FLAG_DOWN|KB_FLAG_CTRL|'g':
      case KB_FLAG_DOWN|KEY_ESC:
         IInputBinder_GetContext (g_pInputBinder, &context);
         if (context == HK_COMMAND_MODE && HotkeyContext == HK_GAME_MODE) {
            IInputBinder_SetContext (g_pInputBinder, HK_GAME_MODE,TRUE);
         }
         if (text&&((*text)!='\0'))
            Status("");
         *text = '\0';
         update = TRUE;
         clear = TRUE;
         break;
      case KB_FLAG_DOWN|KB_FLAG_CTRL|'p':
         tmp=history_get(-1);
         cursor_pos=move_cursor_into_command(tmp,sizeof(CmdTerm.cmdbuf));
         LGadTextBoxSetCursor(box,cursor_pos);   // cp was strlen(tmp)
         break;
      case KB_FLAG_DOWN|KB_FLAG_CTRL|'n':
         tmp=history_get(+1);
         cursor_pos=move_cursor_into_command(tmp,sizeof(CmdTerm.cmdbuf));
         LGadTextBoxSetCursor(box,cursor_pos);
         break;
      case KB_FLAG_DOWN|KEY_PGUP:
      case KB_FLAG_DOWN|KEY_PAD_PGUP:
         if (cmdterm_is_scrollable())
         {
            cmdterm_scroll_by(CmdTerm.visiblelines);
            cmdterm_rebuild_output();
            update = TRUE;
         }
         break;
      case KB_FLAG_DOWN|KEY_PGDN:
      case KB_FLAG_DOWN|KEY_PAD_PGDN:
         if (cmdterm_is_scrollable())
         {
            cmdterm_scroll_by(-CmdTerm.visiblelines);
            cmdterm_rebuild_output();
            update = TRUE;
         }
         break;
      case KB_FLAG_DOWN|KEY_HOME:
      case KB_FLAG_DOWN|KEY_PAD_HOME:
         if (cmdterm_is_scrollable())
         {
            cmdterm_set_scroll(cmdterm_max_scroll());
            cmdterm_rebuild_output();
            update = TRUE;
         }
         break;
      case KB_FLAG_DOWN|KEY_END:
      case KB_FLAG_DOWN|KEY_PAD_END:
         if (cmdterm_is_scrollable())
         {
            cmdterm_set_scroll(0);
            cmdterm_rebuild_output();
            update = TRUE;
         }
         break;
      case KB_FLAG_DOWN|KEY_TAB:
         text[LGadTextBoxCursor(box)] = '\0';
         if (strnicmp(last_prefix,text,strlen(last_prefix)))
         {  // if last string start not the same, the reset 
            new_base=TRUE;
            cmd_history_last_found=HISTORY_NO_LAST;
            strcpy(last_prefix,text);
         }
         if (!cmd_try_command)
         {
            if ((tmp=history_find(text))==NULL)
            {
               cmd_try_command=TRUE;
               tmp=command_find(text,TRUE);
            }
         }
         else
         {
            if ((tmp=command_find(text,new_base))==NULL)
            {
               cmd_try_command=FALSE;
               tmp=history_find(text);
            }
         }
         if (tmp==NULL)
            Status("No Completion");
         else if (cmd_try_command)
            if (cur_command_help_str(help_buf))
               Status(help_buf);
      }
      if (tmp!=NULL)
      {
         strcpy(text,tmp);
         update = TRUE;
      }
   }
   if (update)
   {
      LGadUpdateTextBox(box);
      if (clear)
      {
         if (cmdterm_is_scrollable())
         {
            CmdTerm.statebits &= ~kStateVisible;
            if (CmdTerm.scrolldragging)
            {
               uiReleaseFocus(LGadBoxRegion(&CmdTerm.scrollbar),
                              UI_EVENT_MOUSE | UI_EVENT_MOUSE_MOVE);
               CmdTerm.scrolldragging = FALSE;
            }
         }
         LGadUnfocusTextBox(box);
         if (CmdTerm.flags & kCmdTermHideUnfocused)
            region_set_invisible(LGadBoxRegion(&CmdTerm.root),TRUE);
         if (CmdTerm.statebits & kStatePaused)
         {
            SimStateUnpause();
            CmdTerm.statebits &= ~kStatePaused;
         }
         if (cmdterm_is_scrollable())
            cmdterm_restore_canvas();
      }
      else if (!(CmdTerm.statebits & kStatePaused))
      {
         SimStatePause();
         CmdTerm.statebits |= kStatePaused;
      }

      if (!clear && cmdterm_is_scrollable())
         cmdterm_draw();
   }
   return update;
}
#pragma on(unreferenced)

void CreateCommandTerminal(LGadRoot* root, Rect* bounds, ulong flags)
{
   IResMan* resman;
   short font_width;
   short font_height;
   short row_height;
   short title_height;
   short input_top;
   short output_width;

   if (CmdTerm.statebits & kStateInUse)
      DestroyCommandTerminal();
   memset(&CmdTerm,0,sizeof(CmdTerm));
   CmdTerm.flags = flags;

   // DromEd owns a compact, permanently embedded command-entry pane.  Keep
   // that legacy presentation separate from the optional in-game overlay.
   if (!cmdterm_is_scrollable())
   {
      LGadTextBoxDesc tdesc;

      LGadSetupSubRoot(&CmdTerm.root,root,bounds->ul.x,bounds->ul.y,
                       (short)RectWidth(bounds),(short)RectHeight(bounds));

      memset(&tdesc,0,sizeof(tdesc));
      tdesc.bounds.lr = MakePoint(RectWidth(bounds),RectHeight(bounds));
      tdesc.bounds.ul = MakePoint(0,0);
      tdesc.editbuf = CmdTerm.cmdbuf;
      tdesc.editbuflen = sizeof(CmdTerm.cmdbuf);
      CmdTerm.cmdbuf[0] = '\0';
      tdesc.flags = TEXTBOX_BORDER_FLAG|TEXTBOX_FOCUS_FLAG;
      tdesc.cb = cmdterm_textbox_cb;

      LGadCreateTextBoxDesc(&CmdTerm.textbox,&CmdTerm.root,&tdesc);
      LGadTextBoxSetSpecialKeys(&CmdTerm.textbox,speckeys);
      LGadBoxSetFlags(&CmdTerm.textbox,
         LGadBoxFlags(&CmdTerm.textbox)|BOXFLAG_FLIP);

      CmdTerm.statebits = kStateInUse;
      if (flags & kCmdTermBeginFocused)
         cmdterm_focus("");
      else if (flags & kCmdTermHideUnfocused)
         region_set_invisible(LGadBoxRegion(&CmdTerm.root),TRUE);
      LGadDrawBox(VB(&CmdTerm.root),NULL);
      cmdterm_setup_cmds();
      return;
   }

   // Use the same larger bitmap font as Thief's in-game messages. The normal
   // UI font was designed for the low-resolution menus and is hard to read
   // when the game canvas is 1080p or larger.
   if (GetCurrentStyle())
      memcpy(&CmdTerm.style, GetCurrentStyle(), sizeof(CmdTerm.style));
   resman = AppGetObj(IResMan);
   if (resman)
   {
      CmdTerm.fontres = IResMan_Bind(resman, "textfont", RESTYPE_FONT,
                                    NULL, "intrface", 0);
      SafeRelease(resman);
   }
   if (CmdTerm.fontres)
   {
      CmdTerm.style.fonts[StyleFontNormal] = (IDataSource*)CmdTerm.fontres;
      CmdTerm.style.fonts[StyleFontTitle] = (IDataSource*)CmdTerm.fontres;
   }

   LGadSetupSubRoot(&CmdTerm.root,root,bounds->ul.x,bounds->ul.y,(short)RectWidth(bounds),(short)RectHeight(bounds));

   guiStyleSetupFont(&CmdTerm.style,StyleFontNormal);
   gr_string_size("X",&font_width,&font_height);
   guiStyleCleanupFont(&CmdTerm.style,StyleFontNormal);
   row_height = font_height + 2;
   title_height = row_height;
   input_top = (short)(RectHeight(bounds) - row_height);
   CmdTerm.scrollbarwidth = row_height;
   if (CmdTerm.scrollbarwidth < 14)
      CmdTerm.scrollbarwidth = 14;
   if (CmdTerm.scrollbarwidth > 24)
      CmdTerm.scrollbarwidth = 24;
   output_width = (short)(RectWidth(bounds) - CmdTerm.scrollbarwidth);
   CmdTerm.visiblelines = (input_top - title_height) / font_height;
   if (CmdTerm.visiblelines < 1)
      CmdTerm.visiblelines = 1;

   // Use a separate title row so an empty command line is still visibly open.
   {
      LGadTextBoxDesc tdesc;

      memset(&tdesc,0,sizeof(tdesc));
      tdesc.bounds.lr = MakePoint(RectWidth(bounds),title_height);
      tdesc.bounds.ul = MakePoint(0,0);

      tdesc.editbuf = CmdTerm.titlebuf;
      tdesc.editbuflen = sizeof(CmdTerm.titlebuf);
      tdesc.style = &CmdTerm.style;
      strcpy(CmdTerm.titlebuf,
             "Console - wheel/Page Up/Page Down/Home/End scroll; Esc closes");

      LGadCreateTextBoxDesc(&CmdTerm.titlebox,&CmdTerm.root,&tdesc);
      LGadTextBoxClrFlag(&CmdTerm.titlebox,
         TEXTBOX_EDIT_EDITABLE|TEXTBOX_EDIT_BRANDNEW);
      LGadBoxSetFlags(&CmdTerm.titlebox,
         LGadBoxFlags(&CmdTerm.titlebox)|BOXFLAG_FLIP);
   }

   // Scrollable output area between the title and command line.
   {
      LGadTextBoxDesc tdesc;

      memset(&tdesc,0,sizeof(tdesc));
      tdesc.bounds.lr = MakePoint(output_width,input_top);
      tdesc.bounds.ul = MakePoint(0,title_height);

      tdesc.editbuf = CmdTerm.outputbuf;
      tdesc.editbuflen = sizeof(CmdTerm.outputbuf);
      tdesc.flags = TEXTBOX_BORDER_FLAG;
      tdesc.style = &CmdTerm.style;

      LGadCreateTextBoxDesc(&CmdTerm.outputbox,&CmdTerm.root,&tdesc);
      LGadTextBoxClrFlag(&CmdTerm.outputbox,
         TEXTBOX_EDIT_EDITABLE|TEXTBOX_EDIT_BRANDNEW);
      LGadBoxSetFlags(&CmdTerm.outputbox,
         LGadBoxFlags(&CmdTerm.outputbox)|BOXFLAG_FLIP);
   }

   // A conventional proportional scrollbar for the retained output history.
   LGadCreateBox(&CmdTerm.scrollbar, &CmdTerm.root, output_width, title_height,
                 (short)CmdTerm.scrollbarwidth,
                 (short)(input_top - title_height), cmdterm_scrollbar_mouse,
                 NULL, cmdterm_scrollbar_draw, 0);
   LGadBoxSetStyle(&CmdTerm.scrollbar, &CmdTerm.style);
   LGadBoxSetFlags(&CmdTerm.scrollbar,
      LGadBoxFlags(&CmdTerm.scrollbar)|BOXFLAG_FLIP);
   LGadBoxMouseMotion(&CmdTerm.scrollbar, cmdterm_scrollbar_motion);

   if (uiInstallRegionHandler(LGadBoxRegion(&CmdTerm.root), UI_EVENT_MOUSE,
                              cmdterm_mouse_handler, NULL,
                              &CmdTerm.mousehandler) == OK)
      CmdTerm.mousehandlerinstalled = TRUE;

   // Make the editable command line on the bottom row.
   {   
      LGadTextBoxDesc tdesc;

      memset(&tdesc,0,sizeof(tdesc));
      tdesc.bounds.lr = MakePoint(RectWidth(bounds),RectHeight(bounds));
      tdesc.bounds.ul = MakePoint(0,input_top);
      
      tdesc.editbuf = CmdTerm.cmdbuf;
      tdesc.editbuflen = sizeof(CmdTerm.cmdbuf);
      CmdTerm.cmdbuf[0] = '\0';
      tdesc.flags = TEXTBOX_BORDER_FLAG|TEXTBOX_FOCUS_FLAG;
      tdesc.style = &CmdTerm.style;
      tdesc.cb = cmdterm_textbox_cb;

      LGadCreateTextBoxDesc(&CmdTerm.textbox,&CmdTerm.root,&tdesc);
      LGadTextBoxSetSpecialKeys(&CmdTerm.textbox,speckeys);
      LGadBoxSetFlags(&CmdTerm.textbox,
         LGadBoxFlags(&CmdTerm.textbox)|BOXFLAG_FLIP);
   }
   CmdTerm.statebits = kStateInUse;
   if (!CmdTermLineCount)
      cmdterm_print("Thief 2 console ready. Type help for commands.");
   else
      cmdterm_rebuild_output();
   if (flags & kCmdTermBeginFocused)
   {
      cmdterm_focus("");
   }
   else if (flags & kCmdTermHideUnfocused)
   {
      region_set_invisible(LGadBoxRegion(&CmdTerm.root),TRUE);
   }
   LGadDrawBox(VB(&CmdTerm.root),NULL);
   cmdterm_setup_cmds();
}

void DestroyCommandTerminal(void)
{
   unsigned long context;

   if (CmdTerm.statebits & kStatePaused)
   {
      SimStateUnpause();
      CmdTerm.statebits &= ~kStatePaused;
   }
   IInputBinder_GetContext (g_pInputBinder, &context);
   if (context == HK_COMMAND_MODE && HotkeyContext == HK_GAME_MODE)
      IInputBinder_SetContext (g_pInputBinder, HK_GAME_MODE, TRUE);
   if (cmdterm_is_scrollable())
      cmdterm_restore_canvas();

   CmdTerm.statebits &= ~kStateInUse;
   if (cmdterm_is_scrollable() && CmdTerm.scrolldragging)
   {
      uiReleaseFocus(LGadBoxRegion(&CmdTerm.scrollbar),
                     UI_EVENT_MOUSE | UI_EVENT_MOUSE_MOVE);
      CmdTerm.scrolldragging = FALSE;
   }
   if (CmdTerm.mousehandlerinstalled)
   {
      uiRemoveRegionHandler(LGadBoxRegion(&CmdTerm.root),
                            CmdTerm.mousehandler);
      CmdTerm.mousehandlerinstalled = FALSE;
   }
   LGadDestroyTextBox(&CmdTerm.textbox);
   if (cmdterm_is_scrollable())
   {
      LGadDestroyBox(&CmdTerm.scrollbar, FALSE);
      LGadDestroyTextBox(&CmdTerm.outputbox);
      LGadDestroyTextBox(&CmdTerm.titlebox);
   }
   LGadDestroyRoot(&CmdTerm.root);
   SafeRelease(CmdTerm.fontres);
   
}

void cmdterm_focus(char* prefix)
{
   char* buf = &CmdTerm.cmdbuf[0];
   int bufsiz = sizeof(CmdTerm.cmdbuf);
   int cursor_pos;
   unsigned long context;

   if (!(CmdTerm.statebits & kStateInUse))
      return;

   if (prefix)
      strncpy(buf,prefix,bufsiz);
   else
      strcpy(buf,"");
   buf[bufsiz-1] = '\0';
   // make the region visible, if necessary
   region_set_invisible(LGadBoxRegion(&CmdTerm.root),FALSE);
   if (cmdterm_is_scrollable())
   {
      CmdTerm.statebits |= kStateVisible;
      if (!CmdTerm.canvasredirected)
      {
         CmdTerm.previousguicanvas = DefaultGUIcanvas;
         DefaultGUIcanvas = ScrnGetDrawCanvas();
         CmdTerm.canvasredirected = TRUE;
      }
   }
   cursor_pos=move_cursor_into_command(buf,bufsiz);

   LGadTextBoxSetCursor(&CmdTerm.textbox,cursor_pos);
   LGadUpdateTextBox(&CmdTerm.textbox);
   LGadFocusTextBox(&CmdTerm.textbox);
   LGadTextBoxClrFlag(&CmdTerm.textbox,TEXTBOX_EDIT_BRANDNEW);
   if (cmdterm_is_scrollable())
   {
      cmdterm_rebuild_output();
      cmdterm_draw();
   }
   else
      LGadDrawBox(VB(&CmdTerm.textbox),NULL);
   // pause the sim state
   SimStatePause();
   

   IInputBinder_GetContext (g_pInputBinder, &context);
   if (context == HK_GAME_MODE)
      IInputBinder_SetContext (g_pInputBinder, HK_COMMAND_MODE, TRUE);
   
   CmdTerm.statebits |= kStatePaused;
}

static void new_cmdterm(char *prefix)
{
   history_new_cmdterm();
   cmdterm_focus(prefix);
}

static void history_cmdterm(int offset)
{
   int idx;
   history_new_cmdterm();
   idx=_history_get_idx(offset);
   if (history_string(idx))
   {
      _history_set_idx(idx);
      cmdterm_focus(history_string(idx));
   }
   else
      cmdterm_focus("");
}

static Command commands[] = 
{
   { "edit_command", FUNC_STRING, new_cmdterm, "edit a command in the command editor: edit_command <cmd>",HK_ALL},
   { "history_cmd", FUNC_INT, history_cmdterm, "edit command from history offset" },
#ifdef DBG_ON   
   { "history_dump", FUNC_VOID, history_show },
#endif   
};

void cmdterm_setup_cmds(void)
{
   static bool setup = FALSE;
   if (!setup)
   {
     COMMANDS(commands,HK_ALL);
     setup = TRUE;
   }
}
