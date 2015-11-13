/**
 * @copyright
 * ====================================================================
 *    Licensed to the Apache Software Foundation (ASF) under one
 *    or more contributor license agreements.  See the NOTICE file
 *    distributed with this work for additional information
 *    regarding copyright ownership.  The ASF licenses this file
 *    to you under the Apache License, Version 2.0 (the
 *    "License"); you may not use this file except in compliance
 *    with the License.  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing,
 *    software distributed under the License is distributed on an
 *    "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 *    KIND, either express or implied.  See the License for the
 *    specific language governing permissions and limitations
 *    under the License.
 * ====================================================================
 * @endcopyright
 *
 * @file svnmover.h
 * @brief Concept Demo for Move Tracking and Branching
 */

#ifndef SVNMOVER_H
#define SVNMOVER_H

#include "svn_types.h"
#include "svn_client.h"
#include "svn_ra.h"

#include "private/svn_branch.h"

/* Decide whether to use the 'linenoise' library for command-line input
   editing and completion. */
#ifndef WIN32
#define HAVE_LINENOISE
#endif


#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */


/* Like apr_hash_overlay() and apr_hash_merge() except allocating the
 * result in the pool of the first input hash (OVERLAY and H1 respectively).
 *
 * When APR pool debugging is enabled, these functions require that the
 * result pool does not have greater lifetime than the inputs, so passing
 * an arbitrary result pool doesn't work well.
 *
 * If the second hash's pool has a shorter lifetime than that of the first,
 * you're out of luck.
 */
#define hash_overlay(overlay, base) \
  apr_hash_overlay(apr_hash_pool_get(overlay), overlay, base)
#define hash_merge(overlay, h1) \
  apr_hash_merge(apr_hash_pool_get(overlay), h1, h2, merger, data)


/* Display PROMPT_STR, read a line of text, and set *RESULT to that line.
 *
 * The interface here is similar to svn_cmdline_prompt_user2().
 */
svn_error_t *
svnmover_prompt_user(const char **result,
                     const char *prompt_str,
                     apr_pool_t *pool);

/* Print a notification. */
__attribute__((format(printf, 1, 2)))
void
svnmover_notify(const char *fmt,
                ...);

/* Print a verbose notification: in 'quiet' mode, don't print it. */
__attribute__((format(printf, 1, 2)))
void
svnmover_notify_v(const char *fmt,
                  ...);

typedef struct svnmover_wc_version_t
{
  svn_revnum_t revision;  /* always SVN_INVALID_REVNUM in working version */
  svn_branch__state_t *branch;
} svnmover_wc_version_t;

/* Return (left, right) pairs of element content that differ between
 * subtrees LEFT and RIGHT.
 *
 * Set *DIFF_P to a hash of (eid -> (svn_element__content_t *)[2]).
 */
svn_error_t *
svnmover_element_differences(apr_hash_t **diff_p,
                             const svn_element__tree_t *left,
                             const svn_element__tree_t *right,
                             apr_pool_t *result_pool,
                             apr_pool_t *scratch_pool);

/*  */
typedef struct conflict_storage_t conflict_storage_t;

typedef struct svnmover_wc_t
{
  apr_pool_t *pool;
  const char *repos_root_url;
  /*const char *anchor_repos_relpath;*/
  svn_revnum_t head_revision;

  svn_ra_session_t *ra_session;
  svn_branch__txn_t *edit_txn;
  conflict_storage_t *conflicts;

  /* Base and working versions. */
  svnmover_wc_version_t *base, *working;

  /* Textual list of commands the commands that were executed, suitable
     for putting in a log message */
  const char *list_of_commands;

  svn_client_ctx_t *ctx;

} svnmover_wc_t;

struct conflict_storage_t
{
  /* Single-element conflicts */
  /* (eid -> element_merge3_conflict_t) */
  apr_hash_t *single_element_conflicts;

  /* Name-clash conflicts */
  /* ("%{parent_eid}d/%{name}s" -> name_clash_conflict_t) */
  apr_hash_t *name_clash_conflicts;

  /* Cycle conflicts */
  /* (eid -> cycle_conflict_t) */
  apr_hash_t *cycle_conflicts;

  /* Orphan conflicts */
  /* (eid -> orphan_conflict_t) */
  apr_hash_t *orphan_conflicts;
};

/* Merge SRC into TGT, using the common ancestor YCA.
 *
 * Merge the two sets of changes: YCA -> SRC and YCA -> TGT, applying
 * the result to the transaction at TGT.
 *
 * If conflicts arise, return them in *CONFLICT_STORAGE_P; otherwise set
 * that to null.
 *
 * SRC, TGT and YCA must be existing and corresponding (same EID) elements.
 *
 * None of SRC, TGT and YCA is a subbranch root element.
 *
 * Nested subbranches will also be merged.
 */
svn_error_t *
svnmover_branch_merge(svn_branch__txn_t *edit_txn,
                      conflict_storage_t **conflict_storage_p,
                      svn_branch__el_rev_id_t *src,
                      svn_branch__el_rev_id_t *tgt,
                      svn_branch__el_rev_id_t *yca,
                      apr_pool_t *result_pool,
                      apr_pool_t *scratch_pool);

/*  */
svn_error_t *
svnmover_display_conflicts(conflict_storage_t *conflict_storage,
                           apr_pool_t *scratch_pool);

svn_error_t *
svnmover_conflict_resolved(conflict_storage_t *conflicts,
                           const char *id_string,
                           apr_pool_t *scratch_pool);

/*  */
svn_boolean_t
svnmover_any_conflicts(const conflict_storage_t *conflicts);


#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* SVNMOVER_H */
