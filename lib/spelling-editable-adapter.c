/* spelling-editable-adapter.c
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
#include "spelling-editable-adapter-private.h"
#include "spelling-menu-private.h"

struct _SpellingEditableAdapter
{
  GObject             parent_instance;

  GWeakRef            widget_wr;
  GSignalGroup       *signals;
  SpellingChecker    *checker;
  GSimpleActionGroup *actions;
  GMenuModel         *menu;
  GtkEventController *click;
  GtkEventController *key;
  PangoAttrList      *attributes;
  PangoAttrList      *applied_attributes;
  SpellingMistakes   *mistakes;
  DexFuture          *check;
  char               *word;
  guint64             revision;
  gulong              language_handler;
  guint               timeout;
  int                 begin;
  int                 end;
  guint               enabled : 1;
  guint               checking : 1;
  guint               composing : 1;
  guint               disposed : 1;
};

typedef struct
{
  GWeakRef adapter_wr;
  guint64  revision;
} Check;

enum
{
  PROP_0,
  PROP_ENABLED,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (SpellingEditableAdapter, spelling_editable_adapter, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void queue_check (SpellingEditableAdapter *self);

static gboolean
can_check (SpellingEditableAdapter *self,
           GtkEditable             *editable)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  if (self->disposed ||
      !self->enabled ||
      self->composing ||
      editable == NULL ||
      !gtk_editable_get_editable (editable))
    return FALSE;

  if (GTK_IS_TEXT (editable))
    {
      GtkInputPurpose purpose = gtk_text_get_input_purpose (GTK_TEXT (editable));

      return gtk_text_get_visibility (GTK_TEXT (editable)) &&
             purpose != GTK_INPUT_PURPOSE_PASSWORD &&
             purpose != GTK_INPUT_PURPOSE_PIN;
    }

  return TRUE;
}

static GSimpleAction *
get_action (SpellingEditableAdapter *self,
            const char              *name)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));
  g_assert (name != NULL);

  return G_SIMPLE_ACTION (g_action_map_lookup_action (G_ACTION_MAP (self->actions), name));
}

static void
clear_context (SpellingEditableAdapter *self)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  g_clear_pointer (&self->word, g_free);
  g_simple_action_set_enabled (get_action (self, "add"), FALSE);
  g_simple_action_set_enabled (get_action (self, "ignore"), FALSE);
  g_simple_action_set_enabled (get_action (self, "correct"), FALSE);
  spelling_menu_set_corrections (self->menu, NULL, NULL);
}

static void
update_attributes (SpellingEditableAdapter *self)
{
  g_autoptr(GtkEditable) editable = NULL;
  g_autoptr(PangoAttrList) attrs = NULL;
  int cursor;

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  editable = g_signal_group_dup_target (self->signals);

  if (!GTK_IS_TEXT (editable))
    return;

  if (self->mistakes != NULL)
    {
      attrs = self->attributes ? pango_attr_list_copy (self->attributes) : pango_attr_list_new ();
      cursor = gtk_editable_get_position (editable);

      for (guint i = 0; i < spelling_mistakes_get_n_mistakes (self->mistakes); i++)
        {
          const SpellingMistake *mistake = spelling_mistakes_get_mistake (self->mistakes, i);
          PangoAttribute *attr;

          if (gtk_widget_has_focus (GTK_WIDGET (editable)) &&
              cursor >= mistake->offset &&
              cursor <= mistake->offset + mistake->length)
            continue;

          attr = pango_attr_underline_new (PANGO_UNDERLINE_ERROR_LINE);
          attr->start_index = mistake->byte_offset;
          attr->end_index = mistake->byte_offset + mistake->byte_length;
          pango_attr_list_change (attrs, attr);

          attr = pango_attr_underline_color_new (65535, 0, 0);
          attr->start_index = mistake->byte_offset;
          attr->end_index = mistake->byte_offset + mistake->byte_length;
          pango_attr_list_change (attrs, attr);
        }
    }

  if (attrs == NULL && self->attributes != NULL)
    attrs = pango_attr_list_ref (self->attributes);

  if (gtk_text_get_attributes (GTK_TEXT (editable)) == attrs ||
      (attrs != NULL && gtk_text_get_attributes (GTK_TEXT (editable)) != NULL &&
       pango_attr_list_equal (gtk_text_get_attributes (GTK_TEXT (editable)), attrs)))
    return;

  g_clear_pointer (&self->applied_attributes, pango_attr_list_unref);
  self->applied_attributes = attrs ? pango_attr_list_ref (attrs) : NULL;
  gtk_text_set_attributes (GTK_TEXT (editable), attrs);
}

static void
check_free (gpointer data)
{
  Check *check = data;

  g_weak_ref_clear (&check->adapter_wr);
  g_free (check);
}

static DexFuture *
check_finished (DexFuture *future,
                gpointer   user_data)
{
  Check *check = user_data;
  g_autoptr(SpellingEditableAdapter) self = NULL;
  g_autoptr(GtkEditable) editable = NULL;
  const GValue *value;

  g_assert (check != NULL);
  g_assert (DEX_IS_FUTURE (future));

  self = g_weak_ref_get (&check->adapter_wr);
  g_assert (!self || SPELLING_IS_EDITABLE_ADAPTER (self));

  if (self == NULL || self->disposed)
    return dex_future_new_true ();

  self->checking = FALSE;
  editable = g_signal_group_dup_target (self->signals);

  if (check->revision != self->revision)
    {
      queue_check (self);
    }
  else if (can_check (self, editable) && (value = dex_future_get_value (future, NULL)))
    {
      self->mistakes = g_value_dup_boxed (value);
      update_attributes (self);
    }

  return dex_future_new_true ();
}

static gboolean
start_check (gpointer data)
{
  SpellingEditableAdapter *self = data;
  g_autoptr(GtkEditable) editable = NULL;
  g_autoptr(GBytes) bytes = NULL;
  const char *text;
  Check *check;

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  editable = g_signal_group_dup_target (self->signals);

  self->timeout = 0;

  if (self->checking || !can_check (self, editable))
    return G_SOURCE_REMOVE;

  text = gtk_editable_get_text (editable);
  if (text[0] == 0)
    return G_SOURCE_REMOVE;

  bytes = g_bytes_new (text, strlen (text));
  check = g_new0 (Check, 1);
  g_weak_ref_init (&check->adapter_wr, self);
  check->revision = self->revision;
  self->checking = TRUE;

  dex_clear (&self->check);
  self->check = dex_future_finally (spelling_checker_check_ranges (self->checker, bytes, NULL, 0),
                                    check_finished,
                                    check,
                                    check_free);

  return G_SOURCE_REMOVE;
}

static void
queue_check (SpellingEditableAdapter *self)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  if (!self->disposed && self->enabled && self->timeout == 0)
    self->timeout = g_timeout_add (100, start_check, self);
}

/**
 * spelling_editable_adapter_invalidate_all:
 * @self: a `SpellingEditableAdapter`
 *
 * Rechecks the text, for example after changing the checker's dictionary.
 */
