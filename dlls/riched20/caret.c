/*
 * RichEdit - Caret and selection functions.
 *
 * Copyright 2004 by Krzysztof Foltman
 * Copyright 2005 by Phil Krylov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */


#include "editor.h"

WINE_DEFAULT_DEBUG_CHANNEL(richedit);

void ME_SetCursorToStart(ME_TextEditor *editor, ME_Cursor *cursor)
{
  cursor->pPara = para_get_di( editor_first_para( editor ) );
  cursor->pRun = run_get_di( para_first_run( &cursor->pPara->member.para ) );
  cursor->nOffset = 0;
}

static void ME_SetCursorToEnd(ME_TextEditor *editor, ME_Cursor *cursor, BOOL final_eop)
{
  cursor->pPara = para_get_di( para_prev( editor_end_para( editor ) ) );
  cursor->pRun = run_get_di( para_end_run( &cursor->pPara->member.para ) );
  cursor->nOffset = final_eop ? cursor->pRun->member.run.len : 0;
}


int ME_GetSelectionOfs(ME_TextEditor *editor, int *from, int *to)
{
  *from = ME_GetCursorOfs(&editor->pCursors[0]);
  *to =   ME_GetCursorOfs(&editor->pCursors[1]);

  if (*from > *to)
  {
    int tmp = *from;
    *from = *to;
    *to = tmp;
    return 1;
  }
  return 0;
}

int ME_GetSelection(ME_TextEditor *editor, ME_Cursor **from, ME_Cursor **to)
{
  int from_ofs = ME_GetCursorOfs( &editor->pCursors[0] );
  int to_ofs   = ME_GetCursorOfs( &editor->pCursors[1] );
  BOOL swap = (from_ofs > to_ofs);

  if (from_ofs == to_ofs)
  {
      /* If cursor[0] is at the beginning of a run and cursor[1] at the end
         of the prev run then we need to swap. */
      if (editor->pCursors[0].nOffset < editor->pCursors[1].nOffset)
          swap = TRUE;
  }

  if (!swap)
  {
    *from = &editor->pCursors[0];
    *to = &editor->pCursors[1];
    return 0;
  } else {
    *from = &editor->pCursors[1];
    *to = &editor->pCursors[0];
    return 1;
  }
}

int ME_GetTextLength(ME_TextEditor *editor)
{
  ME_Cursor cursor;
  ME_SetCursorToEnd(editor, &cursor, FALSE);
  return ME_GetCursorOfs(&cursor);
}


int ME_GetTextLengthEx(ME_TextEditor *editor, const GETTEXTLENGTHEX *how)
{
  int length;

  if (how->flags & GTL_PRECISE && how->flags & GTL_CLOSE)
    return E_INVALIDARG;
  if (how->flags & GTL_NUMCHARS && how->flags & GTL_NUMBYTES)
    return E_INVALIDARG;
  
  length = ME_GetTextLength(editor);

  if ((editor->styleFlags & ES_MULTILINE)
        && (how->flags & GTL_USECRLF)
        && !editor->bEmulateVersion10) /* Ignore GTL_USECRLF flag in 1.0 emulation */
    length += editor->nParagraphs - 1;

  if (how->flags & GTL_NUMBYTES ||
      (how->flags & GTL_PRECISE &&     /* GTL_PRECISE seems to imply GTL_NUMBYTES */
       !(how->flags & GTL_NUMCHARS)))  /* unless GTL_NUMCHARS is given */
  {
    CPINFO cpinfo;
    
    if (how->codepage == 1200)
      return length * 2;
    if (how->flags & GTL_PRECISE)
      FIXME("GTL_PRECISE flag unsupported. Using GTL_CLOSE\n");
    if (GetCPInfo(how->codepage, &cpinfo))
      return length * cpinfo.MaxCharSize;
    ERR("Invalid codepage %u\n", how->codepage);
    return E_INVALIDARG;
  }
  return length; 
}

/******************************************************************
 *    set_selection_cursors
 *
 * Updates the selection cursors.
 *
 * Note that this does not invalidate either the old or the new selections.
 */
int set_selection_cursors(ME_TextEditor *editor, int from, int to)
{
  int selectionEnd = 0;
  const int len = ME_GetTextLength(editor);

  /* all negative values are effectively the same */
  if (from < 0)
    from = -1;
  if (to < 0)
    to = -1;

  /* select all */
  if (from == 0 && to == -1)
  {
    ME_SetCursorToStart(editor, &editor->pCursors[1]);
    ME_SetCursorToEnd(editor, &editor->pCursors[0], TRUE);
    return len + 1;
  }

  /* if both values are equal and also out of bound, that means to */
  /* put the selection at the end of the text */
  if ((from == to) && (to < 0 || to > len))
  {
    selectionEnd = 1;
  }
  else
  {
    /* if from is negative and to is positive then selection is */
    /* deselected and caret moved to end of the current selection */
    if (from < 0)
    {
      int start, end;
      ME_GetSelectionOfs(editor, &start, &end);
      if (start != end)
      {
          if (end > len)
          {
              editor->pCursors[0].nOffset = 0;
              end --;
          }
          editor->pCursors[1] = editor->pCursors[0];
      }
      return end;
    }

    /* adjust to if it's a negative value */
    if (to < 0)
      to = len + 1;

    /* flip from and to if they are reversed */
    if (from>to)
    {
      int tmp = from;
      from = to;
      to = tmp;
    }

    /* after fiddling with the values, we find from > len && to > len */
    if (from > len)
      selectionEnd = 1;
    /* special case with to too big */
    else if (to > len)
      to = len + 1;
  }

  if (selectionEnd)
  {
    ME_SetCursorToEnd(editor, &editor->pCursors[0], FALSE);
    editor->pCursors[1] = editor->pCursors[0];
    return len;
  }

  cursor_from_char_ofs( editor, from, &editor->pCursors[1] );
  editor->pCursors[0] = editor->pCursors[1];
  ME_MoveCursorChars(editor, &editor->pCursors[0], to - from, FALSE);
  /* Selection is not allowed in the middle of an end paragraph run. */
  if (editor->pCursors[1].pRun->member.run.nFlags & MERF_ENDPARA)
    editor->pCursors[1].nOffset = 0;
  if (editor->pCursors[0].pRun->member.run.nFlags & MERF_ENDPARA)
  {
    if (to > len)
      editor->pCursors[0].nOffset = editor->pCursors[0].pRun->member.run.len;
    else
      editor->pCursors[0].nOffset = 0;
  }
  return to;
}


