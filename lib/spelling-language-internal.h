/* spelling-language-internal.h
 *
 * Copyright 2023 Christian Hergert <chergert@redhat.com>
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

#pragma once

#include "spelling-language.h"

G_BEGIN_DECLS

#define SPELLING_LANGUAGE_GET_CLASS(obj) G_TYPE_INSTANCE_GET_CLASS(obj, SPELLING_TYPE_LANGUAGE, SpellingLanguageClass)

struct _SpellingLanguage
{
  GObject parent_instance;
  const char *code;
};

struct _SpellingLanguageClass
{
  GObjectClass parent_class;

  gboolean     (*contains_word)        (SpellingLanguage *self,
                                        const char       *word,
                                        gssize            word_len);
  char       **(*list_corrections)     (SpellingLanguage *self,
                                        const char       *word,
                                        gssize            word_len);
  void         (*add_word)             (SpellingLanguage *self,
                                        const char       *word);
  void         (*ignore_word)          (SpellingLanguage *self,
                                        const char       *word);
  const char  *(*get_extra_word_chars) (SpellingLanguage *self);
};

G_END_DECLS
