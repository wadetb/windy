# Windy

I want to quickly [re]organize the windows on my desktop.

My original idea was to have a "rules" file, written in YAML.
Windows would be matched against the rules by regex, and then dumped into buckets accordingly.
Monitors would be divided into grids.
So if I start up a terminal, it just kind of naturally goes to the right place based on its window title.

The other idea is some kind of highly interactive whim-based mouse-driven system.

The underlying data model is maybe very similar, you could envision the file as a serialization of the interactive state, that can serve as portable defaults.

Different arrangements can be used - shelf, swirl, tabs, stack (depth)
Arrangements are hierarchal - swirl in a slot of a shelf for instance

To place a window, hover it and hotkey, then mouse over region, one or more hotkeys to slice, and click to place. Hotkey hides the window and click inserts and places it. While the hotkey is active, a transparent fullscreen window covers the screen and outlines the grid.

When the hotkey is up, regions draw themselves onto the semitransparent window so they can be seen.
They inset a bit to make it easier to select them. 
Large tooltip appears guiding keyboard and mouse options based on context.
Escape cancels and restores the on-deck window.

Might be nice to keep a small border between all windows, since screen real estate is not at a premium.
Might be nice to have a key to bring a section to the foreground or send to the background.
In fact, Z buckets (slices / layers / shells) could be an element of the hierarchy.

In many cases, mouse position can define the behavior, but keyboard makes it more predictable.
With the hotkey up the arrow keys can maybe navigate around, if it's doable intuitively.

Hotkey activates for the window under the mouse, but then the transparent window jumps from monitor to monitor as the mouse moves around, highlighting the model on that monitor.

Might be nice to have a default "shelf" as a root, for simplicity.

Should coordinates be given absolutely or as a fraction of the monitor size?
In most cases it's implicit. But if a monitor is resized, we need to "reflow" everything - which means Bins need to maintain persistent HWNDs if they have a window! And I'd need to handle monitor resize events, of course Windows may do something too so I'd have to deal with the race.

Try making the window focus follow the mouse (but only for unobscured windows?)

Send to front / send to back hotkey and mouse bindings.

## Shelf (aka grid)

2D grid of windows. R and C keys add rows and columns, with shift delete hovered row or column.
Windows can span cells.

I CCCC I CCCC I

What happens when a row or column is deleted that has windows in it? Are they reflowed to other cells automatically? Is the deletion forbidden? Are they closed?

In a shelf, hotkey to jam it to fill the entire vertical (and /or horizontal?) space temporarily. Like Z in tmux.

Use a mouse third button instead of a Windows key. Actually use any and all extra mouse buttons, why not.

### 1/6/2018 Shelf Notes
In revisiting the program I find that R and C defining a grid is quite awkward. I hit C to split into two columns, which was fine, and then I hit R, intending to split only the right column, but instead it split both. 
It strikes me that a grid encourages a maximum amount of reflowing, since any edit casts a swath of change across the desktop.
On the one hand I felt that I would like to have had a recursive split happen. The first division by C into columns, followed by a division of just the right half into rows. 
This isn't strictly the same as SN-style K-d tree, since it's conceivable that the C simply splits the current cell into a new "column" shelf, with N slots, that can be increased or decreased (using some other hotkey or mouse placement...).
Ultimately the functionality of a shelf-as-a-grid still feels fine, it's simply a matter of what the keypresses do - spawn a new child shelf, or change the dimensions of the current one.
I'll try spawning a child shelf with a different key and see how that goes. The visual representation *should* be good enough that it's clear what's going on. Need to play around in Inkscape.

## Swirl

Spiral of windows arranged so a corner of each is visible, can configure "tightness". 
Click an open corner to explode and pick a new top.

## Void

Might be nice to have a place unwanted windows that we might want later, e.g. offscreen.
And be able to quickly swap them with live tiles.

## Implementation plan

Try moving the foreground window in a hardcoded manner and see how it goes.
Get the basic hotkey and transparent window appearance working.
Come up with the initial data model. Start with the shelf.
Tabs can be last ("if"), they are the only thing that requires some persistent onscreen ui.

At some point, document the key globals and functions in the code.

+ [DONE] Get hotkey working
+ [DONE] Test GetCursorPos on multi monitor
+ Root per monitor.
+ [DONE] Create transparent fullscreen topmost overlay that responds to the moues
+ [DONE] Get the foreground window and try moving it.
+ [DONE] Make the bin model, start with shelf
+ Unmaximize the on deck window before moving it.
+ Escape as a cancel hotkey.
+ [DONE] Achieve some basic drawing functionality.
+ [DONE] Can't move Slack or Outlook window. Might be because I'm getting a child window and not a top level!
+ After a few Shift-Rs or Shift-Cs, the next R or C is missed.
+ Need to keep a separate root per monitor.
+ When rows or columns are added, resize existing Windows.
+ Allow dragging over a rectangular region of cells.
+ Detect when the mouse moves to another monitor and move the overlay.