void ME_GetCursorCoordinates(ME_TextEditor *editor, ME_Cursor *pCursor,
                             int *x, int *y, int *height)
{
  ME_DisplayItem *row;
  ME_Run *run = &pCursor->pRun->member.run;
  ME_Paragraph *para = &pCursor->pPara->member.para;
  ME_Run *size_run = run, *prev;
  ME_Context c;
  int run_x;

  assert(height && x && y);
  assert(~para->nFlags & MEPF_REWRAP);

  row = ME_FindItemBack( run_get_di( run ), diStartRowOrParagraph );
  assert(row && row->type == diStartRow);

  ME_InitContext(&c, editor, ITextHost_TxGetDC(editor->texthost));

  if (!pCursor->nOffset && (prev = run_prev( run ))) size_run = prev;

  run_x = ME_PointFromCharContext( &c, run, pCursor->nOffset, TRUE );

  *height = size_run->nAscent + size_run->nDescent;
  *x = c.rcView.left + run->pt.x + run_x - editor->horz_si.nPos;
  *y = c.rcView.top + para->pt.y + row->member.row.nBaseline
       + run->pt.y - size_run->nAscent - editor->vert_si.nPos;
  ME_DestroyContext(&c);
  return;
}

void create_caret(ME_TextEditor *editor)
{
  int x, y, height;

  ME_GetCursorCoordinates(editor, &editor->pCursors[0], &x, &y, &height);
  ITextHost_TxCreateCaret(editor->texthost, NULL, 0, height);
  editor->caret_height = height;
  editor->caret_hidden = TRUE;
}

void show_caret(ME_TextEditor *editor)
{
  ITextHost_TxShowCaret(editor->texthost, TRUE);
  editor->caret_hidden = FALSE;
}

void hide_caret(ME_TextEditor *editor)
{
  /* calls to HideCaret are cumulative; do so only once */
  if (!editor->caret_hidden)
  {
    ITextHost_TxShowCaret(editor->texthost, FALSE);
    editor->caret_hidden = TRUE;
  }
}

void update_caret(ME_TextEditor *editor)
{
  int x, y, height;

  if (!editor->bHaveFocus) return;
  if (!ME_IsSelection(editor))
  {
    ME_GetCursorCoordinates(editor, &editor->pCursors[0], &x, &y, &height);
    if (height != editor->caret_height) create_caret(editor);
    x = min(x, editor->rcFormat.right-1);
    ITextHost_TxSetCaretPos(editor->texthost, x, y);
    show_caret(editor);
  }
  else
    hide_caret(editor);
}

BOOL ME_InternalDeleteText(ME_TextEditor *editor, ME_Cursor *start,
                           int nChars, BOOL bForce)
{
  ME_Cursor c = *start;
  int nOfs = ME_GetCursorOfs(start), text_len = ME_GetTextLength( editor );
  int shift = 0;
  int totalChars = nChars;
  ME_Paragraph *start_para;
  BOOL delete_all = FALSE;

  /* Prevent deletion past last end of paragraph run. */
  nChars = min(nChars, text_len - nOfs);
  if (nChars == text_len) delete_all = TRUE;
  start_para = &c.pPara->member.para;

  if (!bForce)
  {
    ME_ProtectPartialTableDeletion(editor, &c, &nChars);
    if (nChars == 0)
      return FALSE;
  }

  while(nChars > 0)
  {
    ME_Run *run;
    cursor_from_char_ofs( editor, nOfs + nChars, &c );
    if (!c.nOffset)
    {
      /* We aren't deleting anything in this run, so we will go back to the
       * last run we are deleting text in. */
      c.pRun = run_get_di( run_prev_all_paras( &c.pRun->member.run ) );
      c.pPara = para_get_di( c.pRun->member.run.para );
      c.nOffset = c.pRun->member.run.len;
    }
    run = &c.pRun->member.run;
    if (run->nFlags & MERF_ENDPARA)
    {
      int eollen = c.pRun->member.run.len;
      BOOL keepFirstParaFormat;

      if (!para_next( para_next( &c.pPara->member.para ) )) return TRUE;

      keepFirstParaFormat = (totalChars == nChars && nChars <= eollen &&
                             run->nCharOfs);
      if (!editor->bEmulateVersion10) /* v4.1 */
      {
        ME_Paragraph *this_para = run->para;
        ME_Paragraph *next_para = para_next( this_para );

        /* The end of paragraph before a table row is only deleted if there
         * is nothing else on the line before it. */
        if (this_para == start_para && next_para->nFlags & MEPF_ROWSTART)
        {
          /* If the paragraph will be empty, then it should be deleted, however
           * it still might have text right now which would inherit the
           * MEPF_STARTROW property if we joined it right now.
           * Instead we will delete it after the preceding text is deleted. */
          if (nOfs > this_para->nCharOfs)
          {
            /* Skip this end of line. */
            nChars -= (eollen < nChars) ? eollen : nChars;
            continue;
          }
          keepFirstParaFormat = TRUE;
        }
      }
      para_join( editor, &c.pPara->member.para, keepFirstParaFormat );
      /* ME_SkipAndPropagateCharOffset(p->pRun, shift); */
      ME_CheckCharOffsets(editor);
      nChars -= (eollen < nChars) ? eollen : nChars;
      continue;
    }
    else
    {
      ME_Cursor cursor;
      int nCharsToDelete = min(nChars, c.nOffset);
      int i;

      c.nOffset -= nCharsToDelete;

      para_mark_rewrap( editor, c.pRun->member.run.para );

      cursor = c;
      /* nChars is the number of characters that should be deleted from the
         PRECEDING runs (these BEFORE cursor.pRun)
         nCharsToDelete is a number of chars to delete from THIS run */
      nChars -= nCharsToDelete;
      shift -= nCharsToDelete;
      TRACE("Deleting %d (remaining %d) chars at %d in %s (%d)\n",
        nCharsToDelete, nChars, c.nOffset,
        debugstr_run( run ), run->len);

      /* nOfs is a character offset (from the start of the document
         to the current (deleted) run */
      add_undo_insert_run( editor, nOfs + nChars, get_text( run, c.nOffset ), nCharsToDelete, run->nFlags, run->style );

      ME_StrDeleteV(run->para->text, run->nCharOfs + c.nOffset, nCharsToDelete);
      run->len -= nCharsToDelete;
      TRACE("Post deletion string: %s (%d)\n", debugstr_run( run ), run->len);
      TRACE("Shift value: %d\n", shift);

      /* update cursors (including c) */
      for (i=-1; i<editor->nCursors; i++) {
        ME_Cursor *pThisCur = editor->pCursors + i;
        if (i == -1) pThisCur = &c;
        if (pThisCur->pRun == cursor.pRun) {
          if (pThisCur->nOffset > cursor.nOffset) {
            if (pThisCur->nOffset-cursor.nOffset < nCharsToDelete)
              pThisCur->nOffset = cursor.nOffset;
            else
              pThisCur->nOffset -= nCharsToDelete;
            assert(pThisCur->nOffset >= 0);
            assert(pThisCur->nOffset <= run->len);
          }
          if (pThisCur->nOffset == run->len)
          {
            pThisCur->pRun = ME_FindItemFwd(pThisCur->pRun, diRunOrParagraphOrEnd);
            assert(pThisCur->pRun->type == diRun);
            pThisCur->nOffset = 0;
          }
        }
      }

      /* c = updated data now */

      if (c.pRun == cursor.pRun)
        ME_SkipAndPropagateCharOffset(c.pRun, shift);
      else
        ME_PropagateCharOffset(c.pRun, shift);

      if (!cursor.pRun->member.run.len)
      {
        TRACE("Removing empty run\n");
        ME_Remove(cursor.pRun);
        ME_DestroyDisplayItem(cursor.pRun);
      }

      shift = 0;
      /*
      ME_CheckCharOffsets(editor);
      */
      continue;
    }
  }
  if (delete_all) ME_SetDefaultParaFormat( editor, &start_para->fmt );
  return TRUE;
}

