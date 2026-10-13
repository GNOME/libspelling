/* spelling-text-view-adapter.c
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

#include "spelling-checker.h"
#include "spelling-menu-private.h"
#include "spelling-text-buffer-adapter-private.h"
#include "spelling-text-view-adapter.h"

struct _SpellingTextViewAdapter
{
  GObject                    parent_instance;

  GWeakRef                   view_wr;
  SpellingTextBufferAdapter *buffer_adapter;
  /* The view retains its action group. Keep it separate so disposing this
   * adapter can remove the group from the view.
   */
  GSimpleActionGroup        *actions;
  GMenuModel                *menu;
  GMenu                     *top_menu;
  GtkEventController        *click;
  GtkEventController        *key;
  GtkTextMark               *begin_mark;
  GtkTextMark               *end_mark;
  GtkTextBuffer             *context_buffer;
  char                      *word;
  gulong                     checker_notify_handler;
};

G_DEFINE_FINAL_TYPE (SpellingTextViewAdapter, spelling_text_view_adapter, G_TYPE_OBJECT)

static GtkTextBuffer *
get_buffer (SpellingTextViewAdapter *self)
{
  g_autoptr(GtkTextView) view = g_weak_ref_get (&self->view_wr);
  GtkTextBuffer *buffer;

  if (view == NULL || self->buffer_adapter == NULL)
    return NULL;

  buffer = gtk_text_view_get_buffer (view);

  if (buffer != GTK_TEXT_BUFFER (spelling_text_buffer_adapter_get_buffer (self->buffer_adapter)))
    return NULL;

  return buffer;
}

static void
set_action_enabled (SpellingTextViewAdapter *self,
                    const char              *name,
                    gboolean                 enabled)
{
  GAction *action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), name);

  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled);
}

static void
clear_context (SpellingTextViewAdapter *self)
{
  GtkTextBuffer *buffer = self->context_buffer;

  if (buffer != NULL)
    {
      if (self->begin_mark != NULL)
        gtk_text_buffer_delete_mark (buffer, self->begin_mark);

      if (self->end_mark != NULL)
        gtk_text_buffer_delete_mark (buffer, self->end_mark);
    }

  self->begin_mark = NULL;
  self->end_mark = NULL;
  g_clear_object (&self->context_buffer);
  g_clear_pointer (&self->word, g_free);
  set_action_enabled (self, "add", FALSE);
  set_action_enabled (self, "ignore", FALSE);
  set_action_enabled (self, "correct", FALSE);

  if (self->menu != NULL)
    spelling_menu_set_corrections (self->menu, NULL, NULL);
}

static gboolean
get_context_range (SpellingTextViewAdapter *self,
                   GtkTextIter             *begin,
                   GtkTextIter             *end)
{
  g_autofree char *slice = NULL;
  GtkTextBuffer *buffer = get_buffer (self);

  if (buffer == NULL ||
      buffer != self->context_buffer ||
      self->word == NULL ||
      self->begin_mark == NULL ||
      self->end_mark == NULL)
    return FALSE;

  gtk_text_buffer_get_iter_at_mark (buffer, begin, self->begin_mark);
  gtk_text_buffer_get_iter_at_mark (buffer, end, self->end_mark);
  slice = gtk_text_iter_get_slice (begin, end);

  return g_strcmp0 (slice, self->word) == 0;
}

static void
prepare_at_iter (SpellingTextViewAdapter *self,
                 const GtkTextIter       *iter)
{
  g_auto(GStrv) corrections = NULL;
  g_autofree char *word = NULL;
  GtkTextBuffer *buffer = get_buffer (self);
  SpellingChecker *checker;

  GtkTextIter begin, end, selection_begin, selection_end;

  clear_context (self);

  if (buffer == NULL ||
      !spelling_text_buffer_adapter_get_enabled (self->buffer_adapter) ||
      gtk_text_buffer_get_selection_bounds (buffer, &selection_begin, &selection_end))
    return;

  checker = spelling_text_buffer_adapter_get_checker (self->buffer_adapter);

  if (checker == NULL ||
      !_spelling_text_buffer_adapter_get_word_at_position (self->buffer_adapter,
                                                           gtk_text_iter_get_offset (iter),
                                                           &begin,
                                                           &end))
    return;

  word = gtk_text_iter_get_slice (&begin, &end);

  if (spelling_checker_check_word (checker, word, -1))
    return;

  corrections = spelling_checker_list_corrections (checker, word);
  self->begin_mark = gtk_text_buffer_create_mark (buffer, NULL, &begin, TRUE);
  self->end_mark = gtk_text_buffer_create_mark (buffer, NULL, &end, FALSE);
  self->context_buffer = g_object_ref (buffer);
  self->word = g_steal_pointer (&word);
  set_action_enabled (self, "add", TRUE);
  set_action_enabled (self, "ignore", TRUE);
  set_action_enabled (self, "correct", corrections != NULL && corrections[0] != NULL);
  spelling_menu_set_corrections (self->menu, self->word, (const char * const *)corrections);
}

