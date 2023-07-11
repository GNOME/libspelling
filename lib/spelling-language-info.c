/*
 * spelling-language-info.c
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

#include "spelling-language-info-private.h"

struct _SpellingLanguageInfo
{
  GObject parent_instance;
  char *name;
  char *code;
  char *group;
};

G_DEFINE_FINAL_TYPE (SpellingLanguageInfo, spelling_language_info, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CODE,
  PROP_GROUP,
  PROP_NAME,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

/**
 * spelling_language_info_new:
 *
 * Create a new #SpellingLanguageInfo.
 *
 * Returns: (transfer full): a newly created #SpellingLanguageInfo
 */
SpellingLanguageInfo *
spelling_language_info_new (const char *name,
                            const char *code,
                            const char *group)
{
  return g_object_new (SPELLING_TYPE_LANGUAGE_INFO,
                       "name", name,
                       "code", code,
                       "group", group,
                       NULL);
}

static void
spelling_language_info_finalize (GObject *object)
{
  SpellingLanguageInfo *self = (SpellingLanguageInfo *)object;

  g_clear_pointer (&self->name, g_free);
  g_clear_pointer (&self->code, g_free);
  g_clear_pointer (&self->group, g_free);

  G_OBJECT_CLASS (spelling_language_info_parent_class)->finalize (object);
}

static void
spelling_language_info_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  SpellingLanguageInfo *self = SPELLING_LANGUAGE_INFO (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_set_string (value, spelling_language_info_get_name (self));
      break;

    case PROP_CODE:
      g_value_set_string (value, spelling_language_info_get_code (self));
      break;

    case PROP_GROUP:
      g_value_set_string (value, spelling_language_info_get_group (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_language_info_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  SpellingLanguageInfo *self = SPELLING_LANGUAGE_INFO (object);

  switch (prop_id)
    {
    case PROP_NAME:
      self->name = g_value_dup_string (value);
      break;

    case PROP_CODE:
      self->code = g_value_dup_string (value);
      break;

    case PROP_GROUP:
      self->group = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_language_info_class_init (SpellingLanguageInfoClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = spelling_language_info_finalize;
  object_class->get_property = spelling_language_info_get_property;
  object_class->set_property = spelling_language_info_set_property;

  properties[PROP_NAME] =
    g_param_spec_string ("name",
                         "Name",
                         "The name of the language",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_CODE] =
    g_param_spec_string ("code",
                         "Code",
                         "The language code",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties[PROP_GROUP] =
    g_param_spec_string ("group",
                         "Group",
                         "A group for sorting, usually the country name",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
spelling_language_info_init (SpellingLanguageInfo *self)
{
}

const char *
spelling_language_info_get_name (SpellingLanguageInfo *self)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE_INFO (self), NULL);

  return self->name;
}

const char *
spelling_language_info_get_code (SpellingLanguageInfo *self)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE_INFO (self), NULL);

  return self->code;
}

const char *
spelling_language_info_get_group (SpellingLanguageInfo *self)
{
  g_return_val_if_fail (SPELLING_IS_LANGUAGE_INFO (self), NULL);

  return self->group;
}
