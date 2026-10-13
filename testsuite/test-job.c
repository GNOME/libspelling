/* test-job.c
 *
 * Copyright 2024 Christian Hergert <chergert@redhat.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <locale.h>

#include <glib/gi18n.h>

#include <libspelling.h>

#include "spelling-dictionary-internal.h"
#include "spelling-provider-internal.h"
#include "spelling-job-private.h"

static const char *test_dictionary[] = {
  ".",
  "Straße",
  "a",
  "calle",
  "caminar",
  "die",
  "entlang",
  "gehen",
  "has",
  "it",
  "it's",
  "la",
  "misspelled",
  "not",
  "por",
  "say'",
  "text",
  "this",
  "word",
  "गच्छन्तु",
  "वीथिं",
  "དུ",
  "འགྲོ",
  "ལམ",
  "སྲང",
  NULL
};

#define TEST_TYPE_DICTIONARY (test_dictionary_get_type())

G_DECLARE_FINAL_TYPE (TestDictionary, test_dictionary, TEST, DICTIONARY, SpellingDictionary)

struct _TestDictionary
{
  SpellingDictionary parent_instance;
};

G_DEFINE_FINAL_TYPE (TestDictionary, test_dictionary, SPELLING_TYPE_DICTIONARY)

static gboolean
test_dictionary_contains_word (SpellingDictionary *dictionary,
                               const char         *word,
                               gssize              word_len)
{
  g_autofree char *copy = g_strndup (word, word_len < 0 ? strlen (word) : word_len);

  return g_strv_contains ((const char * const *)test_dictionary, copy);
}

static const char *
test_dictionary_get_extra_word_chars (SpellingDictionary *dictionary)
{
  return "'.’";
}

static void
test_dictionary_class_init (TestDictionaryClass *klass)
{
  SpellingDictionaryClass *dictionary_class = SPELLING_DICTIONARY_CLASS (klass);

  dictionary_class->contains_word = test_dictionary_contains_word;
  dictionary_class->get_extra_word_chars = test_dictionary_get_extra_word_chars;
}

static void
test_dictionary_init (TestDictionary *self)
{
}

#define TEST_TYPE_PROVIDER (test_provider_get_type())

G_DECLARE_FINAL_TYPE (TestProvider, test_provider, TEST, PROVIDER, SpellingProvider)

struct _TestProvider
{
  SpellingProvider  parent_instance;
  TestDictionary   *dictionary;
};

G_DEFINE_FINAL_TYPE (TestProvider, test_provider, SPELLING_TYPE_PROVIDER)

static SpellingDictionary *
test_provider_load_dictionary (SpellingProvider *provider,
                               const char       *language)
{
  TestProvider *self = TEST_PROVIDER (provider);

  return g_object_ref (SPELLING_DICTIONARY (self->dictionary));
}

static const char *
test_provider_get_default_code (SpellingProvider *provider)
{
  return "C";
}

static void
test_provider_finalize (GObject *object)
{
  TestProvider *self = (TestProvider *)object;

  g_clear_object (&self->dictionary);

  G_OBJECT_CLASS (test_provider_parent_class)->finalize (object);
}

static void
test_provider_class_init (TestProviderClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SpellingProviderClass *provider_class = SPELLING_PROVIDER_CLASS (klass);

  object_class->finalize = test_provider_finalize;

  provider_class->load_dictionary = test_provider_load_dictionary;
  provider_class->get_default_code = test_provider_get_default_code;
}

static void
test_provider_init (TestProvider *self)
{
  self->dictionary = g_object_new (TEST_TYPE_DICTIONARY, NULL);
}

typedef struct _TestJob
{
  const char *text;
  guint n_mistakes;
  const SpellingBoundary *mistakes;
} TestJob;

static void
validate (SpellingMistakes *mistakes,
          TestJob         *test)
{
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, test->n_mistakes);

  for (guint i = 0; i < test->n_mistakes; i++)
    {
      const SpellingMistake *mistake = spelling_mistakes_get_mistake (mistakes, i);

      g_assert_cmpint (test->mistakes[i].offset, ==, mistake->offset);
      g_assert_cmpint (test->mistakes[i].length, ==, mistake->length);
    }
}

static void
test_job_basic (void)
{
  g_autoptr(SpellingProvider) provider = g_object_new (TEST_TYPE_PROVIDER, NULL);
  const char *default_code = spelling_provider_get_default_code (provider);
  g_autoptr(SpellingDictionary) dictionary = spelling_provider_load_dictionary (provider, default_code);

  TestJob tests[] = {
    { "it's not a misspelled word", 0, NULL },
    { "not", 0, NULL },
    { "it's", 0, NULL },
    { "it's ", 0, NULL },
    { "\t\nsay' \t\n", 0, NULL },
    { "say'", 0, NULL },
    { "die Straße entlang gehen", 0, NULL },
    { "वीथिं गच्छन्तु", 0, NULL },
    { "སྲང་ལམ་དུ་འགྲོ།", 0, NULL },
    { "caminar por la   calle", 0, NULL },
    { ".\nthis ", 0, NULL },
    { "'", 0, NULL },
    { "' ", 0, NULL },
    { "''", 0, NULL },
    { "'' ", 0, NULL },
    { "’", 0, NULL },
    { "’ ", 0, NULL },
    { "’\nthis ", 0, NULL },
    { "misplled",
      1,
      (const SpellingBoundary[]) {
        { .offset = 0, .length = 8 },
      },
    },
    { "'misplled",
      1,
      (const SpellingBoundary[]) {
        { .offset = 0, .length = 9 },
      },
    },
    { "misplled'",
      1,
      (const SpellingBoundary[]) {
        { .offset = 0, .length = 9 },
      },
    },
    { "it'",
      1,
      (const SpellingBoundary[]) {
        { .offset = 0, .length = 3 },
      },
    },
    { "it' ",
      1,
      (const SpellingBoundary[]) {
        { .offset = 0, .length = 3 },
      },
    },
    { "this text has a misplled word",
      1,
      (const SpellingBoundary[]) {
        { .offset = 16, .length = 8 },
      },
    },
  };

  for (guint i = 0; i < G_N_ELEMENTS (tests); i++)
    {
      g_autoptr(GBytes) bytes = g_bytes_new (tests[i].text, strlen (tests[i].text));
      g_autoptr(SpellingJob) job = spelling_job_new (dictionary, pango_language_get_default ());
      g_autoptr(SpellingMistakes) mistakes = NULL;
      g_autoptr(GError) error = NULL;

      spelling_job_add_fragment (job, bytes, 0, g_utf8_strlen (tests[i].text, -1));

      mistakes = dex_await_boxed (spelling_job_run (job), &error);
      g_assert_no_error (error);
      g_assert_nonnull (mistakes);
      validate (mistakes, &tests[i]);
    }
}

static void
test_job_discard (void)
{
  g_autoptr(SpellingProvider) provider = g_object_new (TEST_TYPE_PROVIDER, NULL);
  const char *default_code = spelling_provider_get_default_code (provider);
  g_autoptr(SpellingDictionary) dictionary = spelling_provider_load_dictionary (provider, default_code);
  g_autoptr(GBytes) bytes = g_bytes_new ("misplled word", 13);
  g_autoptr(SpellingJob) job = NULL;
  g_autoptr(SpellingMistakes) mistakes = NULL;
  g_autoptr(GError) error = NULL;

  /* First make sure things work */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 1, 13);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->offset, ==, 1);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->length, ==, 8);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);

  /* Now try to do an INSERT that SHOULD NOT collide */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 1, 13);
  spelling_job_notify_insert (job, 0, 3);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->offset, ==, 1 + 3);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->length, ==, 8);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);

  /* Now try to do an INSERT that SHOULD collide */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 1, 13);
  spelling_job_notify_insert (job, 1, 3);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 0);
  g_assert_nonnull (mistakes);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);

  /* Now try to do a DELETE that SHOULD NOT collide */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 2, 13);
  spelling_job_notify_delete (job, 0, 1);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->offset, ==, 1);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->length, ==, 8);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);

  /* Now try to do a DELETE that SHOULD collide */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 1, 13);
  spelling_job_notify_delete (job, 0, 1);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 0);
  g_assert_nonnull (mistakes);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);

  /* Now try to do a DELETE that SHOULD collide */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 0, 13);
  spelling_job_notify_delete (job, 13, 1);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 0);
  g_assert_nonnull (mistakes);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);

  /* Now try to do a DELETE that SHOULD NOT collide */
  job = spelling_job_new (dictionary, pango_language_get_default ());
  spelling_job_add_fragment (job, bytes, 0, 13);
  spelling_job_notify_delete (job, 14, 1);
  mistakes = dex_await_boxed (spelling_job_run (job), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->offset, ==, 0);
  g_assert_cmpint (spelling_mistakes_get_mistake (mistakes, 0)->length, ==, 8);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);
  g_clear_object (&job);
}