void
spelling_editable_adapter_invalidate_all (SpellingEditableAdapter *self)
{
  g_return_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self));

  self->revision++;
  g_clear_handle_id (&self->timeout, g_source_remove);
  g_clear_pointer (&self->mistakes, spelling_mistakes_unref);
  clear_context (self);
  update_attributes (self);
  queue_check (self);
}

static void
changed_cb (SpellingEditableAdapter *self)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  spelling_editable_adapter_invalidate_all (self);
}

static void
preedit_changed_cb (SpellingEditableAdapter *self,
                    const char              *preedit)
{
  gboolean composing = preedit != NULL && preedit[0] != 0;

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  if (self->composing != composing)
    {
      self->composing = composing;
      spelling_editable_adapter_invalidate_all (self);
    }
}

static void
notify_cb (SpellingEditableAdapter *self,
           GParamSpec              *pspec,
           GtkEditable             *editable)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));
  g_assert (GTK_IS_EDITABLE (editable));

  if (GTK_IS_TEXT (editable) && g_str_equal (pspec->name, "attributes"))
    {
      PangoAttrList *attrs;

      attrs = gtk_text_get_attributes (GTK_TEXT (editable));
      if (attrs == self->applied_attributes)
        return;

      g_clear_pointer (&self->attributes, pango_attr_list_unref);
      self->attributes = attrs ? pango_attr_list_copy (attrs) : NULL;
      update_attributes (self);
    }
  else if (g_str_equal (pspec->name, "cursor-position") ||
           g_str_equal (pspec->name, "selection-bound"))
    {
      clear_context (self);
      update_attributes (self);
    }
  else if (g_str_equal (pspec->name, "has-focus"))
    {
      update_attributes (self);
    }
  else if (g_str_equal (pspec->name, "editable") ||
           g_str_equal (pspec->name, "visibility") ||
           g_str_equal (pspec->name, "input-purpose"))
    {
      spelling_editable_adapter_invalidate_all (self);
    }
}

