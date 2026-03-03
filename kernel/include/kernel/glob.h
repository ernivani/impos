#ifndef _KERNEL_GLOB_H
#define _KERNEL_GLOB_H

/* Match a glob pattern against a string.
 * Supports: * (any sequence), ? (single char), [abc], [a-z]
 * Returns 1 on match, 0 on no match. */
int glob_match(const char *pattern, const char *str);

/* Expand a glob pattern to matching filenames.
 * Writes results into matches[][256], returns number of matches found.
 * If pattern has no directory component, matches in CWD.
 * If pattern has a directory prefix, matches in that dir. */
int glob_expand(const char *pattern, char matches[][256], int max_matches);

#endif
