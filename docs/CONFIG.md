# Configuration Reference

Finder Plus stores its configuration in `~/.config/finder-plus/config.json`. The configuration is automatically created with default values on first run.

## Configuration File Location

```
~/.config/finder-plus/
├── config.json          # Main configuration
├── keybindings.conf     # Custom keybindings (optional)
└── themes/              # Custom themes (optional)
```

## Configuration Options

### Window Settings

```json
{
  "window_width": 1280,
  "window_height": 720,
  "window_x": -1,
  "window_y": -1,
  "fullscreen": false,
  "remember_position": true
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `window_width` | int | 1280 | Window width in pixels |
| `window_height` | int | 720 | Window height in pixels |
| `window_x` | int | -1 | Window X position (-1 = center) |
| `window_y` | int | -1 | Window Y position (-1 = center) |
| `fullscreen` | bool | false | Start in fullscreen mode |
| `remember_position` | bool | true | Remember window position on exit |

### Appearance

```json
{
  "theme": 0,
  "view_mode": 0,
  "font_size": 14,
  "icon_size": 24,
  "sidebar_width": 200,
  "preview_width": 300,
  "show_hidden_files": false
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `theme` | int | 0 | Theme: 0=Dark, 1=Light, 2=System |
| `view_mode` | int | 0 | Default view: 0=List, 1=Grid, 2=Column |
| `font_size` | int | 14 | UI font size in points |
| `icon_size` | int | 24 | Icon size in pixels |
| `sidebar_width` | int | 200 | Sidebar width in pixels |
| `preview_width` | int | 300 | Preview panel width in pixels |
| `show_hidden_files` | bool | false | Show hidden files by default |

### Keyboard

```json
{
  "vim_mode": true
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `vim_mode` | bool | true | Enable vim-style navigation (j/k/h/l) |

### AI Features

```json
{
  "ai_enabled": false,
  "semantic_search": false,
  "smart_rename": false
}
```

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `ai_enabled` | bool | false | Enable AI features (requires API key) |
| `semantic_search` | bool | false | Enable semantic file search |
| `smart_rename` | bool | false | Enable AI-powered rename suggestions |

**Note**: The API key is not stored in the config file for security. Set it via environment variable:

```bash
export CLAUDE_API_KEY="your-api-key-here"
```

## Complete Example Configuration

```json
{
  "window_width": 1400,
  "window_height": 900,
  "window_x": 100,
  "window_y": 100,
  "fullscreen": false,
  "remember_position": true,
  "theme": 0,
  "view_mode": 0,
  "font_size": 14,
  "icon_size": 24,
  "sidebar_width": 220,
  "preview_width": 350,
  "show_hidden_files": false,
  "vim_mode": true,
  "ai_enabled": true,
  "semantic_search": true,
  "smart_rename": true
}
```

## Custom Keybindings

Custom keybindings are stored in `~/.config/finder-plus/keybindings.conf`:

```conf
# Finder Plus Keybindings
# Format: action_name = shortcut
# Modifiers: Cmd, Ctrl, Shift, Alt
# Example: copy = Cmd+C

# File operations
copy = Cmd+C
cut = Cmd+X
paste = Cmd+V
delete = Cmd+Backspace
rename = Enter
duplicate = Cmd+D
select_all = Cmd+A
new_file = Cmd+Shift+N
new_folder = Cmd+Alt+N
open = Cmd+O

# View
view_list = Cmd+1
view_grid = Cmd+2
view_columns = Cmd+3
toggle_hidden = Cmd+Shift+.
toggle_preview = Cmd+Shift+P
toggle_sidebar = Cmd+Shift+S
toggle_fullscreen = Cmd+Ctrl+F
toggle_theme = Cmd+Shift+T

# Navigation
go_back = Cmd+[
go_forward = Cmd+]
go_parent = Cmd+Up
go_home = Cmd+Shift+H
refresh = Cmd+R

# Tabs
new_tab = Cmd+T
close_tab = Cmd+W
next_tab = Ctrl+Tab
prev_tab = Ctrl+Shift+Tab

# Special
command_palette = Cmd+Shift+P
ai_command = Cmd+K
show_queue = Cmd+Shift+Q
quit = Cmd+Q
```

### Available Actions

| Action Name | Description |
|-------------|-------------|
| `none` | No action |
| `new_file` | Create new file |
| `new_folder` | Create new folder |
| `open` | Open selected item |
| `copy` | Copy to clipboard |
| `cut` | Cut to clipboard |
| `paste` | Paste from clipboard |
| `delete` | Delete (move to Trash) |
| `rename` | Rename selected item |
| `duplicate` | Duplicate selected item |
| `select_all` | Select all items |
| `view_list` | Switch to list view |
| `view_grid` | Switch to grid view |
| `view_columns` | Switch to column view |
| `toggle_hidden` | Toggle hidden files |
| `toggle_preview` | Toggle preview panel |
| `toggle_sidebar` | Toggle sidebar |
| `toggle_fullscreen` | Toggle fullscreen |
| `toggle_theme` | Toggle dark/light theme |
| `go_back` | Navigate back in history |
| `go_forward` | Navigate forward in history |
| `go_parent` | Go to parent directory |
| `go_home` | Go to home directory |
| `refresh` | Refresh current directory |
| `new_tab` | Open new tab |
| `close_tab` | Close current tab |
| `next_tab` | Switch to next tab |
| `prev_tab` | Switch to previous tab |
| `command_palette` | Open command palette |
| `ai_command` | Open AI command bar |
| `show_queue` | Show operation queue |
| `quit` | Quit application |

## Theme Colors

The default dark theme uses the following colors (Catppuccin-inspired):

| Element | Color |
|---------|-------|
| Background | `#1E1E2E` |
| Sidebar | `#181825` |
| Selection | `#45475A` |
| Hover | `#313244` |
| Text Primary | `#CDD6F4` |
| Text Secondary | `#A6ADC8` |
| Accent | `#89B4FA` |
| AI Accent | `#CBA6F7` |
| Folder | `#F9E2AF` |
| Success | `#A6E3A1` |
| Warning | `#FAB387` |
| Error | `#F38BA8` |
| Git Modified | `#FAB387` |
| Git Staged | `#A6E3A1` |

## Environment Variables

| Variable | Description |
|----------|-------------|
| `CLAUDE_API_KEY` | Claude API key for AI features |
| `FINDER_PLUS_CONFIG` | Override config file path |

## Resetting Configuration

To reset to defaults, delete the config file:

```bash
rm ~/.config/finder-plus/config.json
```

A new config with default values will be created on next launch.
