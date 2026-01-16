# Keyboard Shortcuts

Finder Plus is designed for keyboard-first navigation. All features are accessible via keyboard shortcuts.

## Navigation

### Basic Movement

| Action | Primary | Vim-style |
|--------|---------|-----------|
| Move selection down | `Down Arrow` | `j` |
| Move selection up | `Up Arrow` | `k` |
| Enter directory / Open file | `Enter` | `l` |
| Go to parent directory | `Backspace` | `h` |
| Go to first item | `Home` | `gg` |
| Go to last item | `End` | `G` |
| Page down | `Page Down` | `Ctrl+D` |
| Page up | `Page Up` | `Ctrl+U` |

### History Navigation

| Action | Shortcut |
|--------|----------|
| Go back | `Cmd+[` |
| Go forward | `Cmd+]` |
| Go to parent | `Cmd+Up` |
| Go to home | `Cmd+Shift+H` |

## File Operations

### Clipboard

| Action | Primary | Vim-style |
|--------|---------|-----------|
| Copy | `Cmd+C` | `yy` |
| Cut | `Cmd+X` | `dd` |
| Paste | `Cmd+V` | `p` |
| Duplicate | `Cmd+D` | - |

### File Management

| Action | Shortcut |
|--------|----------|
| Delete (move to Trash) | `Cmd+Backspace` |
| Rename | `Enter` (on selected) or `r` |
| AI Smart Rename | `Shift+R` |
| New folder | `Cmd+Alt+N` |
| New file | `Cmd+Shift+N` |
| Open with default app | `Cmd+O` |

### Selection

| Action | Shortcut |
|--------|----------|
| Select all | `Cmd+A` |
| Clear selection | `Escape` |
| Toggle selection (multi-select) | `Cmd+Click` |
| Range select | `Shift+Click` |
| Extend selection down | `Shift+Down` or `Shift+J` |
| Extend selection up | `Shift+Up` or `Shift+K` |

## Views

### View Modes

| Action | Shortcut |
|--------|----------|
| List view | `Cmd+1` |
| Grid view | `Cmd+2` |
| Column view | `Cmd+3` |

### Panels and Sidebars

| Action | Shortcut |
|--------|----------|
| Toggle sidebar | `Cmd+Shift+S` |
| Toggle preview panel | `Cmd+Shift+P` (when not in perf mode) |
| Toggle hidden files | `Cmd+Shift+.` |
| Toggle fullscreen | `Cmd+Ctrl+F` |
| Toggle theme (dark/light) | `Cmd+Shift+T` |
| Toggle performance stats | `Cmd+Shift+P` |

### Dual Pane Mode

| Action | Shortcut |
|--------|----------|
| Toggle dual pane | `F3` or `Cmd+Shift+D` |
| Switch active pane | `Tab` |
| Copy to other pane | `F5` |
| Move to other pane | `F6` |
| Sync scrolling | `Cmd+Shift+S` (in dual pane) |
| Compare directories | `Cmd+=` |

## Tabs

| Action | Shortcut |
|--------|----------|
| New tab | `Cmd+T` |
| Close tab | `Cmd+W` |
| Next tab | `Ctrl+Tab` or `Cmd+Shift+]` |
| Previous tab | `Ctrl+Shift+Tab` or `Cmd+Shift+[` |
| Go to tab 1-9 | `Cmd+1` through `Cmd+9` |

## Search and Filter

| Action | Shortcut |
|--------|----------|
| Start search/filter | `/` |
| Clear search | `Escape` |
| Next match | `n` |
| Previous match | `N` |
| Toggle semantic search | `Tab` (in search mode) |

## AI Features

| Action | Shortcut |
|--------|----------|
| AI Command Bar | `Cmd+K` |
| Smart rename | `Shift+R` |
| Summarize file | `s` (on selected file) |

### AI Command Examples

When the AI Command Bar is open (`Cmd+K`), you can type natural language commands:

- "find all PDFs modified last week"
- "organize downloads by type"
- "rename these photos by date"
- "move all images to Pictures folder"
- "find duplicate files"
- "show large files over 1GB"

## Command Palette

| Action | Shortcut |
|--------|----------|
| Open command palette | `Cmd+Shift+P` |
| Navigate commands | `Up` / `Down` |
| Execute command | `Enter` |
| Close palette | `Escape` |

The command palette provides fuzzy search access to all available commands.

## Operation Queue

| Action | Shortcut |
|--------|----------|
| Show/hide queue panel | `Cmd+Shift+Q` |
| Pause queue | (in queue panel) |
| Cancel operation | (in queue panel) |

## Quick Look Preview

| Action | Shortcut |
|--------|----------|
| Toggle quick look | `Space` |
| Close preview | `Escape` or `Space` |

## Git Integration

Git status is shown automatically when in a git repository. The status bar shows the current branch and modification indicators.

| Indicator | Meaning |
|-----------|---------|
| `+` | Staged changes |
| `*` | Modified files |
| `?` | Untracked files |

## Refresh

| Action | Shortcut |
|--------|----------|
| Refresh directory | `Cmd+R` |

## Application

| Action | Shortcut |
|--------|----------|
| Quit | `Cmd+Q` |

## Custom Keybindings

Keybindings can be customized in `~/.config/finder-plus/keybindings.conf`:

```
# Format: action_name = shortcut
# Modifiers: Cmd, Ctrl, Shift, Alt

copy = Cmd+C
paste = Cmd+V
new_folder = Cmd+Shift+N
```

See [CONFIG.md](CONFIG.md) for more configuration options.