BOOL ME_DeleteTextAtCursor(ME_TextEditor *editor, int nCursor, int nChars)
{
  assert(nCursor>=0 && nCursor<editor->nCursors);
  /* text operations set modified state */
  editor->nModifyStep = 1;
  return ME_InternalDeleteText(editor, &editor->pCursors[nCursor],
                               nChars, FALSE);
}

static struct re_object* create_re_object(const REOBJECT *reo)
{
  struct re_object *reobj = heap_alloc(sizeof(*reobj));

  if (!reobj)
  {
    WARN("Fail to allocate re_object.\n");
    return NULL;
  }
  ME_CopyReObject(&reobj->obj, reo, REO_GETOBJ_ALL_INTERFACES);
  return reobj;
}

void ME_InsertOLEFromCursor(ME_TextEditor *editor, const REOBJECT* reo, int nCursor)
{
  ME_Run *run, *prev;
  const WCHAR space = ' ';
  struct re_object *reobj_prev = NULL;
  ME_Cursor *cursor = editor->pCursors + nCursor;
  ME_Style *style = style_get_insert_style( editor, cursor );

  /* FIXME no no no */
  if (ME_IsSelection(editor))
    ME_DeleteSelection(editor);

  run = run_insert( editor, cursor, style, &space, 1, MERF_GRAPHICS );

  run->reobj = create_re_object( reo );

  prev = run;
  while ((prev = run_prev_all_paras( prev )))
  {
    if (prev->reobj)
    {
      reobj_prev = prev->reobj;
      break;
    }
  }
  if (reobj_prev)
    list_add_after(&reobj_prev->entry, &run->reobj->entry);
  else
    list_add_head(&editor->reobj_list, &run->reobj->entry);

  ME_ReleaseStyle( style );
}


void ME_InsertEndRowFromCursor(ME_TextEditor *editor, int nCursor)
{
  const WCHAR space = ' ';
  ME_Cursor *cursor = editor->pCursors + nCursor;
  ME_Style *style = style_get_insert_style( editor, cursor );

  /* FIXME no no no */
  if (ME_IsSelection(editor))
    ME_DeleteSelection(editor);

  run_insert( editor, cursor, style, &space, 1, MERF_ENDROW );

  ME_ReleaseStyle( style );
}


void ME_InsertTextFromCursor(ME_TextEditor *editor, int nCursor, 
                             const WCHAR *str, int len, ME_Style *style)
{
  const WCHAR *pos;
  ME_Cursor *cursor = editor->pCursors + nCursor;
  int oldLen;

  /* FIXME really HERE ? */
  if (ME_IsSelection(editor))
    ME_DeleteSelection(editor);

  oldLen = ME_GetTextLength(editor);

  /* text operations set modified state */
  editor->nModifyStep = 1;

  assert(style);

  if (len == -1) len = lstrlenW( str );

  /* grow the text limit to fit our text */
  if (editor->nTextLimit < oldLen + len) editor->nTextLimit = oldLen + len;

  pos = str;

  while (len)
  {
    /* FIXME this sucks - no respect for unicode (what else can be a line separator in unicode?) */
    while (pos - str < len && *pos != '\r' && *pos != '\n' && *pos != '\t')
      pos++;

    if (pos != str) /* handle text */
      run_insert( editor, cursor, style, str, pos - str, 0 );
    else if (*pos == '\t') /* handle tabs */
    {
      const WCHAR tab = '\t';
      run_insert( editor, cursor, style, &tab, 1, MERF_TAB );
      pos++;
    }
    else /* handle EOLs */
    {
      ME_Run *end_run, *run, *prev;
      ME_Paragraph *new_para;
      int eol_len = 0;

      /* Check if new line is allowed for this control */
      if (!(editor->styleFlags & ES_MULTILINE))
        break;

      /* Find number of CR and LF in end of paragraph run */
      if (*pos =='\r')
      {
        if (len > 1 && pos[1] == '\n')
          eol_len = 2;
        else if (len > 2 && pos[1] == '\r' && pos[2] == '\n')
          eol_len = 3;
        else
          eol_len = 1;
      }
      else
      {
        assert(*pos == '\n');
        eol_len = 1;
      }
      pos += eol_len;

      if (!editor->bEmulateVersion10 && eol_len == 3)
      {
        /* handle special \r\r\n sequence (richedit 2.x and higher only) */
        const WCHAR space = ' ';
        run_insert( editor, cursor, style, &space, 1, 0 );
      }
      else
      {
        const WCHAR cr = '\r', *eol_str = str;

        if (!editor->bEmulateVersion10)
        {
          eol_str = &cr;
          eol_len = 1;
        }

        if (cursor->nOffset == cursor->pRun->member.run.len)
        {
           run = run_next( &cursor->pRun->member.run );
           if (!run) run = &cursor->pRun->member.run;
        }
        else
        {
          if (cursor->nOffset) run_split( editor, cursor );
          run = &cursor->pRun->member.run;
        }

        new_para = para_split( editor, run, style, eol_str, eol_len, 0 );
        end_run = para_end_run( para_prev( new_para ) );

        /* Move any cursors that were at the end of the previous run to the beginning of the new para */
        prev = run_prev( end_run );
        if (prev)
        {
          int i;
          for (i = 0; i < editor->nCursors; i++)
          {
            if (editor->pCursors[i].pRun == run_get_di( prev ) &&
                editor->pCursors[i].nOffset == prev->len)
            {
              editor->pCursors[i].pPara = para_get_di( new_para );
              editor->pCursors[i].pRun = run_get_di( run );
              editor->pCursors[i].nOffset = 0;
            }
          }
        }

      }
    }
    len -= pos - str;
    str = pos;
  }
}

