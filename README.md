# libspelling

A spellcheck library for GTK 4.

This library is heavily based upon GNOME Text Editor and GNOME Builder's
spellcheck implementation. However, it is licensed LGPLv2.1+.

## Documentation

[Our documentation](https://gnome.pages.gitlab.gnome.org/libspelling/libspelling-1/) is updated on every commit.

## Installing Dictionaries

Currently, libspelling wraps `enchant-2` only.
That means to get additional dictionaries you need to follow the same directions as enchant.
Generally, that means installing packages like `apsell-en` or `hunspell-fr` and so forth.

Enchant should pick those up and use them the next time a libspelling-based application is run.

## Example

### In C

```c
SpellingChecker *checker = spelling_checker_get_default ();
SpellingTextBufferAdapter *adapter = spelling_text_buffer_adapter_new (source_buffer, checker);
SpellingTextViewAdapter *view_adapter = spelling_text_view_adapter_new (GTK_TEXT_VIEW (source_view), adapter);
GMenuModel *extra_menu = spelling_text_view_adapter_get_menu_model (view_adapter);

gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (source_view), extra_menu);
spelling_text_buffer_adapter_set_enabled (adapter, TRUE);
```

Store both adapters in their owning document and view, and release them when
they are no longer used. `SpellingTextBufferAdapter` is bound to its
`GtkSourceBuffer`, and `SpellingTextViewAdapter` is bound to
that adapter and the view's buffer at construction. If you set a different
buffer on the view, remove the old spelling menu from the view's extra menu,
release the old view adapter, and create a new view adapter using the spelling
adapter for the new buffer. Create a new spelling buffer adapter too if the new
buffer does not already have one. Releasing the old view adapter removes its
action group and event controllers. `SpellingTextViewAdapter` does not rebind
itself when the view's buffer changes.

For text stored outside `GtkTextBuffer`, use
`spelling_checker_check_ranges()`. Pass one UTF-8 `GBytes` snapshot and
optional sorted, non-overlapping ranges measured in Unicode characters.
Passing `NULL` and zero checks the whole snapshot. The returned `DexFuture`
resolves to an immutable `SpellingMistakes` boxed value. Each mistake has
Unicode character offsets for text positions and byte offsets for slicing the
same `GBytes` snapshot. Keep a text revision with each future and apply its
mistakes only if the text still has that revision.

Call `spelling_init()` on the main thread before using libspelling, typically
during `main()` initialization.

### In Python

```python
from gi.repository import Spelling

checker = Spelling.Checker.get_default()
adapter = Spelling.TextBufferAdapter.new(buffer, checker)
view_adapter = Spelling.TextViewAdapter.new(view, adapter)
extra_menu = view_adapter.get_menu_model()

view.set_extra_menu(extra_menu)

adapter.set_enabled(True)
```

### In JavaScript

```js
const Spelling = imports.gi.Spelling;

let checker = Spelling.Checker.get_default()
let adapter = Spelling.TextBufferAdapter.new(buffer, checker)
let viewAdapter = Spelling.TextViewAdapter.new(view, adapter)
let extra_menu = viewAdapter.get_menu_model()

view.set_extra_menu(extra_menu)

adapter.set_enabled(true)
```

### In Rust

Add the [bindings dependency](https://crates.io/crates/libspelling) to your Cargo.toml

```rust
let checker = libspelling::Checker::default();
let adapter = libspelling::TextBufferAdapter::new(&buffer, &checker);
let view_adapter = libspelling::TextViewAdapter::new(&view, &adapter);
let extra_menu = view_adapter.menu_model();

view.set_extra_menu(Some(&extra_menu));

adapter.set_enabled(true);
```
