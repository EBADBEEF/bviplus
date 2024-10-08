/*************************************************************
 *
 * File:        key_handler.c
 * Author:      David Kelley
 * Description: Handle key presses and other user input
 *
 * Copyright (C) 2009 David Kelley
 *
 * This file is part of bviplus.
 *
 * Bviplus is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Bviplus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with bviplus.  If not, see <http://www.gnu.org/licenses/>.
 *
 *************************************************************/

#include <ncurses.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include "key_handler.h"
#include "user_prefs.h"
#include "display.h"
#include "actions.h"
#include "app_state.h"
#include "help.h"
#include "virt_file.h"

#define ALPHANUMERIC(x) ((x >= 'a' && x <= 'z') || (x >= 'A' && x <= 'Z') ||  (x >= '0' && x <= '9'))
#define WHITESPACE(x) (x == ' ' || x == '\t' || !isprint(x)) /* includes all non-print chars */

macro_record_t  macro_record[26];
int             macro_key = -1;
int             last_macro_key = -1;

int mwgetch(WINDOW *w)
{
  int i, k;

  if (macro_key == -1)
    return wgetch(w);
  else
  {
    k = wgetch(w);
    i = macro_record[macro_key].key_index++;
    macro_record[macro_key].key[i] = k;
    return k;
  }
}
int mgetch(void)
{
  int i, k;

  if (macro_key == -1)
    return getch();
  else
  {
    k = getch();
    i = macro_record[macro_key].key_index++;
    macro_record[macro_key].key[i] = k;
    return k;
  }
}

action_code_t show_set(void)
{
  action_code_t error = E_SUCCESS;
  int i = 0, num_elements = 0, eq_tab1 = 25, eq_tab2 = 35, len;
  char **text;

  while (user_prefs[num_elements].flags != P_NONE)
    num_elements++;

  /* one extra for delimeter */
  text = malloc(sizeof(char *)*(num_elements+2));
  if (text == NULL)
  {
    msg_box("Could not allocate memory for display window");
    return E_INVALID;
  }

  text[0] = (char *)malloc(256);
  snprintf(text[0], 256, " [Setting Name]          [Alias]     [Value]");

  for(i=0; i<num_elements; i++)
  {
    text[i+1] = (char *)malloc(256);
    snprintf(text[i+1], 256, " %s", user_prefs[i].name);
    len = strlen(text[i+1]);
    for (;len<eq_tab1;len++)
      snprintf(text[i+1] + len, 256 - len, " ");
    snprintf(text[i+1] + len, 256 - len, "%s", user_prefs[i].short_name);
    len = strlen(text[i+1]);
    for (;len<eq_tab2;len++)
      snprintf(text[i+1] + len, 256 - len, " ");

    if (user_prefs[i].flags == P_INT)
      snprintf(text[i+1] + len, 256 - len, "= %d",
               user_prefs[i].value);
    else if (user_prefs[i].flags == P_BOOL)
      snprintf(text[i+1] + len, 256 - len, "= %s",
               user_prefs[i].value == TRUE ? "TRUE" : "FALSE");
  }

  text[i+1] = NULL;
  scrollable_window_display(text);

  for(i=0; i<num_elements+1; i++)
    free(text[i]);

  free(text);

  return error;
}

action_code_t do_set(void)
{
  action_code_t error = E_SUCCESS;
  const char delimiters[] = " =";
  char *option, *value;

  /* process same string as last strtok() call from cmd_parse()*/
  option = strtok(NULL, delimiters);
  value = strtok(NULL, delimiters);

  if (option == NULL)
  {
    error = show_set();
    return error;
  }

  error = set_pref(option, value);

  action_do_resize(); /* just in case a display pref was set */

  return error;
}

static int all(const struct dirent *unused)
{ return 1; }
BOOL file_browser(const char *dir, char *fname, int name_len)
{
  int top = 0, selection = 0, count = 0;
  int c = 0, i, j;
  int dirchange = 1, update = 1;
  BOOL found = FALSE;
  struct stat stat_buf;
  struct dirent **eps;
  WINDOW *fb;

  memset(fname, 0, name_len);
  strncat(fname, dir, name_len);

  fb = newwin(SCROLL_BOX_H, SCROLL_BOX_W, SCROLL_BOX_Y, SCROLL_BOX_X);
  curs_set(0);

  do
  {
    switch (c)
    {
      case 'j':
      case KEY_DOWN:
        selection++;
        if (selection >= count)
          selection = count - 1;

        if (selection - top > SCROLL_BOX_H - 5)
          top++;

        update = 1;
        break;
      case 'k':
      case KEY_UP:
        selection--;
        if (selection < 0)
          selection = 0;

        if (selection < top)
          top--;

        update = 1;
        break;
      case NL:
      case CR:
      case KEY_ENTER:
      case 'g':
        strncat(fname, "/", name_len);
        strncat(fname, eps[selection]->d_name, name_len);
        dirchange = 1;
        update = 1;
        break;
      default:
        break;
    }

    if (dirchange)
    {
      dirchange = 0;
      selection = 0;
      top = 0;

      if (stat(fname, &stat_buf))
      {
        msg_box("Could not find %s", fname);
        break;
      }
      if (!S_ISDIR(stat_buf.st_mode))
      {
        found = TRUE;
        break;
      }

      count = scandir (fname, &eps, all, alphasort);
      if (count < 0)
      {
        msg_box("Could not scan directory %s", fname);
        break;
      }
    }

    if (update)
    {
      update = 0;
      werase(fb);
      box(fb, 0, 0);
      for (i=top,j=1; i<count; i++)
      {
        if (j >= (SCROLL_BOX_H - 3))
          break;
        else
        {
          if (i == selection)
            wattron(fb, A_STANDOUT);
          mvwprintw(fb, j, 1, "%s", eps[i]->d_name);
          wattroff(fb, A_STANDOUT);
        }
        j++;
      }
      mvwprintw(fb, SCROLL_BOX_H - 3, 1, "___________________________________________________________");
      mvwprintw(fb, SCROLL_BOX_H - 2, 1, " [j|DOWN] Down  [k|UP] Up  [ENTER|g] Select  [q|ESC] Cancel |");
      wrefresh(fb);
    }
    c = mgetch();
  } while(c != ESC && c != 'q' && c != 'Q');

  delwin(fb);
  curs_set(1);
  print_screen(display_info.page_start);

  return found;
}