/* Move the cursor nRelOfs characters (either forwards or backwards)
 * If final_eop is TRUE, allow moving the cursor to the end of the final eop.
 *
 * returns the actual number of characters moved.
 **/
int ME_MoveCursorChars(ME_TextEditor *editor, ME_Cursor *cursor, int nRelOfs, BOOL final_eop)
{
  cursor->nOffset += nRelOfs;
  if (cursor->nOffset < 0)
  {
    cursor->nOffset += cursor->pRun->member.run.nCharOfs;
    if (cursor->nOffset >= 0)
    {
      /* new offset in the same paragraph */
      do {
        cursor->pRun = run_get_di( run_prev( &cursor->pRun->member.run ) );
      } while (cursor->nOffset < cursor->pRun->member.run.nCharOfs);
      cursor->nOffset -= cursor->pRun->member.run.nCharOfs;
      return nRelOfs;
    }

    cursor->nOffset += cursor->pPara->member.para.nCharOfs;
    if (cursor->nOffset <= 0)
    {
      /* moved to the start of the text */
      nRelOfs -= cursor->nOffset;
      ME_SetCursorToStart(editor, cursor);
      return nRelOfs;
    }

    /* new offset in a previous paragraph */
    do {
      cursor->pPara = para_get_di( para_prev( &cursor->pPara->member.para ) );
    } while (cursor->nOffset < cursor->pPara->member.para.nCharOfs);
    cursor->nOffset -= cursor->pPara->member.para.nCharOfs;

    cursor->pRun = run_get_di( para_end_run( &cursor->pPara->member.para ) );
    while (cursor->nOffset < cursor->pRun->member.run.nCharOfs) {
      cursor->pRun = run_get_di( run_prev( &cursor->pRun->member.run ) );
    }
    cursor->nOffset -= cursor->pRun->member.run.nCharOfs;
  }
  else if (cursor->nOffset >= cursor->pRun->member.run.len)
  {
    ME_Paragraph *next_para;
    int new_offset;

    new_offset = ME_GetCursorOfs(cursor);
    next_para = para_next( &cursor->pPara->member.para );
    if (new_offset < next_para->nCharOfs)
    {
      /* new offset in the same paragraph */
      do {
        cursor->nOffset -= cursor->pRun->member.run.len;
        cursor->pRun = run_get_di( run_next( &cursor->pRun->member.run ) );
      } while (cursor->nOffset >= cursor->pRun->member.run.len);
      return nRelOfs;
    }

    if (new_offset >= ME_GetTextLength(editor) + (final_eop ? 1 : 0))
    {
      /* new offset at the end of the text */
      ME_SetCursorToEnd(editor, cursor, final_eop);
      nRelOfs -= new_offset - (ME_GetTextLength(editor) + (final_eop ? 1 : 0));
      return nRelOfs;
    }

    /* new offset in a following paragraph */
    do {
      cursor->pPara = para_get_di( next_para );
      next_para = para_next( next_para );
    } while (new_offset >= next_para->nCharOfs);

    cursor->nOffset = new_offset - cursor->pPara->member.para.nCharOfs;
    cursor->pRun = run_get_di( para_first_run( &cursor->pPara->member.para ) );
    while (cursor->nOffset >= cursor->pRun->member.run.len)
    {
      cursor->nOffset -= cursor->pRun->member.run.len;
      cursor->pRun = run_get_di( run_next( &cursor->pRun->member.run ) );
    }
  } /* else new offset is in the same run */
  return nRelOfs;
}


BOOL
ME_MoveCursorWords(ME_TextEditor *editor, ME_Cursor *cursor, int nRelOfs)
{
  ME_Run *run = &cursor->pRun->member.run, *other_run;
  ME_Paragraph *para = &cursor->pPara->member.para;
  int nOffset = cursor->nOffset;

  if (nRelOfs == -1)
  {
    /* Backward movement */
    while (TRUE)
    {
      nOffset = ME_CallWordBreakProc( editor, get_text( run, 0 ), run->len, nOffset, WB_MOVEWORDLEFT );
      if (nOffset) break;
      other_run = run_prev( run );
      if (other_run)
      {
        if (ME_CallWordBreakProc( editor, get_text( other_run, 0 ), other_run->len, other_run->len - 1, WB_ISDELIMITER )
            && !(run->nFlags & MERF_ENDPARA)
            && !(&cursor->pRun->member.run == run && cursor->nOffset == 0)
            && !ME_CallWordBreakProc( editor, get_text( run, 0 ), run->len, 0, WB_ISDELIMITER ))
          break;
        run = other_run;
        nOffset = other_run->len;
      }
      else
      {
        if (&cursor->pRun->member.run == run && cursor->nOffset == 0)
        {
          para = run->para;
          /* Skip empty start of table row paragraph */
          if (para_prev( para ) && para_prev( para )->nFlags & MEPF_ROWSTART)
            para = para_prev( para );
          /* Paragraph breaks are treated as separate words */
          if (!para_prev( para )) return FALSE;
          para = para_prev( para );
          run = para_end_run( para );
        }
        break;
      }
    }
  }
  else
  {
    /* Forward movement */
    BOOL last_delim = FALSE;
    
    while (TRUE)
    {
      if (last_delim && !ME_CallWordBreakProc( editor, get_text( run, 0 ), run->len, nOffset, WB_ISDELIMITER ))
        break;
      nOffset = ME_CallWordBreakProc( editor, get_text( run, 0 ), run->len, nOffset, WB_MOVEWORDRIGHT );
      if (nOffset < run->len) break;
      other_run = run_next( run );
      if (other_run)
      {
        last_delim = ME_CallWordBreakProc( editor, get_text( run, 0 ), run->len, nOffset - 1, WB_ISDELIMITER );
        run = other_run;
        nOffset = 0;
      }
      else
      {
        para = para_next( para );
        if (para_get_di( para )->type == diTextEnd)
        {
          if (&cursor->pRun->member.run == run) return FALSE;
          nOffset = 0;
          break;
        }
        if (para->nFlags & MEPF_ROWSTART) para = para_next( para );
        if (&cursor->pRun->member.run == run) run = para_first_run( para );
        nOffset = 0;
        break;
      }
    }
  }
  cursor->pPara = para_get_di( para );
  cursor->pRun = run_get_di( run );
  cursor->nOffset = nOffset;
  return TRUE;
}