static void
language_changed_cb (SpellingEditableAdapter *self)
{
  const char *language;

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  language = spelling_checker_get_language (self->checker);

  g_simple_action_set_state (get_action (self, "language"),
                             g_variant_new_string (language ? language : ""));
  spelling_editable_adapter_invalidate_all (self);
}

/**
 * spelling_editable_adapter_update_at_position:
 * @self: a `SpellingEditableAdapter`
 * @position: a position in Unicode characters, or -1 for the cursor
 *
 * Prepares corrections without moving the cursor. Corrections are available
 * once the asynchronous check has completed, and are cleared by edits or
 * selection changes.
 *
 * An active selection suppresses corrections.
 */
void
spelling_editable_adapter_update_at_position (SpellingEditableAdapter *self,
                                              int                      position)
{
  g_autoptr(GtkEditable) editable = NULL;
  int begin, end;

  g_return_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self));
  g_return_if_fail (position >= -1);

  clear_context (self);
  editable = g_signal_group_dup_target (self->signals);

  if (!can_check (self, editable) ||
      self->mistakes == NULL ||
      gtk_editable_get_selection_bounds (editable, &begin, &end))
    return;

  if (position == -1)
    position = gtk_editable_get_position (editable);

  for (guint i = 0; i < spelling_mistakes_get_n_mistakes (self->mistakes); i++)
    {
      const SpellingMistake *mistake = spelling_mistakes_get_mistake (self->mistakes, i);
      g_auto(GStrv) corrections = NULL;

      if (position < mistake->offset || position > mistake->offset + mistake->length)
        continue;

      self->begin = mistake->offset;
      self->end = mistake->offset + mistake->length;
      self->word = gtk_editable_get_chars (editable, self->begin, self->end);
      corrections = spelling_checker_list_corrections (self->checker, self->word);
      g_simple_action_set_enabled (get_action (self, "add"), TRUE);
      g_simple_action_set_enabled (get_action (self, "ignore"), TRUE);
      g_simple_action_set_enabled (get_action (self, "correct"), corrections && corrections[0]);
      spelling_menu_set_corrections (self->menu, self->word, (const char * const *)corrections);
      break;
    }
}

void
_spelling_editable_adapter_update_at_point (SpellingEditableAdapter *self,
                                            double                   x,
                                            double                   y)
{
  g_autoptr(GtkEditable) editable = NULL;
  int begin, end;

  g_return_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self));

  clear_context (self);
  editable = g_signal_group_dup_target (self->signals);

  if (!can_check (self, editable) ||
      gtk_editable_get_selection_bounds (editable, &begin, &end))
    return;

  if (GTK_IS_ACCESSIBLE_TEXT (editable) &&
      GTK_ACCESSIBLE_TEXT_GET_IFACE (editable)->get_offset != NULL)
    {
      graphene_point_t point = GRAPHENE_POINT_INIT (x, y);
      unsigned int offset;

      if (!GTK_ACCESSIBLE_TEXT_GET_IFACE (editable)->get_offset (GTK_ACCESSIBLE_TEXT (editable),
                                                                 &point,
                                                                 &offset) ||
          offset > G_MAXINT)
        return;

      gtk_editable_set_position (editable, offset);
    }

  spelling_editable_adapter_update_at_position (self, -1);
}

static void
click_pressed_cb (SpellingEditableAdapter *self,
                  int                      n_press,
                  double                   x,
                  double                   y,
                  GtkGestureClick         *click)
{
  GdkEvent *event = gtk_event_controller_get_current_event (GTK_EVENT_CONTROLLER (click));

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  if (n_press != 1 ||
      event == NULL ||
      !gdk_event_triggers_context_menu (event))
    return;

  _spelling_editable_adapter_update_at_point (self, x, y);
}