/**
 * spelling_text_view_adapter_update_at_iter:
 * @self: a `SpellingTextViewAdapter`
 * @iter: the position for which to prepare corrections
 *
 * Prepares corrections for @iter without moving the insertion mark. This is
 * useful before showing a context menu programmatically.
 */
void
spelling_text_view_adapter_update_at_iter (SpellingTextViewAdapter *self,
                                           const GtkTextIter       *iter)
{
  GtkTextBuffer *buffer;

  g_return_if_fail (SPELLING_IS_TEXT_VIEW_ADAPTER (self));
  g_return_if_fail (iter != NULL);

  buffer = get_buffer (self);
  g_return_if_fail (buffer != NULL);
  g_return_if_fail (gtk_text_iter_get_buffer (iter) == buffer);

  prepare_at_iter (self, iter);
}

static void
on_click_pressed_cb (SpellingTextViewAdapter *self,
                     int                      n_press,
                     double                   x,
                     double                   y,
                     GtkGestureClick         *click)
{
  g_autoptr(GtkTextView) view = g_weak_ref_get (&self->view_wr);
  GdkEventSequence *sequence;
  GdkEvent *event;
  GtkTextIter iter;
  int buf_x, buf_y;

  if (view == NULL ||
      get_buffer (self) == NULL ||
      n_press != 1)
    return;

  sequence = gtk_gesture_single_get_current_sequence (GTK_GESTURE_SINGLE (click));
  event = gtk_gesture_get_last_event (GTK_GESTURE (click), sequence);

  if (event == NULL || !gdk_event_triggers_context_menu (event))
    return;

  gtk_text_view_window_to_buffer_coords (view,
                                         GTK_TEXT_WINDOW_WIDGET,
                                         x,
                                         y,
                                         &buf_x,
                                         &buf_y);
  gtk_text_view_get_iter_at_location (view, &iter, buf_x, buf_y);
  spelling_text_view_adapter_update_at_iter (self, &iter);
}

static gboolean
on_key_pressed_cb (SpellingTextViewAdapter *self,
                   guint                    keyval,
                   guint                    keycode,
                   GdkModifierType          state,
                   GtkEventControllerKey   *key)
{
  GtkTextBuffer *buffer = get_buffer (self);
  GtkTextIter iter;

  if (buffer == NULL ||
      !(((state & GDK_SHIFT_MASK) != 0 && keyval == GDK_KEY_F10) ||
        keyval == GDK_KEY_Menu))
    return FALSE;

  gtk_text_buffer_get_iter_at_mark (buffer, &iter, gtk_text_buffer_get_insert (buffer));
  spelling_text_view_adapter_update_at_iter (self, &iter);

  return FALSE;
}

static void
on_buffer_changed_cb (SpellingTextViewAdapter *self,
                      GtkTextBuffer           *buffer)
{
  clear_context (self);
}

static void
on_view_buffer_changed_cb (SpellingTextViewAdapter *self,
                           GParamSpec              *pspec,
                           GtkTextView             *view)
{
  clear_context (self);
}

static void
on_adapter_checker_changed_cb (SpellingTextViewAdapter   *self,
                               GParamSpec                *pspec,
                               SpellingTextBufferAdapter *buffer_adapter)
{
  clear_context (self);
}

static void
on_adapter_action_state_changed_cb (SpellingTextViewAdapter *self,
                                    const char              *name,
                                    GVariant                *state,
                                    GActionGroup            *group)
{
  GAction *action;

  if (g_strcmp0 (name, "enabled") != 0 && g_strcmp0 (name, "language") != 0)
    return;

  action = g_action_map_lookup_action (G_ACTION_MAP (self->actions), name);
  g_simple_action_set_state (G_SIMPLE_ACTION (action), state);

  if (g_strcmp0 (name, "language") == 0 ||
      (g_strcmp0 (name, "enabled") == 0 &&
       !g_variant_get_boolean (state)))
    clear_context (self);
}

static void
on_add_activate_cb (SpellingTextViewAdapter *self,
                    GVariant                *parameter,
                    GSimpleAction           *action)
{
  SpellingChecker *checker;

  GtkTextIter begin, end;

  if (!get_context_range (self, &begin, &end))
    return;

  checker = spelling_text_buffer_adapter_get_checker (self->buffer_adapter);
  if (checker != NULL)
    {
      spelling_checker_add_word (checker, self->word);
      spelling_text_buffer_adapter_invalidate_all (self->buffer_adapter);
      clear_context (self);
    }
}

