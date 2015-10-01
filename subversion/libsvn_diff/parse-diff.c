#include "private/svn_diff_private.h"
#include "diff.h"

#include "svn_private_config.h"


  /* Did we see a 'file does not end with eol' marker in this hunk? */
  svn_boolean_t original_no_final_eol;
  svn_boolean_t modified_no_final_eol;
};

struct svn_diff_binary_patch_t {
  /* The patch this hunk belongs to. */
  svn_patch_t *patch;

  /* APR file handle to the patch file this hunk came from. */
  apr_file_t *apr_file;

  /* Offsets inside APR_FILE representing the location of the patch */
  apr_off_t src_start;
  apr_off_t src_end;
  svn_filesize_t src_filesize; /* Expanded/final size */

  /* Offsets inside APR_FILE representing the location of the patch */
  apr_off_t dst_start;
  apr_off_t dst_end;
  svn_filesize_t dst_filesize; /* Expanded/final size */
/* Common guts of svn_diff_hunk__create_adds_single_line() and
 * svn_diff_hunk__create_deletes_single_line().
 *
 * ADD is TRUE if adding and FALSE if deleting.
 */
static svn_error_t *
add_or_delete_single_line(svn_diff_hunk_t **hunk_out,
                          const char *line,
                          svn_patch_t *patch,
                          svn_boolean_t add,
                          apr_pool_t *result_pool,
                          apr_pool_t *scratch_pool)
{
  svn_diff_hunk_t *hunk = apr_palloc(result_pool, sizeof(*hunk));
  static const char *hunk_header[] = { "@@ -1 +0,0 @@\n", "@@ -0,0 +1 @@\n" };
  const apr_size_t header_len = strlen(hunk_header[add]);
  const apr_size_t len = strlen(line);
  const apr_size_t end = header_len + (1 + len); /* The +1 is for the \n. */
  svn_stringbuf_t *buf = svn_stringbuf_create_ensure(end + 1, scratch_pool);

  hunk->patch = patch;

  /* hunk->apr_file is created below. */

  hunk->diff_text_range.start = header_len;
  hunk->diff_text_range.current = header_len;

  if (add)
    {
      hunk->original_text_range.start = 0; /* There's no "original" text. */
      hunk->original_text_range.current = 0;
      hunk->original_text_range.end = 0;
      hunk->original_no_final_eol = FALSE;

      hunk->modified_text_range.start = header_len;
      hunk->modified_text_range.current = header_len;
      hunk->modified_text_range.end = end;
      hunk->modified_no_final_eol = TRUE;

      hunk->original_start = 0;
      hunk->original_length = 0;

      hunk->modified_start = 1;
      hunk->modified_length = 1;
    }
  else /* delete */
    {
      hunk->original_text_range.start = header_len;
      hunk->original_text_range.current = header_len;
      hunk->original_text_range.end = end;
      hunk->original_no_final_eol = TRUE;

      hunk->modified_text_range.start = 0; /* There's no "original" text. */
      hunk->modified_text_range.current = 0;
      hunk->modified_text_range.end = 0;
      hunk->modified_no_final_eol = FALSE;

      hunk->original_start = 1;
      hunk->original_length = 1;

      hunk->modified_start = 0;
      hunk->modified_length = 0; /* setting to '1' works too */
    }

  hunk->leading_context = 0;
  hunk->trailing_context = 0;

  /* Create APR_FILE and put just a hunk in it (without a diff header).
   * Save the offset of the last byte of the diff line. */
  svn_stringbuf_appendbytes(buf, hunk_header[add], header_len);
  svn_stringbuf_appendbyte(buf, add ? '+' : '-');
  svn_stringbuf_appendbytes(buf, line, len);
  svn_stringbuf_appendbyte(buf, '\n');
  svn_stringbuf_appendcstr(buf, "\\ No newline at end of hunk\n");

  hunk->diff_text_range.end = buf->len;

  SVN_ERR(svn_io_open_unique_file3(&hunk->apr_file, NULL /* filename */,
                                   NULL /* system tempdir */,
                                   svn_io_file_del_on_pool_cleanup,
                                   result_pool, scratch_pool));
  SVN_ERR(svn_io_file_write_full(hunk->apr_file,
                                 buf->data, buf->len,
                                 NULL, scratch_pool));
  /* No need to seek. */

  *hunk_out = hunk;
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_adds_single_line(svn_diff_hunk_t **hunk_out,
                                       const char *line,
                                       svn_patch_t *patch,
                                       apr_pool_t *result_pool,
                                       apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch, TRUE,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

svn_error_t *
svn_diff_hunk__create_deletes_single_line(svn_diff_hunk_t **hunk_out,
                                          const char *line,
                                          svn_patch_t *patch,
                                          apr_pool_t *result_pool,
                                          apr_pool_t *scratch_pool)
{
  SVN_ERR(add_or_delete_single_line(hunk_out, line, patch, FALSE,
                                    result_pool, scratch_pool));
  return SVN_NO_ERROR;
}

/* Baton for the base85 stream implementation */
struct base85_baton_t
{
  apr_file_t *file;
  apr_pool_t *iterpool;
  char buffer[52];        /* Bytes on current line */
  apr_off_t next_pos;     /* Start position of next line */
  apr_off_t end_pos;      /* Position after last line */
  apr_size_t buf_size;    /* Bytes available (52 unless at eof) */
  apr_size_t buf_pos;     /* Bytes in linebuffer */
  svn_boolean_t done;     /* At eof? */
};

/* Implements svn_read_fn_t for the base85 read stream */
static svn_error_t *
read_handler_base85(void *baton, char *buffer, apr_size_t *len)
{
  struct base85_baton_t *b85b = baton;
  apr_pool_t *iterpool = b85b->iterpool;
  apr_size_t remaining = *len;
  char *dest = buffer;

  svn_pool_clear(iterpool);

  if (b85b->done)
    {
      *len = 0;
      return SVN_NO_ERROR;
    }

  while (remaining && (b85b->buf_size > b85b->buf_pos
                       || b85b->next_pos < b85b->end_pos))
    {
      svn_stringbuf_t *line;
      svn_boolean_t at_eof;

      apr_size_t available = b85b->buf_size - b85b->buf_pos;
      if (available)
        {
          apr_size_t n = (remaining < available) ? remaining : available;

          memcpy(dest, b85b->buffer + b85b->buf_pos, n);
          dest += n;
          remaining -= n;
          b85b->buf_pos += n;

          if (!remaining)
            return SVN_NO_ERROR; /* *len = OK */
        }

      if (b85b->next_pos >= b85b->end_pos)
        break; /* At EOF */
      SVN_ERR(svn_io_file_seek(b85b->file, APR_SET, &b85b->next_pos,
                               iterpool));
      SVN_ERR(svn_io_file_readline(b85b->file, &line, NULL, &at_eof,
                                   APR_SIZE_MAX, iterpool, iterpool));
      if (at_eof)
        b85b->next_pos = b85b->end_pos;
      else
        {
          b85b->next_pos = 0;
          SVN_ERR(svn_io_file_seek(b85b->file, APR_CUR, &b85b->next_pos,
                                   iterpool));
        }

      if (line->len && line->data[0] >= 'A' && line->data[0] <= 'Z')
        b85b->buf_size = line->data[0] - 'A' + 1;
      else if (line->len && line->data[0] >= 'a' && line->data[0] <= 'z')
        b85b->buf_size = line->data[0] - 'a' + 26 + 1;
      else
        return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                                _("Unexpected data in base85 section"));

      if (b85b->buf_size < 52)
        b85b->next_pos = b85b->end_pos; /* Handle as EOF */

      SVN_ERR(svn_diff__base85_decode_line(b85b->buffer, b85b->buf_size,
                                           line->data + 1, line->len - 1,
                                           iterpool));
      b85b->buf_pos = 0;
    }

  *len -= remaining;
  b85b->done = TRUE;

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t for the base85 read stream */
static svn_error_t *
close_handler_base85(void *baton)
{
  struct base85_baton_t *b85b = baton;

  svn_pool_destroy(b85b->iterpool);

  return SVN_NO_ERROR;
}

/* Gets a stream that reads decoded base85 data from a segment of a file.
   The current implementation might assume that both start_pos and end_pos
   are located at line boundaries. */
static svn_stream_t *
get_base85_data_stream(apr_file_t *file,
                       apr_off_t start_pos,
                       apr_off_t end_pos,
                       apr_pool_t *result_pool)
{
  struct base85_baton_t *b85b = apr_pcalloc(result_pool, sizeof(*b85b));
  svn_stream_t *base85s = svn_stream_create(b85b, result_pool);

  b85b->file = file;
  b85b->iterpool = svn_pool_create(result_pool);
  b85b->next_pos = start_pos;
  b85b->end_pos = end_pos;

  svn_stream_set_read2(base85s, NULL /* only full read support */,
                       read_handler_base85);
  svn_stream_set_close(base85s, close_handler_base85);
  return base85s;
}

/* Baton for the length verification stream functions */
struct length_verify_baton_t
{
  svn_stream_t *inner;
  svn_filesize_t remaining;
};

/* Implements svn_read_fn_t for the length verification stream */
static svn_error_t *
read_handler_length_verify(void *baton, char *buffer, apr_size_t *len)
{
  struct length_verify_baton_t *lvb = baton;
  apr_size_t requested_len = *len;

  SVN_ERR(svn_stream_read_full(lvb->inner, buffer, len));

  if (*len > lvb->remaining)
    return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                            _("Base85 data expands to longer than declared "
                              "filesize"));
  else if (requested_len > *len && *len != lvb->remaining)
    return svn_error_create(SVN_ERR_DIFF_UNEXPECTED_DATA, NULL,
                            _("Base85 data expands to smaller than declared "
                              "filesize"));

  lvb->remaining -= *len;

  return SVN_NO_ERROR;
}

/* Implements svn_close_fn_t for the length verification stream */
static svn_error_t *
close_handler_length_verify(void *baton)
{
  struct length_verify_baton_t *lvb = baton;

  return svn_error_trace(svn_stream_close(lvb->inner));
}

/* Gets a stream that verifies on reads that the inner stream is exactly
   of the specified length */
static svn_stream_t *
get_verify_length_stream(svn_stream_t *inner,
                         svn_filesize_t expected_size,
                         apr_pool_t *result_pool)
{
  struct length_verify_baton_t *lvb = apr_palloc(result_pool, sizeof(*lvb));
  svn_stream_t *len_stream = svn_stream_create(lvb, result_pool);

  lvb->inner = inner;
  lvb->remaining = expected_size;

  svn_stream_set_read2(len_stream, NULL /* only full read support */,
                       read_handler_length_verify);
  svn_stream_set_close(len_stream, close_handler_length_verify);

  return len_stream;
}

svn_stream_t *
svn_diff_get_binary_diff_original_stream(const svn_diff_binary_patch_t *bpatch,
                                         apr_pool_t *result_pool)
{
  svn_stream_t *s = get_base85_data_stream(bpatch->apr_file, bpatch->src_start,
                                           bpatch->src_end, result_pool);

  s = svn_stream_compressed(s, result_pool);

  /* ### If we (ever) want to support the DELTA format, then we should hook the
         undelta handling here */

  return get_verify_length_stream(s, bpatch->src_filesize, result_pool);
}

svn_stream_t *
svn_diff_get_binary_diff_result_stream(const svn_diff_binary_patch_t *bpatch,
                                       apr_pool_t *result_pool)
{
  svn_stream_t *s = get_base85_data_stream(bpatch->apr_file, bpatch->dst_start,
                                           bpatch->dst_end, result_pool);

  s = svn_stream_compressed(s, result_pool);

  /* ### If we (ever) want to support the DELTA format, then we should hook the
  undelta handling here */

  return get_verify_length_stream(s, bpatch->dst_filesize, result_pool);
}

 * is being read. NO_FINAL_EOL declares if the hunk contains a no final
 * EOL marker.
                                   svn_boolean_t no_final_eol,
  const char *eol_p;

  if (!eol)
    eol = &eol_p;
      *eol = NULL;
      *eol = NULL;
      /* Return the line as-is. Handle as a chopped leading spaces */
  if (!filtered && *eof && !*eol && !no_final_eol && *str->data)
    {
      /* Ok, we miss a final EOL in the patch file, but didn't see a
         no eol marker line.

         We should report that we had an EOL or the patch code will
         misbehave (and it knows nothing about no eol markers) */

      if (eol != &eol_p)
        {
          apr_off_t start = 0;

          SVN_ERR(svn_io_file_seek(file, APR_SET, &start, scratch_pool));

          SVN_ERR(svn_io_file_readline(file, &str, eol, NULL, APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);
        }

      *eof = FALSE;
      /* Fall through to seek back to the right location */
    }
                                       hunk->patch->reverse
                                          ? hunk->modified_no_final_eol
                                          : hunk->original_no_final_eol,
                                       hunk->patch->reverse
                                          ? hunk->original_no_final_eol
                                          : hunk->modified_no_final_eol,
  const char *eol_p;

  if (!eol)
    eol = &eol_p;
      *eol = NULL;

  if (*eof && !*eol && *line->data)
    {
      /* Ok, we miss a final EOL in the patch file, but didn't see a
          no eol marker line.

          We should report that we had an EOL or the patch code will
          misbehave (and it knows nothing about no eol markers) */

      if (eol != &eol_p)
        {
          /* Lets pick the first eol we find in our patch file */
          apr_off_t start = 0;
          svn_stringbuf_t *str;

          SVN_ERR(svn_io_file_seek(hunk->apr_file, APR_SET, &start,
                                   scratch_pool));

          SVN_ERR(svn_io_file_readline(hunk->apr_file, &str, eol, NULL,
                                       APR_SIZE_MAX,
                                       scratch_pool, scratch_pool));

          /* Every patch file that has hunks has at least one EOL*/
          SVN_ERR_ASSERT(*eol != NULL);
        }

      *eof = FALSE;

      /* Fall through to seek back to the right location */
    }

  svn_boolean_t original_no_final_eol = FALSE;
  svn_boolean_t modified_no_final_eol = FALSE;
          /* Set for the type and context by using != the other type */
          if (last_line_type != modified_line)
            original_no_final_eol = TRUE;
          if (last_line_type != original_line)
            modified_no_final_eol = TRUE;
                *prop_operation = (patch->reverse ? svn_diff_op_deleted
                                                  : svn_diff_op_added);
                *prop_operation = (patch->reverse ? svn_diff_op_added
                                                  : svn_diff_op_deleted);
      (*hunk)->original_no_final_eol = original_no_final_eol;
      (*hunk)->modified_no_final_eol = modified_no_final_eol;
   state_start,             /* initial */
   state_git_diff_seen,     /* diff --git */
   state_git_tree_seen,     /* a tree operation, rather than content change */
   state_git_minus_seen,    /* --- /dev/null; or --- a/ */
   state_git_plus_seen,     /* +++ /dev/null; or +++ a/ */
   state_old_mode_seen,     /* old mode 100644 */
   state_git_mode_seen,     /* new mode 100644 */
   state_move_from_seen,    /* rename from foo.c */
   state_copy_from_seen,    /* copy from foo.c */
   state_minus_seen,        /* --- foo.c */
   state_unidiff_found,     /* valid start of a regular unidiff header */
   state_git_header_found,  /* valid start of a --git diff header */
   state_binary_patch_found /* valid start of binary patch */
/* Helper for git_old_mode() and git_new_mode().  Translate the git
 * file mode MODE_STR into a binary "executable?" notion EXECUTABLE_P. */
static svn_error_t *
parse_bits_into_executability(svn_tristate_t *executable_p,
                              const char *mode_str)
{
  apr_uint64_t mode;
  SVN_ERR(svn_cstring_strtoui64(&mode, mode_str,
                                0 /* min */,
                                0777777 /* max: six octal digits */,
                                010 /* radix (octal) */));

  /* Note: 0644 and 0755 are the only modes that can occur for plain files.
   * We deliberately choose to parse only those values: we are strict in what
   * we accept _and_ in what we produce.
   *
   * (Having said that, though, we could consider relaxing the parser to also
   * map
   *     (mode & 0111) == 0000 -> svn_tristate_false
   *     (mode & 0111) == 0111 -> svn_tristate_true
   *        [anything else]    -> svn_tristate_unknown
   * .)
   */

  switch (mode & 0777)
    {
      case 0644:
        *executable_p = svn_tristate_false;
        break;

      case 0755:
        *executable_p = svn_tristate_true;
        break;

      default:
        /* Ignore unknown values. */
        *executable_p = svn_tristate_unknown;
        break;
    }

  return SVN_NO_ERROR;
}

/* Parse the 'old mode ' line of a git extended unidiff. */
static svn_error_t *
git_old_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_bits_into_executability(&patch->old_executable_p,
                                        line + STRLEN_LITERAL("old mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->old_executable_p != svn_tristate_unknown);
#endif

  *new_state = state_old_mode_seen;
  return SVN_NO_ERROR;
}

/* Parse the 'new mode ' line of a git extended unidiff. */
static svn_error_t *
git_new_mode(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  SVN_ERR(parse_bits_into_executability(&patch->new_executable_p,
                                        line + STRLEN_LITERAL("new mode ")));

#ifdef SVN_DEBUG
  /* If this assert trips, the "old mode" is neither ...644 nor ...755 . */
  SVN_ERR_ASSERT(patch->new_executable_p != svn_tristate_unknown);
#endif

  /* Don't touch patch->operation. */

  *new_state = state_git_mode_seen;
  return SVN_NO_ERROR;
}

  SVN_ERR(
    parse_bits_into_executability(&patch->new_executable_p,
                                  line + STRLEN_LITERAL("new file mode ")));

  SVN_ERR(
    parse_bits_into_executability(&patch->old_executable_p,
                                  line + STRLEN_LITERAL("deleted file mode ")));

/* Parse the 'GIT binary patch' header */
static svn_error_t *
binary_patch_start(enum parse_state *new_state, char *line, svn_patch_t *patch,
             apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  patch->operation = svn_diff_op_modified;

  *new_state = state_binary_patch_found;
  return SVN_NO_ERROR;
}


static svn_error_t *
parse_binary_patch(svn_patch_t *patch, apr_file_t *apr_file,
                   svn_boolean_t reverse,
                   apr_pool_t *result_pool, apr_pool_t *scratch_pool)
{
  apr_pool_t *iterpool = svn_pool_create(scratch_pool);
  apr_off_t pos, last_line;
  svn_stringbuf_t *line;
  svn_boolean_t eof = FALSE;
  svn_diff_binary_patch_t *bpatch = apr_pcalloc(result_pool, sizeof(*bpatch));
  svn_boolean_t in_blob = FALSE;
  svn_boolean_t in_src = FALSE;

  bpatch->apr_file = apr_file;

  patch->operation = svn_diff_op_modified;
  patch->prop_patches = apr_hash_make(result_pool);

  pos = 0;
  SVN_ERR(svn_io_file_seek(apr_file, APR_CUR, &pos, scratch_pool));

  while (!eof)
    {
      last_line = pos;
      SVN_ERR(svn_io_file_readline(apr_file, &line, NULL, &eof, APR_SIZE_MAX,
                               iterpool, iterpool));

      /* Update line offset for next iteration. */
      pos = 0;
      SVN_ERR(svn_io_file_seek(apr_file, APR_CUR, &pos, iterpool));

      if (in_blob)
        {
          char c = line->data[0];

          /* 66 = len byte + (52/4*5) chars */
          if (((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
              && line->len <= 66
              && !strchr(line->data, ':')
              && !strchr(line->data, ' '))
            {
              /* One more blop line */
              if (in_src)
                bpatch->src_end = pos;
              else
                bpatch->dst_end = pos;
            }
          else if (svn_stringbuf_first_non_whitespace(line) < line->len
                   && !(in_src && bpatch->src_start < last_line))
            {
              break; /* Bad patch */
            }
          else if (in_src)
            {
              patch->binary_patch = bpatch; /* SUCCESS! */
              break; 
            }
          else
            {
              in_blob = FALSE;
              in_src = TRUE;
            }
        }
      else if (starts_with(line->data, "literal "))
        {
          apr_uint64_t expanded_size;
          svn_error_t *err = svn_cstring_strtoui64(&expanded_size,
                                                   &line->data[8],
                                                   0, APR_UINT64_MAX, 10);

          if (err)
            {
              svn_error_clear(err);
              break;
            }

          if (in_src)
            {
              bpatch->src_start = pos;
              bpatch->src_filesize = expanded_size;
            }
          else
            {
              bpatch->dst_start = pos;
              bpatch->dst_filesize = expanded_size;
            }
          in_blob = TRUE;
        }
      else
        break; /* We don't support GIT deltas (yet) */
    }
  svn_pool_destroy(iterpool);

  if (!eof)
    /* Rewind to the start of the line just read, so subsequent calls
     * don't end up skipping the line. It may contain a patch or hunk header.*/
    SVN_ERR(svn_io_file_seek(apr_file, APR_SET, &last_line, scratch_pool));
  else if (in_src
           && ((bpatch->src_end > bpatch->src_start) || !bpatch->src_filesize))
    {
      patch->binary_patch = bpatch; /* SUCCESS */
    }

  /* Reverse patch if requested */
  if (reverse && patch->binary_patch)
    {
      apr_off_t tmp_start = bpatch->src_start;
      apr_off_t tmp_end = bpatch->src_end;
      svn_filesize_t tmp_filesize = bpatch->src_filesize;

      bpatch->src_start = bpatch->dst_start;
      bpatch->src_end = bpatch->dst_end;
      bpatch->src_filesize = bpatch->dst_filesize;

      bpatch->dst_start = tmp_start;
      bpatch->dst_end = tmp_end;
      bpatch->dst_filesize = tmp_filesize;
    }

  return SVN_NO_ERROR;
}

  {"--- ",              state_start,            diff_minus},
  {"+++ ",              state_minus_seen,       diff_plus},

  {"diff --git",        state_start,            git_start},
  {"--- a/",            state_git_diff_seen,    git_minus},
  {"--- a/",            state_git_mode_seen,    git_minus},
  {"--- a/",            state_git_tree_seen,    git_minus},
  {"--- /dev/null",     state_git_mode_seen,    git_minus},
  {"--- /dev/null",     state_git_tree_seen,    git_minus},
  {"+++ b/",            state_git_minus_seen,   git_plus},
  {"+++ /dev/null",     state_git_minus_seen,   git_plus},

  {"old mode ",         state_git_diff_seen,    git_old_mode},
  {"new mode ",         state_old_mode_seen,    git_new_mode},
  {"rename from ",      state_git_diff_seen,    git_move_from},
  {"rename from ",      state_git_mode_seen,    git_move_from},
  {"rename to ",        state_move_from_seen,   git_move_to},
  {"copy from ",        state_git_diff_seen,    git_copy_from},
  {"copy from ",        state_git_mode_seen,    git_copy_from},
  {"copy to ",          state_copy_from_seen,   git_copy_to},
  {"new file ",         state_git_diff_seen,    git_new_file},
  {"deleted file ",     state_git_diff_seen,    git_deleted_file},
  {"GIT binary patch",  state_git_diff_seen,    binary_patch_start},
  {"GIT binary patch",  state_git_tree_seen,    binary_patch_start},
  patch->old_executable_p = svn_tristate_unknown;
  patch->new_executable_p = svn_tristate_unknown;
      if (state == state_unidiff_found
          || state == state_git_header_found
          || state == state_binary_patch_found)
      else if ((state == state_git_tree_seen || state == state_git_mode_seen)
               && line_after_tree_header_read
               && !valid_header_line)
      else if (state == state_git_tree_seen
               || state == state_git_mode_seen)
      svn_tristate_t ts_tmp;


      switch (patch->operation)
        {
          case svn_diff_op_added:
            patch->operation = svn_diff_op_deleted;
            break;
          case svn_diff_op_deleted:
            patch->operation = svn_diff_op_added;
            break;

          /* ### case svn_diff_op_copied:
             ### case svn_diff_op_moved:*/

          case svn_diff_op_modified:
            break; /* Stays modify */
        }

      ts_tmp = patch->old_executable_p;
      patch->old_executable_p = patch->new_executable_p;
      patch->new_executable_p = ts_tmp;
    {
      if (state == state_binary_patch_found)
        {
          SVN_ERR(parse_binary_patch(patch, patch_file->apr_file, reverse,
                                     result_pool, iterpool));
          /* And fall through in property parsing */
        }

      SVN_ERR(parse_hunks(patch, patch_file->apr_file, ignore_whitespace,
                          result_pool, iterpool));
    }
  if (patch && patch->hunks)