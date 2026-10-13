/* spelling-text-view-adapter.h
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

#include <gtksourceview/gtksource.h>

#include "spelling-types.h"
#include "spelling-version-macros.h"

G_BEGIN_DECLS

#define SPELLING_TYPE_TEXT_VIEW_ADAPTER (spelling_text_view_adapter_get_type())

SPELLING_AVAILABLE_IN_ALL
G_DECLARE_FINAL_TYPE (SpellingTextViewAdapter, spelling_text_view_adapter, SPELLING, TEXT_VIEW_ADAPTER, GObject)

SPELLING_AVAILABLE_IN_ALL
SpellingTextViewAdapter *spelling_text_view_adapter_new            (GtkTextView               *view,
                                                                    SpellingTextBufferAdapter *buffer_adapter);
SPELLING_AVAILABLE_IN_ALL
GMenuModel              *spelling_text_view_adapter_get_menu_model (SpellingTextViewAdapter   *self);
SPELLING_AVAILABLE_IN_ALL
void                     spelling_text_view_adapter_update_at_iter (SpellingTextViewAdapter   *self,
                                                                    const GtkTextIter         *iter);

G_END_DECLS
