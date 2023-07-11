/* spelling-empty-provider-private.h
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

#include "spelling-provider-internal.h"

G_BEGIN_DECLS

#define SPELLING_TYPE_EMPTY_PROVIDER (spelling_empty_provider_get_type())

G_DECLARE_FINAL_TYPE (SpellingEmptyProvider, spelling_empty_provider, SPELLING, EMPTY_SPELL_PROVIDER, SpellingProvider)

SpellingProvider *spelling_empty_provider_new (void);

G_END_DECLS