static void
test_check_ranges (void)
{
  g_autoptr(SpellingProvider) provider = g_object_new (TEST_TYPE_PROVIDER, NULL);
  g_autoptr(SpellingChecker) checker = spelling_checker_new (provider, "C");
  const char *sample = "die Straße misplled word";
  g_autoptr(GBytes) text = g_bytes_new_static (sample, strlen (sample));
  g_autoptr(SpellingMistakes) mistakes = NULL;
  g_autoptr(DexFuture) future = NULL;
  g_autoptr(GError) error = NULL;
  const SpellingMistake *mistake;
  SpellingRange range = { 11, 8 };
  SpellingRange adjacent[] = { { 11, 3 }, { 14, 5 } };

  future = spelling_checker_check_ranges (checker, text, NULL, 0);
  mistakes = dex_await_boxed (dex_ref (future), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  mistake = spelling_mistakes_get_mistake (mistakes, 0);
  g_assert_cmpuint (mistake->offset, ==, 11);
  g_assert_cmpuint (mistake->length, ==, 8);
  g_assert_cmpuint (mistake->byte_offset, ==, 12);
  g_assert_cmpuint (mistake->byte_length, ==, 8);
  dex_clear (&future);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);

  {
    const char *utf8_sample = "die Straße mísplled word";
    g_autoptr(GBytes) utf8_text = g_bytes_new_static (utf8_sample, strlen (utf8_sample));
    g_autofree char *word = NULL;
    const char *utf8_data = g_bytes_get_data (utf8_text, NULL);

    mistakes = dex_await_boxed (spelling_checker_check_ranges (checker, utf8_text, NULL, 0),
                                &error);
    g_assert_no_error (error);
    g_assert_nonnull (mistakes);
    g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);

    mistake = spelling_mistakes_get_mistake (mistakes, 0);
    g_assert_cmpuint (mistake->offset, ==, 11);
    g_assert_cmpuint (mistake->length, ==, 8);
    g_assert_cmpuint (mistake->byte_offset, ==, 12);
    g_assert_cmpuint (mistake->byte_length, ==, 9);

    word = g_strndup (utf8_data + mistake->byte_offset, mistake->byte_length);
    g_assert_cmpstr (word, ==, "mísplled");

    g_clear_pointer (&mistakes, spelling_mistakes_unref);
  }

  future = spelling_checker_check_ranges (checker, text, &range, 1);
  range.offset = 0;
  range.length = 3;
  mistakes = dex_await_boxed (dex_ref (future), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  mistake = spelling_mistakes_get_mistake (mistakes, 0);
  g_assert_cmpuint (mistake->byte_offset, ==, 12);
  g_assert_cmpuint (mistake->byte_length, ==, 8);
  dex_clear (&future);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);

  range.offset = 0;
  range.length = 14;
  future = spelling_checker_check_ranges (checker, text, &range, 1);
  mistakes = dex_await_boxed (dex_ref (future), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 0);
  dex_clear (&future);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);

  future = spelling_checker_check_ranges (checker, text, adjacent, G_N_ELEMENTS (adjacent));
  mistakes = dex_await_boxed (dex_ref (future), &error);
  g_assert_no_error (error);
  g_assert_nonnull (mistakes);
  g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  dex_clear (&future);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);

  adjacent[1].offset = 13;
  future = spelling_checker_check_ranges (checker, text, adjacent, G_N_ELEMENTS (adjacent));
  g_assert_null (dex_future_get_value (future, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT);
  dex_clear (&future);
  g_clear_error (&error);

  {
    g_autoptr(GBytes) invalid = g_bytes_new_static ("bad\xff", 4);

    future = spelling_checker_check_ranges (checker, invalid, NULL, 0);
    g_assert_null (dex_future_get_value (future, &error));
    g_assert_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA);
  }

  dex_clear (&future);
  g_clear_error (&error);

  {
    g_autoptr(GBytes) temporary = g_bytes_new_static ("misplled", 8);

    future = spelling_checker_check_ranges (checker, temporary, NULL, 0);
    g_clear_pointer (&temporary, g_bytes_unref);
    mistakes = dex_await_boxed (dex_ref (future), &error);
    g_assert_no_error (error);
    g_assert_nonnull (mistakes);
    g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 1);
  }

  dex_clear (&future);
  g_clear_pointer (&mistakes, spelling_mistakes_unref);

  {
    g_autoptr(GBytes) punctuation = g_bytes_new_static ("' ’ ", 6);

    mistakes = dex_await_boxed (spelling_checker_check_ranges (checker, punctuation, NULL, 0),
                                &error);
    g_assert_no_error (error);
    g_assert_nonnull (mistakes);
    g_assert_cmpuint (spelling_mistakes_get_n_mistakes (mistakes), ==, 0);
  }
}

int
main (int argc,
      char *argv[])
{
  setlocale (LC_ALL, "");
  bind_textdomain_codeset (GETTEXT_PACKAGE, "UTF-8");
  textdomain (GETTEXT_PACKAGE);

  dex_init ();
  g_test_init (&argc, &argv, NULL);
  dex_test_add_func ("/Spelling/Job/basic", test_job_basic);
  dex_test_add_func ("/Spelling/Job/discard", test_job_discard);
  dex_test_add_func ("/Spelling/Checker/check-ranges", test_check_ranges);
  return g_test_run ();
}