static gboolean
key_pressed_cb (SpellingEditableAdapter *self,
                guint                    keyval,
                guint                    keycode,
                GdkModifierType          state)
{
  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  if (keyval == GDK_KEY_Menu ||
      (keyval == GDK_KEY_F10 &&
       (state & GDK_SHIFT_MASK)))
    spelling_editable_adapter_update_at_position (self, -1);

  return FALSE;
}

static void
word_action_cb (SpellingEditableAdapter *self,
                GVariant                *parameter,
                GSimpleAction           *action)
{
  g_autoptr(GtkEditable) editable = NULL;
  g_autofree char *word = NULL;
  const char *name;
  int begin;
  int end;

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  editable = g_signal_group_dup_target (self->signals);
  name = g_action_get_name (G_ACTION (action));
  begin = self->begin;
  end = self->end;

  if (!can_check (self, editable) || self->word == NULL)
    return;

  word = gtk_editable_get_chars (editable, begin, end);
  if (!g_str_equal (word, self->word))
    return;

  if (g_str_equal (name, "correct"))
    {
      int position = begin;
      int cursor = gtk_editable_get_position (editable);
      int old_length = g_utf8_strlen (gtk_editable_get_text (editable), -1);
      int delta;

      gtk_editable_delete_text (editable, begin, end);
      gtk_editable_insert_text (editable, g_variant_get_string (parameter, NULL), -1, &position);
      delta = g_utf8_strlen (gtk_editable_get_text (editable), -1) - old_length;
      gtk_editable_set_position (editable,
                                 cursor < begin ? cursor :
                                 cursor >= end ? cursor + delta : position);
    }
  else if (g_str_equal (name, "add"))
    {
      spelling_checker_add_word (self->checker, word);
    }
  else
    {
      spelling_checker_ignore_word (self->checker, word);
    }

  spelling_editable_adapter_invalidate_all (self);
}

static void
enabled_change_cb (SpellingEditableAdapter *self,
                   GVariant                *state)
{
  spelling_editable_adapter_set_enabled (self, g_variant_get_boolean (state));
}

static void
enabled_activate_cb (SpellingEditableAdapter *self)
{
  spelling_editable_adapter_set_enabled (self, !self->enabled);
}

static void
language_action_cb (SpellingEditableAdapter *self,
                    GVariant                *state)
{
  spelling_checker_set_language (self->checker, g_variant_get_string (state, NULL));
}

static void
add_action (SpellingEditableAdapter *self,
            const char              *name,
            const GVariantType      *type,
            GVariant                *state,
            GCallback                callback)
{
  g_autoptr(GSimpleAction) action = NULL;

  g_assert (SPELLING_IS_EDITABLE_ADAPTER (self));

  action = state ? g_simple_action_new_stateful (name, type, state) :
                   g_simple_action_new (name, type);
  g_signal_connect_object (action,
                           "activate",
                           callback,
                           self,
                           G_CONNECT_SWAPPED);
  g_action_map_add_action (G_ACTION_MAP (self->actions), G_ACTION (action));
}

static void
spelling_editable_adapter_dispose (GObject *object)
{
  SpellingEditableAdapter *self = SPELLING_EDITABLE_ADAPTER (object);
  g_autoptr(GtkWidget) widget = g_weak_ref_get (&self->widget_wr);
  g_autoptr(GtkEditable) editable = g_signal_group_dup_target (self->signals);

  if (!self->disposed)
    {
      self->disposed = TRUE;

      g_clear_handle_id (&self->timeout, g_source_remove);
      g_clear_signal_handler (&self->language_handler, self->checker);
      dex_clear (&self->check);
      g_clear_pointer (&self->mistakes, spelling_mistakes_unref);
      clear_context (self);
      update_attributes (self);
      g_signal_group_set_target (self->signals, NULL);

      if (widget != NULL)
        gtk_widget_insert_action_group (widget, "spelling", NULL);

      if (editable != NULL)
        {
          gtk_widget_remove_controller (GTK_WIDGET (editable), self->click);
          gtk_widget_remove_controller (GTK_WIDGET (editable), self->key);
        }

      g_clear_object (&self->click);
      g_clear_object (&self->key);
      g_clear_object (&self->checker);
    }

  G_OBJECT_CLASS (spelling_editable_adapter_parent_class)->dispose (object);
}