static void
on_ignore_activate_cb (SpellingTextViewAdapter *self,
                       GVariant                *parameter,
                       GSimpleAction           *action)
{
  SpellingChecker *checker;

  GtkTextIter begin, end;

  if (!get_context_range (self, &begin, &end))
    return;

  checker = spelling_text_buffer_adapter_get_checker (self->buffer_adapter);
  if (checker != NULL)
    {
      spelling_checker_ignore_word (checker, self->word);
      spelling_text_buffer_adapter_invalidate_all (self->buffer_adapter);
      clear_context (self);
    }
}

static void
on_correct_activate_cb (SpellingTextViewAdapter *self,
                        GVariant                *parameter,
                        GSimpleAction           *action)
{
  GtkTextBuffer *buffer = get_buffer (self);

  GtkTextIter begin, end;

  if (buffer == NULL || !get_context_range (self, &begin, &end))
    return;

  gtk_text_buffer_begin_user_action (buffer);
  gtk_text_buffer_delete (buffer, &begin, &end);
  gtk_text_buffer_insert (buffer, &begin, g_variant_get_string (parameter, NULL), -1);
  gtk_text_buffer_end_user_action (buffer);
  clear_context (self);
}

static void
on_enabled_activate_cb (SpellingTextViewAdapter *self,
                        GVariant                *parameter,
                        GSimpleAction           *action)
{
  spelling_text_buffer_adapter_set_enabled (self->buffer_adapter,
                                            !spelling_text_buffer_adapter_get_enabled (self->buffer_adapter));
}

static void
on_language_activate_cb (SpellingTextViewAdapter *self,
                         GVariant                *parameter,
                         GSimpleAction           *action)
{
  spelling_text_buffer_adapter_set_language (self->buffer_adapter,
                                             g_variant_get_string (parameter, NULL));
}

static void
on_enabled_change_state_cb (SpellingTextViewAdapter *self,
                            GVariant                *state,
                            GSimpleAction           *action)
{
  spelling_text_buffer_adapter_set_enabled (self->buffer_adapter, g_variant_get_boolean (state));
}

static void
on_language_change_state_cb (SpellingTextViewAdapter *self,
                             GVariant                *state,
                             GSimpleAction           *action)
{
  spelling_text_buffer_adapter_set_language (self->buffer_adapter,
                                             g_variant_get_string (state, NULL));
}

static void
add_action (SpellingTextViewAdapter *self,
            const char              *name,
            const GVariantType      *parameter_type,
            GVariant                *state,
            GCallback                callback)
{
  GSimpleAction *action;

  if (state != NULL)
    action = g_simple_action_new_stateful (name, parameter_type, state);
  else
    action = g_simple_action_new (name, parameter_type);

  g_signal_connect_object (action,
                           "activate",
                           callback,
                           self,
                           G_CONNECT_SWAPPED);
  g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (action));
  g_object_unref (action);
}

static void
spelling_text_view_adapter_dispose (GObject *object)
{
  SpellingTextViewAdapter *self = SPELLING_TEXT_VIEW_ADAPTER (object);
  g_autoptr(GtkTextView) view = g_weak_ref_get (&self->view_wr);
  GtkTextBuffer *buffer = get_buffer (self);

  if (view != NULL)
    {
      gtk_widget_insert_action_group (GTK_WIDGET (view), "spelling", NULL);
      if (self->click != NULL)
        gtk_widget_remove_controller (GTK_WIDGET (view), self->click);

      if (self->key != NULL)
        gtk_widget_remove_controller (GTK_WIDGET (view), self->key);
      g_signal_handlers_disconnect_by_func (view, on_view_buffer_changed_cb, self);
    }

  if (buffer != NULL)
    g_signal_handlers_disconnect_by_func (buffer, on_buffer_changed_cb, self);

  if (self->buffer_adapter != NULL)
    g_clear_signal_handler (&self->checker_notify_handler, self->buffer_adapter);

  clear_context (self);
  g_clear_object (&self->click);
  g_clear_object (&self->key);
  g_clear_object (&self->actions);
  g_clear_object (&self->menu);
  g_clear_object (&self->top_menu);
  g_clear_object (&self->buffer_adapter);

  G_OBJECT_CLASS (spelling_text_view_adapter_parent_class)->dispose (object);
}

static void
spelling_text_view_adapter_finalize (GObject *object)
{
  SpellingTextViewAdapter *self = SPELLING_TEXT_VIEW_ADAPTER (object);

  g_weak_ref_clear (&self->view_wr);

  G_OBJECT_CLASS (spelling_text_view_adapter_parent_class)->finalize (object);
}

static void
spelling_text_view_adapter_class_init (SpellingTextViewAdapterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = spelling_text_view_adapter_dispose;
  object_class->finalize = spelling_text_view_adapter_finalize;
}

static void
spelling_text_view_adapter_init (SpellingTextViewAdapter *self)
{
  g_weak_ref_init (&self->view_wr, NULL);
}

