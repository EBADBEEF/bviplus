/******************************************************
 *
 * Project: Virtual File
 * Author:  David Kelley
 * Date:    29 Sept, 2008
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
 *****************************************************/

/****************
    INCLUDES
 ***************/
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "virt_file.h"
#include "vf_backend.h"

/****************
  MACROS/DEFINES
 ***************/
#define BUF_TOGGLE(x) (x ^ 1)
#define MAX_SAVE_SHIFT (4 * 1024 * 1024) /* two megs (change save so that we can grow this dynamically if we need more? */

/****************
    FUNCTIONS
 ***************/

BOOL vf_parse_path(char *out, const char *in)
{
  if (in[0]=='~') {
    const char *home = getenv("HOME");
    if (NULL == home)
      return FALSE;
    snprintf(out, MAX_PATH_LEN - 1, "%s%s", home, &in[1]);
  } else {
    snprintf(out, MAX_PATH_LEN - 1, "%s", in);
  }
  out[MAX_PATH_LEN] = 0;
  return TRUE;
}

vf_ring_t *vf_create_fm_ring(void)
{
  vf_ring_t *tmp;
  tmp = (vf_ring_t *)calloc(1, sizeof(vf_ring_t));
  return tmp;
}
BOOL vf_destroy_fm_ring(vf_ring_t *r)
{
  vf_ring_t *tmp, *tmp2;

  if (r == NULL)
    return FALSE;

  tmp = r->next;

  while(tmp != NULL)
  {
    vf_term(&tmp->fm);

    if (tmp->next != NULL)
      tmp->next->last = tmp->last;

    if (tmp->last != NULL)
      tmp->last->next = tmp->next;

    if (tmp == tmp->next)
      tmp2 = NULL;
    else
      tmp2 = tmp->next;

    free(tmp);

    tmp = tmp2;
  }

  free(r);

  return TRUE;
}
file_manager_t *vf_add_fm_to_ring(vf_ring_t *r)
{
  vf_ring_t *tmp, *new;

  new = (vf_ring_t *)calloc(1, sizeof(vf_ring_t));

  if (r->next == NULL)
  {
    r->next = new;
    r->head = new;
    new->next = new;
    new->last = new;
  }
  else
  {
    tmp = r->next;
    new->next = tmp;
    new->last = tmp->last;
    tmp->last->next = new;
    tmp->last = new;
  }

  return &new->fm;
}
BOOL vf_remove_fm_from_ring(vf_ring_t *r, file_manager_t *fm)
{
  vf_ring_t *tmp;

  if (r == NULL)
    return FALSE;

  if (r->next == NULL)
    return FALSE;

  tmp = r->next;

  if (fm == NULL)
  {
    if (tmp->next == tmp)
    {
      tmp->next = NULL;
      tmp->last = NULL;
    }
    else
    {
      tmp->last->next = tmp->next;
      tmp->next->last = tmp->last;
    }
    r->next = tmp->next;
    if (r->head == tmp)
      r->head = tmp->next;
    vf_term(&tmp->fm);
    free(tmp);
    return TRUE;
  }
  else
  {
    while (tmp != NULL)
    {
      if (&tmp->fm == fm)
      {
        if (tmp->next == tmp)
        {
          tmp->next = NULL;
          tmp->last = NULL;
        }
        else
        {
          tmp->last->next = tmp->next;
          tmp->next->last = tmp->last;
        }
        if (r->next == tmp)
          r->next = tmp->next;
        if (r->head == tmp)
          r->head = tmp->next;
        vf_term(fm);
        free(tmp);
        return TRUE;
      }
      tmp = tmp->next;
      if (tmp == r->next)
        return FALSE;
    }
  }
  return FALSE;
}
file_manager_t *vf_get_next_fm_from_ring(vf_ring_t *r)
{
  if (r == NULL)
    return NULL;

  if (r->next == NULL)
    return NULL;

  r->next = r->next->next;

  return &r->next->fm;
}
file_manager_t *vf_get_last_fm_from_ring(vf_ring_t *r)
{
  if (r == NULL)
    return NULL;

  if (r->next == NULL)
    return NULL;

  r->next = r->next->last;

  return &r->next->fm;
}
file_manager_t *vf_get_current_fm_from_ring(vf_ring_t *r)
{
  if (r == NULL)
    return NULL;

  if (r->next == NULL)
    return NULL;

  return &r->next->fm;
}
BOOL vf_set_current_fm_from_ring(vf_ring_t *r, file_manager_t *fm)
{
  vf_ring_t *tmp;

  if (r == NULL)
    return FALSE;

  tmp = r->next;

  while (tmp != NULL)
  {
    if (&tmp->fm == fm)
    {
      r->next = tmp;
      return TRUE;
    }

    tmp = tmp->next;
    if (tmp == r->next)
      return FALSE;
  }

  return FALSE;
}
file_manager_t *vf_get_head_fm_from_ring(vf_ring_t *r)
{
  if (r == NULL)
    return NULL;

  if (r->head == NULL)
    return NULL;

  r->next = r->head;

  return &r->head->fm;
}


