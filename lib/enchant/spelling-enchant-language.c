/* spelling-enchant-language.c
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

#include <pango/pango.h>
#include <enchant.h>

#include "spelling-enchant-language.h"

struct _SpellingEnchantLanguage
{
  SpellingLanguage parent_instance;
  PangoLanguage *language;
  EnchantDict *native;
  char *extra_word_chars;
};

G_DEFINE_FINAL_TYPE (SpellingEnchantLanguage, spelling_enchant_language, SPELLING_TYPE_LANGUAGE)

enum {
  PROP_0,
  PROP_NATIVE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

/**
 * spelling_enchant_language_new:
 *
 * Create a new #SpellingEnchantLanguage.
 *
 * Returns: (transfer full): a newly created #SpellingEnchantLanguage
 */
SpellingLanguage *
spelling_enchant_language_new (const char *code,
                               gpointer    native)
{
  return g_object_new (SPELLING_TYPE_ENCHANT_LANGUAGE,
                       "code", code,
                       "native", native,
                       NULL);
}

static gboolean
spelling_enchant_language_contains_word (SpellingLanguage *language,
                                         const char       *word,
                                         gssize            word_len)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)language;

  g_assert (SPELLING_IS_ENCHANT_LANGUAGE (self));
  g_assert (word != NULL);
  g_assert (word_len > 0);

  return enchant_dict_check (self->native, word, word_len) == 0;
}

static char **
spelling_enchant_language_list_corrections (SpellingLanguage *language,
                                            const char       *word,
                                            gssize            word_len)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)language;
  size_t count = 0;
  char **tmp;
  char **ret = NULL;

  g_assert (SPELLING_IS_ENCHANT_LANGUAGE (self));
  g_assert (word != NULL);
  g_assert (word_len > 0);

  if ((tmp = enchant_dict_suggest (self->native, word, word_len, &count)) && count > 0)
    {
      ret = g_strdupv (tmp);
      enchant_dict_free_string_list (self->native, tmp);
    }

  return g_steal_pointer (&ret);
}

static char **
spelling_enchant_language_split (SpellingEnchantLanguage *self,
                                 const char              *words)
{
  PangoLogAttr *attrs;
  GArray *ar;
  gsize n_chars;

  g_assert (SPELLING_IS_ENCHANT_LANGUAGE (self));

  if (words == NULL || self->language == NULL)
    return NULL;

  /* We don't care about splitting obnoxious stuff */
  if ((n_chars = g_utf8_strlen (words, -1)) > 1024)
    return NULL;

  attrs = g_newa (PangoLogAttr, n_chars + 1);
  pango_get_log_attrs (words, -1, -1, self->language, attrs, n_chars + 1);

  ar = g_array_new (TRUE, FALSE, sizeof (char*));

  for (gsize i = 0; i < n_chars + 1; i++)
    {
      if (attrs[i].is_word_start)
        {
          for (gsize j = i + 1; j < n_chars + 1; j++)
            {
              if (attrs[j].is_word_end)
                {
                  char *substr = g_utf8_substring (words, i, j);
                  g_array_append_val (ar, substr);
                  i = j;
                  break;
                }
            }
        }
    }

  return (char **)(gpointer)g_array_free (ar, FALSE);
}

static void
spelling_enchant_language_add_all_to_session (SpellingEnchantLanguage *self,
                                              const char * const      *words)
{
  g_assert (SPELLING_IS_ENCHANT_LANGUAGE (self));

  if (words == NULL || words[0] == NULL)
    return;

  for (guint i = 0; words[i]; i++)
    enchant_dict_add_to_session (self->native, words[i], -1);
}

static void
spelling_enchant_language_add_word (SpellingLanguage *language,
                                    const char       *word)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)language;

  g_assert (SPELLING_IS_LANGUAGE (language));
  g_assert (word != NULL);

  enchant_dict_add (self->native, word, -1);
}

static void
spelling_enchant_language_ignore_word (SpellingLanguage *language,
                                       const char       *word)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)language;

  g_assert (SPELLING_IS_LANGUAGE (language));
  g_assert (word != NULL);

  enchant_dict_add_to_session (self->native, word, -1);
}

static const char *
spelling_enchant_language_get_extra_word_chars (SpellingLanguage *language)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)language;

  g_assert (SPELLING_IS_LANGUAGE (language));

  return self->extra_word_chars;
}

static void
spelling_enchant_language_constructed (GObject *object)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)object;
  g_auto(GStrv) split = NULL;
  const char *extra_word_chars;
  const char *code;

  g_assert (SPELLING_IS_ENCHANT_LANGUAGE (self));

  G_OBJECT_CLASS (spelling_enchant_language_parent_class)->constructed (object);

  code = spelling_language_get_code (SPELLING_LANGUAGE (self));
  self->language = pango_language_from_string (code);

  if ((split = spelling_enchant_language_split (self, g_get_real_name ())))
    spelling_enchant_language_add_all_to_session (self, (const char * const *)split);

  if ((extra_word_chars = enchant_dict_get_extra_word_characters (self->native)))
    {
      const char *end_pos = NULL;

      /* Sometimes we get invalid UTF-8 from enchant, so handle that directly.
       * In particular, the data seems corrupted from Fedora.
       */
      if (g_utf8_validate (extra_word_chars, -1, &end_pos))
        self->extra_word_chars = g_strdup (extra_word_chars);
      else
        self->extra_word_chars = g_strndup (extra_word_chars, end_pos - extra_word_chars);
    }
}

static void
spelling_enchant_language_finalize (GObject *object)
{
  SpellingEnchantLanguage *self = (SpellingEnchantLanguage *)object;

  /* Owned by provider */
  self->native = NULL;

  G_OBJECT_CLASS (spelling_enchant_language_parent_class)->finalize (object);
}

static void
spelling_enchant_language_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SpellingEnchantLanguage *self = SPELLING_ENCHANT_LANGUAGE (object);

  switch (prop_id)
    {
    case PROP_NATIVE:
      g_value_set_pointer (value, self->native);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_enchant_language_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  SpellingEnchantLanguage *self = SPELLING_ENCHANT_LANGUAGE (object);

  switch (prop_id)
    {
    case PROP_NATIVE:
      self->native = g_value_get_pointer (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_enchant_language_class_init (SpellingEnchantLanguageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  SpellingLanguageClass *spell_language_class = SPELLING_LANGUAGE_CLASS (klass);

  object_class->constructed = spelling_enchant_language_constructed;
  object_class->finalize = spelling_enchant_language_finalize;
  object_class->get_property = spelling_enchant_language_get_property;
  object_class->set_property = spelling_enchant_language_set_property;

  spell_language_class->contains_word = spelling_enchant_language_contains_word;
  spell_language_class->list_corrections = spelling_enchant_language_list_corrections;
  spell_language_class->add_word = spelling_enchant_language_add_word;
  spell_language_class->ignore_word = spelling_enchant_language_ignore_word;
  spell_language_class->get_extra_word_chars = spelling_enchant_language_get_extra_word_chars;

  properties[PROP_NATIVE] =
    g_param_spec_pointer ("native",
                          "Native",
                          "The native enchant dictionary",
                          (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
spelling_enchant_language_init (SpellingEnchantLanguage *self)
{
}

gpointer
spelling_enchant_language_get_native (SpellingEnchantLanguage *self)
{
  g_return_val_if_fail (SPELLING_IS_ENCHANT_LANGUAGE (self), NULL);

  return self->native;
}
