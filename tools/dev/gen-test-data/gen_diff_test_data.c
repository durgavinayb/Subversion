/* gen_diff_test_data.c -- Generate sample data to test svn's diff
 *
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
 *
 *
 * This is a program to generate some pathological sample data for
 * testing and improving diff implementations.
 *
 * The output is deterministic but varies based on a seed value, like
 * that provided to a pseudo random number generator. The output
 * length is controlled as well. Both parameters are given at the
 * command line. The output is written to stdout.
 *
 * Presumably if two large outputs are generated by two runs with
 * different seed values, it will take a diff algorithm a long time to
 * calculate their longest common subsequence.
 *
 *
 * Usage:
 *
 * $ gen_diff_test_data <seed> <length>
 *
 *
 * Implementation notes:
 *
 * Rather than use the system-provided pseudo random number generator,
 * this program implements the hailstone sequence (see [1]) to assure
 * that users on different systems can produce same outputs when using
 * same seed and length values. That way people don't have to send
 * each other huge >100M files of useless junk. :-)
 *
 *
 * References:
 *
 * [1] Hailstone sequence: See Collatz Conjecture
 *     https://en.wikipedia.org/wiki/Collatz_conjecture
 */


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <limits.h>


#define PROGRAM_VERSION "0.01"


/* starting number for a hailstone sequence
 */
static uint64_t g_seed;


/* desired length of the output (approximately; actual may be longer)
 */
static uint64_t g_length;


/* current number in the hailstone sequence
 */
static uint64_t g_curr;


/* current word in words[] array
 */
static uint64_t g_word_index;


/* number of bytes written to stdout
 */
static uint64_t g_written;


/* how much to indent lines
 */
static int g_indents;


/* a bunch of random words to print in the output
 */
static const char * words[] = {
  "list", "exe", "MODULE", "EXE", "BIT", "database", "POINT", "link",
  "node", "parent", "BYTE", "enumerated", "OPTION", "managed",
  "deprecated", "point", "inheritance", "OUT", "VARIABLE", "PERL",
  "core", "else", "provider", "IMPLEMENTATION", "ENDIANNESS",
  "platform", "TYPE", "SCANNER", "libc", "lisp", "PROCESSOR", "path",
  "optimisation", "NANO", "subversion", "FORTRAN", "support", "EMPTY",
  "parser", "EXTENSION", "LOOP", "COLUMN", "resource", "end",
  "SUBCLASS", "optimal", "silicon", "row", "EXTENSIONS", "config",
  "EXCEPTION", "INHERITANCE", "BEGIN", "emacs", "VALLEY", "PROJECT",
  "EXTERNAL", "version", "subclass", "array", "ABI", "OPTIMISATION",
  "CLEAN", "ENVIRONMENT", "COL", "string", "RESOURCE", "VECTOR",
  "true", "STANDALONE", "VAR", "cobol", "DATA", "main", "TOOL",
  "ERROR", "IF", "drive", "errno", "artifact", "NO", "no", "DEVICE",
  "namespace", "name", "while", "dependencies", "IOCTL", "FLOAT",
  "SUBVERSION", "variable", "fortran", "external", "COBOL", "SILICON",
  "table", "API", "DATABASE", "ioctl", "BUILTIN", "polymorphism",
  "empty", "extensions", "OPTIMAL", "target", "optimization",
  "superclass", "INTERFACE", "interface", "PREFERENCES", "FOR", "asm",
  "var", "diagnostic", "PARALLELIZATION", "type", "xml", "linker",
  "PROVIDER", "leaf", "valley", "LINK", "TOOLCHAIN", "false",
  "DIAGNOSTIC", "RUNTIME", "CONFIGURATION", "CORE", "CONST",
  "MANAGED", "LEAF", "encoding", "switch", "CASE", "ERRNO", "DEBUG",
  "LIST", "double", "STATE", "builtin", "TARGET", "PYTHON", "SCRIPT",
  "definitions", "file", "if", "TABLE", "SETTINGS", "compiler",
  "ENUMERATED", "FALSE", "EXECUTABLE", "technical", "POLYMORPHISM",
  "vector", "STUDIO", "NAME", "float", "VERSION", "exception", "TRUE",
  "bit", "STORAGE", "INCANTATION", "endianness", "NODE", "id", "XML",
  "DONE", "INVOCATION", "environment", "PARENT", "SUPPORT", "tool",
  "ARRAY", "state", "project", "configuration", "const", "module",
  "builder", "BUILDER", "parallelization", "perl", "standalone",
  "ARTIFACT", "OPTIMIZATION", "COMPILER", "executable",
  "DEPENDENCIES", "nil", "column", "debug", "FILE", "option",
  "DEPRECATED", "COMMAND", "abi", "processor", "ENCODING", "command",
  "WHILE", "LISP", "vim", "DOUBLE", "folder", "script", "EMACS",
  "col", "DRIVE", "build", "case", "PARSER", "device", "clean", "NIL",
  "storage", "preferences", "VIM", "END", "NAMESPACE", "data",
  "toolchain", "STRING", "error", "description", "RELEASE",
  "incantation", "nano", "do", "TECHNICAL", "ROW", "scanner",
  "binary", "SUPERCLASS", "DESCRIPTION", "DO", "CONFIG", "invocation",
  "DIRECTORY", "done", "SWITCH", "NULL", "FOLDER", "LIBC", "BUILD",
  "ASM", "directory", "LINKER", "MAIN", "ID", "THEN",
  "implementation", "ELSE", "PLATFORM", "PATH", "then", "connection",
  "studio", "DEFINITIONS", "out", "null", "CONNECTION", "loop",
  "python", "runtime", "api", "BINARY"
};