/*---------------------------

  ---------------------------*/
BOOL vf_init(file_manager_t * f, const char *file_name)
{
  struct stat stat_buf;

  if (f == NULL)
    return FALSE;

  f->private_data = NULL;
  /* If given a file name fill in some info.
     If not the user must open the stream and set the size.
     Filename is still required for saving at this point.
    (which reminds me, need to impliment saving to a new file name) */
  if (NULL != file_name)
  {
    if (FALSE == vf_parse_path(f->fname, file_name))
      return FALSE;

    f->fm.fp = fopen(f->fname, "a");
    if(NULL != f->fm.fp)
      fclose(f->fm.fp);

    if (stat(f->fname, &stat_buf))
    {
      vf_term(f);
      return FALSE;
    }
    else
    {
      if (S_ISDIR(stat_buf.st_mode))
        return FALSE;
      f->fm.size = stat_buf.st_size;
    }

    f->fm.fp = fopen(f->fname, "r");
    if(NULL == f->fm.fp)
    {
      vf_term(f);
      return FALSE;
    }
  }
  else
  {
    f->fname[0] = 0;
    f->fm.fp = NULL;
    f->fm.size = 0;
  }

  f->fm.parent = NULL;
  f->fm.first_child = NULL;
  f->fm.next = NULL;
  f->fm.prev = NULL;
  f->fm.buf = NULL;
  f->fm.start = 0;
  f->fm.buf_type = TYPE_FILE;
  f->fm.active = TRUE;
  f->ul.last = NULL;
  f->ul.vb_list = NULL;
  f->ul.applied = FALSE;
  f->ul.saved = FALSE;

  return TRUE;
}


/*---------------------------

  ---------------------------*/
void vf_term(file_manager_t * f)
{
  if (NULL == f)
    return;

  cleanup(f);
  if (NULL != f->fm.fp)
  {
    fclose(f->fm.fp);
    f->fm.fp = NULL;
  }
}


void vf_stat(file_manager_t * f, vf_stat_t * s)
{
  if (s == NULL)
    return;

  s->file_size = 0;

  if (f == NULL)
    return;

  s->file_size = f->fm.size;
}

/*---------------------------
  ---------------------------*/
char *vf_get_fname(file_manager_t * f)
{
  if (f == NULL)
    return NULL;

  return f->fname;
}
/*---------------------------
  ---------------------------*/
char *vf_get_fname_file(file_manager_t * f)
{
  char *tmp1, *tmp2;

  if (f == NULL)
    return NULL;

  tmp1 = f->fname;

  while((tmp2 = strchr(tmp1, '/')) != NULL)
    tmp1 = tmp2+1;

  return tmp1;
}

/*---------------------------
Call this before saving when you have made edits to an initialized
file_manager_t that is not linked to a file.
  ---------------------------*/
BOOL vf_create_file(file_manager_t * f, const char *file_name)
{
  if (f == NULL)
    return FALSE;

  if (NULL == file_name)
    return FALSE;

  if (FALSE == vf_parse_path(f->fname, file_name))
    return FALSE;

  f->fm.fp = fopen(f->fname, "r");
  if(NULL != f->fm.fp) /* file already exists */
    return FALSE;

  f->fm.fp = fopen(f->fname, "w+");
  if(NULL == f->fm.fp) /* couldn't create file */
    return FALSE;

  fclose(f->fm.fp);
  f->fm.fp = fopen(f->fname, "r");
  if(NULL == f->fm.fp) /* couldn't open the file just created */
    return FALSE;

  return TRUE;
}

/*---------------------------
Call this before saving when you want to save an existing file to a
new file.
  ---------------------------*/
