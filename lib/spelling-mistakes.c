/* spelling-mistakes.c
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

#include "config.h"

#include <string.h>

#include "spelling-mistakes-private.h"

/**
 * SpellingMistakes:
 *
 * An immutable collection of misspelled word ranges.
 */
struct _SpellingMistakes
{
  grefcount        refs;
  guint            n_mistakes;
  SpellingMistake *mistakes;
  guint            n_checked;
  SpellingRange   *checked;
  guint64          revision;
};

G_DEFINE_BOXED_TYPE (SpellingMistakes, spelling_mistakes,
                     spelling_mistakes_ref, spelling_mistakes_unref)

SpellingMistakes *
_spelling_mistakes_new (const SpellingMistake *mistakes,
                        guint                  n_mistakes,
                        const SpellingRange   *checked,
                        guint                  n_checked,
                        guint64                revision)
{
  SpellingMistakes *self;

  g_return_val_if_fail (n_mistakes == 0 || mistakes != NULL, NULL);
  g_return_val_if_fail (n_checked == 0 || checked != NULL, NULL);

  self = g_new0 (SpellingMistakes, 1);
  g_ref_count_init (&self->refs);
  self->n_mistakes = n_mistakes;
  self->n_checked = n_checked;
  self->revision = revision;

  if (n_mistakes > 0)
    {
      self->mistakes = g_new (SpellingMistake, n_mistakes);
      memcpy (self->mistakes, mistakes, (gsize)n_mistakes * sizeof *mistakes);
    }

  if (n_checked > 0)
    {
      self->checked = g_new (SpellingRange, n_checked);
      memcpy (self->checked, checked, (gsize)n_checked * sizeof *checked);
    }

  return self;
}

const SpellingRange *
_spelling_mistakes_get_checked (SpellingMistakes *self,
                                guint            *n_checked)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (n_checked != NULL, NULL);

  *n_checked = self->n_checked;
  return self->checked;
}

guint64
_spelling_mistakes_get_revision (SpellingMistakes *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->revision;
}

/**
 * spelling_mistakes_ref:
 * @self: a `SpellingMistakes`
 *
 * Returns: (transfer full): @self
 */
SpellingMistakes *
spelling_mistakes_ref (SpellingMistakes *self)
{
  g_return_val_if_fail (self != NULL, NULL);

  g_ref_count_inc (&self->refs);
  return self;
}

/**
 * spelling_mistakes_unref:
 * @self: a `SpellingMistakes`
 */
void
spelling_mistakes_unref (SpellingMistakes *self)
{
  g_return_if_fail (self != NULL);

  if (g_ref_count_dec (&self->refs))
    {
      g_free (self->mistakes);
      g_free (self->checked);
      g_free (self);
    }
}

/**
 * spelling_mistakes_get_n_mistakes:
 * @self: a `SpellingMistakes`
 *
 * Returns: the number of misspelled words
 */
guint
spelling_mistakes_get_n_mistakes (SpellingMistakes *self)
{
  g_return_val_if_fail (self != NULL, 0);

  return self->n_mistakes;
}

/**
 * spelling_mistakes_get_mistake:
 * @self: a `SpellingMistakes`
 * @index: index of a mistake
 *
 * Returns: (transfer none) (nullable): a mistake in Unicode character offsets
 */
const SpellingMistake *
spelling_mistakes_get_mistake (SpellingMistakes *self,
                               guint             index)
{
  g_return_val_if_fail (self != NULL, NULL);
  g_return_val_if_fail (index < self->n_mistakes, NULL);

  return &self->mistakes[index];
}
