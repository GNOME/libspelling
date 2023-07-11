/* spelling-enchant-language.h
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

#pragma once

#include "spelling-language-internal.h"

G_BEGIN_DECLS

#define SPELLING_TYPE_ENCHANT_LANGUAGE (spelling_enchant_language_get_type())

G_DECLARE_FINAL_TYPE (SpellingEnchantLanguage, spelling_enchant_language, SPELLING, ENCHANT_LANGUAGE, SpellingLanguage)

SpellingLanguage *spelling_enchant_language_new        (const char              *code,
                                                        gpointer                 native);
gpointer          spelling_enchant_language_get_native (SpellingEnchantLanguage *self);

G_END_DECLS