/* temporary space for constructing strings
 */
static char scratchpad1[1024];
static char scratchpad2[1024];


/* something bad happened; print message and terminate execution
 */
static void
die(const char * s)
{
  if (s)
    {
      fprintf(stderr, "gen_diff_test_data: %s\n", s);
    }

  exit(1);
}


/* given a value, calculate next value in hailstone sequence
 *
 * f(n) = 3n+1 if n odd, n/2 if n even
 */
static uint64_t
hailstone(uint64_t n)
{
  return (n & 1) ? (n * 3) + 1 : n >> 1;
}


/* advance global variable to next value in hailstone sequence
 * if reached end of sequence, reseed and restart
 */
static void
advance(void)
{
  if (g_curr == 1)
    {
      g_seed++;
      g_curr = g_seed;
    }
  else
    {
      g_curr = hailstone(g_curr);
    }
}


/* get another "pseudo-random" word from words[] and advance in
 * hailstone sequence
 */
static const char *
word(void)
{
  const char * ret;

  g_word_index += g_curr;
  ret = words[g_word_index % (sizeof(words) / sizeof(words[0]))];

  advance();

  return ret;
}


/* get another "pseudo-random" number and advance in hailstone
 * sequence
 */
static int
number(void)
{
  int ret = (int) g_curr;

  advance();

  return ret;
}


/* print a hopefully helpful message and then quit
 */
static void
usage(void)
{
  fprintf(stderr, "gen_diff_test_data version %s\n\n",
          PROGRAM_VERSION);

  fprintf(stderr,
          "Usage: gen_diff_test_data <seed> <length>\n"
          "Where:\n"
          "        seed   - controls the content of the output\n"
          "        length - in bytes controls amount written\n"
          "                 approximately; actual output could be\n"
          "                 longer; can use k, m, or g suffix\n\n");

  exit(1);
}


/* parse command line arguments and validate them successfully or quit
 */
static void
parse_args(int argc, const char * argv[])
{
  char * endptr;
  long int val;

  if (argc != 3)
    {
      usage();
    }

  /* parse the seed value */

  val = strtol(argv[1], &endptr, 0);
  if ((val < 2) || (val == LONG_MAX))
    {
      die("seed must be in 1 < seed < LONG_MAX");
    }

  if ((endptr) && (*endptr))
    {
      die("unexpected stuff after seed");
    }

  g_seed = (uint64_t) val;

  /* parse the length value */

  val = strtol(argv[2], &endptr, 0);
  if ((val < 1) || (val == LONG_MAX))
    {
      die("length must be in 0 < length < LONG_MAX");
    }

  g_length = (uint64_t) val;

  if (endptr)
    {
      switch (*endptr)
        {
          case 0:
            break;

          case 'g': case 'G':
            g_length <<= 10;
          case 'm': case 'M':
            g_length <<= 10;
          case 'k': case 'K':
            g_length <<= 10;

            endptr++;
            if (*endptr)
              {
                die("unexpected stuff after length");
              }

            break;

          default: die("unknown length suffix");
        }
    }
}


/* print a string to stdout or else!
 */
static void
print_or_die(const char * s, ...)
{
  va_list args;
  int ret;

  va_start(args, s);
  ret = vfprintf(stdout, s, args);
  va_end(args);

  if (ret < 0)
    {
      die("sorry, vfprintf() failed!");
    }

  g_written += (uint64_t) ret;
}


