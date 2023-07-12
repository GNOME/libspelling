/*
 * spelling-checker.c
 *
 * Copyright 2021-2023 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include "config.h"

#include <string.h>

#include "spelling-checker.h"
#include "spelling-language.h"
#include "spelling-provider.h"

struct _SpellingChecker
{
  GObject           parent_instance;
  SpellingProvider *provider;
  SpellingLanguage *language;
};

G_DEFINE_FINAL_TYPE (SpellingChecker, spelling_checker, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_LANGUAGE,
  PROP_PROVIDER,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

/**
 * spelling_checker_new:
 *
 * Create a new #SpellingChecker.
 *
 * Returns: (transfer full): a newly created #SpellingChecker
 */
SpellingChecker *
spelling_checker_new (SpellingProvider *provider,
                      const char       *language)
{
  g_return_val_if_fail (!provider || SPELLING_IS_PROVIDER (provider), NULL);

  if (provider == NULL)
    provider = spelling_provider_get_default ();

  if (language == NULL)
    language = spelling_provider_get_default_code (provider);

  return g_object_new (SPELLING_TYPE_CHECKER,
                       "provider", provider,
                       "language", language,
                       NULL);
}

static void
spelling_checker_constructed (GObject *object)
{
  SpellingChecker *self = (SpellingChecker *)object;

  g_assert (SPELLING_IS_CHECKER (self));

  G_OBJECT_CLASS (spelling_checker_parent_class)->constructed (object);

  if (self->provider == NULL)
    self->provider = spelling_provider_get_default ();
}

static void
spelling_checker_finalize (GObject *object)
{
  SpellingChecker *self = (SpellingChecker *)object;

  g_clear_object (&self->provider);
  g_clear_object (&self->language);

  G_OBJECT_CLASS (spelling_checker_parent_class)->finalize (object);
}

static void
spelling_checker_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  SpellingChecker *self = SPELLING_CHECKER (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      g_value_set_object (value, spelling_checker_get_provider (self));
      break;

    case PROP_LANGUAGE:
      g_value_set_string (value, spelling_checker_get_language (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_checker_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  SpellingChecker *self = SPELLING_CHECKER (object);

  switch (prop_id)
    {
    case PROP_PROVIDER:
      self->provider = g_value_dup_object (value);
      break;

    case PROP_LANGUAGE:
      spelling_checker_set_language (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_checker_class_init (SpellingCheckerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = spelling_checker_constructed;
  object_class->finalize = spelling_checker_finalize;
  object_class->get_property = spelling_checker_get_property;
  object_class->set_property = spelling_checker_set_property;

  /**
   * SpellingChecker:language:
   *
   * The "language" to use when checking words with the configured
   * #SpellingProvider. For example, `en_US`.
   */
  properties[PROP_LANGUAGE] =
    g_param_spec_string ("language",
                         "Language",
                         "The language code",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  /**
   * SpellingChecker:provider:
   *
   * The "provider" property contains the provider that is providing
   * information to the spell checker.
   *
   * Currently, only Enchant is supported, and requires using the
   * #SpellingEnchantProvider. Setting this to %NULL will get
   * the default provider.
   */
  properties[PROP_PROVIDER] =
    g_param_spec_object ("provider",
                         "Provider",
                         "The spell check provider",
                         SPELLING_TYPE_PROVIDER,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
spelling_checker_init (SpellingChecker *self)
{
}

/**
 * spelling_checker_get_language:
 *
 * Gets the language being used by the spell checker.
 *
 * Returns: (nullable): a string describing the current language.
 */
const char *
spelling_checker_get_language (SpellingChecker *self)
{
  g_return_val_if_fail (SPELLING_IS_CHECKER (self), NULL);

  return self->language ? spelling_language_get_code (self->language) : NULL;
}

/**
 * spelling_checker_set_language:
 * @self: an #SpellingChecker
 * @language: the language to use
 *
 * Sets the language code to use when communicating with the provider,
 * such as `en_US`.
 */
void
spelling_checker_set_language (SpellingChecker *self,
                               const char           *language)
{
  g_return_if_fail (SPELLING_IS_CHECKER (self));

  if (g_strcmp0 (language, spelling_checker_get_language (self)) != 0)
    {
      self->language = spelling_provider_get_language (self->provider, language);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_LANGUAGE]);
    }
}

/**
 * spelling_checker_get_provider:
 *
 * Gets the spell provider used by the spell checker.
 *
 * Currently, only Enchant-2 is supported.
 *
 * Returns: (transfer none) (not nullable): an #SpellingProvider
 */
SpellingProvider *
spelling_checker_get_provider (SpellingChecker *self)
{
  g_return_val_if_fail (SPELLING_IS_CHECKER (self), NULL);

  return self->provider;
}

static inline gboolean
word_is_number (const char *word,
                gssize      word_len)
{
  g_assert (word_len > 0);

  for (gssize i = 0; i < word_len; i++)
    {
      if (word[i] < '0' || word[i] > '9')
        return FALSE;
    }

  return TRUE;
}

gboolean
spelling_checker_check_word (SpellingChecker *self,
                             const char      *word,
                             gssize           word_len)
{
  g_return_val_if_fail (SPELLING_IS_CHECKER (self), FALSE);

  if (word == NULL || word_len == 0)
    return FALSE;

  if (self->language == NULL)
    return TRUE;

  if (word_len < 0)
    word_len = strlen (word);

  if (word_is_number (word, word_len))
    return TRUE;

  return spelling_language_contains_word (self->language, word, word_len);
}

/**
 * spelling_checker_list_corrections:
 * @self: a #SpellingChecker
 * @word: a word to be checked
 *
 * Retrieves a list of possible corrections for @word.
 *
 * Returns: (nullable) (transfer full) (array zero-terminated=1) (type utf8):
 *   A list of possible corrections, or %NULL.
 */
char **
spelling_checker_list_corrections (SpellingChecker *self,
                                   const char      *word)
{
  g_return_val_if_fail (SPELLING_IS_CHECKER (self), NULL);
  g_return_val_if_fail (word != NULL, NULL);

  if (self->language == NULL)
    return NULL;

  return spelling_language_list_corrections (self->language, word, -1);
}

void
spelling_checker_add_word (SpellingChecker *self,
                           const char      *word)
{
  g_return_if_fail (SPELLING_IS_CHECKER (self));
  g_return_if_fail (word != NULL);

  if (self->language != NULL)
    spelling_language_add_word (self->language, word);
}

void
spelling_checker_ignore_word (SpellingChecker *self,
                              const char      *word)
{
  g_return_if_fail (SPELLING_IS_CHECKER (self));
  g_return_if_fail (word != NULL);

  if (self->language != NULL)
    spelling_language_ignore_word (self->language, word);
}

const char *
spelling_checker_get_extra_word_chars (SpellingChecker *self)
{
  g_return_val_if_fail (SPELLING_IS_CHECKER (self), NULL);

  if (self->language != NULL)
    return spelling_language_get_extra_word_chars (self->language);

  return "";
}

/**
 * spelling_checker_get_default:
 *
 * Gets a default #SpellingChecker using the default provider and language.
 *
 * Returns: (transfer none): a #SpellingChecker
 */
SpellingChecker *
spelling_checker_get_default (void)
{
  static SpellingChecker *instance;

  if (instance == NULL)
    {
      SpellingProvider *provider = spelling_provider_get_default ();
      const char *code = spelling_provider_get_default_code (provider);

      instance = spelling_checker_new (provider, code);
      g_object_add_weak_pointer (G_OBJECT (instance), (gpointer *)&instance);
    }

  return instance;
}