action_code_t cmd_parse(char *cbuff)
{
  action_code_t error = E_SUCCESS;
  char *tok = 0, *endptr = 0;
  const char delimiters[] = " =";
  long long num = 0;
  char fname[MAX_FILE_NAME], *ftemp;
  off_t caddrsave, paddrsave;
  struct stat stat_buf;

  tok = strtok(cbuff, delimiters);
  if (tok != NULL)
  {
    int relative = 0; // -1 or +1 means that the jump is relative to the cursor
    if (tok[0] == '+' || tok[0] == '-') {
      relative = (tok[0] == '-') ? -1 : +1;
      tok++;
    }
    num = strtoll(tok, &endptr, 0);
    if ((endptr - tok) == strlen(tok))
    {
      if (relative != 0)
        num = display_info.cursor_addr + relative * num;
      if (address_invalid(num))
        msg_box("Invalid jump address: %d", num);
      else
        action_jump_to(num, CURSOR_REAL);
      return error;
    }

    if (strncmp(tok, "set", MAX_CMD_BUF) == 0)
    {
      error = do_set();
      return error;
    }
    if ((strncmp(tok, "next",     MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "tabn",     MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "bn",       MAX_CMD_BUF) == 0))
    {
      action_load_next_file();
      return error;
    }
    if ((strncmp(tok, "prev",     MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "previous", MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "bp",       MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "tabp",     MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "last",     MAX_CMD_BUF) == 0))
    {
      action_load_prev_file();
      return error;
    }
    if ((strncmp(tok, "e",    MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "tabe", MAX_CMD_BUF) == 0))
    {
      tok = strtok(NULL, delimiters);
      if (tok == NULL)
      {
        current_file = vf_add_fm_to_ring(file_ring);
        if (vf_init(current_file, NULL) == FALSE)
          fprintf(stderr, "Empty file failed?\n");
        update_display_info();
        print_screen(0);
      }
      else
      {
        char expanded_path[MAX_PATH_LEN+1];
        if (FALSE == vf_parse_path(expanded_path, tok))
        {
          msg_box("Could not parse path %s", tok);
          return error;
        }
        if (stat(expanded_path, &stat_buf))
        {
          msg_box("Could not find %s", expanded_path);
          return error;
        }
        if (S_ISDIR(stat_buf.st_mode))
        {
          if (file_browser(expanded_path, fname, MAX_FILE_NAME) != FALSE)
            ftemp = fname;
          else
            return error;
        }
        else
        {
          ftemp = expanded_path;
        }
        current_file = vf_add_fm_to_ring(file_ring);
        if (vf_init(current_file, ftemp) == FALSE)
          fprintf(stderr, "Could not open %s\n", ftemp);
        update_display_info();
        print_screen(0);
      }
      return error;
    }
    if (strncmp(tok, "e!", MAX_CMD_BUF) == 0)
    {
      snprintf(fname, MAX_FILE_NAME, "%s", vf_get_fname(current_file));
      caddrsave = display_info.cursor_addr;
      paddrsave = display_info.page_start;
      vf_term(current_file);
      vf_init(current_file, fname);
      update_display_info();
      place_cursor(caddrsave, CALIGN_NONE, CURSOR_REAL);
      if (address_invalid(paddrsave))
        print_screen(0);
      else
        print_screen(paddrsave);
      return error;
    }
    if ((strncmp(tok, "q", MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "bd", MAX_CMD_BUF) == 0))
    {
      action_quit(FALSE);
      return error;
    }
    if (strncmp(tok, "q!", MAX_CMD_BUF) == 0)
    {
      action_quit(TRUE);
      return error;
    }
    if (strncmp(tok, "qa", MAX_CMD_BUF) == 0)
    {
      action_quit_all(FALSE);
      return error;
    }
    if (strncmp(tok, "qa!", MAX_CMD_BUF) == 0)
    {
      action_quit_all(TRUE);
      return error;
    }
    if (strncmp(tok, "wa", MAX_CMD_BUF) == 0)
    {
      action_save_all();
      return error;
    }
    if ((strncmp(tok, "wqa", MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "waq", MAX_CMD_BUF) == 0))
    {
      action_save_all();
      action_quit_all(FALSE);
      return error;
    }
    if (strncmp(tok, "saveas", MAX_CMD_BUF) == 0)
    {
      tok = strtok(NULL, delimiters);
      if (tok == NULL)
        error = E_NO_ACTION;
      else
        action_save_as(tok, TRUE);
      return error;
    }
    if (strncmp(tok, "w", MAX_CMD_BUF) == 0)
    {
      tok = strtok(NULL, delimiters);
      if (tok == NULL)
        action_save();
      else
        action_save_as(tok, FALSE);
      return error;
    }
    if ((strncmp(tok, "wq", MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "qw", MAX_CMD_BUF) == 0))
    {
      tok = strtok(NULL, delimiters);
      if (tok == NULL)
        action_save();
      else
        action_save_as(tok, FALSE);

      action_quit(FALSE);
      return error;
    }

    if ((strncmp(tok, "help", MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "h", MAX_CMD_BUF) == 0))
    {
      scrollable_window_display(help_text);
      return error;
    }

    if ((strncmp(tok, "external", MAX_CMD_BUF) == 0) ||
        (strncmp(tok, "ex", MAX_CMD_BUF) == 0))
    {
      run_external();
      return error;
    }

  }

  flash();
  return error;
}

action_code_t do_search(int c, cursor_t cursor)
{
  cmd_hist_t *search_hist;
  char *cmd, prompt[3];
  search_direction_t direction = SEARCH_FORWARD;

  if (c == '?')
  {
    direction = SEARCH_BACKWARD;
    prompt[0] = c;
    prompt[1] = 0;
    werase(window_list[WINDOW_STATUS]);
    mvwprintw(window_list[WINDOW_STATUS], 0, 0, "%s", prompt);
    wrefresh(window_list[WINDOW_STATUS]);
    c = mgetch();
    while (c != '/' && c != '\\' && c != ESC && c != BVICTRL('c'))
    {
      flash();
      c = mgetch();
    }
    prompt[1] = c;
    prompt[2] = 0;
    werase(window_list[WINDOW_STATUS]);
  }
  else
  {
    prompt[0] = c;
    prompt[1] = 0;
  }

  if (c == '/')
    search_hist = ascii_search_hist;
  else
    search_hist = hex_search_hist;

  werase(window_list[WINDOW_STATUS]);
  cmd = creadline(prompt, window_list[WINDOW_STATUS], 0, 0, search_hist);

  if (cmd)
  {
    action_do_search(c, cmd, cursor, direction);
    free(cmd);
  }

  return E_SUCCESS;
}