static void
spelling_editable_adapter_finalize (GObject *object)
{
  SpellingEditableAdapter *self = SPELLING_EDITABLE_ADAPTER (object);

  g_weak_ref_clear (&self->widget_wr);
  g_clear_object (&self->signals);
  g_clear_object (&self->actions);
  g_clear_object (&self->menu);
  g_clear_pointer (&self->attributes, pango_attr_list_unref);
  g_clear_pointer (&self->applied_attributes, pango_attr_list_unref);

  G_OBJECT_CLASS (spelling_editable_adapter_parent_class)->finalize (object);
}

static void
spelling_editable_adapter_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  SpellingEditableAdapter *self = SPELLING_EDITABLE_ADAPTER (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_editable_adapter_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  SpellingEditableAdapter *self = SPELLING_EDITABLE_ADAPTER (object);

  switch (prop_id)
    {
    case PROP_ENABLED:
      spelling_editable_adapter_set_enabled (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
spelling_editable_adapter_class_init (SpellingEditableAdapterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = spelling_editable_adapter_dispose;
  object_class->finalize = spelling_editable_adapter_finalize;
  object_class->get_property = spelling_editable_adapter_get_property;
  object_class->set_property = spelling_editable_adapter_set_property;

  /**
   * SpellingEditableAdapter:enabled:
   *
   * Whether spellchecking is enabled.
   */
  properties[PROP_ENABLED] =
    g_param_spec_boolean ("enabled", NULL, NULL,
                          TRUE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
spelling_editable_adapter_init (SpellingEditableAdapter *self)
{
  g_weak_ref_init (&self->widget_wr, NULL);

  self->enabled = TRUE;
  self->signals = g_signal_group_new (G_TYPE_OBJECT);
  self->actions = g_simple_action_group_new ();
  self->menu = spelling_menu_new ();

  add_action (self, "add", NULL, NULL, G_CALLBACK (word_action_cb));
  add_action (self, "ignore", NULL, NULL, G_CALLBACK (word_action_cb));
  add_action (self, "correct", G_VARIANT_TYPE_STRING, NULL, G_CALLBACK (word_action_cb));
  add_action (self,
              "enabled",
              NULL,
              g_variant_new_boolean (TRUE),
              G_CALLBACK (enabled_activate_cb));
  add_action (self,
              "language",
              G_VARIANT_TYPE_STRING,
              g_variant_new_string (""),
              G_CALLBACK (language_action_cb));
  g_signal_connect_object (get_action (self, "enabled"),
                           "change-state",
                           G_CALLBACK (enabled_change_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (get_action (self, "language"),
                           "change-state",
                           G_CALLBACK (language_action_cb),
                           self,
                           G_CONNECT_SWAPPED);
  clear_context (self);
}

/**
 * spelling_editable_adapter_new:
 * @editable: a widget implementing `GtkEditable`
 * @checker: (nullable): the checker, or %NULL for the default checker
 *
 * Creates a spellchecking adapter for a short text field. The adapter follows
 * editable delegates at construction and installs a `spelling` action group
 * on @editable. Keep one adapter per editable alive while its menu is used.
 * The adapter does not keep the widget alive.
 *
 * GtkText delegates also get spelling underlines. Context clicks move the
 * insertion cursor to the clicked position when there is no selection and
 * the delegate implements GtkAccessibleText hit testing. This is provided
 * as a convenience to match the [class@Spelling.TextViewAdapter] semantics.
 *
 * Keyboard menus and delegates without hit testing use the existing insertion
 * cursor like [class@Spelling.TextBufferAdapter] before the view adapter was
 * provided.
 *
 * Existing Pango attributes are preserved, including subsequent replacements
 * through gtk_text_set_attributes().
 *
 * Hidden text and password/PIN fields are never submitted for checking.
 * Custom editables without a GtkText delegate must suppress checking
 * themselves for sensitive text.
 *
 * Add the adapter's menu model to the widget's extra menu. For custom menus,
 * call spelling_editable_adapter_update_at_position() before showing them.
 *
 * Recreate the adapter if the editable's delegate changes.
 *
 * Returns: (transfer full): a new `SpellingEditableAdapter`
 */
SpellingEditableAdapter *
spelling_editable_adapter_new (GtkEditable     *editable,
                               SpellingChecker *checker)
{
  SpellingEditableAdapter *self;
  GtkEditable *delegate;
  GtkGesture *click;
  GtkEventController *key;

  g_return_val_if_fail (GTK_IS_EDITABLE (editable), NULL);
  g_return_val_if_fail (GTK_IS_WIDGET (editable), NULL);
  g_return_val_if_fail (checker == NULL || SPELLING_IS_CHECKER (checker), NULL);

  self = g_object_new (SPELLING_TYPE_EDITABLE_ADAPTER, NULL);
  g_weak_ref_set (&self->widget_wr, editable);
  self->checker = g_object_ref (checker ? checker : spelling_checker_get_default ());
  gtk_widget_insert_action_group (GTK_WIDGET (editable), "spelling", G_ACTION_GROUP (self->actions));

  while ((delegate = gtk_editable_get_delegate (editable)))
    editable = delegate;

  g_clear_object (&self->signals);
  self->signals = g_signal_group_new (G_OBJECT_TYPE (editable));

  if (GTK_IS_TEXT (editable) && gtk_text_get_attributes (GTK_TEXT (editable)))
    self->attributes = pango_attr_list_copy (gtk_text_get_attributes (GTK_TEXT (editable)));

  g_signal_group_connect_object (self->signals,
                                 "changed",
                                 G_CALLBACK (changed_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
  g_signal_group_connect_object (self->signals,
                                 "notify",
                                 G_CALLBACK (notify_cb),
                                 self,
                                 G_CONNECT_SWAPPED);

  if (GTK_IS_TEXT (editable))
    g_signal_group_connect_object (self->signals,
                                   "preedit-changed",
                                   G_CALLBACK (preedit_changed_cb),
                                   self,
                                   G_CONNECT_SWAPPED);

  g_signal_group_set_target (self->signals, editable);
  self->language_handler =
    g_signal_connect_object (self->checker,
                             "notify::language",
                             G_CALLBACK (language_changed_cb),
                             self,
                             G_CONNECT_SWAPPED);

  click = gtk_gesture_click_new ();
  self->click = g_object_ref (GTK_EVENT_CONTROLLER (click));
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (click), 0);
  gtk_event_controller_set_propagation_phase (self->click, GTK_PHASE_CAPTURE);
  g_signal_connect_object (click,
                           "pressed",
                           G_CALLBACK (click_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (editable), GTK_EVENT_CONTROLLER (click));

  key = gtk_event_controller_key_new ();
  self->key = g_object_ref (key);
  gtk_event_controller_set_propagation_phase (key, GTK_PHASE_CAPTURE);
  g_signal_connect_object (key,
                           "key-pressed",
                           G_CALLBACK (key_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (editable), key);

  language_changed_cb (self);

  return self;
}

/**
 * spelling_editable_adapter_get_checker:
 * @self: a `SpellingEditableAdapter`
 *
 * Gets the checker. Its language can be changed with
 * spelling_checker_set_language().
 *
 * Returns: (transfer none): the checker
 */
SpellingChecker *
spelling_editable_adapter_get_checker (SpellingEditableAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self), NULL);

  return self->checker;
}

/**
 * spelling_editable_adapter_get_enabled:
 * @self: a `SpellingEditableAdapter`
 *
 * Gets whether spellchecking is enabled.
 *
 * Returns: %TRUE if enabled
 */
gboolean
spelling_editable_adapter_get_enabled (SpellingEditableAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self), FALSE);

  return self->enabled;
}

/**
 * spelling_editable_adapter_set_enabled:
 * @self: a `SpellingEditableAdapter`
 * @enabled: whether to enable spellchecking
 *
 * Enables or disables checking. Disabling removes spelling underlines and
 * pending corrections immediately.
 */
void
spelling_editable_adapter_set_enabled (SpellingEditableAdapter *self,
                                       gboolean                 enabled)
{
  g_return_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self));

  enabled = !!enabled;
  if (self->enabled == enabled)
    return;

  self->enabled = enabled;
  g_simple_action_set_state (get_action (self, "enabled"), g_variant_new_boolean (enabled));
  spelling_editable_adapter_invalidate_all (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENABLED]);
}

/**
 * spelling_editable_adapter_get_menu_model:
 * @self: a `SpellingEditableAdapter`
 *
 * Gets the menu to add to the editable widget's extra menu.
 *
 * Returns: (transfer none): the spelling menu
 */
GMenuModel *
spelling_editable_adapter_get_menu_model (SpellingEditableAdapter *self)
{
  g_return_val_if_fail (SPELLING_IS_EDITABLE_ADAPTER (self), NULL);

  return self->menu;
}