static void
ME_SelectByType(ME_TextEditor *editor, ME_SelectionType selectionType)
{
  /* pCursor[0] is the end of the selection
   * pCursor[1] is the start of the selection (or the position selection anchor)
   * pCursor[2] and [3] are the selection anchors that are backed up
   * so they are kept when the selection changes for drag selection.
   */

  editor->nSelectionType = selectionType;
  switch(selectionType)
  {
    case stPosition:
      break;
    case stWord:
      ME_MoveCursorWords(editor, &editor->pCursors[0], +1);
      editor->pCursors[1] = editor->pCursors[0];
      ME_MoveCursorWords(editor, &editor->pCursors[1], -1);
      break;
    case stParagraph:
      editor->pCursors[1] = editor->pCursors[0];

      editor->pCursors[0].pRun = run_get_di( para_end_run( &editor->pCursors[0].pPara->member.para ) );
      editor->pCursors[0].pPara = para_get_di( editor->pCursors[0].pRun->member.run.para );
      editor->pCursors[0].nOffset = editor->pCursors[0].pRun->member.run.len;

      editor->pCursors[1].pRun = run_get_di( para_first_run( &editor->pCursors[1].pPara->member.para ) );
      editor->pCursors[1].nOffset = 0;
      break;
    case stLine:
    {
      ME_Row *row = row_from_cursor( editor->pCursors );

      row_first_cursor( row, editor->pCursors + 1 );
      row_end_cursor( row, editor->pCursors, TRUE );
      break;
    }
    case stDocument:
      /* Select everything with cursor anchored from the start of the text */
      ME_SetCursorToStart(editor, &editor->pCursors[1]);
      ME_SetCursorToEnd(editor, &editor->pCursors[0], TRUE);
      break;
    default: assert(0);
  }
  /* Store the anchor positions for extending the selection. */
  editor->pCursors[2] = editor->pCursors[0];
  editor->pCursors[3] = editor->pCursors[1];
}

int ME_GetCursorOfs(const ME_Cursor *cursor)
{
  return cursor->pPara->member.para.nCharOfs
         + cursor->pRun->member.run.nCharOfs + cursor->nOffset;
}

/* Helper function for cursor_from_virtual_coords() to find paragraph within tables */
static ME_Paragraph *pixel_pos_in_table_row( int x, int y, ME_Paragraph *para )
{
  ME_Cell *cell, *next_cell;

  assert( para->nFlags & MEPF_ROWSTART );
  cell = table_row_first_cell( para );
  assert( cell );

  /* find the cell we are in */
  while ((next_cell = cell_next( cell )) != NULL)
  {
    if (x < next_cell->pt.x)
    {
      para = cell_first_para( cell );
      /* Found the cell, but there might be multiple paragraphs in
       * the cell, so need to search down the cell for the paragraph. */
      while (cell == para_cell( para ))
      {
        if (y < para->pt.y + para->nHeight)
        {
          if (para->nFlags & MEPF_ROWSTART) return pixel_pos_in_table_row( x, y, para );
          else return para;
        }
        para = para_next( para );
      }
      /* Past the end of the cell, so go back to the last cell paragraph */
      return para_prev( para );
    }
    cell = next_cell;
  }
  /* Return table row delimiter */
  para = table_row_end( para );
  assert( para->nFlags & MEPF_ROWEND );
  assert( para->fmt.dwMask & PFM_TABLEROWDELIMITER );
  assert( para->fmt.wEffects & PFE_TABLEROWDELIMITER );
  return para;
}

static BOOL row_cursor( ME_TextEditor *editor, ME_Row *row, int x,
                        ME_Cursor *cursor )
{
    ME_Run *run, *last;
    BOOL exact = TRUE;

    if (x < row->pt.x)
    {
        x = row->pt.x;
        exact = FALSE;
    }

    run = row_first_run( row );
    assert( run );
    cursor->nOffset = 0;
    do
    {
        if (x >= run->pt.x && x < run->pt.x + run->nWidth)
        {
            cursor->nOffset = ME_CharFromPoint( editor,  x - run->pt.x, run, TRUE, TRUE );
            cursor->pRun = run_get_di( run );
            cursor->pPara = para_get_di( run->para );
            return exact;
        }
        last = run;
        run = row_next_run( row, run );
    } while (run);

    run = last;

    cursor->pRun = run_get_di( run );
    cursor->pPara = para_get_di( run->para );
    return FALSE;
}

/* Finds the run and offset from the pixel position.
 *
 * x & y are pixel positions in virtual coordinates into the rich edit control,
 * so client coordinates must first be adjusted by the scroll position.
 *
 * If final_eop is TRUE consider the final end-of-paragraph.
 *
 * returns TRUE if the result was exactly under the cursor, otherwise returns
 * FALSE, and result is set to the closest position to the coordinates.
 */
static BOOL cursor_from_virtual_coords( ME_TextEditor *editor, int x, int y,
                                        ME_Cursor *result, BOOL final_eop )
{
  ME_Paragraph *para = editor_first_para( editor );
  ME_Row *row = NULL, *next_row;
  BOOL isExact = TRUE;

  x -= editor->rcFormat.left;
  y -= editor->rcFormat.top;

  /* find paragraph */
  for (; para_next( para ); para = para_next( para ))
  {
    if (y < para->pt.y + para->nHeight)
    {
      if (para->nFlags & MEPF_ROWSTART)
        para = pixel_pos_in_table_row( x, y, para );
      y -= para->pt.y;
      row = para_first_row( para );
      break;
    }
    else if (para->nFlags & MEPF_ROWSTART)
    {
      para = table_row_end( para );
    }
  }
  /* find row */
  while (row)
  {
    if (y < row->pt.y + row->nHeight) break;
    next_row = row_next( row );
    if (!next_row) break;
    row = next_row;
  }

  if (!row && !final_eop && para_prev( para ))
  {
    /* The position is below the last paragraph, so the last row will be used
     * rather than the end of the text, so the x position will be used to
     * determine the offset closest to the pixel position. */
    isExact = FALSE;
    row = para_end_row( para_prev( para ) );
  }

  if (row) return row_cursor( editor, row, x, result ) && isExact;

  ME_SetCursorToEnd(editor, result, TRUE);
  return FALSE;
}