action_code_t word_move(int c, cursor_t cursor)
{
  int i, size = 0;
  off_t cur_addr, next_addr;
  char buf[256], current_char;
  int require_whitespace = 0;

  cur_addr = display_info.cursor_addr;
  next_addr = cur_addr;

  size = vf_get_buf(current_file, buf, cur_addr, 256);

  current_char = buf[0];

  /* This seems like an ugly hack to me, but when a user presses 'w' they expect to go
     to the next word, but when they type 'cw' they expect to change the current word,
     and not the trailing whitespace or the first character of the next word */
  if (cursor == CURSOR_VIRTUAL)
  {
    if (c == 'w')
      c = 'e';
    if (c == 'W')
      c = 'E';
  }

  switch(c)
  {
    case 'W': /* next word, only whilespace are delimeters */
      require_whitespace = 1;
      /* no break */
    case 'w': /* next word, any non-alphanumeric is delimiter,
                 and whitespace delimits groups of delimeters */
      while (size > 1)
      {
        for (i=0; i<size; i++)
        {
          if (ALPHANUMERIC(current_char))
          {
            if (WHITESPACE(buf[i]))
            {
              current_char = buf[i];
              continue;
            }
            else if (!ALPHANUMERIC(buf[i]) && require_whitespace == 0)
            {
              place_cursor(next_addr + i, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
          else if (WHITESPACE(current_char))
          {
            if (!WHITESPACE(buf[i]))
            {
              place_cursor(next_addr + i, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
          else
          {
            if (WHITESPACE(buf[i]))
            {
              current_char = buf[i];
              continue;
            }
            else if (ALPHANUMERIC(buf[i]) && require_whitespace == 0)
            {
              place_cursor(next_addr + i, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
        }

        next_addr += size;
        size = vf_get_buf(current_file, buf, next_addr, 256);
      }

      break;
    case 'E': /* end of word, same delimit rules as W */
      require_whitespace = 1;
      /* no break */
    case 'e': /* end of word, same delimit rules as w */
      while (size > 1)
      {
        for (i=0; i<size; i++)
        {
          if (ALPHANUMERIC(current_char))
          {
            if (WHITESPACE(buf[i]))
            {
              if (i == 1 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i - 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
            else if (!ALPHANUMERIC(buf[i]) && require_whitespace == 0)
            {
              if (i == 1 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i - 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
          else if (WHITESPACE(current_char))
          {
            if (!WHITESPACE(buf[i]))
            {
              current_char = buf[i];
              continue;
            }
          }
          else
          {
            if (WHITESPACE(buf[i]))
            {
              if (i == 1 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i - 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
            else if (ALPHANUMERIC(buf[i]) && require_whitespace == 0)
            {
              if (i == 1 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i - 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
        }

        next_addr += size;
        size = vf_get_buf(current_file, buf, next_addr, 256);
      }
      break;
    default:
      break;
  }

  flash();
  return E_NO_ACTION;
}

action_code_t word_move_back(int c, cursor_t cursor)
{
  int i, size = 0;
  off_t cur_addr, next_addr = 0;
  char buf[256], current_char;
  int require_whitespace = 0;

  size = 256;
  cur_addr = display_info.cursor_addr - (size-1);
  if (address_invalid(cur_addr))
  {
    cur_addr = 0;
    size = display_info.cursor_addr;
  }

  size = vf_get_buf(current_file, buf, cur_addr, size);

  current_char = buf[size-1];

  next_addr = cur_addr;

  switch(c)
  {
    case 'B': /* end of word, same delimit rules as W */
      require_whitespace = 1;
      /* no break */
    case 'b': /* end of word, same delimit rules as w */
      while (size > 1)
      {
        for (i=size-1; i>=0; i--)
        {
          if (ALPHANUMERIC(current_char))
          {
            if (WHITESPACE(buf[i]))
            {
              if (i == size-2 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i + 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
            else if (!ALPHANUMERIC(buf[i]) && require_whitespace == 0)
            {
              if (i == size-2 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i + 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
          else if (WHITESPACE(current_char))
          {
            if (!WHITESPACE(buf[i]))
            {
              current_char = buf[i];
              continue;
            }
          }
          else
          {
            if (WHITESPACE(buf[i]))
            {
              if (i == size-2 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i + 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
            else if (ALPHANUMERIC(buf[i]) && require_whitespace == 0)
            {
              if (i == size-2 && next_addr == cur_addr)
              {
                /* if we were on the last character in a word we need
                   to move on to the next word */
                current_char = buf[i];
                continue;
              }
              place_cursor(next_addr + i + 1, CALIGN_NONE, cursor);
              return E_SUCCESS;
            }
          }
        }

        next_addr -= size;
        if (address_invalid(next_addr))
        {
          size = size + next_addr;
          next_addr = 0;
        }

        size = vf_get_buf(current_file, buf, next_addr, size);
      }
      break;
    default:
      break;
  }

  flash();
  return E_NO_ACTION;
}

action_code_t do_cmd_line(cursor_t cursor)
{
  char *cmd;

  werase(window_list[WINDOW_STATUS]);
  cmd = creadline(":", window_list[WINDOW_STATUS], 0, 0, cmd_hist);

  if (cmd)
  {
    cmd_parse(cmd);
    free(cmd);
  }

  return E_SUCCESS;
}

off_t get_next_motion_addr(void)
{
  int c, int_c, mark;
  static int multiplier = 0;
  static off_t jump_addr = -1;

  display_info.virtual_cursor_addr = -1;

  c = mgetch();
  while (c != ESC && c != BVICTRL('c'))
  {
    if (c >= '0' && c <= '9')
    {
      int_c = c - '0';

      if (multiplier == 0 && int_c == 0)
      {
        action_cursor_move_line_start(CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      }

      multiplier *= 10;
      multiplier += int_c;

      if (jump_addr == -1)
        jump_addr = 0;
      else
        jump_addr *= 10;
      jump_addr += int_c;
    }

    switch (c)
    {
      case '`':
        mark = mgetch();
        jump_addr = action_get_mark(mark);
        action_jump_to(jump_addr, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'g':
        c = mgetch();
        switch (c)
        {
          case 'g': /* gg */
            action_cursor_move_file_start(CURSOR_VIRTUAL);
            return display_info.virtual_cursor_addr;
          /* ... 'g' + something else */
          default:
            break;
        }
      case 'G':
        if (jump_addr == -1)
          action_cursor_move_file_end(CURSOR_VIRTUAL);
        else
          action_jump_to(jump_addr, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'j':
      case KEY_DOWN:
        action_cursor_move_down(multiplier, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'k':
      case KEY_UP:
        action_cursor_move_up(multiplier, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'h':
      case KEY_LEFT:
        action_cursor_move_left(multiplier, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'l':
      case KEY_RIGHT:
        action_cursor_move_right(multiplier, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case '$':
      case KEY_END:
        action_cursor_move_line_end(CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case KEY_HOME:
        action_cursor_move_line_start(CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case BVICTRL('d'):
        action_cursor_move_half_page(CURSOR_VIRTUAL, 1);
        return display_info.virtual_cursor_addr;
      case BVICTRL('u'):
        action_cursor_move_half_page(CURSOR_VIRTUAL, -1);
        return display_info.virtual_cursor_addr;
      case BVICTRL('f'):
      case KEY_NPAGE:
        action_cursor_move_half_page(CURSOR_VIRTUAL, 2);
        return display_info.virtual_cursor_addr;
      case BVICTRL('b'):
      case KEY_PPAGE:
        action_cursor_move_half_page(CURSOR_VIRTUAL, -2);
        return display_info.virtual_cursor_addr;
      case 'n':
        action_move_cursor_next_search(CURSOR_VIRTUAL, TRUE);
        return display_info.virtual_cursor_addr;
      case 'N':
        action_move_cursor_prev_search(CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case ':':
        do_cmd_line(CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'w':
      case 'W':
      case 'e':
      case 'E':
        word_move(c, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case 'b':
      case 'B':
        word_move_back(c, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case '?':
      case '/':
      case '\\':
        do_search(c, CURSOR_VIRTUAL);
        return display_info.virtual_cursor_addr;
      case BVICTRL('c'):
      case ESC:
        return display_info.cursor_addr;
      default:
        break;
    }

    if (c < '0' || c > '9')
    {
      jump_addr = -1;
      multiplier = 0;
    }

    flash();
    c = mgetch();
  }
  return display_info.virtual_cursor_addr;
}


int is_hex(int c)
{
  c = toupper(c);

  if (c < '0')
    return 0;
  if (c > '9' && c < 'A')
    return 0;
  if (c > 'F')
    return 0;

  return 1;
}
void do_insert(int count, int c)
{
  char *screen_buf, *ins_buf, *tmp_ins_buf;
  char tmp[9], tmp2[MAX_GRP], tmpc;
  int c2 = 0, i;
  int hy, hx, ay, ax;
  int ins_buf_size;
  int chars_per_byte, char_count = 0, tmp_char_count = 0, low_tmp_char_count = 0;
  int offset = 0, len1, ins_buf_offset, len2, len3;
  off_t ins_addr, page_start;

  screen_buf = (char *)malloc(2 * PAGE_SIZE); /* fix this later, but make it big for now */
  ins_buf_size = user_prefs[GROUPING].value;
  ins_buf = (char *)malloc(ins_buf_size);

  /* later mod this depending on c = a/i/A/I */
  switch (c)
  {
    case 'A': /* no break */
    case 'a': /* no break */
      if (display_info.file_size == 0)
        ins_addr = display_info.cursor_addr;
      else
        ins_addr = display_info.cursor_addr + user_prefs[GROUPING].value;
      break;
    case INS:
    case 'I': /* no break */
    case 'i': /* no break */
    default:
      ins_addr = display_info.cursor_addr;
      break;
#if 0
    case 'c':
    case 'C':
    case 's':
    case 'S':
#endif
  }

  page_start = display_info.page_start;

  while (c2 != ESC && c2 != BVICTRL('c'))
  {
    if ((offset + char_count + 1) > PAGE_SIZE)
      page_start += BYTES_PER_LINE;
    offset = ins_addr - page_start;
    if (offset < 0)
    {
      len1 = 0;
      ins_buf_offset = page_start - ins_addr;
      len2 = char_count - ins_buf_offset;
    }
    else
    {
      len1 = offset;
      len2 = char_count;
      ins_buf_offset = 0;
    }
    if ((len1 + len2) > PAGE_SIZE)
      len3 = 0;
    else
    {
      len3 = (PAGE_END - page_start) - len1 + user_prefs[GROUPING].value;
      if ((len1 + len2 + len3) > PAGE_SIZE)
        len3 = PAGE_SIZE - (len1 + len2);
    }

    if (len1 != 0)
      vf_get_buf(current_file, screen_buf, page_start, len1);
    if (len2 != 0)
      memcpy(screen_buf + len1, ins_buf + ins_buf_offset, len2);
    if (len3 > 0)
      len3 = vf_get_buf(current_file, screen_buf + len1 + len2 + user_prefs[GROUPING].value, ins_addr, len3);
    else
      len3 = 0;

    print_screen_buf(page_start, screen_buf, len1+len2+user_prefs[GROUPING].value+len3, NULL);

    if (display_info.cursor_window == WINDOW_HEX)
    {
      hy = get_y_from_page_offset(len1+len2);
      hx = get_x_from_page_offset(len1+len2);
      display_info.cursor_window = WINDOW_ASCII;
      ay = get_y_from_page_offset(len1+len2);
      ax = get_x_from_page_offset(len1+len2);
      display_info.cursor_window = WINDOW_HEX;
      chars_per_byte = 2;
    }
    else
    {
      display_info.cursor_window = WINDOW_HEX;
      hy = get_y_from_page_offset(len1+len2);
      hx = get_x_from_page_offset(len1+len2);
      display_info.cursor_window = WINDOW_ASCII;
      ay = get_y_from_page_offset(len1+len2);
      ax = get_x_from_page_offset(len1+len2);
      chars_per_byte = 1;
    }

    for (i=0; i<user_prefs[GROUPING].value; i++) /* print from temp buf here to clear or print partial insert */
    {
      if (i>=tmp_char_count)
      {
        if (low_tmp_char_count && i == tmp_char_count)
          mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), tmp[0]);
        else
          mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), ' ');
        mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i)+1, ' ');
        mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, ' ');
      }
      else
      {
        mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), HEX(tmp2[i]>>4&0xF));
        mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i)+1, HEX(tmp2[i]>>0&0xF));
        if (isprint(tmp2[i]))
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, tmp2[i]);
        else
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, '.');
      }
    }

    update_panels();
    doupdate();
    if (display_info.cursor_window == WINDOW_HEX)
      wmove(window_list[WINDOW_HEX], hy, hx);
    else
      wmove(window_list[WINDOW_ASCII], ay, ax);
    c2 = mgetch();
    switch (c2)
    {
      case BACKSPACE:
      case KEY_BACKSPACE:
        low_tmp_char_count--;
        if (low_tmp_char_count < 0)
        {
          low_tmp_char_count = chars_per_byte - 1;
          tmp_char_count--;
          if (tmp_char_count < 0)
          {
            tmp_char_count = user_prefs[GROUPING].value - 1;
            char_count--;
            if (char_count < 0)
            {
              low_tmp_char_count = 0;
              tmp_char_count = 0;
              char_count = 0;
              flash();
            }
            else
            {
              memcpy(tmp2, ins_buf + char_count, user_prefs[GROUPING].value);
              tmp[1] = HEX((tmp2[tmp_char_count] & 0xF0) >> 4);
              tmp[0] = HEX((tmp2[tmp_char_count] & 0x0F));
            }
          }
          else
          {
            tmp[1] = HEX((tmp2[tmp_char_count] & 0xF0) >> 4);
            tmp[0] = HEX((tmp2[tmp_char_count] & 0x0F));
          }
        }
        break;
      case KEY_RESIZE:
        break;
      case BVICTRL('c'):
      case ESC:
        break;
      default:
        if (display_info.cursor_window == WINDOW_HEX)
        {
          if (is_hex(c2) == 0)
          {
            flash();
            continue;
          }
          tmp[low_tmp_char_count] = (char)c2;
          low_tmp_char_count++;
          if ((low_tmp_char_count % chars_per_byte) == 0)
          {
            low_tmp_char_count = 0;
            tmp[chars_per_byte] = 0;
            tmpc = (char)strtol(tmp, NULL, 16);
            tmp2[tmp_char_count % user_prefs[GROUPING].value] = tmpc;
            tmp_char_count++;

            if ((tmp_char_count % user_prefs[GROUPING].value) == 0)
            {
              while (char_count + tmp_char_count >= ins_buf_size)
              {
                tmp_ins_buf = calloc(1, ins_buf_size * 2);
                memcpy(tmp_ins_buf, ins_buf, ins_buf_size);
                ins_buf_size *= 2;
                free(ins_buf);
                ins_buf = tmp_ins_buf;
              }

              memcpy(ins_buf + char_count, tmp2, tmp_char_count);
              char_count += tmp_char_count;
              tmp_char_count = 0;
            }
          }
        }
        else /* cursor in ascii window */
        {
          tmp2[tmp_char_count % user_prefs[GROUPING].value] = (char)c2;
          tmp_char_count++;

          if ((tmp_char_count % user_prefs[GROUPING].value) == 0)
          {
            while (char_count + tmp_char_count >= ins_buf_size)
            {
              tmp_ins_buf = calloc(1, ins_buf_size * 2);
              memcpy(tmp_ins_buf, ins_buf, ins_buf_size);
              ins_buf_size *= 2;
              free(ins_buf);
              ins_buf = tmp_ins_buf;
            }

            memcpy(ins_buf + char_count, tmp2, tmp_char_count);
            char_count += tmp_char_count;
            tmp_char_count = 0;
          }
        }
        break;
    }
  }

  if (char_count)
  {
    if (count == 0)
      count = 1;

    switch (c)
    {
      case 'A': /* no break */
      case 'a': /* no break */
        if (display_info.file_size == 0)
        {
          for (i=0; i<count; i++)
            action_insert_before(count,ins_buf,char_count);
        }
        else
        {
          for (i=0; i<count; i++)
            action_insert_after(count,ins_buf,char_count);
        }
        break;
      case 'I': /* no break */
      case 'i': /* no break */
      default:
        for (i=0; i<count; i++)
          action_insert_before(count,ins_buf,char_count);
        break;
    }
  }

  free(ins_buf);
  free(screen_buf);

  place_cursor(ins_addr+char_count, CALIGN_NONE, CURSOR_REAL);
  print_screen(page_start);
}

void do_yank(int count, int c)
{
  off_t end_addr = INVALID_ADDR;

  if (action_visual_select_check())
    end_addr = INVALID_ADDR;
  else if (c == 'Y')
    end_addr = display_info.cursor_addr;
  else
    end_addr = get_next_motion_addr();

  action_yank(count, end_addr, TRUE);
}

void do_replace(int count)
{
  int hx, hy, ax, ay, c, i, char_count = 0, chars_per_byte = 0;
  char tmp[3], tmpc, *tmp_fill;
  char replace_buf[256];
  off_t tmp_addr = 0;

  if (count == 0)
    count = 1;

  if (display_info.cursor_window == WINDOW_HEX)
  {
    hy = get_y_from_addr(display_info.cursor_addr);
    hx = get_x_from_addr(display_info.cursor_addr);
    display_info.cursor_window = WINDOW_ASCII;
    ay = get_y_from_addr(display_info.cursor_addr);
    ax = get_x_from_addr(display_info.cursor_addr);
    display_info.cursor_window = WINDOW_HEX;
    chars_per_byte = 2;
  }
  else
  {
    display_info.cursor_window = WINDOW_HEX;
    hy = get_y_from_addr(display_info.cursor_addr);
    hx = get_x_from_addr(display_info.cursor_addr);
    display_info.cursor_window = WINDOW_ASCII;
    ay = get_y_from_addr(display_info.cursor_addr);
    ax = get_x_from_addr(display_info.cursor_addr);
    chars_per_byte = 1;
  }

  for (i=0; i<user_prefs[GROUPING].value; i++)
  {
    mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), ' ');
    mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i)+1, ' ');
    mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, ' ');
  }
  if (display_info.cursor_window == WINDOW_HEX)
    wmove(window_list[WINDOW_HEX], hy, hx);
  else
    wmove(window_list[WINDOW_ASCII], ay, ax);

  while(char_count < user_prefs[GROUPING].value * chars_per_byte)
  {
    update_panels();
    doupdate();
    c = mgetch();
    if (c == ESC || c == BVICTRL('c'))
      break;

    if (display_info.cursor_window == WINDOW_HEX)
    {
      if (is_hex(c) == 0)
      {
        flash();
        continue;
      }
      tmp[char_count % chars_per_byte] = (char)c;
      mvwaddch(window_list[WINDOW_HEX], hy, hx+char_count, tmp[char_count % chars_per_byte]);
      char_count++;
      if ((char_count % chars_per_byte) == 0)
      {
        tmp[2] = 0;
        tmpc = (char)strtol(tmp, NULL, 16);
        if (isprint(tmpc))
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+char_count/2-1, tmpc);
        else
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+char_count/2-1, '.');

        replace_buf[char_count / chars_per_byte - 1] = tmpc;

      }
    }
    else
    {
        mvwaddch(window_list[WINDOW_HEX], hy, hx+char_count/2-1, (c&0xF)<<4);
        mvwaddch(window_list[WINDOW_HEX], hy, hx+char_count/2,   (c&0xF));
        if (isprint(c))
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+char_count/2-1, c);
        else
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+char_count/2-1, '.');

        replace_buf[char_count] = (char)c;
        char_count++;
    }
  }
  if (char_count >= user_prefs[GROUPING].value * chars_per_byte)
  {
    if (is_visual_on())
    {
      tmp_addr = visual_addr();
      count = visual_span();
      count /= user_prefs[GROUPING].value;
      action_visual_select_off();
    }
    else
    {
      tmp_addr = display_info.cursor_addr;
    }

    if (address_invalid(tmp_addr) == 0)
    {
      count *= user_prefs[GROUPING].value;
      while (address_invalid(tmp_addr + count - 1) && count > 0)
        count -= user_prefs[GROUPING].value;

      if (count > 0)
      {
        tmp_fill = (char *)malloc(count);
        for (i=0; i<count/user_prefs[GROUPING].value; i++)
          memcpy(tmp_fill+i*(user_prefs[GROUPING].value), replace_buf, user_prefs[GROUPING].value);
        vf_replace(current_file, tmp_fill, tmp_addr, count);
        free(tmp_fill);
      }
    }
  }

  print_screen(display_info.page_start);

}

void do_change(int multiplier, int c)
{
  off_t end_addr = 0;
  /* later we may want to create a real change operation on the file backend,
     but for now we have to do a delete/insert, so just do it as two operations,
     which will make this implimentation easy */

  if (is_visual_on())
  {
    action_delete(1, INVALID_ADDR);
  }
  else
  {
    switch(c)
    {
      case 'c':
      case 'C':
        end_addr = get_next_motion_addr();
        break;
      case 's':
      case 'S':
        end_addr = display_info.cursor_addr;
        break;
    }

    action_delete(multiplier, end_addr);
  }

  action_visual_select_off();
  do_insert(1, 'i');
}

void do_overwrite(int count)
{
  char *screen_buf, *rep_buf, *tmp_rep_buf;
  char tmp[9], tmp2[MAX_GRP], tmpc;
  int c2 = 0, i;
  int hy, hx, ay, ax;
  int rep_buf_size;
  int chars_per_byte, char_count = 0, tmp_char_count = 0, low_tmp_char_count = 0;
  int offset = 0, len1, rep_buf_offset, len2, len3;
  off_t ins_addr, page_start;

  if (is_visual_on())
  {
    /* What should we do if user tries to OVERWRITE in visual select mode? */
    flash();
    return;
  }

  screen_buf = (char *)malloc(2 * PAGE_SIZE); /* fix this later, but make it big for now */
  rep_buf_size = user_prefs[GROUPING].value;
  rep_buf = (char *)malloc(rep_buf_size);

  ins_addr = display_info.cursor_addr;

  page_start = display_info.page_start;

  while (c2 != ESC && c2 != BVICTRL('c'))
  {
    if ((offset + char_count) > PAGE_SIZE)
      page_start += BYTES_PER_LINE;
    offset = ins_addr - page_start;
    if (offset < 0)
    {
      len1 = 0;
      rep_buf_offset = page_start - ins_addr;
      len2 = char_count - rep_buf_offset;
    }
    else
    {
      len1 = offset;
      len2 = char_count;
      rep_buf_offset = 0;
    }
    if ((len1 + len2) > PAGE_SIZE)
      len3 = 0;
    else
      len3 = PAGE_SIZE - (len1 + len2);
    if ((ins_addr + len2 + len3) > current_file->fm.size)
      len3 = current_file->fm.size - (ins_addr + len2);

    if (len1 != 0)
      vf_get_buf(current_file, screen_buf, page_start, len1);
    if (len2 != 0)
      memcpy(screen_buf + len1, rep_buf + rep_buf_offset, len2);
    if (len3 > 0)
      vf_get_buf(current_file, screen_buf + len1 + len2, ins_addr + len2, len3);
    else
      len3 = 0;

    print_screen_buf(page_start, screen_buf, len1+len2+len3, NULL);

    if (display_info.cursor_window == WINDOW_HEX)
    {
      hy = get_y_from_page_offset(len1+len2);
      hx = get_x_from_page_offset(len1+len2);
      display_info.cursor_window = WINDOW_ASCII;
      ay = get_y_from_page_offset(len1+len2);
      ax = get_x_from_page_offset(len1+len2);
      display_info.cursor_window = WINDOW_HEX;
      chars_per_byte = 2;
    }
    else
    {
      display_info.cursor_window = WINDOW_HEX;
      hy = get_y_from_page_offset(len1+len2);
      hx = get_x_from_page_offset(len1+len2);
      display_info.cursor_window = WINDOW_ASCII;
      ay = get_y_from_page_offset(len1+len2);
      ax = get_x_from_page_offset(len1+len2);
      chars_per_byte = 1;
    }

    if (tmp_char_count > 0 || low_tmp_char_count)
    {
      for (i=0; i<user_prefs[GROUPING].value; i++) /* print from temp buf here to clear or print partial insert */
      {
        if (i>=tmp_char_count)
        {
          if (low_tmp_char_count && i == tmp_char_count)
            mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), tmp[0]);
          else
            mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), ' ');
          mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i)+1, ' ');
          mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, ' ');
        }
        else
        {
          mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i), HEX(tmp2[i]>>4&0xF));
          mvwaddch(window_list[WINDOW_HEX], hy, hx+(2*i)+1, HEX(tmp2[i]>>0&0xF));
          if (isprint(tmp2[i]))
            mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, tmp2[i]);
          else
            mvwaddch(window_list[WINDOW_ASCII], ay, ax+i, '.');
        }
      }
    }

    update_panels();
    doupdate();
    if (display_info.cursor_window == WINDOW_HEX)
      wmove(window_list[WINDOW_HEX], hy, hx);
    else
      wmove(window_list[WINDOW_ASCII], ay, ax);
    c2 = mgetch();
    switch (c2)
    {
      case BACKSPACE:
      case KEY_BACKSPACE:
        low_tmp_char_count--;
        if (low_tmp_char_count < 0)
        {
          low_tmp_char_count = chars_per_byte - 1;
          tmp_char_count--;
          if (tmp_char_count < 0)
          {
            tmp_char_count = user_prefs[GROUPING].value - 1;
            char_count--;
            if (char_count < 0)
            {
              low_tmp_char_count = 0;
              tmp_char_count = 0;
              char_count = 0;
              flash();
            }
            else
            {
              memcpy(tmp2, rep_buf + char_count, user_prefs[GROUPING].value);
              tmp[1] = HEX((tmp2[tmp_char_count] & 0xF0) >> 4);
              tmp[0] = HEX((tmp2[tmp_char_count] & 0x0F));
            }
          }
          else
          {
            tmp[1] = HEX((tmp2[tmp_char_count] & 0xF0) >> 4);
            tmp[0] = HEX((tmp2[tmp_char_count] & 0x0F));
          }
        }
        break;
      case KEY_RESIZE:
        break;
      case BVICTRL('c'):
      case ESC:
        break;
      default:
        if ((ins_addr + char_count + tmp_char_count) >= current_file->fm.size)
        {
          flash();
          continue;
        }
        if (display_info.cursor_window == WINDOW_HEX)
        {
          if (is_hex(c2) == 0)
          {
            flash();
            continue;
          }
          tmp[low_tmp_char_count] = (char)c2;
          low_tmp_char_count++;
          if ((low_tmp_char_count % chars_per_byte) == 0)
          {
            low_tmp_char_count = 0;
            tmp[chars_per_byte] = 0;
            tmpc = (char)strtol(tmp, NULL, 16);
            tmp2[tmp_char_count % user_prefs[GROUPING].value] = tmpc;
            tmp_char_count++;

            if ((tmp_char_count % user_prefs[GROUPING].value) == 0)
            {
              while (char_count + tmp_char_count >= rep_buf_size)
              {
                tmp_rep_buf = calloc(1, rep_buf_size * 2);
                memcpy(tmp_rep_buf, rep_buf, rep_buf_size);
                rep_buf_size *= 2;
                free(rep_buf);
                rep_buf = tmp_rep_buf;
              }

              memcpy(rep_buf + char_count, tmp2, tmp_char_count);
              char_count += tmp_char_count;
              tmp_char_count = 0;
            }
          }
        }
        else /* cursor in ascii window */
        {
          tmp2[tmp_char_count % user_prefs[GROUPING].value] = (char)c2;
          tmp_char_count++;

          if ((tmp_char_count % user_prefs[GROUPING].value) == 0)
          {
            while (char_count + tmp_char_count >= rep_buf_size)
            {
              tmp_rep_buf = calloc(1, rep_buf_size * 2);
              memcpy(tmp_rep_buf, rep_buf, rep_buf_size);
              rep_buf_size *= 2;
              free(rep_buf);
              rep_buf = tmp_rep_buf;
            }

            memcpy(rep_buf + char_count, tmp2, tmp_char_count);
            char_count += tmp_char_count;
            tmp_char_count = 0;
          }
        }
        break;
    }
  }

  if (char_count)
  {
    if (count == 0)
      count = 1;

    action_replace(count,rep_buf,char_count);
  }

  free(rep_buf);
  free(screen_buf);

  place_cursor(ins_addr+char_count, CALIGN_NONE, CURSOR_REAL);
  print_screen(page_start);
}

void do_delete(int count, int c)
{
  off_t end_addr = INVALID_ADDR;

  if (action_visual_select_check())
    end_addr = INVALID_ADDR;
  else
    end_addr = get_next_motion_addr();

  action_delete(count, end_addr);
}

void handle_key(int c)
{
  int int_c, mark, tab;
  static int multiplier = 0;
  static int esc_count = 0;
  static off_t jump_addr = -1;

  if (c >= '0' && c <= '9')
  {
    int_c = c - '0';

    if (multiplier == 0 && int_c == 0)
      action_cursor_move_line_start(CURSOR_REAL);

    multiplier *= 10;
    multiplier += int_c;

    if (jump_addr == -1)
      jump_addr = 0;
    else
      jump_addr *= 10;
    jump_addr += int_c;

    if (esc_count)
    {
      if (current_file->private_data == NULL)
        current_file->private_data = malloc(sizeof(display_info_t));
      *(display_info_t *)current_file->private_data = display_info;

      tab = c - '0';
      current_file = vf_get_head_fm_from_ring(file_ring);
      tab--;
      while (tab > 0)
      {
        tab--;
        current_file = vf_get_next_fm_from_ring(file_ring);
      }
      esc_count = 0;
      multiplier = 0;
      jump_addr = -1;
      if (current_file->private_data == NULL)
        reset_display_info();
      else
        display_info = *(display_info_t *)current_file->private_data;
      print_screen(display_info.page_start);
    }
  }

  switch (c)
  {
    case '`':
      mark = mgetch();
      jump_addr = action_get_mark(mark);
      action_jump_to(jump_addr, CURSOR_REAL);
      break;
    case '<':
      action_blob_shift_left(multiplier);
      break;
    case '>':
      action_blob_shift_right(multiplier);
      break;
    case '@': /* macro playback */
    {
      int i, k = getch(); /* do not use mgetch here since we don't want this key if recording a macro */

      if (k == '@')
        k = last_macro_key;

      if (k < 'a' || k > 'z')
        break;

      last_macro_key = k;

      k -= 'a';

      if (macro_key != -1)
        /* Playing one macro inside another --
           Remove the @ from the key list and allow all the keys from one macro to go into the new one
           In the future we could use mungetch() which would increment an 'ignore' count, causing mgetch or mwgetch to ignore the keys pushed on to the stack by this macro. Then we could record the @<key> directly into this macro */
        macro_record[k].key_index--;

      for (i=macro_record[k].key_index-1; i>=0; i--)
        ungetch(macro_record[k].key[i]);

      break;
    }
    case 'q': /* macro record */
      if (macro_key == -1)
      {
        macro_key = getch(); /* don't save the macro-key specifying key */
        if (macro_key < 'a' || macro_key > 'z')
        {
          macro_key = -1;
          msg_box("Record a macro on keys 'a' through 'z'");
        }
        else
        {
          macro_key -= 'a';
          macro_record[macro_key].key_index = 0;
        }
      }
      else
      {
        macro_record[macro_key].key_index--; /* don't save the macro-closing 'q' key */
        macro_key = -1;
      }
      break;
    case 'm':
      mark = mgetch();
      action_set_mark(mark);
      break;
    case 'g':
      c = mgetch();
      if (c > '0' && c <= '9')
      {
        flash();
        break;
      }
      else if (c != 'g')
      {
        ungetch(c);
        jump_addr = get_next_motion_addr();
      }
      if (jump_addr == -1)
        action_cursor_move_file_start(CURSOR_REAL);
      else
        action_jump_to(jump_addr, CURSOR_REAL);
      multiplier = 0;
      jump_addr = -1;
      break;
    case 'G':
      if (jump_addr == -1)
        action_cursor_move_file_end(CURSOR_REAL);
      else
        action_jump_to(jump_addr, CURSOR_REAL);
      jump_addr = -1;
      break;
    case BVICTRL('n'):
    case 'j':
    case KEY_DOWN:
      action_cursor_move_down(multiplier, CURSOR_REAL);
      break;
    case BVICTRL('p'):
    case 'k':
    case KEY_UP:
      action_cursor_move_up(multiplier, CURSOR_REAL);
      break;
    case 'h':
    case KEY_LEFT:
    case BACKSPACE:
    case KEY_BACKSPACE:
      action_cursor_move_left(multiplier, CURSOR_REAL);
      break;
    case 'l':
    case ' ':
    case KEY_RIGHT:
      action_cursor_move_right(multiplier, CURSOR_REAL);
      break;
    case '$':
    case KEY_END:
      action_cursor_move_line_end(CURSOR_REAL);
      break;
    case '^':
    case KEY_HOME:
      action_cursor_move_line_start(CURSOR_REAL);
      break;
    case TAB:
      action_cursor_toggle_hex_ascii();
      break;
    case BVICTRL('d'):
      action_cursor_move_half_page(CURSOR_REAL, 1);
      break;
    case BVICTRL('u'):
      action_cursor_move_half_page(CURSOR_REAL, -1);
      break;
    case BVICTRL('f'):
    case KEY_NPAGE:
      action_cursor_move_half_page(CURSOR_REAL, 2);
      break;
    case BVICTRL('b'):
    case KEY_PPAGE:
      action_cursor_move_half_page(CURSOR_REAL, -2);
      break;
    case 'X':
      action_cursor_move_left(multiplier, CURSOR_REAL);
      /* no break */
    case 'x':
      action_yank(multiplier, INVALID_ADDR, FALSE);
      action_delete(multiplier, INVALID_ADDR);
      action_visual_select_off();
      break;
    case 'v':
      action_visual_select_toggle();
      break;
    case 'c':
    case 'C':
    case 's':
    case 'S':
      do_change(multiplier, c);
      break;
    case 'w':
    case 'W':
    case 'e':
    case 'E':
      word_move(c, CURSOR_REAL);
      break;
    case 'b':
    case 'B':
      word_move_back(c, CURSOR_REAL);
      break;
    case INS:
    case 'i':
    case 'I':
    case 'a':
    case 'A':
      do_insert(multiplier, c);
      break;
    case 'R':
      do_overwrite(multiplier);
      break;
    case 'r':
      do_replace(multiplier);
      action_visual_select_off();
      break;
    case 'y': /* no separate behavior from Y, right now */
    case 'Y':
      do_yank(multiplier, c);
      action_visual_select_off();
      break;
    case 'd':
    case 'D':
      do_delete(multiplier, c);
      action_visual_select_off();
      break;
    case 'p':
      action_paste_after(multiplier);
      break;
    case 'P':
      action_paste_before(multiplier);
      break;
    case '"':
      mark = mgetch();
      action_set_yank_register(mark);
    case 'u':
      action_undo(multiplier);
      break;
    case BVICTRL('r'):
    case 'U':
      action_redo(multiplier);
      break;
    case 'n':
      action_move_cursor_next_search(CURSOR_REAL, TRUE);
      break;
    case 'N':
      action_move_cursor_prev_search(CURSOR_REAL);
      break;
    case ':':
      do_cmd_line(CURSOR_REAL);
      break;
    case '?':
    case '/':
    case '\\':
      do_search(c, CURSOR_REAL);
      break;
    case '~':
      action_load_next_file();
      break;
    case BVICTRL('c'):
    case ESC:
      if (action_visual_select_check())
      {
        action_visual_select_off();
        esc_count = 0;
      }
      else if (esc_count)
      {
        action_clear_search_highlight();
        esc_count = 0;
      }
      else
      {
        esc_count ++;
      }
      multiplier = 0;
      jump_addr = -1;
      break;
    case KEY_RESIZE:
    case BVICTRL('l'):
      action_do_resize();
      break;
    default:
      break;
  }

  if (c != ESC)
    esc_count = 0;

  if (c < '0' || c > '9')
  {
    jump_addr = -1;
    multiplier = 0;
  }
}


