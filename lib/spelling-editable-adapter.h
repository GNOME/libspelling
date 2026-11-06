/* spelling-editable-adapter.h
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

#include <gtk/gtk.h>

#include "spelling-types.h"
#include "spelling-version-macros.h"

G_BEGIN_DECLS

#define SPELLING_TYPE_EDITABLE_ADAPTER (spelling_editable_adapter_get_type())

SPELLING_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SpellingEditableAdapter, spelling_editable_adapter, SPELLING, EDITABLE_ADAPTER, GObject)

SPELLING_AVAILABLE_IN_ALL
SpellingChecker         *spelling_editable_adapter_get_checker        (SpellingEditableAdapter *self);
SPELLING_AVAILABLE_IN_ALL
gboolean                 spelling_editable_adapter_get_enabled        (SpellingEditableAdapter *self);
SPELLING_AVAILABLE_IN_ALL
GMenuModel              *spelling_editable_adapter_get_menu_model     (SpellingEditableAdapter *self);
SPELLING_AVAILABLE_IN_ALL
void                     spelling_editable_adapter_invalidate_all     (SpellingEditableAdapter *self);
SPELLING_AVAILABLE_IN_ALL
SpellingEditableAdapter *spelling_editable_adapter_new                (GtkEditable             *editable,
                                                                       SpellingChecker         *checker);
SPELLING_AVAILABLE_IN_ALL
void                     spelling_editable_adapter_set_enabled        (SpellingEditableAdapter *self,
                                                                       gboolean                 enabled);
SPELLING_AVAILABLE_IN_ALL
void                     spelling_editable_adapter_update_at_position (SpellingEditableAdapter *self,
                                                                       int                      position);

G_END_DECLS