/* Sets the cursor to the position closest to the pixel position
 *
 * x & y are pixel positions in client coordinates.
 *
 * isExact will be set to TRUE if the run is directly under the pixel
 * position, FALSE if it not, unless isExact is set to NULL.
 *
 * return FALSE if outside client area and the cursor is not set,
 * otherwise TRUE is returned.
 */
BOOL ME_CharFromPos(ME_TextEditor *editor, int x, int y,
                    ME_Cursor *cursor, BOOL *isExact)
{
  RECT rc;
  BOOL bResult;

  ITextHost_TxGetClientRect(editor->texthost, &rc);
  if (x < 0 || y < 0 || x >= rc.right || y >= rc.bottom) {
    if (isExact) *isExact = FALSE;
    return FALSE;
  }
  x += editor->horz_si.nPos;
  y += editor->vert_si.nPos;
  bResult = cursor_from_virtual_coords( editor, x, y, cursor, FALSE );
  if (isExact) *isExact = bResult;
  return TRUE;
}



/* Extends the selection with a word, line, or paragraph selection type.
 *
 * The selection is anchored by editor->pCursors[2-3] such that the text
 * between the anchors will remain selected, and one end will be extended.
 *
 * editor->pCursors[0] should have the position to extend the selection to
 * before this function is called.
 *
 * Nothing will be done if editor->nSelectionType equals stPosition.
 */
static void ME_ExtendAnchorSelection(ME_TextEditor *editor)
{
  ME_Cursor tmp_cursor;
  int curOfs, anchorStartOfs, anchorEndOfs;
  if (editor->nSelectionType == stPosition || editor->nSelectionType == stDocument)
      return;
  curOfs = ME_GetCursorOfs(&editor->pCursors[0]);
  anchorStartOfs = ME_GetCursorOfs(&editor->pCursors[3]);
  anchorEndOfs = ME_GetCursorOfs(&editor->pCursors[2]);

  tmp_cursor = editor->pCursors[0];
  editor->pCursors[0] = editor->pCursors[2];
  editor->pCursors[1] = editor->pCursors[3];
  if (curOfs < anchorStartOfs)
  {
      /* Extend the left side of selection */
      editor->pCursors[1] = tmp_cursor;
      if (editor->nSelectionType == stWord)
          ME_MoveCursorWords(editor, &editor->pCursors[1], -1);
      else
      {
          ME_DisplayItem *pItem;
          ME_DIType searchType = ((editor->nSelectionType == stLine) ?
                                  diStartRowOrParagraph:diParagraph);
          pItem = ME_FindItemBack(editor->pCursors[1].pRun, searchType);
          editor->pCursors[1].pRun = ME_FindItemFwd(pItem, diRun);
          editor->pCursors[1].pPara = ME_GetParagraph(editor->pCursors[1].pRun);
          editor->pCursors[1].nOffset = 0;
      }
  }
  else if (curOfs >= anchorEndOfs)
  {
      /* Extend the right side of selection */
      editor->pCursors[0] = tmp_cursor;
      if (editor->nSelectionType == stWord)
          ME_MoveCursorWords(editor, &editor->pCursors[0], +1);
      else
      {
          ME_DisplayItem *pItem;
          ME_DIType searchType = ((editor->nSelectionType == stLine) ?
                                  diStartRowOrParagraphOrEnd:diParagraphOrEnd);
          pItem = ME_FindItemFwd(editor->pCursors[0].pRun, searchType);
          if (pItem->type == diTextEnd)
              editor->pCursors[0].pRun = ME_FindItemBack(pItem, diRun);
          else
              editor->pCursors[0].pRun = ME_FindItemFwd(pItem, diRun);
          editor->pCursors[0].pPara = ME_GetParagraph(editor->pCursors[0].pRun);
          editor->pCursors[0].nOffset = 0;
      }
  }
}

void ME_LButtonDown(ME_TextEditor *editor, int x, int y, int clickNum)
{
  ME_Cursor tmp_cursor;
  BOOL is_selection = FALSE, is_shift;

  editor->nUDArrowX = -1;

  x += editor->horz_si.nPos;
  y += editor->vert_si.nPos;

  tmp_cursor = editor->pCursors[0];
  is_selection = ME_IsSelection(editor);
  is_shift = GetKeyState(VK_SHIFT) < 0;

  cursor_from_virtual_coords( editor, x, y, &editor->pCursors[0], FALSE );

  if (x >= editor->rcFormat.left || is_shift)
  {
    if (clickNum > 1)
    {
      editor->pCursors[1] = editor->pCursors[0];
      if (is_shift) {
          if (x >= editor->rcFormat.left)
              ME_SelectByType(editor, stWord);
          else
              ME_SelectByType(editor, stParagraph);
      } else if (clickNum % 2 == 0) {
          ME_SelectByType(editor, stWord);
      } else {
          ME_SelectByType(editor, stParagraph);
      }
    }
    else if (!is_shift)
    {
      editor->nSelectionType = stPosition;
      editor->pCursors[1] = editor->pCursors[0];
    }
    else if (!is_selection)
    {
      editor->nSelectionType = stPosition;
      editor->pCursors[1] = tmp_cursor;
    }
    else if (editor->nSelectionType != stPosition)
    {
      ME_ExtendAnchorSelection(editor);
    }
  }
  else
  {
    if (clickNum < 2) {
        ME_SelectByType(editor, stLine);
    } else if (clickNum % 2 == 0 || is_shift) {
        ME_SelectByType(editor, stParagraph);
    } else {
        ME_SelectByType(editor, stDocument);
    }
  }
  ME_InvalidateSelection(editor);
  update_caret(editor);
  ME_SendSelChange(editor);
}

