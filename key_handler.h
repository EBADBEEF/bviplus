/*************************************************************
 *
 * File:        key_handler.h
 * Author:      David Kelley
 * Description: Defines and function prototypes related to
 *              user input
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

#ifndef __KEY_HANDLER_H__
#define __KEY_HANDLER_H__

#define CR      '\r'
#define NL      '\n'
#define ESC     27
#define INS     331
#define TAB     9
#define DEL     330
#define BACKSPACE 127
#define BVICTRL(n)    (n&0x1f)

typedef struct macro_record_s
{
  int key[256];
  int key_index;
} macro_record_t;

extern macro_record_t  macro_record[];
extern int             macro_key;

int mwgetch(WINDOW *w);
int mgetch(void);
void handle_key(int c);
int is_hex(int c);

#endif /* __KEY_HANDLER_H__ */