/* print a string to a buffer or else!
 */
static void
snprintf_or_die(char * s, size_t n, const char * fmt, ...)
{
  va_list args;
  int ret;

  va_start(args, fmt);
  ret = vsnprintf(s, n, fmt, args);
  va_end(args);

  if ((ret < 0) || ((size_t) ret >= n))
    {
      die("sorry, vsnprintf() failed!");
    }
}


/* really lame function to "indent" by the current indent level by
 * repeatedly printing spaces
 */
static void
indent_or_die(void)
{
  int indents = g_indents;

  while (indents > 0)
    {
      print_or_die("  ");
      indents--;
    }
}


/* print an opening XML-looking tag and increase indent level
 */
static void
open_tag(const char * s)
{
  indent_or_die();
  print_or_die("<%s>\n", s);
  g_indents++;
}


/* safely decrease indent level and print a closing XML-looking tag
 */
static void
close_tag(const char * s)
{
  if (g_indents > 0)
    {
      g_indents--;
    }

  indent_or_die();
  print_or_die("</%s>\n", s);
}


/* on one line, print an opening XML-looking tag, possibly with
 * params, then print some contents, then print a closing tag; does
 * not change indent level
 */
static void
one_line_tag(const char * tag, const char * params,
             const char * contents)
{
  indent_or_die();

  if ((params) && (*params))
    {
      print_or_die("<%s %s>%s</%s>\n", tag, params, contents, tag);
    }
  else
    {
      print_or_die("<%s>%s</%s>\n", tag, contents, tag);
    }
}


static void
print_thing_1(void)
{
  snprintf_or_die(scratchpad1, sizeof(scratchpad1), "%s=\"%s\"",
                  word(), word());
  snprintf_or_die(scratchpad2, sizeof(scratchpad2), "%d", number());
  one_line_tag(word(), scratchpad1, scratchpad2);
}


static void
print_thing_2(void)
{
  snprintf_or_die(scratchpad2, sizeof(scratchpad2), "%d", number());
  one_line_tag(word(), NULL, scratchpad2);
}


static void
print_thing_3(void)
{
  snprintf_or_die(scratchpad2, sizeof(scratchpad2), "%s", word());
  one_line_tag(word(), NULL, scratchpad2);
}


static void
print_thing_4(void)
{
  snprintf_or_die(scratchpad1, sizeof(scratchpad1),
                  "%s=\"%s\" %s=\"%s\" %s=\"%s\"",
                  word(), word(), word(), word(), word(), word());
  snprintf_or_die(scratchpad2, sizeof(scratchpad2), "%d", number());
  one_line_tag(word(), scratchpad1, scratchpad2);
}


static void
print_thing_x(int x)
{
  if ((x + 30) >= (sizeof(words) / sizeof(words[0])))
    {
      x = 0;
    }

  snprintf_or_die(scratchpad1, sizeof(scratchpad1),
                  "%s=\"%s\" %s=\"%s\" %s=\"%s\"",
                  words[x + 5], words[x + 10], words[x + 15],
                  words[x + 20], words[x + 25], words[x + 30]);
  snprintf_or_die(scratchpad2, sizeof(scratchpad2), "%d", x);
  one_line_tag(word(), scratchpad1, scratchpad2);
}


static void
print_sequence_1(void)
{
  open_tag("level1");

  print_thing_1();
  print_thing_2();

  open_tag("level2");

  print_thing_3();
  print_thing_x(10);
  print_thing_4();

  open_tag("level3");

  print_thing_1();
  print_thing_2();
  print_thing_x(35);
  print_thing_3();

  open_tag("level4");

  print_thing_3();
  print_thing_2();
  print_thing_4();

  close_tag("level4");
  close_tag("level3");
  close_tag("level2");

  print_thing_3();

  close_tag("level1");
}


/* generate a whole bunch of output that looks like XML with pseudo
 * random contents, but lots of similar lines; in other words, stuff
 * to keep a diff algorithm busy for a while
 */
static void
generate_output(void)
{
  open_tag("level0");

  while (g_written < g_length)
    {
      print_sequence_1();
    }

  close_tag("level0");
}


int
main(int argc, const char * argv[])
{
  parse_args(argc, argv);

  g_curr = g_seed;
  g_word_index = 0;
  g_written = 0;
  g_indents = 0;

  generate_output();

  return 0;
}