void ME_MouseMove(ME_TextEditor *editor, int x, int y)
{
  ME_Cursor tmp_cursor;

  if (editor->nSelectionType == stDocument)
      return;
  x += editor->horz_si.nPos;
  y += editor->vert_si.nPos;

  tmp_cursor = editor->pCursors[0];
  /* FIXME: do something with the return value of cursor_from_virtual_coords */
  cursor_from_virtual_coords( editor, x, y, &tmp_cursor, TRUE );

  ME_InvalidateSelection(editor);
  editor->pCursors[0] = tmp_cursor;
  ME_ExtendAnchorSelection(editor);

  if (editor->nSelectionType != stPosition &&
      memcmp(&editor->pCursors[1], &editor->pCursors[3], sizeof(ME_Cursor)))
  {
      /* The scroll the cursor towards the other end, since it was the one
       * extended by ME_ExtendAnchorSelection */
      ME_EnsureVisible(editor, &editor->pCursors[1]);
  } else {
      ME_EnsureVisible(editor, &editor->pCursors[0]);
  }

  ME_InvalidateSelection(editor);
  update_caret(editor);
  ME_SendSelChange(editor);
}

static int ME_GetXForArrow(ME_TextEditor *editor, ME_Cursor *pCursor)
{
  ME_Run *run = &pCursor->pRun->member.run;
  int x;

  if (editor->nUDArrowX != -1)
    x = editor->nUDArrowX;
  else
  {
    x = run->pt.x;
    x += ME_PointFromChar( editor, run, pCursor->nOffset, TRUE );
    editor->nUDArrowX = x;
  }
  return x;
}


static void
ME_MoveCursorLines(ME_TextEditor *editor, ME_Cursor *pCursor, int nRelOfs, BOOL extend)
{
  ME_DisplayItem *pRun = pCursor->pRun;
  ME_Paragraph *old_para = &pCursor->pPara->member.para, *new_para;
  ME_DisplayItem *pItem;
  int x = ME_GetXForArrow(editor, pCursor);

  if (nRelOfs == -1)
  {
    /* start of this row */
    pItem = ME_FindItemBack(pRun, diStartRow);
    assert(pItem);
    /* start of the previous row */
    pItem = ME_FindItemBack(pItem, diStartRow);
    if (!pItem) /* row not found */
    {
      if (extend)
        ME_SetCursorToStart(editor, pCursor);
      return;
    }
    new_para = &ME_GetParagraph(pItem)->member.para;
    if (old_para->nFlags & MEPF_ROWEND ||
        (para_cell( old_para ) && para_cell( old_para ) != para_cell( new_para )))
    {
      /* Brought out of a cell */
      new_para = para_prev( table_row_start( old_para ));
      if (!new_para) return; /* At the top, so don't go anywhere. */
      pItem = ME_FindItemFwd( para_get_di( new_para ), diStartRow);
    }
    if (new_para->nFlags & MEPF_ROWEND)
    {
      /* Brought into a table row */
      ME_Cell *cell = table_row_end_cell( new_para );
      while (x < cell->pt.x && cell_prev( cell ))
        cell = cell_prev( cell );
      if (cell_next( cell )) /* else - we are still at the end of the row */
        pItem = ME_FindItemBack( cell_get_di( cell_next( cell ) ), diStartRow );
    }
  }
  else
  {
    /* start of the next row */
    pItem = ME_FindItemFwd(pRun, diStartRow);
    if (!pItem) /* row not found */
    {
      if (extend)
        ME_SetCursorToEnd(editor, pCursor, TRUE);
      return;
    }
    new_para = &ME_GetParagraph(pItem)->member.para;
    if (old_para->nFlags & MEPF_ROWSTART ||
        (para_cell( old_para ) && para_cell( old_para ) != para_cell( new_para )))
    {
      /* Brought out of a cell */
      new_para = para_next( table_row_end( old_para ) );
      if (!para_next( new_para )) return; /* At the bottom, so don't go anywhere. */
      pItem = ME_FindItemFwd( para_get_di( new_para ), diStartRow );
    }
    if (new_para->nFlags & MEPF_ROWSTART)
    {
      /* Brought into a table row */
      ME_Cell *cell = table_row_first_cell( new_para );
      while (cell_next( cell ) && x >= cell_next( cell )->pt.x)
        cell = cell_next( cell );
      pItem = ME_FindItemFwd( cell_get_di( cell ), diStartRow );
    }
  }
  if (!pItem)
  {
    /* row not found - ignore */
    return;
  }
  row_cursor( editor, &pItem->member.row, x, pCursor );
  assert(pCursor->pRun);
  assert(pCursor->pRun->type == diRun);
}

static void ME_ArrowPageUp(ME_TextEditor *editor, ME_Cursor *pCursor)
{
  ME_DisplayItem *p = ME_FindItemFwd(editor->pBuffer->pFirst, diStartRow);

  if (editor->vert_si.nPos < p->member.row.nHeight)
  {
    ME_SetCursorToStart(editor, pCursor);
    /* Native clears seems to clear this x value on page up at the top
     * of the text, but not on page down at the end of the text.
     * Doesn't make sense, but we try to be bug for bug compatible. */
    editor->nUDArrowX = -1;
  } else {
    ME_DisplayItem *pRun = pCursor->pRun;
    ME_DisplayItem *pLast;
    int x, y, yd, yp;
    int yOldScrollPos = editor->vert_si.nPos;

    x = ME_GetXForArrow(editor, pCursor);

    p = ME_FindItemBack(pRun, diStartRowOrParagraph);
    assert(p->type == diStartRow);
    yp = ME_FindItemBack(p, diParagraph)->member.para.pt.y;
    y = yp + p->member.row.pt.y;

    ME_ScrollUp(editor, editor->sizeWindow.cy);
    /* Only move the cursor by the amount scrolled. */
    yd = y + editor->vert_si.nPos - yOldScrollPos;
    pLast = p;

    do {
      p = ME_FindItemBack(p, diStartRowOrParagraph);
      if (!p)
        break;
      if (p->type == diParagraph) { /* crossing paragraphs */
        if (p->member.para.prev_para == NULL)
          break;
        yp = p->member.para.prev_para->member.para.pt.y;
        continue;
      }
      y = yp + p->member.row.pt.y;
      if (y < yd)
        break;
      pLast = p;
    } while(1);

    row_cursor( editor, &pLast->member.row, x, pCursor );
  }
  assert(pCursor->pRun);
  assert(pCursor->pRun->type == diRun);
}