/**
 * spelling_text_view_adapter_new:
 * @view: a `GtkTextView`
 * @buffer_adapter: the adapter for @view's buffer
 *
 * Creates a per-view spelling menu. The caller can add the returned menu model
 * to the view's extra menu. The view adapter installs its own `spelling` action
 * group and handles context clicks without changing the insertion mark.
 *
 * Returns: (transfer full): a new `SpellingTextViewAdapter`
 */
SpellingTextViewAdapter *
spelling_text_view_adapter_new (GtkTextView               *view,
                                SpellingTextBufferAdapter *buffer_adapter)
{
  SpellingTextViewAdapter *self;
  GtkGesture *click;
  GtkEventController *key;
  GVariant *state;

  g_return_val_if_fail (GTK_IS_TEXT_VIEW (view), NULL);
  g_return_val_if_fail (SPELLING_IS_TEXT_BUFFER_ADAPTER (buffer_adapter), NULL);
  g_return_val_if_fail (gtk_text_view_get_buffer (view) ==
                        GTK_TEXT_BUFFER (spelling_text_buffer_adapter_get_buffer (buffer_adapter)), NULL);

  self = g_object_new (SPELLING_TYPE_TEXT_VIEW_ADAPTER, NULL);
  g_weak_ref_set (&self->view_wr, view);
  self->buffer_adapter = g_object_ref (buffer_adapter);
  self->actions = g_simple_action_group_new ();
  self->menu = spelling_menu_new ();
  self->top_menu = g_menu_new ();
  g_menu_append_section (self->top_menu, NULL, self->menu);

  add_action (self, "add", NULL, NULL, G_CALLBACK (on_add_activate_cb));
  add_action (self, "ignore", NULL, NULL, G_CALLBACK (on_ignore_activate_cb));
  add_action (self, "correct", G_VARIANT_TYPE_STRING, NULL,
              G_CALLBACK (on_correct_activate_cb));
  state = g_action_group_get_action_state (G_ACTION_GROUP (buffer_adapter), "enabled");
  add_action (self, "enabled", NULL, state, G_CALLBACK (on_enabled_activate_cb));
  g_signal_connect_object (g_action_map_lookup_action (G_ACTION_MAP (self->actions), "enabled"),
                           "change-state",
                           G_CALLBACK (on_enabled_change_state_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_clear_pointer (&state, g_variant_unref);
  state = g_action_group_get_action_state (G_ACTION_GROUP (buffer_adapter), "language");
  add_action (self, "language", G_VARIANT_TYPE_STRING, state,
              G_CALLBACK (on_language_activate_cb));
  g_signal_connect_object (g_action_map_lookup_action (G_ACTION_MAP (self->actions), "language"),
                           "change-state",
                           G_CALLBACK (on_language_change_state_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_clear_pointer (&state, g_variant_unref);
  clear_context (self);

  g_signal_connect_object (buffer_adapter,
                           "action-state-changed",
                           G_CALLBACK (on_adapter_action_state_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  self->checker_notify_handler =
    g_signal_connect_object (buffer_adapter,
                             "notify::checker",
                             G_CALLBACK (on_adapter_checker_changed_cb),
                             self,
                             G_CONNECT_SWAPPED);
  g_signal_connect_object (gtk_text_view_get_buffer (view),
                           "changed",
                           G_CALLBACK (on_buffer_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (view,
                           "notify::buffer",
                           G_CALLBACK (on_view_buffer_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  click = gtk_gesture_click_new ();
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (click), GTK_PHASE_CAPTURE);
  g_signal_connect_object (click,
                           "pressed",
                           G_CALLBACK (on_click_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  self->click = g_object_ref (GTK_EVENT_CONTROLLER (click));
  gtk_widget_add_controller (GTK_WIDGET (view), GTK_EVENT_CONTROLLER (click));

  key = gtk_event_controller_key_new ();
  gtk_event_controller_set_propagation_phase (key, GTK_PHASE_CAPTURE);
  g_signal_connect_object (key,
                           "key-pressed",
                           G_CALLBACK (on_key_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  self->key = g_object_ref (key);
  gtk_widget_add_controller (GTK_WIDGET (view), key);
  gtk_widget_insert_action_group (GTK_WIDGET (view), "spelling", G_ACTION_GROUP (self->actions));

  return self;
}

/**
 * spelling_text_view_adapter_get_menu_model:
 * @self: a `SpellingTextViewAdapter`
 *
 * Gets the menu model for insertion into the text view's extra menu.
 *
 * Returns: (transfer none): a `GMenuModel`
 */
GMenuModel *
spelling_text_view_adapter_get_menu_model (SpellingTextViewAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_TEXT_VIEW_ADAPTER (self), NULL);

  return G_MENU_MODEL (self->top_menu);
}
