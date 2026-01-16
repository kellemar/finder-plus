# Finder Plus User Guide

This guide covers how to use Finder Plus effectively.

## Table of Contents

1. [Getting Started](#getting-started)
2. [Navigation](#navigation)
3. [View Modes](#view-modes)
4. [File Operations](#file-operations)
5. [Selection](#selection)
6. [Search and Filter](#search-and-filter)
7. [AI Features](#ai-features)
8. [Tabs](#tabs)
9. [Dual Pane Mode](#dual-pane-mode)
10. [Git Integration](#git-integration)
11. [Network Locations](#network-locations)
12. [Customization](#customization)

---

## Getting Started

### Launching

Launch Finder Plus from the terminal:

```bash
./finder-plus              # Opens in home directory
./finder-plus ~/Documents  # Opens in specified directory
```

### Interface Overview

```
┌─────────────────────────────────────────────────────────────────┐
│  ← → ↑  │  ~/Code/finder-plus              │ ≡  □  ×            │
├─────────┴───────────────────────────────────────────────────────┤
│ 🔍 Ask AI...                                           [Cmd+K]  │
├─────────┬───────────────────────────────────────────────────────┤
│ Tab 1   │ Tab 2 │ +                                             │
├─────────┼───────────────────────────────────┬───────────────────┤
│         │ Name              Size    Modified│                   │
│ ★ Fav   │ 📁 src            --      Jan 10 │   Preview         │
│ ▸ Home  │ 📁 docs           --      Jan 11 │                   │
│ ▸ Docs  │ 📄 README.md      4.2 KB  Jan 11 │   [File info]     │
│ ▸ Down  │ 📄 Makefile       2.3 KB  Jan 10 │                   │
├─────────┴───────────────────────────────────┴───────────────────┤
│ 5 items │ 12.4 GB free │ List View │ main ✓ │ 60 FPS           │
└─────────────────────────────────────────────────────────────────┘
```

**Components:**
- **Toolbar**: Navigation buttons and path bar
- **AI Command Bar**: Natural language commands (Cmd+K)
- **Tab Bar**: Multiple directory tabs
- **Sidebar**: Favorites and locations
- **Browser**: File listing (list/grid/column view)
- **Preview**: Quick look panel
- **Status Bar**: Item count, disk space, git info

---

## Navigation

### Keyboard Navigation

Finder Plus supports both standard and vim-style navigation:

| Action | Standard | Vim |
|--------|----------|-----|
| Move down | `↓` | `j` |
| Move up | `↑` | `k` |
| Enter directory | `Enter` | `l` |
| Go to parent | `Backspace` | `h` |
| First item | `Home` | `gg` |
| Last item | `End` | `G` |

### History

- **Back**: `Cmd+[` - Go to previous location
- **Forward**: `Cmd+]` - Go to next location
- **Parent**: `Cmd+↑` - Go to parent directory
- **Home**: `Cmd+Shift+H` - Go to home directory

### Sidebar

Click sidebar items to navigate:
- **Favorites**: Quick access to common folders
- **Locations**: Mounted volumes and drives
- **Network**: SFTP connections (when configured)

Resize the sidebar by dragging its right edge.

---

## View Modes

### List View (Cmd+1)

Detailed view showing:
- File/folder name
- Size
- Modified date
- Type/extension

Best for: Detailed file information, sorting by attributes.

### Grid View (Cmd+2)

Icon-based grid showing:
- Large file/folder icons
- Truncated filenames

Best for: Visual browsing, image folders.

### Column View (Cmd+3)

Miller columns showing:
- Parent directory
- Current directory
- Child directory or preview

Best for: Deep folder hierarchies, quick navigation.

---

## File Operations

### Copy, Cut, Paste

| Action | Shortcut |
|--------|----------|
| Copy | `Cmd+C` |
| Cut | `Cmd+X` |
| Paste | `Cmd+V` |
| Duplicate | `Cmd+D` |

**Visual Feedback:**
- Copied items appear dimmed
- Cut items appear faded

### Delete

- `Cmd+Backspace` - Move to Trash

A confirmation dialog appears before deletion.

### Rename

1. Select a file
2. Press `Enter` or `r`
3. Edit the name inline
4. Press `Enter` to confirm, `Escape` to cancel

### Create New

| Action | Shortcut |
|--------|----------|
| New Folder | `Cmd+Alt+N` |
| New File | `Cmd+Shift+N` |

---

## Selection

### Single Selection

Click an item or use `j`/`k` to navigate.

### Multi-Selection

- **Add to selection**: `Cmd+Click`
- **Range selection**: `Shift+Click` or `Shift+j`/`Shift+k`
- **Select all**: `Cmd+A`
- **Clear selection**: `Escape`

### Rubber Band Selection (Grid View)

In grid view, click and drag to select multiple items.

---

## Search and Filter

### Quick Filter

1. Press `/` to start filtering
2. Type to filter current directory
3. Use `n`/`N` for next/previous match
4. Press `Enter` to select, `Escape` to cancel

### Fuzzy Matching

The filter uses fuzzy matching:
- "readme" matches "README.md"
- "cfgjs" matches "config.js"

### Semantic Search

Press `Tab` while in search mode to toggle between:
- **Fuzzy**: Matches filename characters
- **Semantic**: Matches by meaning (AI-powered)

Semantic search requires indexing and AI features enabled.

---

## AI Features

### AI Command Bar

Press `Cmd+K` to open the AI command bar. Type natural language commands:

**Examples:**
- "find all PDFs modified this week"
- "organize downloads by file type"
- "move images to Pictures folder"
- "find duplicate files"
- "rename these photos by date"
- "show files larger than 1GB"

### Smart Rename

1. Select files to rename
2. Press `Shift+R`
3. AI suggests names based on file content
4. Review and apply suggestions

### File Summarization

1. Select a document
2. Press `s`
3. View AI-generated summary in preview panel

### Duplicate Detection

Use the AI command: "find duplicate files"

The AI identifies:
- Exact duplicates (same content)
- Near-duplicates (similar images, etc.)

---

## Tabs

### Managing Tabs

| Action | Shortcut |
|--------|----------|
| New tab | `Cmd+T` |
| Close tab | `Cmd+W` |
| Next tab | `Ctrl+Tab` |
| Previous tab | `Ctrl+Shift+Tab` |
| Go to tab N | `Cmd+1` through `Cmd+9` |

### Tab Features

- Each tab has independent:
  - Current directory
  - Selection state
  - History
  - Scroll position

- Drag tabs to reorder
- Hover over tab for close button

---

## Dual Pane Mode

### Enabling

Press `F3` or `Cmd+Shift+D` to toggle dual pane mode.

### Using Dual Panes

- Each pane navigates independently
- Press `Tab` to switch active pane
- Active pane has highlighted border

### Operations Between Panes

| Action | Shortcut |
|--------|----------|
| Copy to other pane | `F5` |
| Move to other pane | `F6` |
| Compare directories | `Cmd+=` |
| Sync scrolling | `Cmd+Shift+S` |

### Directory Comparison

`Cmd+=` highlights differences:
- Files only in left pane
- Files only in right pane
- Modified files (different size/date)

---

## Git Integration

When in a git repository, Finder Plus shows:

### Status Bar

- Branch name (e.g., `main`)
- Status indicators:
  - `+` = staged changes
  - `*` = modified files
  - `?` = untracked files

### File Indicators

Files show git status:
- **Green**: Staged
- **Orange**: Modified
- **Gray**: Untracked

### Preview Panel

Modified files show diff in preview.

---

## Network Locations

### Adding SFTP Connection

1. Open sidebar
2. Click "+" in Network section
3. Enter connection details:
   - Host
   - Username
   - Password or key file
4. Optionally save as profile

### Connection Status

Sidebar shows connection status:
- `[+]` Green = Connected
- `[~]` Yellow = Connecting
- `[!]` Red = Error
- `[N]` Gray = Disconnected

### Saved Profiles

Saved connections appear in sidebar for quick access.

---

## Customization

### View Options

| Toggle | Shortcut |
|--------|----------|
| Hidden files | `Cmd+Shift+.` |
| Preview panel | `Cmd+Shift+P` |
| Sidebar | `Cmd+Shift+S` |
| Fullscreen | `Cmd+Ctrl+F` |
| Theme | `Cmd+Shift+T` |

### Configuration

Edit `~/.config/finder-plus/config.json`:

```json
{
  "theme": 0,
  "vim_mode": true,
  "show_hidden_files": false,
  "sidebar_width": 200
}
```

See [CONFIG.md](CONFIG.md) for all options.

### Custom Keybindings

Edit `~/.config/finder-plus/keybindings.conf`:

```
copy = Cmd+C
paste = Cmd+V
new_folder = Cmd+Shift+N
```

See [KEYBOARD.md](KEYBOARD.md) for all actions.

---

## Command Palette

Press `Cmd+Shift+P` to open the command palette.

- Type to fuzzy search commands
- See keyboard shortcuts for each command
- Press `Enter` to execute
- Recently used commands appear first

---

## Performance Tips

### Large Directories

For directories with many files:
- Use list view (fastest)
- Disable preview panel
- Use search to filter

### Monitoring Performance

Press `Cmd+Shift+P` (when palette is closed) to toggle performance stats overlay showing:
- FPS
- Frame time (P99)
- Cache usage

---

## Tips and Tricks

1. **Quick Preview**: Press `Space` on any file for quick look
2. **Go to Path**: Click the path bar and type a path directly
3. **Refresh**: `Cmd+R` refreshes the current directory
4. **AI Shortcut**: `Cmd+K` then type any natural language command
5. **Vim Users**: Enable vim_mode in config for full vim navigation
6. **Batch Operations**: Select multiple files, then copy/move/delete all at once

---

## Troubleshooting

### Files Not Showing

- Check if hidden files are enabled (`Cmd+Shift+.`)
- Refresh with `Cmd+R`
- Check file permissions

### AI Features Not Working

- Ensure `CLAUDE_API_KEY` environment variable is set
- Enable AI in config: `"ai_enabled": true`

### Slow Performance

- Try release build instead of debug
- Reduce number of visible files (use search)
- Disable preview panel for large directories

### Network Connection Failed

- Verify host and credentials
- Check network connectivity
- Ensure SSH/SFTP is enabled on remote server

---

For more help, see the [README](../README.md) or open an issue on GitHub.