static void ME_ArrowPageDown(ME_TextEditor *editor, ME_Cursor *pCursor)
{
  ME_DisplayItem *pLast;
  int x, y;

  /* Find y position of the last row */
  pLast = editor->pBuffer->pLast;
  y = pLast->member.para.prev_para->member.para.pt.y
      + ME_FindItemBack(pLast, diStartRow)->member.row.pt.y;

  x = ME_GetXForArrow(editor, pCursor);

  if (editor->vert_si.nPos >= y - editor->sizeWindow.cy)
  {
    ME_SetCursorToEnd(editor, pCursor, FALSE);
  } else {
    ME_DisplayItem *pRun = pCursor->pRun;
    ME_DisplayItem *p;
    int yd, yp;
    int yOldScrollPos = editor->vert_si.nPos;

    p = ME_FindItemBack(pRun, diStartRowOrParagraph);
    assert(p->type == diStartRow);
    yp = ME_FindItemBack(p, diParagraph)->member.para.pt.y;
    y = yp + p->member.row.pt.y;

    /* For native richedit controls:
     * v1.0 - v3.1 can only scroll down as far as the scrollbar lets us
     * v4.1 can scroll past this position here. */
    ME_ScrollDown(editor, editor->sizeWindow.cy);
    /* Only move the cursor by the amount scrolled. */
    yd = y + editor->vert_si.nPos - yOldScrollPos;
    pLast = p;

    do {
      p = ME_FindItemFwd(p, diStartRowOrParagraph);
      if (!p)
        break;
      if (p->type == diParagraph) {
        yp = p->member.para.pt.y;
        continue;
      }
      y = yp + p->member.row.pt.y;
      if (y >= yd)
        break;
      pLast = p;
    } while(1);

    row_cursor( editor, &pLast->member.row, x, pCursor );
  }
  assert(pCursor->pRun);
  assert(pCursor->pRun->type == diRun);
}

static void ME_ArrowHome( ME_TextEditor *editor, ME_Cursor *cursor )
{
    ME_Row *row = row_from_cursor( cursor );

    row_first_cursor( row, cursor );
}

static void ME_ArrowCtrlHome(ME_TextEditor *editor, ME_Cursor *pCursor)
{
  ME_SetCursorToStart(editor, pCursor);
}

static void ME_ArrowEnd( ME_TextEditor *editor, ME_Cursor *cursor )
{
    ME_Row *row = row_from_cursor( cursor );

    row_end_cursor( row, cursor, FALSE );
}

static void ME_ArrowCtrlEnd(ME_TextEditor *editor, ME_Cursor *pCursor)
{
  ME_SetCursorToEnd(editor, pCursor, FALSE);
}

BOOL ME_IsSelection(ME_TextEditor *editor)
{
  return editor->pCursors[0].pRun != editor->pCursors[1].pRun ||
         editor->pCursors[0].nOffset != editor->pCursors[1].nOffset;
}

void ME_DeleteSelection(ME_TextEditor *editor)
{
  int from, to;
  int nStartCursor = ME_GetSelectionOfs(editor, &from, &to);
  int nEndCursor = nStartCursor ^ 1;
  ME_DeleteTextAtCursor(editor, nStartCursor, to - from);
  editor->pCursors[nEndCursor] = editor->pCursors[nStartCursor];
}

ME_Style *ME_GetSelectionInsertStyle(ME_TextEditor *editor)
{
    return style_get_insert_style( editor, editor->pCursors );
}

void ME_SendSelChange(ME_TextEditor *editor)
{
  SELCHANGE sc;

  sc.nmhdr.hwndFrom = NULL;
  sc.nmhdr.idFrom = 0;
  sc.nmhdr.code = EN_SELCHANGE;
  ME_GetSelectionOfs(editor, &sc.chrg.cpMin, &sc.chrg.cpMax);
  sc.seltyp = SEL_EMPTY;
  if (sc.chrg.cpMin != sc.chrg.cpMax)
    sc.seltyp |= SEL_TEXT;
  if (sc.chrg.cpMin < sc.chrg.cpMax+1) /* what were RICHEDIT authors thinking ? */
    sc.seltyp |= SEL_MULTICHAR;

  if (sc.chrg.cpMin != editor->notified_cr.cpMin || sc.chrg.cpMax != editor->notified_cr.cpMax)
  {
    ME_ClearTempStyle(editor);

    editor->notified_cr = sc.chrg;

    if (editor->nEventMask & ENM_SELCHANGE)
    {
      TRACE("cpMin=%d cpMax=%d seltyp=%d (%s %s)\n",
            sc.chrg.cpMin, sc.chrg.cpMax, sc.seltyp,
            (sc.seltyp & SEL_TEXT) ? "SEL_TEXT" : "",
            (sc.seltyp & SEL_MULTICHAR) ? "SEL_MULTICHAR" : "");
      ITextHost_TxNotify(editor->texthost, sc.nmhdr.code, &sc);
    }
  }
}

BOOL
ME_ArrowKey(ME_TextEditor *editor, int nVKey, BOOL extend, BOOL ctrl)
{
  int nCursor = 0;
  ME_Cursor *p = &editor->pCursors[nCursor];
  ME_Cursor tmp_curs = *p;
  BOOL success = FALSE;

  ME_CheckCharOffsets(editor);
  switch(nVKey) {
    case VK_LEFT:
      if (ctrl)
        success = ME_MoveCursorWords(editor, &tmp_curs, -1);
      else
        success = ME_MoveCursorChars(editor, &tmp_curs, -1, extend);
      break;
    case VK_RIGHT:
      if (ctrl)
        success = ME_MoveCursorWords(editor, &tmp_curs, +1);
      else
        success = ME_MoveCursorChars(editor, &tmp_curs, +1, extend);
      break;
    case VK_UP:
      ME_MoveCursorLines(editor, &tmp_curs, -1, extend);
      break;
    case VK_DOWN:
      ME_MoveCursorLines(editor, &tmp_curs, +1, extend);
      break;
    case VK_PRIOR:
      ME_ArrowPageUp(editor, &tmp_curs);
      break;
    case VK_NEXT:
      ME_ArrowPageDown(editor, &tmp_curs);
      break;
    case VK_HOME: {
      if (ctrl)
        ME_ArrowCtrlHome(editor, &tmp_curs);
      else
        ME_ArrowHome(editor, &tmp_curs);
      break;
    }
    case VK_END:
      if (ctrl)
        ME_ArrowCtrlEnd(editor, &tmp_curs);
      else
        ME_ArrowEnd(editor, &tmp_curs);
      break;
  }

  if (!extend)
    editor->pCursors[1] = tmp_curs;
  *p = tmp_curs;

  ME_InvalidateSelection(editor);
  ME_Repaint(editor);
  hide_caret(editor);
  ME_EnsureVisible(editor, &tmp_curs);
  update_caret(editor);
  ME_SendSelChange(editor);
  return success;
}
