/* spelling-mistakes.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
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
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#if !defined(LIBSPELLING_INSIDE) && !defined(LIBSPELLING_COMPILATION)
# error "Only <libspelling.h> can be included directly."
#endif

#include <glib-object.h>

#include "spelling-types.h"
#include "spelling-version-macros.h"

G_BEGIN_DECLS

/**
 * SpellingRange:
 * @offset: start in Unicode characters from the beginning of the text
 * @length: length in Unicode characters
 *
 * A region of a UTF-8 text snapshot eligible for spellchecking.
 */
typedef struct _SpellingRange
{
  guint64 offset;
  guint64 length;
} SpellingRange;

/**
 * SpellingMistake:
 * @offset: start in Unicode characters from the beginning of the checked text
 * @length: length in Unicode characters
 *
 * A misspelled word found in a text snapshot.
 */
typedef struct _SpellingMistake
{
  guint64 offset;
  guint64 length;
} SpellingMistake;

#define SPELLING_TYPE_MISTAKES (spelling_mistakes_get_type())

SPELLING_AVAILABLE_IN_ALL
GType                  spelling_mistakes_get_type       (void);
SPELLING_AVAILABLE_IN_ALL
SpellingMistakes      *spelling_mistakes_ref            (SpellingMistakes *self);
SPELLING_AVAILABLE_IN_ALL
void                   spelling_mistakes_unref          (SpellingMistakes *self);
SPELLING_AVAILABLE_IN_ALL
guint                  spelling_mistakes_get_n_mistakes (SpellingMistakes *self);
SPELLING_AVAILABLE_IN_ALL
const SpellingMistake *spelling_mistakes_get_mistake    (SpellingMistakes *self,
                                                         guint             index);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (SpellingMistakes, spelling_mistakes_unref)

G_END_DECLS
