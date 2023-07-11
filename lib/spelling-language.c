/* spelling-language.c
 *
 * Copyright 2021-2023 Christian Hergert <chergert@redhat.com>
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

#include <string.h>

#include "spelling-language-internal.h"

G_DEFINE_ABSTRACT_TYPE (SpellingLanguage, spelling_language, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CODE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
spelling_language_get_property (GObject    *object,
                                guint       prop_id,
                                GValue     *value,
                                GParamSpec *pspec)
{
  SpellingLanguage *self = SPELLING_LANGUAGE (object);

  switch (prop_id)
    {
    case PROP_CODE:
      g_value_set_string (value, spelling_language_get_code (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_language_set_property (GObject      *object,
                                guint         prop_id,
                                const GValue *value,
                                GParamSpec   *pspec)
{
  SpellingLanguage *self = SPELLING_LANGUAGE (object);

  switch (prop_id)
    {
    case PROP_CODE:
      self->code = g_intern_string (g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_language_class_init (SpellingLanguageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = spelling_language_get_property;
  object_class->set_property = spelling_language_set_property;

  properties[PROP_CODE] =
    g_param_spec_string ("code",
                         "Code",
                         "The language code",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
spelling_language_init (SpellingLanguage *self)
{
}

const char *
spelling_language_get_code (SpellingLanguage *self)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE (self), NULL);

  return self->code;
}

gboolean
spelling_language_contains_word (SpellingLanguage *self,
                                 const char       *word,
                                 gssize            word_len)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE (self), FALSE);
  g_return_val_if_fail (word != NULL, FALSE);

  if (word_len < 0)
    word_len = strlen (word);

  return SPELLING_LANGUAGE_GET_CLASS (self)->contains_word (self, word, word_len);
}

/**
 * spelling_language_list_corrections:
 * @self: a #SpellingLanguage
 * @word: a word to be checked
 * @word_len: the length of @word, or -1 if @word is zero-terminated
 *
 * Retrieves a list of possible corrections for @word.
 *
 * Returns: (nullable) (transfer full) (array zero-terminated=1) (type utf8):
 *   A list of possible corrections, or %NULL.
 */
char **
spelling_language_list_corrections (SpellingLanguage *self,
                                    const char       *word,
                                    gssize            word_len)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE (self), NULL);
  g_return_val_if_fail (word != NULL, NULL);
  g_return_val_if_fail (word != NULL || word_len == 0, NULL);

  if (word_len < 0)
    word_len = strlen (word);

  if (word_len == 0)
    return NULL;

  return SPELLING_LANGUAGE_GET_CLASS (self)->list_corrections (self, word, word_len);
}

void
spelling_language_add_word (SpellingLanguage *self,
                            const char       *word)
{
  g_return_if_fail (SPELLING_IS_LANGUAGE (self));
  g_return_if_fail (word != NULL);

  if (SPELLING_LANGUAGE_GET_CLASS (self)->add_word)
    SPELLING_LANGUAGE_GET_CLASS (self)->add_word (self, word);
}

void
spelling_language_ignore_word (SpellingLanguage *self,
                               const char       *word)
{
  g_return_if_fail (SPELLING_IS_LANGUAGE (self));
  g_return_if_fail (word != NULL);

  if (SPELLING_LANGUAGE_GET_CLASS (self)->ignore_word)
    SPELLING_LANGUAGE_GET_CLASS (self)->ignore_word (self, word);
}

const char *
spelling_language_get_extra_word_chars (SpellingLanguage *self)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE (self), NULL);

  if (SPELLING_LANGUAGE_GET_CLASS (self)->get_extra_word_chars)
    return SPELLING_LANGUAGE_GET_CLASS (self)->get_extra_word_chars (self);

  return "";
}