BOOL vf_copy_file(file_manager_t * f, const char *file_name, BOOL keep_newname)
{
  FILE *out;
  char c;
  char expanded_path[MAX_PATH_LEN+1];

  if (f == NULL)
    return FALSE;

  if (NULL == file_name)
    return FALSE;

  if (f->fm.fp == NULL)
    return FALSE;

  if (FALSE == vf_parse_path(expanded_path, file_name))
    return FALSE;

  out = fopen(expanded_path, "w+");
  if (out == NULL)
    return FALSE;

  fseeko(f->fm.fp, 0, SEEK_SET);
  fread(&c, 1, 1, f->fm.fp);
  while (!feof(f->fm.fp))
  {
    fwrite(&c, 1, 1, out);
    fread(&c, 1, 1, f->fm.fp);
  }
  fclose(out);

  if (keep_newname) {
    fclose(f->fm.fp);
    strcpy(f->fname, expanded_path);
    f->fm.fp = fopen(f->fname, "r");
  }

  return TRUE;
}

/*---------------------------

  ---------------------------*/
/* This may will take forever for large files.
   Also there is not nearly enough error
   checking! Also the total insert changes is
   limited to MAX_SAVE_SHIFT. Consider increasing
   this size dynamically when needed? */
off_t vf_save(file_manager_t * f, int *complete)
{
  static char move_buf[2][MAX_SAVE_SHIFT];  /* 2 x 2 megs from heap */
  int buf_select = 0;
  char *tmp_buf;
  off_t write_offset = 0;
  off_t shift = 0;
  vbuf_t *tmp;

  if (f == NULL)
    return 0; /* save as? */

  tmp = f->fm.first_child;

  *complete = 0;

  prune(&f->ul);

  if (f->fm.fp == NULL)
    return 0;

  fclose(f->fm.fp);
  f->fm.fp = fopen(f->fname, "r+");

  if (f->fm.fp == NULL) /* can't open for writing, permissions? */
  {
    f->fm.fp = fopen(f->fname, "r");
    return 0;
  }

  while(NULL != tmp)
  {
    if(FALSE == tmp->active || 0 == tmp->size)
    {
      tmp = tmp->next;
      continue;
    }

    if(0 != shift)
    {
      if(shift > 0)             /* data has been inserted */
      {
        tmp_buf = (char *)malloc(shift);
        buf_select = BUF_TOGGLE(buf_select);
        memcpy(tmp_buf, move_buf[buf_select], shift);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        if (tmp->start - write_offset - shift > MAX_SAVE_SHIFT)
          fread(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
        else
          fread(move_buf[buf_select], 1, tmp->start - write_offset - shift, f->fm.fp);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        fwrite(tmp_buf, 1, shift, f->fm.fp);
        free(tmp_buf);
        write_offset += shift;
        buf_select = BUF_TOGGLE(buf_select);

        while(tmp->start - write_offset > MAX_SAVE_SHIFT)
        {
          fseeko(f->fm.fp, write_offset, SEEK_SET);
          if (tmp->start - write_offset > 2*MAX_SAVE_SHIFT)
            fread(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
          else
            fread(move_buf[buf_select], 1, tmp->start - write_offset - MAX_SAVE_SHIFT, f->fm.fp);
          buf_select = BUF_TOGGLE(buf_select);
          fseeko(f->fm.fp, write_offset, SEEK_SET);
          fwrite(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
          write_offset += MAX_SAVE_SHIFT;
          compute_percent_complete(write_offset, f->fm.size, complete);
        }

        buf_select = BUF_TOGGLE(buf_select);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        fwrite(move_buf[buf_select], 1, tmp->start - write_offset, f->fm.fp);
        write_offset += tmp->start - write_offset;
        compute_percent_complete(write_offset, f->fm.size, complete);
      }
      else                      /* data has been deleted */
      {
        while(tmp->start - write_offset > MAX_SAVE_SHIFT)
        {
          fseeko(f->fm.fp, write_offset - shift, SEEK_SET);
          fread(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
          fseeko(f->fm.fp, write_offset, SEEK_SET);
          fwrite(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
          write_offset += MAX_SAVE_SHIFT;
          compute_percent_complete(write_offset, f->fm.size, complete);
        }

        fseeko(f->fm.fp, write_offset - shift, SEEK_SET);
        fread(move_buf[buf_select], 1, tmp->start - write_offset, f->fm.fp);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        fwrite(move_buf[buf_select], 1, tmp->start - write_offset, f->fm.fp);
        write_offset += tmp->start - write_offset;
        compute_percent_complete(write_offset, f->fm.size, complete);
      }
    }

    write_offset = tmp->start;
    compute_percent_complete(write_offset, f->fm.size, complete);

    switch (tmp->buf_type)
    {
      case TYPE_INSERT:
        tmp_buf = (char *)malloc(tmp->size);
        if(shift < 0)           /* data has been deleted */
        {
          if(tmp->size + shift > 0)
          {
            fseeko(f->fm.fp, write_offset - shift, SEEK_SET);
            /* if shift > MAX_SAVE_SHIFT we have a problem =( */
            shift = fread(move_buf[buf_select], 1, tmp->size + shift, f->fm.fp);
          }
          else
          {
            shift += tmp->size;
          }
        }
        else                    /* data has been inserted */
        {
          fseeko(f->fm.fp, write_offset, SEEK_SET);
        /* if shift > MAX_SAVE_SHIFT we have a problem =( */
          shift += fread(move_buf[buf_select] + shift, 1, tmp->size, f->fm.fp);
        }
        buf_select = BUF_TOGGLE(buf_select);
        vf_get_buf(f, tmp_buf, write_offset, tmp->size);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        fwrite(tmp_buf, 1, tmp->size, f->fm.fp);
        free(tmp_buf);
        write_offset += tmp->size;
        compute_percent_complete(write_offset, f->fm.size, complete);
        break;
      case TYPE_REPLACE:
        tmp_buf = (char *)malloc(tmp->size);
        if(shift != 0)
        {
          if(shift - tmp->size > 0)
          {
            memcpy(move_buf[buf_select], move_buf[buf_select] + tmp->size,
                   shift - tmp->size);
            fseeko(f->fm.fp, write_offset, SEEK_SET);
            fread(move_buf[buf_select] + shift - tmp->size, 1, tmp->size,
                  f->fm.fp);
          }
          else
          {
            fseeko(f->fm.fp, write_offset + tmp->size - shift, SEEK_SET);
            fread(move_buf[buf_select], 1, shift, f->fm.fp);
          }
          buf_select = BUF_TOGGLE(buf_select);
        }
        vf_get_buf(f, tmp_buf, write_offset, tmp->size);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        fwrite(tmp_buf, 1, tmp->size, f->fm.fp);
        free(tmp_buf);
        write_offset += tmp->size;
        compute_percent_complete(write_offset, f->fm.size, complete);
        break;
      case TYPE_DELETE:
        if(shift - tmp->size > 0)
          memcpy(move_buf[buf_select], move_buf[buf_select] + tmp->size,
                 shift - tmp->size);
        shift -= tmp->size;     /* if shift < -MAX_SAVE_SHIFT we have a problem =( */
        break;
      default:
        break;
    }

    tmp = tmp->next;
  }

  if(shift > 0)                 /* data has been inserted */
  {
    if (f->fm.size > write_offset)
    {
      tmp_buf = (char *)malloc(shift);
      buf_select = BUF_TOGGLE(buf_select);
      memcpy(tmp_buf, move_buf[buf_select], shift);
      fseeko(f->fm.fp, write_offset, SEEK_SET);
      if (f->fm.size - write_offset - shift > MAX_SAVE_SHIFT)
        fread(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
      else
        fread(move_buf[buf_select], 1, f->fm.size - write_offset - shift, f->fm.fp);
      fseeko(f->fm.fp, write_offset, SEEK_SET);
      fwrite(tmp_buf, 1, shift, f->fm.fp);
      free(tmp_buf);
      write_offset += shift;
      buf_select = BUF_TOGGLE(buf_select);

      /* optimize this part to read larger chunks than 'shift'? */
      while(f->fm.size - write_offset > MAX_SAVE_SHIFT)
      {
        fseeko(f->fm.fp, write_offset + (MAX_SAVE_SHIFT - shift), SEEK_SET);
        if (f->fm.size - write_offset > 2*MAX_SAVE_SHIFT)
          fread(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
        else
          fread(move_buf[buf_select], 1, f->fm.size - write_offset - MAX_SAVE_SHIFT, f->fm.fp);
        buf_select = BUF_TOGGLE(buf_select);
        fseeko(f->fm.fp, write_offset, SEEK_SET);
        fwrite(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
        write_offset += MAX_SAVE_SHIFT;
        compute_percent_complete(write_offset, f->fm.size, complete);
      }

      buf_select = BUF_TOGGLE(buf_select);
      fseeko(f->fm.fp, write_offset, SEEK_SET);
      fwrite(move_buf[buf_select], 1, f->fm.size - write_offset, f->fm.fp);
      write_offset += f->fm.size - write_offset;
      compute_percent_complete(write_offset, f->fm.size, complete);
    }
  }
  else if(shift < 0)            /* data has been deleted */
  {
    while(f->fm.size - write_offset > MAX_SAVE_SHIFT)
    {
      fseeko(f->fm.fp, write_offset - shift, SEEK_SET);
      fread(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
      fseeko(f->fm.fp, write_offset, SEEK_SET);
      fwrite(move_buf[buf_select], 1, MAX_SAVE_SHIFT, f->fm.fp);
      write_offset += MAX_SAVE_SHIFT;
      compute_percent_complete(write_offset, f->fm.size, complete);
    }

    fseeko(f->fm.fp, write_offset - shift, SEEK_SET);
    fread(move_buf[buf_select], 1, f->fm.size - write_offset, f->fm.fp);
    fseeko(f->fm.fp, write_offset, SEEK_SET);
    fwrite(move_buf[buf_select], 1, f->fm.size - write_offset, f->fm.fp);
    write_offset += f->fm.size - write_offset;
    compute_percent_complete(write_offset, f->fm.size, complete);
    ftruncate(fileno(f->fm.fp), write_offset);
  }

  fclose(f->fm.fp);
  f->fm.fp = fopen(f->fname, "r");

  cleanup(f);

  *complete = 100;
  return f->fm.size;
}

/*---------------------------

  ---------------------------*/
BOOL vf_need_create(file_manager_t * f)
{
  if (f == NULL)
    return FALSE;

  if (f->fm.fp == NULL)
    return TRUE;

  return FALSE;
}
/*---------------------------

  ---------------------------*/
BOOL vf_need_save(file_manager_t * f)
{
  vbuf_undo_list_t *tmp;

  if (f == NULL)
    return FALSE;

  tmp = f->ul.last;

  while(NULL != tmp)
  {
    if(FALSE == tmp->saved && TRUE == tmp->applied)
      return TRUE;
    tmp = tmp->last;
  }

  return FALSE;
}


/*---------------------------

  ---------------------------*/
/* returns number of changes undone */
/* undo_addr is set to the start address of the last change undone */
int vf_undo(file_manager_t * f, int count, off_t * undo_addr)
{
  vbuf_undo_list_t *tmp_undo_list = f->ul.last;
  vbuf_list_t *tmp_list = NULL;
  vbuf_t *tmp = NULL;
  int undo_count;

  if (f == NULL)
    return 0;

  /* look for the first change still applied */
  while(NULL != tmp_undo_list)
  {
    if(TRUE == tmp_undo_list->applied)
      break;
    tmp_undo_list = tmp_undo_list->last;
  }

  /* now start to undo */
  for(undo_count = 0; undo_count < count; undo_count++)
  {
    if(NULL == tmp_undo_list)
      return undo_count;

    tmp_list = tmp_undo_list->vb_list;

    while(NULL != tmp_list)
    {
      tmp = tmp_list->vb;

      switch (tmp->buf_type)
      {
        case TYPE_INSERT:
          mod_parent_size(tmp->parent, tmp->size, FALSE);
          mod_start_offset(tmp->next, tmp->size, FALSE);
          break;
        case TYPE_DELETE:
          mod_parent_size(tmp->parent, tmp->size, TRUE);
          mod_start_offset(tmp->next, tmp->size, TRUE);
          break;
        case TYPE_REPLACE:     /* no break */
        default:
          break;
      }

      tmp->active = FALSE;
      if(NULL != undo_addr)
        *undo_addr = tmp->start;

      tmp_list = tmp_list->next;
    }

    tmp_undo_list->applied = FALSE;

    tmp_undo_list = tmp_undo_list->last;
  }

  return undo_count;
}


/*---------------------------

  ---------------------------*/
/* returns number of changes redone */
/* redo_addr is set to the start address of the last change redone */
int vf_redo(file_manager_t * f, int count, off_t * redo_addr)
{
  vbuf_undo_list_t *tmp_undo_list = f->ul.last;
  vbuf_list_t *tmp_list = NULL;
  vbuf_t *tmp = NULL;
  int redo_count;

  if (f == NULL)
    return 0;

  if(NULL == tmp_undo_list)
    return 0;

  /* no changes have been undone */
  if(TRUE == tmp_undo_list->applied)
    return 0;

  /* look for the last change undone */
  while(NULL != tmp_undo_list)
  {
    if(NULL == tmp_undo_list->last)
      break;
    if(tmp_undo_list->last->applied)
      break;
    tmp_undo_list = tmp_undo_list->last;
  }

  /* now start to redo */
  for(redo_count = 0; redo_count < count; redo_count++)
  {
    if(NULL == tmp_undo_list)
      return redo_count;

    tmp_list = tmp_undo_list->vb_list;

    while(NULL != tmp_list)
    {
      tmp = tmp_list->vb;

      switch (tmp->buf_type)
      {
        case TYPE_INSERT:
          mod_parent_size(tmp->parent, tmp->size, TRUE);
          mod_start_offset(tmp->next, tmp->size, TRUE);
          break;
        case TYPE_DELETE:
          mod_parent_size(tmp->parent, tmp->size, FALSE);
          mod_start_offset(tmp->next, tmp->size, FALSE);
          break;
        case TYPE_REPLACE:     /* no break */
        default:
          break;
      }

      tmp->active = TRUE;
      if(NULL != redo_addr)
        *redo_addr = tmp->start;

      tmp_list = tmp_list->next;
    }

    tmp_undo_list->applied = TRUE;

    tmp_undo_list = f->ul.last;

    /* no changes have been undone */
    if(TRUE == tmp_undo_list->applied)
      return redo_count + 1;

    /* look for the last change undone */
    while(NULL != tmp_undo_list)
    {
      if(NULL == tmp_undo_list->last)
        break;
      if(tmp_undo_list->last->applied)
        break;
      tmp_undo_list = tmp_undo_list->last;
    }
  }

  return redo_count;
}


/*---------------------------

  ---------------------------*/
size_t vf_insert_before(file_manager_t * f, char *buf, off_t offset, size_t len)
{
  if (f == NULL)
    return 0;
  prune(&f->ul);
  return _insert_before(&f->fm, buf, offset, len, &f->ul.last);
}


/*---------------------------

  ---------------------------*/
size_t vf_insert_after(file_manager_t * f, char *buf, off_t offset, size_t len)
{
  if (f == NULL)
    return 0;
  prune(&f->ul);
  return _insert_before(&f->fm, buf, offset + 1, len, &f->ul.last);
}


/*---------------------------

  ---------------------------*/
size_t vf_replace(file_manager_t * f, char *buf, off_t offset, size_t len)
{
  size_t rep_size = 0;
  vbuf_list_t *vb_list = NULL;
  vbuf_undo_list_t *new_list;

  if (f == NULL)
    return 0;

  prune(&f->ul);

  rep_size = _replace(&f->fm, buf, offset, len, &vb_list);

  if(NULL != vb_list)
  {
    new_list = (vbuf_undo_list_t *) malloc(sizeof(vbuf_undo_list_t));
    new_list->applied = TRUE;
    new_list->saved = FALSE;
    new_list->last = f->ul.last;
    f->ul.last = new_list;
    new_list->vb_list = vb_list;
  }

  return rep_size;
}


/*---------------------------

  ---------------------------*/
size_t vf_delete(file_manager_t * f, off_t offset, size_t len)
{
  size_t del_size = 0;
  vbuf_list_t *vb_list = NULL;
  vbuf_undo_list_t *new_list;

  if (f == NULL)
    return 0;

  prune(&f->ul);

  del_size = _delete(&f->fm, offset, len, &vb_list);

  if(NULL != vb_list)
  {
    new_list = (vbuf_undo_list_t *) malloc(sizeof(vbuf_undo_list_t));
    new_list->applied = TRUE;
    new_list->saved = FALSE;
    new_list->last = f->ul.last;
    f->ul.last = new_list;
    new_list->vb_list = vb_list;
  }

  return del_size;
}


/*---------------------------

  ---------------------------*/
char vf_get_char(file_manager_t * f, char *result, off_t offset)
{
  if (f == NULL)
  {
    *result = 0;
    return 0;
  }

  return _get_char(&f->fm, result, offset);
}


/*---------------------------

  ---------------------------*/
size_t vf_get_buf(file_manager_t * f, char *dest, off_t offset, size_t len)
{
  if (f == NULL)
    return 0;

  return _get_buf(&f->fm, dest, offset, len);
}

