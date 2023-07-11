/*
 * spelling-menu.h
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

#pragma once

#if !defined(LIBSPELLING_INSIDE) && !defined(LIBSPELLING_COMPILATION)
# error "Only <libspelling.h> can be included directly."
#endif

#include <gio/gio.h>

#include "spelling-types.h"
#include "spelling-version-macros.h"

G_BEGIN_DECLS

SPELLING_AVAILABLE_IN_ALL
GMenuModel *spelling_menu_new             (void);
SPELLING_AVAILABLE_IN_ALL
void        spelling_menu_set_corrections (GMenuModel         *menu,
                                           const char         *word,
                                           const char * const *words);

G_END_DECLS
